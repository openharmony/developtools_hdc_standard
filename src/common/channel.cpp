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
    uv_rwlock_init(&mainAsync);
    uv_async_init(loopMain, &asyncMainLoop, MainAsyncCallback);
    uv_rwlock_init(&lockMapChannel);
    uv_mutex_init(&freeChannel);
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
    uv_mutex_destroy(&freeChannel);
}

vector<uint8_t> HdcChannelBase::GetChannelHandshake(string &connectKey) const
{
    vector<uint8_t> ret;
    struct ChannelHandShake handshake = {{0}};
    Base::ZeroStruct(handshake);
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
    map<uint32_t, HChannel>::iterator iter;
    for (iter = mapChannel.begin(); iter != mapChannel.end();) {
        uint32_t channelId = iter->first;
        HChannel hChannel = iter->second;
        if (!hChannel->mainCleared) {
            FreeChannel(channelId);
            while (!hChannel->mainCleared) {
                usleep(1000);
            }
        }
        uv_rwlock_wrlock(&lockMapChannel);
        mapChannel.erase(iter++->first);
        uv_rwlock_wrunlock(&lockMapChannel);
        delete hChannel;
    }
    uv_rwlock_wrlock(&lockMapChannel);
    mapChannel.clear();
    uv_rwlock_wrunlock(&lockMapChannel);
}

void HdcChannelBase::WorkerPendding()
{
    WRITE_LOG(LOG_DEBUG, "Begin host channel pendding");
    uv_run(loopMain, UV_RUN_DEFAULT);
    uv_loop_close(loopMain);
}

void HdcChannelBase::ReadStream(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf)
{
    int size = 0;
    int indexBuf = 0;
    bool needExit = false;
    HChannel hChannel = (HChannel)tcp->data;
    HdcChannelBase *thisClass = (HdcChannelBase *)hChannel->clsChannel;

    if (nread == UV_ENOBUFS) {
        WRITE_LOG(LOG_DEBUG, "HdcChannelBase::ReadStream Pipe IOBuf max");
    } else if (nread <= 0) {
        Base::TryCloseHandle((uv_handle_t *)tcp);
        WRITE_LOG(LOG_DEBUG, "HdcChannelBase::ReadStream failed2:%s", uv_err_name(nread));
        needExit = true;
        goto Finish;
    } else {
        hChannel->availTailIndex += nread;
    }
    while (hChannel->availTailIndex > DWORD_SERIALIZE_SIZE) {
        size = ntohl(*(uint32_t *)(hChannel->ioBuf + indexBuf));  // big endian
        if (size <= 0 || (uint32_t)size > HDC_BUF_MAX_BYTES) {
            needExit = true;
            break;
        }
        if (hChannel->availTailIndex - DWORD_SERIALIZE_SIZE < size) {
            break;
        }
        if (thisClass->ReadChannel(hChannel, (uint8_t *)hChannel->ioBuf + DWORD_SERIALIZE_SIZE + indexBuf, size) < 0) {
            needExit = true;
            break;
        }
        // update io
        hChannel->availTailIndex -= (DWORD_SERIALIZE_SIZE + size);
        indexBuf += DWORD_SERIALIZE_SIZE + size;
    }
    if (indexBuf > 0 && hChannel->availTailIndex > 0) {
        if (EOK
            != memmove_s(hChannel->ioBuf, hChannel->bufSize, hChannel->ioBuf + indexBuf, hChannel->availTailIndex)) {
            needExit = true;
            goto Finish;
        }
    }

Finish:
    if (needExit) {
        WRITE_LOG(LOG_DEBUG, "Read Stream needExit");
        thisClass->FreeChannel(hChannel->channelId);
    }
}

void HdcChannelBase::WriteCallback(uv_write_t *req, int status)
{
    HChannel hChannel = (HChannel)req->handle->data;
    hChannel->sendRef--;
    HdcChannelBase *thisClass = (HdcChannelBase *)hChannel->clsChannel;
    if (status < 0) {
        Base::TryCloseHandle((uv_handle_t *)req->handle);
    }
    if (hChannel->channelDead && !hChannel->sendRef) {
        thisClass->FreeChannel(hChannel->channelId);
        WRITE_LOG(LOG_DEBUG, "WriteCallback TryCloseHandle");
    }
    delete[]((uint8_t *)req->data);
    delete req;
}

