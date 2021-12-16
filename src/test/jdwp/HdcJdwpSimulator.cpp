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
 *
 */
#include "HdcJdwpSimulator.h"
using namespace OHOS;
using namespace OHOS::HiviewDFX;
static constexpr HiLogLabel LABEL = {LOG_CORE, 0, "JDWP_TEST"};
HdcJdwpSimulator::HdcJdwpSimulator(uv_loop_t *loopIn, string pkg)
{
    loop = loopIn;
    exit = false;
    pkgName = pkg;
}

HdcJdwpSimulator::~HdcJdwpSimulator() {}

void HdcJdwpSimulator::FinishWriteCallback(uv_write_t *req, int status)
{
    HiLog::Info(LABEL, "FinishWriteCallback:%{public}d error:%{public}s", status,
                uv_err_name(status));
    delete[]((uint8_t *)req->data);
    delete req;
}

RetErrCode HdcJdwpSimulator::SendToStream(uv_stream_t *handleStream, const uint8_t *buf,
                                          const int bufLen, const void *finishCallback)
{
    HiLog::Info(LABEL, "HdcJdwpSimulator::SendToStream: %{public}s, %{public}d", buf, bufLen);
    RetErrCode ret = RetErrCode::ERR_GENERIC;
    if (bufLen <= 0) {
        HiLog::Error(LABEL, "HdcJdwpSimulator::SendToStream wrong bufLen.");
        return RetErrCode::ERR_GENERIC;
    }
    uint8_t *pDynBuf = new uint8_t[bufLen];
    if (!pDynBuf) {
        HiLog::Error(LABEL, "HdcJdwpSimulator::SendToStream new pDynBuf fail.");
        return RetErrCode::ERR_GENERIC;
    }
    if (memcpy_s(pDynBuf, bufLen, buf, bufLen)) {
        delete[] pDynBuf;
        HiLog::Error(LABEL, "HdcJdwpSimulator::SendToStream memcpy fail.");
        return RetErrCode::ERR_BUF_ALLOC;
    }

    uv_write_t *reqWrite = new uv_write_t();
    if (!reqWrite) {
        HiLog::Error(LABEL, "HdcJdwpSimulator::SendToStream alloc reqWrite fail.");
        return RetErrCode::ERR_GENERIC;
    }
    uv_buf_t bfr;
    while (true) {
        reqWrite->data = (void *)pDynBuf;
        bfr.base = (char *)pDynBuf;
        bfr.len = bufLen;
        if (!uv_is_writable(handleStream)) {
            HiLog::Info(LABEL, "SendToStream uv_is_unwritable!");
            delete reqWrite;
            break;
        }
        HiLog::Info(LABEL, "SendToStream buf:%{public}s", pDynBuf);
        uv_write(reqWrite, handleStream, &bfr, 1, (uv_write_cb)finishCallback);
        ret = RetErrCode::SUCCESS;
        break;
    }
    return ret;
}

void HdcJdwpSimulator::alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    if (suggested_size <= 0) {
        return;
    }
    buf->base = (char *)malloc(suggested_size);
    buf->len = suggested_size;
}

