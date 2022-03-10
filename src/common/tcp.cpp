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
#include "tcp.h"

namespace Hdc {
HdcTCPBase::HdcTCPBase(const bool serverOrDaemonIn, void *ptrMainBase)
{
    // Calling the initialization
    InitialChildClass(serverOrDaemonIn, ptrMainBase);
}

HdcTCPBase::~HdcTCPBase()
{
}

// Subclasses must be explicitly called
void HdcTCPBase::InitialChildClass(const bool serverOrDaemonIn, void *ptrMainBase)
{
    serverOrDaemon = serverOrDaemonIn;
    clsMainBase = ptrMainBase;
}

void HdcTCPBase::RecvUDP(uv_udp_t *handle, ssize_t nread, const uv_buf_t *rcvbuf, const struct sockaddr *addr,
                         unsigned flags)
{
    if (handle == nullptr || rcvbuf == nullptr) {
        return;
    }
    while (true) {
        HdcTCPBase *thisClass = (HdcTCPBase *)handle->data;
        if (nread <= 0 || thisClass == nullptr) {
            // ==0 finish;<0 error
            break;
        }
        WRITE_LOG(LOG_DEBUG, "RecvUDP %s", rcvbuf->base);
        if (strncmp(rcvbuf->base, HANDSHAKE_MESSAGE.c_str(), HANDSHAKE_MESSAGE.size())) {
            break;
        }
        thisClass->RecvUDPEntry(addr, handle, rcvbuf);
        break;
    }
    if (rcvbuf->base != nullptr) {
        delete[] rcvbuf->base;
    }
}

void HdcTCPBase::AllocStreamUDP(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    int bufLen = BUF_SIZE_DEFAULT;
    char *pRecvBuf = (char *)new(std::nothrow) uint8_t[bufLen]();
    if (!pRecvBuf || buf == nullptr) {
        return;
    }
    buf->base = pRecvBuf;
    buf->len = bufLen;
}

void HdcTCPBase::SendUDPFinish(uv_udp_send_t *req, int status)
{
    if (req != nullptr) {
        delete req;
    }
}

void HdcTCPBase::ReadStream(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf)
{
    if (tcp == nullptr || tcp->data == nullptr) {
        return;
    }
    HSessionPtr hSessionPtr = (HSessionPtr)tcp->data;
    HdcTCPBase *thisClass = (HdcTCPBase *)hSessionPtr->classModule;
    if (thisClass == nullptr || thisClass->clsMainBase) {
        return;
    }
    HdcSessionBase *hSessionBase = (HdcSessionBase *)thisClass->clsMainBase;
    bool ret = false;
    while (true) {
        if (nread == UV_ENOBUFS) {
            WRITE_LOG(LOG_DEBUG, "Session IOBuf max");
            break;
        } else if (nread < 0) {
            // I originally in the IO main thread, no need to send asynchronous messages, close the socket as soon as
            // possible
            constexpr int bufSize = 1024;
            char buffer[bufSize] = { 0 };
            uv_strerror_r((int)nread, buffer, bufSize);
            WRITE_LOG(LOG_DEBUG, "HdcTCPBase::ReadStream < 0 %s", buffer);
            break;
        }
        if (hSessionBase->FetchIOBuf(hSessionPtr, hSessionPtr->ioBuf, nread) < 0) {
            break;
        }
        ret = true;
        break;
    }
    if (!ret) {
        // The first time is closed first, prevent the write function from continuing to write
        Base::TryCloseHandle(reinterpret_cast<uv_handle_t *>(tcp));
        hSessionBase->FreeSession(hSessionPtr->sessionId);
    }
}
}  // namespace Hdc
