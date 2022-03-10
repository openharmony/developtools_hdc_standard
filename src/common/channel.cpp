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
#include "channel.h"
namespace Hdc {
HdcChannelBase::HdcChannelBase(const bool serverOrClient, const string &addrString, uv_loop_t *loopMainIn)
{
    SetChannelTCPString(addrString);
    isServerOrClient = serverOrClient;
    loopMain = loopMainIn;
    threadChanneMain = uv_thread_self();
    uv_rwlock_init(&mainAsync);
    uv_async_init(loopMain, &asyncMainLoop, MainAsyncCallback);
    uv_rwlock_init(&lockMapChannel);
}

HdcChannelBase::~HdcChannelBase()
{
    ClearChannels();
    // clear
    if (!uv_is_closing((uv_handle_t *)&asyncMainLoop)) {
        uv_close((uv_handle_t *)&asyncMainLoop, nullptr);
    }

    uv_rwlock_destroy(&mainAsync);
    uv_rwlock_destroy(&lockMapChannel);
}

vector<uint8_t> HdcChannelBase::GetChannelHandshake(string &connectKey) const
{
    vector<uint8_t> ret;
    struct ChannelHandShake handshake = {};
    if (strcpy_s(handshake.banner, sizeof(handshake.banner), HANDSHAKE_MESSAGE.c_str()) != EOK) {
        return ret;
    }
    if (strcpy_s(handshake.connectKey, sizeof(handshake.connectKey), connectKey.c_str()) != EOK) {
        return ret;
    }
    ret.insert(ret.begin(), (uint8_t *)&handshake, (uint8_t *)&handshake + sizeof(ChannelHandShake));
    return ret;
}

bool HdcChannelBase::SetChannelTCPString(const string &addrString)
{
    bool ret = false;
    while (true) {
        if (addrString.find(":") == string::npos) {
            break;
        }
        string host = addrString.substr(0, addrString.find(":"));
        string port = addrString.substr(addrString.find(":") + 1);
        channelPort = std::atoi(port.c_str());
        sockaddr_in addr;
        if (!channelPort || uv_ip4_addr(host.c_str(), channelPort, &addr) != 0) {
            break;
        }
        channelHost = host;
        channelHostPort = addrString;
        ret = true;
        break;
    }
    if (!ret) {
        channelPort = 0;
        channelHost = STRING_EMPTY;
        channelHostPort = STRING_EMPTY;
    }
    return ret;
}

void HdcChannelBase::ClearChannels()
{
    for (auto v : mapChannel) {
        HChannelPtr hChannelPtr = (HChannelPtr)v.second;
        if (!hChannelPtr->isDead) {
            FreeChannel(hChannelPtr->channelId);
        }
    }
}

void HdcChannelBase::WorkerPendding()
{
    WRITE_LOG(LOG_DEBUG, "Begin host channel pendding");
    uv_run(loopMain, UV_RUN_DEFAULT);
    uv_loop_close(loopMain);
}

void HdcChannelBase::ReadStream(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf)
{
    if (tcp == nullptr || tcp->data == nullptr) {
        return;
    }
    int size = 0;
    int indexBuf = 0;
    int childRet = 0;
    bool needExit = false;
    HChannelPtr hChannelPtr = (HChannelPtr)tcp->data;
    HdcChannelBase *thisClass = (HdcChannelBase *)hChannelPtr->clsChannel;
    if (thisClass == nullptr) {
        return;
    }

    if (nread == UV_ENOBUFS) {
        WRITE_LOG(LOG_DEBUG, "HdcChannelBase::ReadStream Pipe IOBuf max");
        return;
    } else if (nread == 0) {
        // maybe just afer accept, second client req
        WRITE_LOG(LOG_DEBUG, "HdcChannelBase::ReadStream idle read");
        return;
    } else if (nread < 0) {
        Base::TryCloseHandle((uv_handle_t *)tcp);
        constexpr int bufSize = 1024;
        char buffer[bufSize] = { 0 };
        uv_err_name_r(nread, buffer, bufSize);
        WRITE_LOG(LOG_DEBUG, "HdcChannelBase::ReadStream failed2:%s", buffer);
        needExit = true;
        goto Finish;
    } else {
        hChannelPtr->availTailIndex += nread;
    }
    while (hChannelPtr->availTailIndex > DWORD_SERIALIZE_SIZE) {
        size = ntohl(*(uint32_t *)(hChannelPtr->ioBuf + indexBuf));  // big endian
        if (size <= 0 || (uint32_t)size > HDC_BUF_MAX_BYTES) {
            needExit = true;
            break;
        }
        if (hChannelPtr->availTailIndex - DWORD_SERIALIZE_SIZE < size) {
            break;
        }
        childRet = thisClass->ReadChannel(hChannelPtr, (uint8_t *)hChannelPtr->ioBuf + DWORD_SERIALIZE_SIZE + indexBuf, size);
        if (childRet < 0) {
            if (!hChannelPtr->keepAlive) {
                needExit = true;
                break;
            }
        }
        // update io
        hChannelPtr->availTailIndex -= (DWORD_SERIALIZE_SIZE + size);
        indexBuf += DWORD_SERIALIZE_SIZE + size;
    }
    if (indexBuf > 0 && hChannelPtr->availTailIndex > 0) {
        if (memmove_s(hChannelPtr->ioBuf, hChannelPtr->bufSize, hChannelPtr->ioBuf + indexBuf, hChannelPtr->availTailIndex)) {
            needExit = true;
            goto Finish;
        }
    }

Finish:
    if (needExit) {
        thisClass->FreeChannel(hChannelPtr->channelId);
        WRITE_LOG(LOG_DEBUG, "Read Stream needExit, FreeChannel finish");
    }
}

void HdcChannelBase::WriteCallback(uv_write_t *req, int status)
{
    if (req == nullptr || req->handle == nullptr || req->handle->data == nullptr) {
        return;
    }
    HChannelPtr hChannelPtr = (HChannelPtr)req->handle->data;
    --hChannelPtr->ref;
    HdcChannelBase *thisClass = (HdcChannelBase *)hChannelPtr->clsChannel;
    if (thisClass == nullptr) {
        return;
    }
    if (status < 0) {
        Base::TryCloseHandle((uv_handle_t *)req->handle);
        if (!hChannelPtr->isDead && !hChannelPtr->ref) {
            thisClass->FreeChannel(hChannelPtr->channelId);
            WRITE_LOG(LOG_DEBUG, "WriteCallback TryCloseHandle");
        }
    }
    if (req->data != nullptr) {
        delete[]((uint8_t *)req->data);
    }
    delete req;
}

void HdcChannelBase::AsyncMainLoopTask(uv_idle_t *handle)
{
    if (handle == nullptr || handle->data == nullptr) {
        return;
    }
    AsyncParam *param = (AsyncParam *)handle->data;
    HdcChannelBase *thisClass = (HdcChannelBase *)param->thisClass;
    if (thisClass == nullptr) {
        return;
    }

    switch (param->method) {
        case ASYNC_FREE_CHANNEL: {
            // alloc/release should pair in main thread.
            thisClass->FreeChannel(param->sid);
            break;
        }
        default:
            break;
    }
    if (param->data) {
        delete[]((uint8_t *)param->data);
    }
    delete param;
    uv_close((uv_handle_t *)handle, Base::CloseIdleCallback);
}

// multiple uv_async_send() calls may be merged by libuv，so not each call will yield callback as expected.
// eg: if uv_async_send() 5 times before callback calling，it will be called only once.
// if uv_async_send() is called again after callback calling, it will be called again.
void HdcChannelBase::MainAsyncCallback(uv_async_t *handle)
{
    if (handle == nullptr || handle->data == nullptr) {
        return;
    }
    HdcChannelBase *thisClass = (HdcChannelBase *)handle->data;
    if (uv_is_closing((uv_handle_t *)thisClass->loopMain)) {
        return;
    }
    list<void *>::iterator i;
    list<void *> &lst = thisClass->lstMainThreadOP;
    uv_rwlock_wrlock(&thisClass->mainAsync);
    for (i = lst.begin(); i != lst.end();) {
        AsyncParam *param = (AsyncParam *)*i;
        Base::IdleUvTask(thisClass->loopMain, param, AsyncMainLoopTask);
        i = lst.erase(i);
    }
    uv_rwlock_wrunlock(&thisClass->mainAsync);
}

void HdcChannelBase::PushAsyncMessage(const uint32_t channelId, const uint8_t method, const void *data,
                                      const int dataSize)
{
    if (uv_is_closing((uv_handle_t *)&asyncMainLoop)) {
        return;
    }
    auto param = new(std::nothrow) AsyncParam();
    if (!param) {
        return;
    }
    param->sid = channelId;  // Borrow SID storage
    param->thisClass = this;
    param->method = method;
    if (dataSize > 0) {
        param->dataSize = dataSize;
        param->data = new(std::nothrow) uint8_t[param->dataSize]();
        if (!param->data) {
            delete param;
            return;
        }
        if (memcpy_s((uint8_t *)param->data, param->dataSize, data, dataSize) != EOK) {
            delete[]((uint8_t *)param->data);
            delete param;
            return;
        }
    }
    asyncMainLoop.data = this;
    uv_rwlock_wrlock(&mainAsync);
    lstMainThreadOP.push_back(param);
    uv_rwlock_wrunlock(&mainAsync);
    uv_async_send(&asyncMainLoop);
}

void HdcChannelBase::SendChannel(HChannelPtr hChannelPtr, uint8_t *bufPtr, const int size)
{
    uv_stream_t *sendStream = nullptr;
    int sizeNewBuf = size + DWORD_SERIALIZE_SIZE;
    auto data = new(std::nothrow) uint8_t[sizeNewBuf]();
    if (!data) {
        return;
    }
    *(uint32_t *)data = htonl(size);  // big endian
    if (memcpy_s(data + DWORD_SERIALIZE_SIZE, sizeNewBuf - DWORD_SERIALIZE_SIZE, bufPtr, size) != EOK) {
        delete[] data;
        return;
    }
    if (hChannelPtr->hWorkThread == uv_thread_self()) {
        sendStream = (uv_stream_t *)&hChannelPtr->hWorkTCP;
    } else {
        sendStream = (uv_stream_t *)&hChannelPtr->hChildWorkTCP;
    }
    if (!uv_is_closing((const uv_handle_t *)sendStream) && uv_is_writable(sendStream)) {
        ++hChannelPtr->ref;
        Base::SendToStreamEx(sendStream, data, sizeNewBuf, nullptr, (void *)WriteCallback, data);
    } else {
        delete[] data;
    }
}

// works only in current working thread
void HdcChannelBase::Send(const uint32_t channelId, uint8_t *bufPtr, const int size)
{
    HChannelPtr hChannelPtr = (HChannelPtr)AdminChannel(OP_QUERY_REF, channelId, nullptr);
    if (!hChannelPtr) {
        return;
    }
    do {
        if (hChannelPtr->isDead) {
            break;
        }
        SendChannel(hChannelPtr, bufPtr, size);
    } while (false);
    --hChannelPtr->ref;
}

void HdcChannelBase::AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    if (handle == nullptr || handle->data == nullptr || buf == nullptr) {
        return;
    }
    HChannelPtr context = (HChannelPtr)handle->data;
    Base::ReallocBuf(&context->ioBuf, &context->bufSize, Base::GetMaxBufSize() * 4);
    buf->base = (char *)context->ioBuf + context->availTailIndex;
    buf->len = context->bufSize - context->availTailIndex;
}