void HdcChannelBase::AsyncMainLoopTask(uv_idle_t *handle)
{
    AsyncParam *param = (AsyncParam *)handle->data;
    HdcChannelBase *thisClass = (HdcChannelBase *)param->thisClass;

    switch (param->method) {
        case ASYNC_FREE_SESSION: {
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
    HdcChannelBase *thisClass = (HdcChannelBase *)handle->data;
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
    auto param = new AsyncParam();
    if (!param) {
        return;
    }
    param->sid = channelId;  // Borrow SID storage
    param->thisClass = this;
    param->method = method;
    if (dataSize > 0) {
        param->dataSize = dataSize;
        param->data = new uint8_t[param->dataSize]();
        if (!param->data) {
            delete param;
            return;
        }
        if (memcpy_s((uint8_t *)param->data, param->dataSize, data, dataSize)) {
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

// client to server, or vice versa
// works only in current working thread
void HdcChannelBase::Send(const uint32_t channelId, uint8_t *bufPtr, const int size)
{
    uv_stream_t *sendStream = nullptr;
    int sizeNewBuf = size + DWORD_SERIALIZE_SIZE;
    HChannel hChannel = (HChannel)AdminChannel(OP_QUERY, channelId, nullptr);
    if (!hChannel || hChannel->channelDead) {
        return;
    }
    auto data = new uint8_t[sizeNewBuf]();
    if (!data) {
        return;
    }
    *(uint32_t *)data = htonl(size);  // big endian
    if (memcpy_s(data + DWORD_SERIALIZE_SIZE, sizeNewBuf - DWORD_SERIALIZE_SIZE, bufPtr, size)) {
        delete[] data;
        return;
    }
    uv_mutex_lock(&hChannel->sendMutex);
    hChannel->sendRef++;
    if (hChannel->hWorkThread == uv_thread_self()) {
        sendStream = (uv_stream_t *)&hChannel->hWorkTCP;
    } else {
        sendStream = (uv_stream_t *)&hChannel->hChildWorkTCP;
    }
    if (uv_is_writable(sendStream)) {
        Base::SendToStreamEx(sendStream, data, sizeNewBuf, nullptr, (void *)WriteCallback, data);
    }
    uv_mutex_unlock(&hChannel->sendMutex);
}

void HdcChannelBase::AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    if (sizeWanted <= 0) {
        return;
    }
    HChannel context = (HChannel)handle->data;
    Base::ReallocBuf(&context->ioBuf, &context->bufSize, context->availTailIndex, sizeWanted);
    buf->base = (char *)context->ioBuf + context->availTailIndex;
    buf->len = context->bufSize - context->availTailIndex - 1;
    if (buf->len < 0) {
        buf->len = 0;
    }
}

uint32_t HdcChannelBase::MallocChannel(HChannel *hOutChannel)
{
    auto hChannel = new HdcChannel();
    if (!hChannel) {
        return 0;
    }
    uint32_t channelId = Base::GetRuntimeMSec();
    if (isServerOrClient) {
        hChannel->serverOrClient = isServerOrClient;
        channelId++;  // Use different value for serverForClient&client in per process
    }
    uv_tcp_init(loopMain, &hChannel->hWorkTCP);
    hChannel->hWorkThread = uv_thread_self();
    hChannel->hWorkTCP.data = hChannel;
    hChannel->clsChannel = this;
    hChannel->channelId = channelId;
    AdminChannel(OP_ADD, channelId, hChannel);
    *hOutChannel = hChannel;
    uv_mutex_init(&hChannel->sendMutex);
    WRITE_LOG(LOG_DEBUG, "Mallocchannel:%d", channelId);
    return channelId;
}

void HdcChannelBase::FreeChannelContinue(HChannel hChannel)
{
    // Call from main thread only
    NotifyInstanceChannelFree(hChannel);
    if (hChannel->hChildWorkTCP.loop) {
        uint8_t abyteFlag[5] = { 0 };
        abyteFlag[0] = SP_DEATCH_CHANNEL;
        if (memcpy_s(abyteFlag + 1, sizeof(abyteFlag) - 1, &hChannel->channelId, 4)) {
        }
        Base::SendToStream((uv_stream_t *)&hChannel->targetSession->ctrlPipe[STREAM_MAIN], (uint8_t *)&abyteFlag, 5);
        // If there is blocking problem in the later stage, we can consider changing the whole release process to
        // asynchronous implementation like FreeSession
        while (!hChannel->childCleared) {
            usleep(1000);
        }
    }
    uv_mutex_destroy(&hChannel->sendMutex);
    if (hChannel->ioBuf) {
        hChannel->availTailIndex = 0;
        hChannel->bufSize = 0;
        delete[] hChannel->ioBuf;
        hChannel->ioBuf = nullptr;
    }
    Base::TryCloseHandle((uv_handle_t *)&hChannel->hWorkTCP);
    // Notify main thread exit for client instance
    if (!hChannel->serverOrClient) {
        Base::TryCloseHandle((uv_handle_t *)&hChannel->stdinPipe);
        Base::TryCloseHandle((uv_handle_t *)&hChannel->stdoutPipe);
        Base::TryCloseHandle((uv_handle_t *)&hChannel->stdinTty);
        Base::TryCloseHandle((uv_handle_t *)&hChannel->stdoutTty);
        uv_stop(loopMain);
    }
    hChannel->mainCleared = true;
    uv_mutex_unlock(&freeChannel);
    WRITE_LOG(LOG_DEBUG, "Freechannel finish id:%d sendref:%d", hChannel->channelId, uint32_t(hChannel->sendRef));
}

void HdcChannelBase::FreeChannel(const uint32_t channelId)
{
    bool bNotTodo = true;
    HChannel hChannel = AdminChannel(OP_QUERY, channelId, nullptr);
    if (!hChannel) {
        return;
    }
    hChannel->channelDead = true;
    // Two cases: alloc in main thread, or work thread
    if (hChannel->hWorkThread != uv_thread_self()) {
        PushAsyncMessage(hChannel->channelId, ASYNC_FREE_SESSION, nullptr, 0);
        return;
    }
    uv_mutex_lock(&freeChannel);
    while (true) {
        if (!hChannel) {
            break;
        }
        if (hChannel->sendRef) {
            break;  // still sending, early exit
        }
        if (hChannel->mainCleared) {
            break;
        }
        bNotTodo = false;
        break;
    }
    if (bNotTodo) {
        uv_mutex_unlock(&freeChannel);
        return;
    }
    FreeChannelContinue(hChannel);
}

HChannel HdcChannelBase::AdminChannel(const uint8_t op, const uint32_t channelId, HChannel hInput)
{
    HChannel hRet = nullptr;
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
        case OP_UPDATE:
            uv_rwlock_wrlock(&lockMapChannel);
            // remove old
            mapChannel.erase(channelId);
            mapChannel[hInput->channelId] = hInput;
            uv_rwlock_wrunlock(&lockMapChannel);
            break;
        default:
            break;
    }
    return hRet;
}
}