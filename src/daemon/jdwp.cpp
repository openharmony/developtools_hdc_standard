/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "jdwp.h"
#include <sys/eventfd.h>
#include <thread>

namespace Hdc {
HdcJdwp::HdcJdwp(uv_loop_t *loopIn)
{
    listenPipe.data = this;
    loop = loopIn;
    refCount = 0;
    stop = false;
    awakenPollFd = -1;
    uv_rwlock_init(&lockMapContext);
    uv_rwlock_init(&lockJdwpTrack);
    awakenPollFd = -1;
    stop = false;
}

HdcJdwp::~HdcJdwp()
{
    if (awakenPollFd >= 0) {
        close(awakenPollFd);
        awakenPollFd = -1;
    }
    uv_rwlock_destroy(&lockMapContext);
    uv_rwlock_destroy(&lockJdwpTrack);
}

bool HdcJdwp::ReadyForRelease()
{
    return refCount == 0;
}

void HdcJdwp::Stop()
{
    stop = true;
    WakePollThread();
    auto funcListenPipeClose = [](uv_handle_t *handle) -> void {
        HdcJdwp *thisClass = (HdcJdwp *)handle->data;
        --thisClass->refCount;
    };
    Base::TryCloseHandle((const uv_handle_t *)&listenPipe, funcListenPipeClose);
    freeContextMutex.lock();
    for (auto &&obj : mapCtxJdwp) {
        HCtxJdwp v = obj.second;
        FreeContext(v);
    }
    AdminContext(OP_CLEAR, 0, nullptr);
    freeContextMutex.unlock();
}

void *HdcJdwp::MallocContext()
{
    HCtxJdwp ctx = nullptr;
    if ((ctx = new ContextJdwp()) == nullptr) {
        return nullptr;
    }
    ctx->thisClass = this;
    ctx->pipe.data = ctx;
    ++refCount;
    return ctx;
}

// Single thread, two parameters can be used
void HdcJdwp::FreeContext(HCtxJdwp ctx)
{
    if (ctx->finish) {
        return;
    }
    ctx->finish = true;
    WRITE_LOG(LOG_INFO, "FreeContext for targetPID :%d", ctx->pid);
    Base::TryCloseHandle((const uv_handle_t *)&ctx->pipe);
    AdminContext(OP_REMOVE, ctx->pid, nullptr);
    auto funcReqClose = [](uv_idle_t *handle) -> void {
        HCtxJdwp ctx = (HCtxJdwp)handle->data;
        --ctx->thisClass->refCount;
        Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseIdleCallback);
        delete ctx;
    };
    Base::IdleUvTask(loop, ctx, funcReqClose);
}

void HdcJdwp::RemoveFdFromPollList(uint32_t pid)
{
    for (auto &&pair : pollNodeMap) {
        if (pair.second.ppid == pid) {
            WRITE_LOG(LOG_INFO, "RemoveFdFromPollList for pid:%d.", pid);
            pollNodeMap.erase(pair.second.pollfd.fd);
            break;
        }
    }
}