uint32_t HdcChannelBase::GetChannelPseudoUid()
{
    uint32_t uid = 0;
    HChannelPtr hInput = nullptr;
    do {
        uid = static_cast<uint32_t>(Base::GetRandom());
    } while ((hInput = AdminChannel(OP_QUERY, uid, nullptr)) != nullptr);
    return uid;
}

uint32_t HdcChannelBase::MallocChannel(HChannelPtr *hOutChannel)
{
    if (hOutChannel == nullptr) {
        return 0;
    }
    auto hChannelPtr = new(std::nothrow) HdcChannel();
    if (!hChannelPtr) {
        return 0;
    }
    uint32_t channelId = GetChannelPseudoUid();
    if (isServerOrClient) {
        hChannelPtr->serverOrClient = isServerOrClient;
        ++channelId;  // Use different value for serverForClient&client in per process
    }
    uv_tcp_init(loopMain, &hChannelPtr->hWorkTCP);
    ++hChannelPtr->uvHandleRef;
    hChannelPtr->hWorkThread = uv_thread_self();
    hChannelPtr->hWorkTCP.data = hChannelPtr;
    hChannelPtr->clsChannel = this;
    hChannelPtr->channelId = channelId;
    AdminChannel(OP_ADD, channelId, hChannelPtr);
    *hOutChannel = hChannelPtr;
    WRITE_LOG(LOG_DEBUG, "Mallocchannel:%u", channelId);
    return channelId;
}