#ifndef JS_JDWP_CONNECT
// Process incoming data.  If no data is available, this will block until some
// arrives.
void HdcJdwpSimulator::ProcessIncoming(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
    HiLog::Debug(LABEL, "ProcessIncoming :%{public}d", nread);
    if (nread > 0) {
        std::unique_ptr<char[]> recv = std::make_unique<char[]>(nread + 1);
        memset_s(recv.get(), nread + 1, 0, nread + 1);
        memcpy_s(recv.get(), nread, buf->base, nread);
        for (int i = 0; i < (nread + 1); i++) {
            HiLog::Info(LABEL, "ProcessIncoming recv2[%{public}d] :%{public}c", i, recv[i]);
        }

        vector<uint8_t> reply;
        reply.clear();
        reply.insert(reply.end(), HANDSHAKE_MESSAGE.c_str(),
                     HANDSHAKE_MESSAGE.c_str() + HANDSHAKE_MESSAGE.size());
        reply.insert(reply.end(), buf->base, buf->base + nread);
        HiLog::Info(LABEL, "ProcessIncoming--reply server");
        uint8_t *buf = reply.data();

        for (int i = 0; i < (HANDSHAKE_MESSAGE.size() + nread + 1); i++) {
            HiLog::Info(LABEL, "ProcessIncoming reply%{public}d :%{public}c", i, reply[i]);
        }
        SendToStream(client, buf, HANDSHAKE_MESSAGE.size() + nread + 1,
                     (void *)FinishWriteCallback);
    } else {
        if (nread != UV_EOF) {
            HiLog::Debug(LABEL, "ProcessIncoming error %s\n", uv_err_name(nread));
        }
        uv_close((uv_handle_t *)client, NULL);
    }
    free(buf->base);
}

// Get new socket fd passed from jdwp control
void HdcJdwpSimulator::ReceiveNewFd(uv_stream_t *q, ssize_t nread, const uv_buf_t *buf)
{
    HCtxJdwpSimulator ctxJdwp = (HCtxJdwpSimulator)q->data;
    HdcJdwpSimulator *thisClass = static_cast<HdcJdwpSimulator *>(ctxJdwp->thisClass);
    int pid_curr = static_cast<int>(getpid());
    HiLog::Debug(LABEL, "HdcJdwpSimulator::ReceiveNewFd pid: %{public}d, nread: %{public}d\n",
                 pid_curr, nread);
    if (nread < 0) {
        if (nread != UV_EOF) {
            HiLog::Error(LABEL, "Read error %s\n", uv_err_name(nread));
        }
        uv_close((uv_handle_t *)q, NULL);
        return;
    }

    uv_pipe_t *pipe = (uv_pipe_t *)q;
    if (!uv_pipe_pending_count(pipe)) {
        HiLog::Error(LABEL, "No pending count\n");
        return;
    }
    uv_handle_type pending = uv_pipe_pending_type(pipe);
    if (pending != UV_TCP) {
        HiLog::Debug(LABEL, "None TCP type: %{public}d", pending);
    }
    uv_tcp_init(thisClass->loop, &ctxJdwp->newFd);
    if (uv_accept(q, (uv_stream_t *)&ctxJdwp->newFd) == 0) {
        uv_os_fd_t fd;
        ctxJdwp->hasNewFd = true;
        uv_fileno((const uv_handle_t *)&ctxJdwp->newFd, &fd);
        HiLog::Debug(LABEL, "Jdwp forward pid %{public}d: new fd %{public}d\n", getpid(), fd);
        uv_read_start((uv_stream_t *)&ctxJdwp->newFd, alloc_buffer, ProcessIncoming);
    } else {
        ctxJdwp->hasNewFd = false;
        uv_close((uv_handle_t *)&ctxJdwp->newFd, NULL);
    }
}
#endif // JS_JDWP_CONNECT