void HdcJdwp::ReadStream(uv_stream_t *pipe, ssize_t nread, const uv_buf_t *buf)
{
    bool ret = true;
    if (nread == UV_ENOBUFS) {  // It is definite enough, usually only 4 bytes
        ret = false;
        WRITE_LOG(LOG_DEBUG, "HdcJdwp::ReadStream IOBuf max");
    } else if (nread == 0) {
        return;
#ifdef JS_JDWP_CONNECT
    } else if (nread < JS_PKG_MIN_SIZE || nread > JS_PKG_MX_SIZE) { // valid Js package size
#else
    } else if (nread < 0 || nread != 4) {  // 4 : 4 bytes
#endif  // JS_JDWP_CONNECT
        ret = false;
        WRITE_LOG(LOG_DEBUG, "HdcJdwp::ReadStream invalid package nread:%d.", nread);
    }

    HCtxJdwp ctxJdwp = static_cast<HCtxJdwp>(pipe->data);
    HdcJdwp *thisClass = static_cast<HdcJdwp *>(ctxJdwp->thisClass);
    if (ret) {
        uint32_t pid = 0;
        char *p = ctxJdwp->buf;
        if (nread == sizeof(uint32_t)) {  // Java: pid
            pid = atoi(p);
        } else {  // JS:pid PkgName
#ifdef JS_JDWP_CONNECT
            struct JsMsgHeader *jsMsg = reinterpret_cast<struct JsMsgHeader *>(p);
            if (jsMsg->msgLen == nread) {
                pid = jsMsg->pid;
                string pkgName = string((char *)p + sizeof(JsMsgHeader), jsMsg->msgLen - sizeof(JsMsgHeader));
                ctxJdwp->pkgName = pkgName;
            } else {
                ret = false;
                WRITE_LOG(LOG_DEBUG, "HdcJdwp::ReadStream invalid js package size %d:%d.", jsMsg->msgLen, nread);
            }
#endif  // JS_JDWP_CONNECT
        }
        if (pid > 0) {
            ctxJdwp->pid = pid;
#ifdef JS_JDWP_CONNECT
            WRITE_LOG(LOG_DEBUG, "JDWP accept pid:%d-pkg:%s", pid, ctxJdwp->pkgName.c_str());
#else
            WRITE_LOG(LOG_DEBUG, "JDWP accept pid:%d", pid);
#endif  // JS_JDWP_CONNECT
            thisClass->AdminContext(OP_ADD, pid, ctxJdwp);
            ret = true;
            int fd = -1;
            if (uv_fileno(reinterpret_cast<uv_handle_t *>(&(ctxJdwp->pipe)), &fd) < 0) {
                WRITE_LOG(LOG_DEBUG, "HdcJdwp::ReadStream uv_fileno fail.");
            } else {
                thisClass->freeContextMutex.lock();
                thisClass->pollNodeMap.emplace(fd, PollNode(fd, pid));
                thisClass->freeContextMutex.unlock();
                thisClass->WakePollThread();
            }
        }
    }
    Base::ZeroArray(ctxJdwp->buf);
    if (!ret) {
        WRITE_LOG(LOG_INFO, "ReadStream proc:%d err, free it.", ctxJdwp->pid);
        thisClass->freeContextMutex.lock();
        thisClass->FreeContext(ctxJdwp);
        thisClass->freeContextMutex.unlock();
    }
}

#ifdef JS_JDWP_CONNECT
string HdcJdwp::GetProcessListExtendPkgName()
{
    string ret;
    uv_rwlock_rdlock(&lockMapContext);
    for (auto &&v : mapCtxJdwp) {
        HCtxJdwp hj = v.second;
        ret += std::to_string(v.first) + " " + hj->pkgName + "\n";
    }
    uv_rwlock_rdunlock(&lockMapContext);
    return ret;
}
#endif  // JS_JDWP_CONNECT

void HdcJdwp::AcceptClient(uv_stream_t *server, int status)
{
    uv_pipe_t *listenPipe = (uv_pipe_t *)server;
    HdcJdwp *thisClass = (HdcJdwp *)listenPipe->data;
    HCtxJdwp ctxJdwp = (HCtxJdwp)thisClass->MallocContext();
    if (!ctxJdwp) {
        return;
    }
    uv_pipe_init(thisClass->loop, &ctxJdwp->pipe, 1);
    if (uv_accept(server, (uv_stream_t *)&ctxJdwp->pipe) < 0) {
        WRITE_LOG(LOG_DEBUG, "uv_accept failed");
        thisClass->freeContextMutex.lock();
        thisClass->FreeContext(ctxJdwp);
        thisClass->freeContextMutex.unlock();
        return;
    }
    auto funAlloc = [](uv_handle_t *handle, size_t sizeSuggested, uv_buf_t *buf) -> void {
        HCtxJdwp ctxJdwp = (HCtxJdwp)handle->data;
        buf->base = (char *)ctxJdwp->buf;
        buf->len = sizeof(ctxJdwp->buf);
    };
    uv_read_start((uv_stream_t *)&ctxJdwp->pipe, funAlloc, ReadStream);
}