// work when libuv-handle at struct of HdcSession has all callback finished
void HdcChannelBase::FreeChannelFinally(uv_idle_t *handle)
{
    if (handle == nullptr || handle->data == nullptr) {
        return;
    }
    HChannelPtr hChannelPtr = (HChannelPtr)handle->data;
    HdcChannelBase *thisClass = (HdcChannelBase *)hChannelPtr->clsChannel;
    if (hChannelPtr->uvHandleRef > 0 || thisClass == nullptr) {
        return;
    }
    thisClass->NotifyInstanceChannelFree(hChannelPtr);
    thisClass->AdminChannel(OP_REMOVE, hChannelPtr->channelId, nullptr);
    WRITE_LOG(LOG_DEBUG, "!!!FreeChannelFinally channelId:%u finish", hChannelPtr->channelId);
    if (!hChannelPtr->serverOrClient) {
        uv_stop(thisClass->loopMain);
    }
    delete hChannelPtr;
    Base::TryCloseHandle((const uv_handle_t *)handle, Base::CloseIdleCallback);
}

void HdcChannelBase::FreeChannelContinue(HChannelPtr hChannelPtr)
{
    if (hChannelPtr == nullptr) {
        return;
    }
    auto closeChannelHandle = [](uv_handle_t *handle) -> void {
        HChannelPtr hChannelPtr = (HChannelPtr)handle->data;
        --hChannelPtr->uvHandleRef;
        Base::TryCloseHandle((uv_handle_t *)handle);
    };
    hChannelPtr->availTailIndex = 0;
    if (hChannelPtr->ioBuf) {
        delete[] hChannelPtr->ioBuf;
        hChannelPtr->ioBuf = nullptr;
    }
    if (!hChannelPtr->serverOrClient) {
        Base::TryCloseHandle((uv_handle_t *)&hChannelPtr->stdinTty, closeChannelHandle);
        Base::TryCloseHandle((uv_handle_t *)&hChannelPtr->stdoutTty, closeChannelHandle);
    }
    if (uv_is_closing((const uv_handle_t *)&hChannelPtr->hWorkTCP)) {
        --hChannelPtr->uvHandleRef;
    } else {
        Base::TryCloseHandle((uv_handle_t *)&hChannelPtr->hWorkTCP, closeChannelHandle);
    }
    Base::IdleUvTask(loopMain, hChannelPtr, FreeChannelFinally);
}

