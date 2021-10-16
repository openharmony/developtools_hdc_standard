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

namespace Hdc {
HdcJdwp::HdcJdwp(uv_loop_t *loopIn)
{
    Base::ZeroStruct(listenPipe);
    listenPipe.data = this;
    loop = loopIn;
    refCount = 0;
    uv_rwlock_init(&lockMapContext);
}

HdcJdwp::~HdcJdwp()
{
    uv_rwlock_destroy(&lockMapContext);
}

bool HdcJdwp::ReadyForRelease()
{
    return refCount == 0;
}

void HdcJdwp::Stop()
{
    auto funcListenPipeClose = [](uv_handle_t *handle) -> void {
        HdcJdwp *thisClass = (HdcJdwp *)handle->data;
        --thisClass->refCount;
    };
    Base::TryCloseHandle((const uv_handle_t *)&listenPipe, funcListenPipeClose);
    for (auto &&obj : mapCtxJdwp) {
        HCtxJdwp v = obj.second;
        FreeContext(v);
    }
    AdminContext(OP_CLEAR, 0, nullptr);
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
    Base::TryCloseHandle((const uv_handle_t *)&ctx->pipe);
    ctx->finish = true;
    AdminContext(OP_REMOVE, ctx->pid, nullptr);
    auto funcReqClose = [](uv_idle_t *handle) -> void {
        HCtxJdwp ctx = (HCtxJdwp)handle->data;
        --ctx->thisClass->refCount;
        Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseIdleCallback);
        delete ctx;
    };
    Base::IdleUvTask(loop, ctx, funcReqClose);
}

void HdcJdwp::ReadStream(uv_stream_t *pipe, ssize_t nread, const uv_buf_t *buf)
{
    bool ret = false;
    HCtxJdwp ctxJdwp = (HCtxJdwp)pipe->data;
    HdcJdwp *thisClass = (HdcJdwp *)ctxJdwp->thisClass;
    char temp[5];
    uint32_t pid = 0;
    int offset = 0;
    if (nread > 0) {
        ctxJdwp->bufIndex += nread;
    }

    while (offset < ctxJdwp->bufIndex) {
        if (nread == UV_ENOBUFS) {  // It is definite enough, usually only 4 bytes
            WRITE_LOG(LOG_DEBUG, "HdcJdwp::ReadStream IOBuf max");
            break;
        } else if (nread <= 0 || nread != 4) { // 4 : 4 bytes
            WRITE_LOG(LOG_DEBUG, "HdcJdwp::ReadStream program exit pid:%d", ctxJdwp->pid);
            break;
        }
        int errCode = memcpy_s(temp, sizeof(temp), ctxJdwp->buf + offset, 4); // 4 : 4 bytes
        if (errCode != EOK) {
            break;
        }
        temp[4] = 0; // 4 : pid length
        if (sscanf_s(temp, "%04x", &pid) != 1) {
            WRITE_LOG(LOG_WARN, "could not decode PID number: '%s'", temp);
            break;
        }
        WRITE_LOG(LOG_DEBUG, "JDWP accept pid:%d", pid);
        ctxJdwp->pid = pid;
        thisClass->AdminContext(OP_ADD, pid, ctxJdwp);
        offset += 4; // 4 : 4 bytes
        ret = true;
        break;  // just 4bytes, now finish
    }
    if (!ret) {
        thisClass->FreeContext(ctxJdwp);
    }
}

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
        thisClass->FreeContext(ctxJdwp);
        return;
    }
    auto funAlloc = [](uv_handle_t *handle, size_t sizeSuggested, uv_buf_t *buf) -> void {
        HCtxJdwp ctxJdwp = (HCtxJdwp)handle->data;
        buf->base = (char *)ctxJdwp->buf + ctxJdwp->bufIndex;
        buf->len = sizeof(ctxJdwp->buf) - ctxJdwp->bufIndex;
    };
    uv_read_start((uv_stream_t *)&ctxJdwp->pipe, funAlloc, ReadStream);
}

// https://andycong.top/2020/03/27/libuv%E5%A4%9A%E7%BA%BF%E7%A8%8B%E4%B8%AD%E4%BD%BF%E7%94%A8uv-accept/
bool HdcJdwp::JdwpListen()
{
#ifdef HDC_PCDEBUG
    // if test, canbe enable
    return true;
    // nc -U path/jdwp-control < hexpid.file
    const char jdwpCtrlName[] = { 'j', 'd', 'w', 'p', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', 0 };
    unlink(jdwpCtrlName);
#else
    const char jdwpCtrlName[] = { '\0', 'j', 'd', 'w', 'p', '-', 'c', 'o', 'n', 't', 'r', 'o', 'l', 0 };
#endif
    struct sockaddr_un addr;
    socklen_t addrlen;
    int s;
    int pathlen = sizeof(jdwpCtrlName) - 1;
    bool ret = false;
    const int DEFAULT_BACKLOG = 4;

    Base::ZeroStruct(addr);
    addr.sun_family = AF_UNIX;
    if (memcpy_s(addr.sun_path, sizeof(addr.sun_path), jdwpCtrlName, pathlen) != EOK) {
        return false;
    }
    s = socket(AF_UNIX, SOCK_STREAM, 0);
    WRITE_LOG(LOG_DEBUG, "JdwpListen begin");
    if (s < 0) {
        WRITE_LOG(LOG_WARN, "could not create vm debug control socket. %d: %s", errno, strerror(errno));
        return false;
    }
    fcntl(s, F_SETFD, FD_CLOEXEC);
    while (true) {
        addrlen = (pathlen + sizeof(addr.sun_family));
        if (bind(s, (struct sockaddr *)&addr, addrlen) < 0) {
            WRITE_LOG(LOG_WARN, "could not bind vm debug control socket: %d: %s", errno, strerror(errno));
            break;
        }
        uv_pipe_init(loop, &listenPipe, 0);
        listenPipe.data = this;
        if (uv_pipe_open(&listenPipe, s)) {
            break;
        }
        if (uv_listen((uv_stream_t *)&listenPipe, DEFAULT_BACKLOG, AcceptClient)) {
            break;
        }
        ++refCount;
        ret = true;
        break;
    }
    if (!ret) {
        close(s);
    }
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
            uv_rwlock_wrunlock(&lockMapContext);
            break;
        }
        default:
            break;
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

// jdb -connect com.sun.jdi.SocketAttach:hostname=localhost,port=8000
int HdcJdwp::Initial()
{
    if (!JdwpListen()) {
        return ERR_MODULE_JDWP_FAILED;
    }
    return RET_SUCCESS;
}
}