// Test bash connnet(UNIX-domain sockets):nc -U path/jdwp-control < hexpid.file
// Test uv connect(pipe): 'uv_pipe_connect'
bool HdcJdwp::JdwpListen()
{
#ifdef HDC_PCDEBUG
    // if test, canbe enable
    return true;
    const char jdwpCtrlName[] = { 'j', 'd', 'w', 'p', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', 0 };
    unlink(jdwpCtrlName);
#else
    const char jdwpCtrlName[] = { '\0', 'j', 'd', 'w', 'p', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', 0 };
#endif
    const int DEFAULT_BACKLOG = 4;
    bool ret = false;
    while (true) {
        uv_pipe_init(loop, &listenPipe, 0);
        listenPipe.data = this;
        if ((uv_pipe_bind(&listenPipe, jdwpCtrlName))) {
            constexpr int bufSize = 1024;
            char buf[bufSize] = { 0 };
            strerror_r(errno, buf, bufSize);
            WRITE_LOG(LOG_WARN, "Bind error : %d: %s", errno, buf);
            return 1;
        }
        if (uv_listen((uv_stream_t *)&listenPipe, DEFAULT_BACKLOG, AcceptClient)) {
            break;
        }
        ++refCount;
        ret = true;
        break;
    }
    // listenPipe close by stop
    return ret;
}

// Working in the main thread, but will be accessed by each session thread, so we need to set thread lock
void *HdcJdwp::AdminContext(const uint8_t op, const uint32_t pid, HCtxJdwp ctxJdwp)
{
    HCtxJdwp hRet = nullptr;
    switch (op) {
        case OP_ADD: {
            uv_rwlock_wrlock(&lockMapContext);
            mapCtxJdwp[pid] = ctxJdwp;
            uv_rwlock_wrunlock(&lockMapContext);
            break;
        }
        case OP_REMOVE:
            uv_rwlock_wrlock(&lockMapContext);
            mapCtxJdwp.erase(pid);
            RemoveFdFromPollList(pid);
            uv_rwlock_wrunlock(&lockMapContext);
            break;
        case OP_QUERY: {
            uv_rwlock_rdlock(&lockMapContext);
            if (mapCtxJdwp.count(pid)) {
                hRet = mapCtxJdwp[pid];
            }
            uv_rwlock_rdunlock(&lockMapContext);
            break;
        }
        case OP_CLEAR: {
            uv_rwlock_wrlock(&lockMapContext);
            mapCtxJdwp.clear();
            pollNodeMap.clear();
            uv_rwlock_wrunlock(&lockMapContext);
            break;
        }
        default:
            break;
    }
    if (op == OP_ADD || op == OP_REMOVE || op == OP_CLEAR) {
        uv_rwlock_wrlock(&lockJdwpTrack);
        ProcessListUpdated();
        uv_rwlock_wrunlock(&lockJdwpTrack);
    }
    return hRet;
}

// work on main thread
void HdcJdwp::SendCallbackJdwpNewFD(uv_write_t *req, int status)
{
    // It usually works successful, not notify session work
    HCtxJdwp ctx = (HCtxJdwp)req->data;
    if (status >= 0) {
        WRITE_LOG(LOG_DEBUG, "SendCallbackJdwpNewFD successful %d, active jdwp forward", ctx->pid);
    } else {
        WRITE_LOG(LOG_WARN, "SendCallbackJdwpNewFD failed %d", ctx->pid);
    }
    // close my process's fd
    Base::TryCloseHandle((const uv_handle_t *)&ctx->jvmTCP);
    delete req;
    --ctx->thisClass->refCount;
}

// Each session calls the interface through the main thread message queue, which cannot be called directly across
// threads
// work on main thread
bool HdcJdwp::SendJdwpNewFD(uint32_t targetPID, int fd)
{
    bool ret = false;
    while (true) {
        HCtxJdwp ctx = (HCtxJdwp)AdminContext(OP_QUERY, targetPID, nullptr);
        if (!ctx) {
            break;
        }
        ctx->dummy = (uint8_t)'!';
        if (uv_tcp_init(loop, &ctx->jvmTCP)) {
            break;
        }
        if (uv_tcp_open(&ctx->jvmTCP, fd)) {
            break;
        }
        // transfer fd to jvm
        // clang-format off
        if (Base::SendToStreamEx((uv_stream_t *)&ctx->pipe, (uint8_t *)&ctx->dummy, 1, (uv_stream_t *)&ctx->jvmTCP,
            (void *)SendCallbackJdwpNewFD, (const void *)ctx) < 0) {
            break;
        }
        // clang-format on
        ++refCount;
        ret = true;
        WRITE_LOG(LOG_DEBUG, "SendJdwpNewFD successful targetPID:%d fd%d", targetPID, fd);
        break;
    }
    return ret;
}

// cross thread call begin
bool HdcJdwp::CheckPIDExist(uint32_t targetPID)
{
    HCtxJdwp ctx = (HCtxJdwp)AdminContext(OP_QUERY, targetPID, nullptr);
    return ctx != nullptr;
}

string HdcJdwp::GetProcessList()
{
    string ret;
    uv_rwlock_rdlock(&lockMapContext);
    for (auto &&v : mapCtxJdwp) {
        ret += std::to_string(v.first) + "\n";
    }
    uv_rwlock_rdunlock(&lockMapContext);
    return ret;
}
// cross thread call finish

size_t HdcJdwp::JdwpProcessListMsg(char *buffer, size_t bufferlen)
{
    // Message is length-prefixed with 4 hex digits in ASCII.
    static constexpr size_t headerLen = 5;
    char head[headerLen + 2];
#ifdef JS_JDWP_CONNECT
    string result = GetProcessListExtendPkgName();
#else
    string result = GetProcessList();
#endif // JS_JDWP_CONNECT

    size_t len = result.length();
    if (bufferlen < (len + headerLen)) {
        WRITE_LOG(LOG_WARN, "truncating JDWP process list (max len = %zu) ", bufferlen);
        len = bufferlen;
    }
    if (snprintf_s(head, sizeof head, sizeof head - 1, "%04zx\n", len) < 0) {
        WRITE_LOG(LOG_WARN, " JdwpProcessListMsg head fail.");
        return 0;
    }
    if (memcpy_s(buffer, bufferlen, head, headerLen) != EOK) {
        WRITE_LOG(LOG_WARN, " JdwpProcessListMsg get head fail.");
        return 0;
    }
    if (memcpy_s(buffer + headerLen, (bufferlen - headerLen), result.c_str(), len) != EOK) {
        WRITE_LOG(LOG_WARN, " JdwpProcessListMsg get data  fail.");
        return 0;
    }
    return len + headerLen;
}

void HdcJdwp::SendProcessList(HTaskInfo t, string data)
{
    if (t == nullptr || data.size() == 0) {
        WRITE_LOG(LOG_WARN, " SendProcessList, Nothing needs to be sent.");
        return;
    }
    void *clsSession = t->ownerSessionClass;
    HdcSessionBase *sessionBase = static_cast<HdcSessionBase *>(clsSession);
    sessionBase->LogMsg(t->sessionId, t->channelId, MSG_OK, data.c_str());
}

void HdcJdwp::ProcessListUpdated(HTaskInfo task)
{
    if (jdwpTrackers.size() <= 0) {
        WRITE_LOG(LOG_DEBUG, "None jdwpTrackers.");
        return;
    }
#ifdef JS_JDWP_CONNECT
    static constexpr uint32_t jpidTrackListSize = 1024 * 4;
#else
    static constexpr uint32_t jpidTrackListSize = 1024;
#endif // JS_JDWP_CONNECT
    std::string data;
    data.resize(jpidTrackListSize);
    size_t len = JdwpProcessListMsg(&data[0], data.size());
    if (len <= 0) {
        return;
    }
    data.resize(len);
    if (task != nullptr) {
        SendProcessList(task, data);
    } else {
        for (auto &t : jdwpTrackers) {
            if (t == nullptr) {
                continue;
            }
            if (t->taskStop || t->taskFree || !t->taskClass) {  // The channel for the track-jpid has been stopped.
                jdwpTrackers.erase(remove(jdwpTrackers.begin(), jdwpTrackers.end(), t), jdwpTrackers.end());
                if (jdwpTrackers.size() <= 0) {
                    return;
                }
            } else {
                SendProcessList(t, data);
            }
        }
    }
}

bool HdcJdwp::CreateJdwpTracker(HTaskInfo taskInfo)
{
    if (taskInfo == nullptr) {
        return false;
    }
    uv_rwlock_wrlock(&lockJdwpTrack);
    auto it = std::find(jdwpTrackers.begin(), jdwpTrackers.end(), taskInfo);
    if (it == jdwpTrackers.end()) {
        jdwpTrackers.push_back(taskInfo);
    }
    ProcessListUpdated(taskInfo);
    uv_rwlock_wrunlock(&lockJdwpTrack);
    return true;
}

void HdcJdwp::RemoveJdwpTracker(HTaskInfo taskInfo)
{
    if (taskInfo == nullptr) {
        return;
    }
    uv_rwlock_wrlock(&lockJdwpTrack);
    auto it = std::find(jdwpTrackers.begin(), jdwpTrackers.end(), taskInfo);
    if (it != jdwpTrackers.end()) {
        WRITE_LOG(LOG_DEBUG, "RemoveJdwpTracker channelId:%d, taskType:%d.", taskInfo->channelId, taskInfo->taskType);
        jdwpTrackers.erase(remove(jdwpTrackers.begin(), jdwpTrackers.end(), *it), jdwpTrackers.end());
    }
    uv_rwlock_wrunlock(&lockJdwpTrack);
}

void HdcJdwp::DrainAwakenPollThread() const
{
    uint64_t value = 0;
    ssize_t retVal = read(awakenPollFd, &value, sizeof(value));
    if (retVal < 0) {
        WRITE_LOG(LOG_FATAL, "DrainAwakenPollThread: Failed to read data from awaken pipe %d", retVal);
    }
}

void HdcJdwp::WakePollThread()
{
    if (awakenPollFd < 0) {
        WRITE_LOG(LOG_DEBUG, "awakenPollFd: MUST initialized before notifying");
        return;
    }
    static const uint64_t increment = 1;
    ssize_t retVal = write(awakenPollFd, &increment, sizeof(increment));
    if (retVal < 0) {
        WRITE_LOG(LOG_FATAL, "WakePollThread: Failed to write data into awaken pipe %d", retVal);
    }
}

void *HdcJdwp::FdEventPollThread(void *args)
{
    auto thisClass = static_cast<HdcJdwp *>(args);
    std::vector<struct pollfd> pollfds;
    size_t size = 0;
    while (!thisClass->stop) {
        thisClass->freeContextMutex.lock();
        if (size != thisClass->pollNodeMap.size() || thisClass->pollNodeMap.size() == 0) {
            pollfds.clear();
            struct pollfd pollFd;
            for (const auto &pair : thisClass->pollNodeMap) {
                pollFd.fd = pair.second.pollfd.fd;
                pollFd.events = pair.second.pollfd.events;
                pollFd.revents = pair.second.pollfd.revents;
                pollfds.push_back(pollFd);
            }
            pollFd.fd = thisClass->awakenPollFd;
            pollFd.events = POLLIN;
            pollFd.revents = 0;
            pollfds.push_back(pollFd);
            size = pollfds.size();
        }
        thisClass->freeContextMutex.unlock();
        poll(&pollfds[0], size, -1);
        for (const auto &pollfdsing : pollfds) {
            if (pollfdsing.revents & (POLLNVAL | POLLRDHUP | POLLHUP | POLLERR)) {  // POLLNVAL:fd not open
                thisClass->freeContextMutex.lock();
                auto it = thisClass->pollNodeMap.find(pollfdsing.fd);
                if (it != thisClass->pollNodeMap.end()) {
                    uint32_t targetPID = it->second.ppid;
                    HCtxJdwp ctx = static_cast<HCtxJdwp>(thisClass->AdminContext(OP_QUERY, targetPID, nullptr));
                    if (ctx != nullptr) {
                        thisClass->AdminContext(OP_REMOVE, targetPID, nullptr);
                    }
                }
                thisClass->freeContextMutex.unlock();
            } else if (pollfdsing.revents & POLLIN) {
                if (pollfdsing.fd == thisClass->awakenPollFd) {
                    thisClass->DrainAwakenPollThread();
                }
            }
        }
    }
    return nullptr;
}

int HdcJdwp::CreateFdEventPoll()
{
    pthread_t tid;
    if (awakenPollFd >= 0) {
        close(awakenPollFd);
        awakenPollFd = -1;
    }
    awakenPollFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (awakenPollFd < 0) {
        WRITE_LOG(LOG_FATAL, "CreateFdEventPoll : Failed to create awakenPollFd");
        return ERR_GENERIC;
    }
    int tret = pthread_create(&tid, nullptr, FdEventPollThread, this);
    if (tret != 0) {
        WRITE_LOG(LOG_INFO, "FdEventPollThread create fail.");
        return tret;
    }
    return RET_SUCCESS;
}

// jdb -connect com.sun.jdi.SocketAttach:hostname=localhost,port=8000
int HdcJdwp::Initial()
{
    freeContextMutex.lock();
    pollNodeMap.clear();
    freeContextMutex.unlock();
    if (!JdwpListen()) {
        return ERR_MODULE_JDWP_FAILED;
    }
    if (CreateFdEventPoll() < 0) {
        return ERR_MODULE_JDWP_FAILED;
    }
    return RET_SUCCESS;
}
}