void HdcChannelBase::FreeChannelOpeate(uv_timer_t *handle)
{
    if (handle == nullptr || handle->data == nullptr) {
        return;
    }
    HChannelPtr hChannelPtr = (HChannelPtr)handle->data;
    HdcChannelBase *thisClass = (HdcChannelBase *)hChannelPtr->clsChannel;
    if (hChannelPtr->ref > 0 || thisClass == nullptr) {
        return;
    }
    if (hChannelPtr->hChildWorkTCP.loop) {
        auto ctrl = HdcSessionBase::BuildCtrlString(SP_DEATCH_CHANNEL, hChannelPtr->channelId, nullptr, 0);
        thisClass->ChannelSendSessionCtrlMsg(ctrl, hChannelPtr->targetSessionId);
        auto callbackCheckFreeChannelContinue = [](uv_timer_t *handle) -> void {
            HChannelPtr hChannelPtr = (HChannelPtr)handle->data;
            HdcChannelBase *thisClass = (HdcChannelBase *)hChannelPtr->clsChannel;
            if (!hChannelPtr->childCleared) {
                return;
            }
            Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
            thisClass->FreeChannelContinue(hChannelPtr);
        };
        Base::TimerUvTask(thisClass->loopMain, hChannelPtr, callbackCheckFreeChannelContinue);
    } else {
        thisClass->FreeChannelContinue(hChannelPtr);
    }
    Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
}