void HdcJdwpSimulator::ConnectJdwp(uv_connect_t *connection, int status)
{
    HiLog::Debug(LABEL, "ConnectJdwp:%{public}d error:%{public}s", status, uv_err_name(status));
    uint32_t pid_curr = static_cast<uint32_t>(getpid());
    HCtxJdwpSimulator ctxJdwp = (HCtxJdwpSimulator)connection->data;
    HdcJdwpSimulator *thisClass = static_cast<HdcJdwpSimulator *>(ctxJdwp->thisClass);
#ifdef JS_JDWP_CONNECT
    string pkgName = thisClass->pkgName;
    uint32_t pkgSize = pkgName.size() + sizeof(JsMsgHeader); // JsMsgHeader pkgName;
    uint8_t *info = new uint8_t[pkgSize]();
    if (!info) {
        HiLog::Error(LABEL, "ConnectJdwp new info fail.");
        return;
    }
    memset_s(info, pkgSize, 0, pkgSize);
    JsMsgHeader *jsMsg = (JsMsgHeader *)info;
    jsMsg->pid = pid_curr;
    jsMsg->msgLen = pkgSize;
    HiLog::Info(LABEL, "ConnectJdwp send pid:%{public}d, pkgName:%{public}s, msgLen:%{public}d,",
                jsMsg->pid, pkgName.c_str(), jsMsg->msgLen);
    bool retFail = false;
    if (memcpy_s(info + sizeof(JsMsgHeader), pkgName.size(), &pkgName[0], pkgName.size()) != 0) {
        HiLog::Error(LABEL, "ConnectJdwp memcpy_s fail :%{public}s.", pkgName.c_str());
        retFail = true;
    }
    if (!retFail) {
        HiLog::Info(LABEL, "ConnectJdwp send JS msg:%{public}s", info);
        thisClass->SendToStream((uv_stream_t *)connection->handle, (uint8_t *)info, pkgSize,
                                (void *)FinishWriteCallback);
    }
    if (info) {
        delete[] info;
        info = nullptr;
    }
#else
    char pid[5] = {0};
    if (sprintf_s(pid, sizeof(pid), "%d", pid_curr) < 0) {
        HiLog::Info(LABEL, "ConnectJdwp trans pid fail :%{public}d.", pid_curr);
        return;
    }
    HiLog::Info(LABEL, "ConnectJdwp send pid:%{public}s", pid);
    thisClass->SendToStream((uv_stream_t *)connection->handle, (uint8_t *)pid, sizeof(pid_curr),
                            (void *)FinishWriteCallback);
    HiLog::Info(LABEL, "ConnectJdwp reading.");
    uv_read_start((uv_stream_t *)&ctxJdwp->pipe, thisClass->alloc_buffer, ReceiveNewFd);
#endif // JS_JDWP_CONNECT
}

void *HdcJdwpSimulator::MallocContext()
{
    HCtxJdwpSimulator ctx = nullptr;
    if ((ctx = new ContextJdwpSimulator()) == nullptr) {
        return nullptr;
    }
    ctx->thisClass = this;
    ctx->pipe.data = ctx;
    ctx->hasNewFd = false;
    return ctx;
}

void HdcJdwpSimulator::FreeContext()
{
    HiLog::Debug(LABEL, "HdcJdwpSimulator::FreeContext start");
    if (!ctxPoint) {
        return;
    }

    if (loop && !uv_is_closing((uv_handle_t *)&ctxPoint->pipe)) {
        uv_close((uv_handle_t *)&ctxPoint->pipe, nullptr);
    }
    if (ctxPoint->hasNewFd && loop && !uv_is_closing((uv_handle_t *)&ctxPoint->newFd)) {
        uv_close((uv_handle_t *)&ctxPoint->newFd, nullptr);
    }
    delete ctxPoint;
    ctxPoint = nullptr;
    HiLog::Debug(LABEL, "HdcJdwpSimulator::FreeContext end");
}

bool HdcJdwpSimulator::Connect()
{
    string jdwpCtrlName = "\0jdwp-control";
    uv_connect_t *connect = new uv_connect_t();
    ctxPoint = (HCtxJdwpSimulator)MallocContext();
    if (!ctxPoint) {
        HiLog::Info(LABEL, "MallocContext failed");
        return false;
    }
    connect->data = ctxPoint;
    uv_pipe_init(loop, (uv_pipe_t *)&ctxPoint->pipe, 1);
    HiLog::Info(LABEL, " HdcJdwpSimulator Connect begin");
    uv_pipe_connect(connect, &ctxPoint->pipe, jdwpCtrlName.c_str(), ConnectJdwp);
    return true;
}

void HdcJdwpSimulator::stop()
{
    HiLog::Debug(LABEL, "HdcJdwpSimulator::stop.");
    FreeContext();
}
