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

HdcJdwpSimulator::HdcJdwpSimulator(uv_loop_t *loopIn)
{
    mLoop = loopIn;
    exit = false;
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
        bfr.base = (char *)buf;
        bfr.len = bufLen;
        if (!uv_is_writable(handleStream)) {
            HiLog::Info(LABEL, "SendToStream uv_is_unwritable!");
            delete reqWrite;
            break;
        }
        HiLog::Info(LABEL, "SendToStream pid_curr:%{public}s", buf);
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

// Process incoming data.  If no data is available, this will block until some
// arrives.
void HdcJdwpSimulator::ProcessIncoming(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
    HiLog::Debug(LABEL, "ProcessIncoming :%{public}d", nread);
    if (nread > 0) {
        std::unique_ptr<char[]> recv = std::make_unique<char[]>(nread + 1);
        std::memset(recv.get(), 0, nread);
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
    uv_tcp_init(thisClass->mLoop, &ctxJdwp->newFd);
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

void HdcJdwpSimulator::ConnectJdwp(uv_connect_t *connection, int status)
{
    HiLog::Debug(LABEL, "ConnectJdwp:%{public}d error:%{public}s", status, uv_err_name(status));
    char pid[4] = {0};
    int pid_curr = static_cast<int>(getpid());
    if (sprintf_s(pid, sizeof(pid), "%d", pid_curr) < 0) {
        HiLog::Info(LABEL, "ConnectJdwp set pid fail.");
    }
    HCtxJdwpSimulator ctxJdwp = (HCtxJdwpSimulator)connection->data;
    HdcJdwpSimulator *thisClass = static_cast<HdcJdwpSimulator *>(ctxJdwp->thisClass);
    HiLog::Info(LABEL, "ConnectJdwp pid:%{public}s", pid);
    thisClass->SendToStream((uv_stream_t *)connection->handle, (uint8_t *)pid, sizeof(pid),
                            (void *)FinishWriteCallback);
    HiLog::Info(LABEL, "ConnectJdwp reading , pid:%{public}s", pid);
    uv_read_start((uv_stream_t *)&ctxJdwp->pipe, thisClass->alloc_buffer, ReceiveNewFd);
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
    if (!mCtxPoint) {
        return;
    }

    if (mLoop && !uv_is_closing((uv_handle_t *)&mCtxPoint->pipe)) {
        uv_close((uv_handle_t *)&mCtxPoint->pipe, nullptr);
    }
    if (mCtxPoint->hasNewFd && mLoop && !uv_is_closing((uv_handle_t *)&mCtxPoint->newFd)) {
        uv_close((uv_handle_t *)&mCtxPoint->newFd, nullptr);
    }
    delete mCtxPoint;
    mCtxPoint = nullptr;
    HiLog::Debug(LABEL, "HdcJdwpSimulator::FreeContext end");
}

bool HdcJdwpSimulator::Connect()
{
    string jdwpCtrlName = "\0jdwp-control";
    uv_connect_t *connect = new uv_connect_t();
    mCtxPoint = (HCtxJdwpSimulator)MallocContext();
    if (!mCtxPoint) {
        HiLog::Info(LABEL, "MallocContext failed");
        return false;
    }
    connect->data = mCtxPoint;
    uv_pipe_init(mLoop, (uv_pipe_t *)&mCtxPoint->pipe, 1);
    HiLog::Info(LABEL, " HdcJdwpSimulator Connect begin");
    uv_pipe_connect(connect, &mCtxPoint->pipe, jdwpCtrlName.c_str(), ConnectJdwp);
    return true;
}

void HdcJdwpSimulator::stop()
{
    HiLog::Debug(LABEL, "HdcJdwpSimulator::stop.");
    FreeContext();
}