void HdcChannelBase::FreeChannel(const uint32_t channelId)
{
    if (threadChanneMain != uv_thread_self()) {
        PushAsyncMessage(channelId, ASYNC_FREE_CHANNEL, nullptr, 0);
        return;
    }
    HChannelPtr hChannelPtr = AdminChannel(OP_QUERY, channelId, nullptr);
    do {
        if (!hChannelPtr || hChannelPtr->isDead) {
            break;
        }
        WRITE_LOG(LOG_DEBUG, "Begin to free channel, channelid:%u", channelId);
        Base::TimerUvTask(loopMain, hChannelPtr, FreeChannelOpeate, MINOR_TIMEOUT);  // do immediately
        hChannelPtr->isDead = true;
    } while (false);
}

HChannelPtr HdcChannelBase::AdminChannel(const uint8_t op, const uint32_t channelId, HChannelPtr hInput)
{
    HChannelPtr hRet = nullptr;
    switch (op) {
        case OP_ADD:
            uv_rwlock_wrlock(&lockMapChannel);
            mapChannel[channelId] = hInput;
            uv_rwlock_wrunlock(&lockMapChannel);
            break;
        case OP_REMOVE:
            uv_rwlock_wrlock(&lockMapChannel);
            mapChannel.erase(channelId);
            uv_rwlock_wrunlock(&lockMapChannel);
            break;
        case OP_QUERY:
            uv_rwlock_rdlock(&lockMapChannel);
            if (mapChannel.count(channelId)) {
                hRet = mapChannel[channelId];
            }
            uv_rwlock_rdunlock(&lockMapChannel);
            break;
        case OP_QUERY_REF:
            uv_rwlock_wrlock(&lockMapChannel);
            if (mapChannel.count(channelId)) {
                hRet = mapChannel[channelId];
                ++hRet->ref;
            }
            uv_rwlock_wrunlock(&lockMapChannel);
            break;
        case OP_UPDATE:
            uv_rwlock_wrlock(&lockMapChannel);
            // remove old
            mapChannel.erase(channelId);
            if (hInput != nullptr) {
                mapChannel[hInput->channelId] = hInput;
            }
            uv_rwlock_wrunlock(&lockMapChannel);
            break;
        default:
            break;
    }
    return hRet;
}

void HdcChannelBase::EchoToClient(HChannelPtr hChannelPtr, uint8_t *bufPtr, const int size)
{
    if (hChannelPtr == nullptr) {
        return;
    }

    uv_stream_t *sendStream = nullptr;
    int sizeNewBuf = size + DWORD_SERIALIZE_SIZE;
    auto data = new(std::nothrow) uint8_t[sizeNewBuf]();
    if (!data) {
        return;
    }
    *(uint32_t *)data = htonl(size);
    if (memcpy_s(data + DWORD_SERIALIZE_SIZE, sizeNewBuf - DWORD_SERIALIZE_SIZE, bufPtr, size) != EOK) {
        delete[] data;
        return;
    }
    sendStream = (uv_stream_t *)&hChannelPtr->hChildWorkTCP;
    if (!uv_is_closing((const uv_handle_t *)sendStream) && uv_is_writable(sendStream)) {
        ++hChannelPtr->ref;
        Base::SendToStreamEx(sendStream, data, sizeNewBuf, nullptr, (void *)WriteCallback, data);
    } else {
        WRITE_LOG(LOG_WARN, "EchoToClient, channelId:%u is unwritable.", hChannelPtr->channelId);
        delete[] data;
    }
}

void HdcChannelBase::EchoToAllChannelsViaSessionId(uint32_t targetSessionId, const string &echo)
{
    for (auto v : mapChannel) {
        HChannelPtr hChannelPtr = (HChannelPtr)v.second;
        if (hChannelPtr != nullptr && !hChannelPtr->isDead && hChannelPtr->targetSessionId == targetSessionId) {
            WRITE_LOG(LOG_INFO, "%s:%u %s", __FUNCTION__, targetSessionId, echo.c_str());
            EchoToClient(hChannelPtr, (uint8_t *)echo.c_str(), echo.size());
        }
    }
}
}
