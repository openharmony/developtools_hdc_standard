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
#include "session.h"
#include "serial_struct.h"

namespace Hdc {
HdcSessionBase::HdcSessionBase(bool serverOrDaemonIn)
{
    // server/daemon common initialization code
    string threadNum = std::to_string(SIZE_THREAD_POOL);
    uv_os_setenv("UV_THREADPOOL_SIZE", threadNum.c_str());
    uv_loop_init(&loopMain);
    WRITE_LOG(LOG_DEBUG, "loopMain init");
    uv_rwlock_init(&mainAsync);
    uv_async_init(&loopMain, &asyncMainLoop, MainAsyncCallback);
    uv_rwlock_init(&lockMapSession);
    serverOrDaemon = serverOrDaemonIn;
    ctxUSB = nullptr;
    wantRestart = false;

#ifdef HDC_HOST
    if (serverOrDaemon) {
        libusb_init((libusb_context **)&ctxUSB);
    }
#endif
}

HdcSessionBase::~HdcSessionBase()
{
    Base::TryCloseHandle((uv_handle_t *)&asyncMainLoop);
    uv_loop_close(&loopMain);
    // clear base
    uv_rwlock_destroy(&mainAsync);
    uv_rwlock_destroy(&lockMapSession);
#ifdef HDC_HOST
    if (serverOrDaemon) {
        libusb_exit((libusb_context *)ctxUSB);
    }
#endif
    WRITE_LOG(LOG_DEBUG, "~HdcSessionBase free sessionRef:%d instance:%s", uint32_t(sessionRef),
              serverOrDaemon ? "server" : "daemon");
}

// remove step2
bool HdcSessionBase::TryRemoveTask(HTaskInfo hTask)
{
    if (hTask->taskFree) {
        return true;
    }
    bool ret = RemoveInstanceTask(OP_REMOVE, hTask);
    if (ret) {
        hTask->taskFree = true;
    } else {
        // This is used to check that the memory cannot be cleaned up. If the memory cannot be released, break point
        // here to see which task has not been released
        // print task clear
    }
    return ret;
}

// remove step1
bool HdcSessionBase::BeginRemoveTask(HTaskInfo hTask)
{
    bool ret = true;
    if (hTask->taskStop || hTask->taskFree || !hTask->taskClass) {
        return true;
    }

    WRITE_LOG(LOG_DEBUG, "BeginRemoveTask taskType:%d", hTask->taskType);
    ret = RemoveInstanceTask(OP_CLEAR, hTask);
    auto taskClassDeleteRetry = [](uv_idle_t *handle) -> void {
        HTaskInfo hTask = (HTaskInfo)handle->data;
        HdcSessionBase *thisClass = (HdcSessionBase *)hTask->ownerSessionClass;
        if (!thisClass->TryRemoveTask(hTask)) {
            return;
        }
        HSession hSession = thisClass->AdminSession(OP_QUERY, hTask->sessionId, nullptr);
        thisClass->AdminTask(OP_REMOVE, hSession, hTask->channelId, nullptr);
        WRITE_LOG(LOG_DEBUG, "TaskDelay task remove finish, channelId:%d", hTask->channelId);
        delete hTask;
        Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseIdleCallback);
    };
    Base::IdleUvTask(hTask->runLoop, hTask, taskClassDeleteRetry);

    hTask->taskStop = true;
    ret = true;
    return ret;
}

// Clear all Task or a single Task, the regular situation is stopped first, and the specific class memory is cleaned up
// after the end of the LOOP.
// When ChannelIdinput == 0, at this time, all of the LOOP ends, all runs in the class end, so directly skip STOP,
// physical memory deletion class trimming
void HdcSessionBase::ClearOwnTasks(HSession hSession, const uint32_t channelIDInput)
{
    // First case: normal task cleanup process (STOP Remove)
    // Second: The task is cleaned up, the session ends
    // Third: The task is cleaned up, and the session is directly over the session.
    map<uint32_t, HTaskInfo>::iterator iter;
    for (iter = hSession->mapTask->begin(); iter != hSession->mapTask->end();) {
        uint32_t channelId = iter->first;
        HTaskInfo hTask = iter->second;
        if (channelIDInput != 0) {  // single
            if (channelIDInput != channelId) {
                ++iter;
                continue;
            }
            BeginRemoveTask(hTask);
            WRITE_LOG(LOG_DEBUG, "ClearOwnTasks OP_CLEAR finish，session:%p channelIDInput:%d", hSession,
                      channelIDInput);
            break;
        }
        // multi
        BeginRemoveTask(hTask);
        ++iter;
    }
}

void HdcSessionBase::ClearSessions()
{
    // no need to lock mapSession
    // broadcast free singal
    for (auto v : mapSession) {
        HSession hSession = (HSession)v.second;
        if (!hSession->isDead) {
            FreeSession(hSession->sessionId);
        }
    }
}

void HdcSessionBase::ReMainLoopForInstanceClear()
{  // reloop
    auto clearSessionsForFinish = [](uv_idle_t *handle) -> void {
        HdcSessionBase *thisClass = (HdcSessionBase *)handle->data;
        if (thisClass->sessionRef > 0) {
            return;
        }
        // all task has been free
        uv_close((uv_handle_t *)handle, Base::CloseIdleCallback);
        uv_stop(&thisClass->loopMain);
    };
    Base::IdleUvTask(&loopMain, this, clearSessionsForFinish);
    uv_run(&loopMain, UV_RUN_DEFAULT);
};

void HdcSessionBase::EnumUSBDeviceRegister(void (*pCallBack)(HSession hSession))
{
    if (!pCallBack) {
        return;
    }
    uv_rwlock_rdlock(&lockMapSession);
    map<uint32_t, HSession>::iterator i;
    for (i = mapSession.begin(); i != mapSession.end(); ++i) {
        HSession hs = i->second;
        if (hs->connType != CONN_USB) {
            continue;
        }
        if (hs->hUSB == nullptr) {
            continue;
        }
        if (pCallBack) {
            pCallBack(hs);
        }
        break;
    }
    uv_rwlock_rdunlock(&lockMapSession);
}

// The PC side gives the device information, determines if the USB device is registered
// PDEV and Busid Devid two choices
HSession HdcSessionBase::QueryUSBDeviceRegister(void *pDev, int busIDIn, int devIDIn)
{
#ifdef HDC_HOST
    libusb_device *dev = (libusb_device *)pDev;
    HSession hResult = nullptr;
    if (!mapSession.size()) {
        return nullptr;
    }
    uint8_t busId = 0;
    uint8_t devId = 0;
    if (pDev) {
        busId = libusb_get_bus_number(dev);
        devId = libusb_get_device_address(dev);
    } else {
        busId = busIDIn;
        devId = devIDIn;
    }
    uv_rwlock_rdlock(&lockMapSession);
    map<uint32_t, HSession>::iterator i;
    for (i = mapSession.begin(); i != mapSession.end(); ++i) {
        HSession hs = i->second;
        if (hs->connType == CONN_USB) {
            continue;
        }
        if (hs->hUSB == nullptr) {
            continue;
        }
        if (hs->hUSB->devId != devId || hs->hUSB->busId != busId) {
            continue;
        }
        hResult = hs;
        break;
    }
    uv_rwlock_rdunlock(&lockMapSession);
    return hResult;
#else
    return nullptr;
#endif
}

void HdcSessionBase::AsyncMainLoopTask(uv_idle_t *handle)
{
    AsyncParam *param = (AsyncParam *)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)param->thisClass;
    switch (param->method) {
        case ASYNC_FREE_SESSION:
            // Destruction is unified in the main thread
            thisClass->FreeSession(param->sid);  // todo Double lock
            break;
        case ASYNC_STOP_MAINLOOP:
            uv_stop(&thisClass->loopMain);
            break;
        default:
            break;
    }
    if (param->data) {
        delete[]((uint8_t *)param->data);
    }
    delete param;
    param = nullptr;
    Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseIdleCallback);
}

void HdcSessionBase::MainAsyncCallback(uv_async_t *handle)
{
    HdcSessionBase *thisClass = (HdcSessionBase *)handle->data;
    list<void *>::iterator i;
    list<void *> &lst = thisClass->lstMainThreadOP;
    uv_rwlock_wrlock(&thisClass->mainAsync);
    for (i = lst.begin(); i != lst.end();) {
        AsyncParam *param = (AsyncParam *)*i;
        Base::IdleUvTask(&thisClass->loopMain, param, AsyncMainLoopTask);
        i = lst.erase(i);
    }
    uv_rwlock_wrunlock(&thisClass->mainAsync);
}

void HdcSessionBase::PushAsyncMessage(const uint32_t sessionId, const uint8_t method, const void *data,
                                      const int dataSize)
{
    AsyncParam *param = new AsyncParam();
    if (!param) {
        return;
    }
    param->sid = sessionId;
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

void HdcSessionBase::WorkerPendding()
{
    uv_run(&loopMain, UV_RUN_DEFAULT);
    ClearInstanceResource();
}

int HdcSessionBase::MallocSessionByConnectType(HSession hSession)
{
    int ret = 0;
    switch (hSession->connType) {
        case CONN_TCP: {
            uv_tcp_init(&loopMain, &hSession->hWorkTCP);
            ++hSession->uvRef;
            hSession->hWorkTCP.data = hSession;
            break;
        }
        case CONN_USB: {
            // Some members need to be placed at the primary thread
            HUSB hUSB = new HdcUSB();
            if (!hUSB) {
                ret = -1;
                break;
            }
            hSession->hUSB = hUSB;
#ifdef HDC_HOST
            constexpr auto maxBufFactor = 1.5;
            int max = Base::GetMaxBufSize() * maxBufFactor + sizeof(USBHead);
            hUSB->sizeEpBuf = max;
            hUSB->bulkInRead.buf = new uint8_t[max]();
            hUSB->bulkOutWrite.buf = new uint8_t[max]();
#else
#endif
            break;
        }
        default:
            ret = -1;
            break;
    }
    return ret;
}

// Avoid unit test when client\server\daemon on the same host, maybe get the same ID value
uint32_t HdcSessionBase::GetSessionPseudoUid()
{
    uint32_t uid = 0;
    Hdc::HSession hInput = nullptr;
    do {
        uid = static_cast<uint32_t>(Base::GetRandom());
    } while ((hInput = AdminSession(OP_QUERY, uid, nullptr)) != nullptr);
    return uid;
}

// when client 0 to automatic generated，when daemon First place 1 followed by
HSession HdcSessionBase::MallocSession(bool serverOrDaemon, const ConnType connType, void *classModule,
                                       uint32_t sessionId)
{
    HSession hSession = new HdcSession();
    if (!hSession) {
        return nullptr;
    }
    int ret = 0;
    ++sessionRef;
    memset_s(hSession->ctrlFd, sizeof(hSession->ctrlFd), 0, sizeof(hSession->ctrlFd));
    hSession->classInstance = this;
    hSession->connType = connType;
    hSession->classModule = classModule;
    hSession->isDead = false;
    hSession->sessionId = ((sessionId == 0) ? GetSessionPseudoUid() : sessionId);
    hSession->serverOrDaemon = serverOrDaemon;
    hSession->hWorkThread = uv_thread_self();
    hSession->mapTask = new map<uint32_t, HTaskInfo>();
    hSession->listKey = new list<void *>;
    hSession->uvRef = 0;
    // pullup child
    WRITE_LOG(LOG_DEBUG, "HdcSessionBase NewSession, sessionId:%u", hSession->sessionId);

    uv_tcp_init(&loopMain, &hSession->ctrlPipe[STREAM_MAIN]);
    ++hSession->uvRef;
    Base::CreateSocketPair(hSession->ctrlFd);
    uv_tcp_open(&hSession->ctrlPipe[STREAM_MAIN], hSession->ctrlFd[STREAM_MAIN]);
    uv_read_start((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], Base::AllocBufferCallback, ReadCtrlFromSession);
    hSession->ctrlPipe[STREAM_MAIN].data = hSession;
    hSession->ctrlPipe[STREAM_WORK].data = hSession;
    // Activate USB DAEMON's data channel, may not for use
    uv_tcp_init(&loopMain, &hSession->dataPipe[STREAM_MAIN]);
    ++hSession->uvRef;
    Base::CreateSocketPair(hSession->dataFd);
    uv_tcp_open(&hSession->dataPipe[STREAM_MAIN], hSession->dataFd[STREAM_MAIN]);
    hSession->dataPipe[STREAM_MAIN].data = hSession;
    hSession->dataPipe[STREAM_WORK].data = hSession;
    Base::SetTcpOptions(&hSession->dataPipe[STREAM_MAIN]);
    ret = MallocSessionByConnectType(hSession);
    if (ret) {
        delete hSession;
        hSession = nullptr;
    } else {
        AdminSession(OP_ADD, hSession->sessionId, hSession);
    }
    return hSession;
}

void HdcSessionBase::FreeSessionByConnectType(HSession hSession)
{
    if (CONN_USB == hSession->connType) {
        // ibusb All context is applied for sub-threaded, so it needs to be destroyed in the subline
        if (!hSession->hUSB) {
            return;
        }
        HUSB hUSB = hSession->hUSB;
        if (!hUSB) {
            return;
        }
#ifdef HDC_HOST
        if (hUSB->devHandle) {
            libusb_release_interface(hUSB->devHandle, hUSB->interfaceNumber);
            libusb_close(hUSB->devHandle);
            hUSB->devHandle = nullptr;
        }
        delete[] hUSB->bulkInRead.buf;
        delete[] hUSB->bulkOutWrite.buf;
#else
        if (hUSB->bulkIn > 0) {
            close(hUSB->bulkIn);
            hUSB->bulkIn = 0;
        }
        if (hUSB->bulkOut > 0) {
            close(hUSB->bulkOut);
            hUSB->bulkOut = 0;
        }
#endif
        delete hSession->hUSB;
        hSession->hUSB = nullptr;
    }
}

// work when libuv-handle at struct of HdcSession has all callback finished
void HdcSessionBase::FreeSessionFinally(uv_idle_t *handle)
{
    HSession hSession = (HSession)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    if (hSession->uvRef > 0) {
        return;
    }
    // Notify Server or Daemon, just UI or display commandline
    thisClass->NotifyInstanceSessionFree(hSession, true);
    // all hsession uv handle has been clear
    thisClass->AdminSession(OP_REMOVE, hSession->sessionId, nullptr);
    WRITE_LOG(LOG_DEBUG, "!!!FreeSessionFinally sessionId:%u finish", hSession->sessionId);
    delete hSession;
    hSession = nullptr;  // fix CodeMars SetNullAfterFree issue
    Base::TryCloseHandle((const uv_handle_t *)handle, Base::CloseIdleCallback);
    --thisClass->sessionRef;
}

// work when child-work thread finish
void HdcSessionBase::FreeSessionContinue(HSession hSession)
{
    auto closeSessionTCPHandle = [](uv_handle_t *handle) -> void {
        HSession hSession = (HSession)handle->data;
        --hSession->uvRef;
        Base::TryCloseHandle((uv_handle_t *)handle);
    };
    if (CONN_TCP == hSession->connType) {
        // Turn off TCP to prevent continuing writing
        Base::TryCloseHandle((uv_handle_t *)&hSession->hWorkTCP, true, closeSessionTCPHandle);
    }
    hSession->availTailIndex = 0;
    if (hSession->ioBuf) {
        delete[] hSession->ioBuf;
        hSession->ioBuf = nullptr;
    }
    Base::TryCloseHandle((uv_handle_t *)&hSession->ctrlPipe[STREAM_MAIN], true, closeSessionTCPHandle);
    Base::TryCloseHandle((uv_handle_t *)&hSession->dataPipe[STREAM_MAIN], true, closeSessionTCPHandle);
    delete hSession->mapTask;
    HdcAuth::FreeKey(!hSession->serverOrDaemon, hSession->listKey);
    delete hSession->listKey;  // to clear
    FreeSessionByConnectType(hSession);
    // finish
    Base::IdleUvTask(&loopMain, hSession, FreeSessionFinally);
}

void HdcSessionBase::FreeSessionOpeate(uv_timer_t *handle)
{
    HSession hSession = (HSession)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    if (hSession->sendRef > 0) {
        return;
    }
#ifdef HDC_HOST
    if (hSession->hUSB != nullptr && (hSession->hUSB->bulkInRead.working || hSession->hUSB->bulkOutWrite.working)) {
        return;
    }
#endif
    // wait workthread to free
    if (hSession->ctrlPipe[STREAM_WORK].loop) {
        auto ctrl = BuildCtrlString(SP_STOP_SESSION, 0, nullptr, 0);
        Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
        WRITE_LOG(LOG_DEBUG, "FreeSession, send workthread fo free. sessionId:%u", hSession->sessionId);
        auto callbackCheckFreeSessionContinue = [](uv_timer_t *handle) -> void {
            HSession hSession = (HSession)handle->data;
            HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
            if (!hSession->childCleared) {
                return;
            }
            Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
            thisClass->FreeSessionContinue(hSession);
        };
        Base::TimerUvTask(&thisClass->loopMain, hSession, callbackCheckFreeSessionContinue);
    } else {
        thisClass->FreeSessionContinue(hSession);
    }
    Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
}

void HdcSessionBase::FreeSession(const uint32_t sessionId)
{
    HSession hSession = AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hSession) {
        return;
    }
    if (hSession->hWorkThread != uv_thread_self()) {
        PushAsyncMessage(hSession->sessionId, ASYNC_FREE_SESSION, nullptr, 0);
        return;
    }
    if (hSession->isDead) {
        return;
    }
    hSession->isDead = true;
    Base::TimerUvTask(&loopMain, hSession, FreeSessionOpeate);
    NotifyInstanceSessionFree(hSession, false);
    WRITE_LOG(LOG_DEBUG, "FreeSession sessionId:%u sendref:%u", hSession->sessionId, uint16_t(hSession->sendRef));
}

HSession HdcSessionBase::AdminSession(const uint8_t op, const uint32_t sessionId, HSession hInput)
{
    HSession hRet = nullptr;
    switch (op) {
        case OP_ADD:
            uv_rwlock_wrlock(&lockMapSession);
            mapSession[sessionId] = hInput;
            uv_rwlock_wrunlock(&lockMapSession);
            break;
        case OP_REMOVE:
            uv_rwlock_wrlock(&lockMapSession);
            mapSession.erase(sessionId);
            uv_rwlock_wrunlock(&lockMapSession);
            break;
        case OP_QUERY:
            uv_rwlock_rdlock(&lockMapSession);
            if (mapSession.count(sessionId)) {
                hRet = mapSession[sessionId];
            }
            uv_rwlock_rdunlock(&lockMapSession);
            break;
        case OP_UPDATE:
            uv_rwlock_wrlock(&lockMapSession);
            // remove old
            mapSession.erase(sessionId);
            mapSession[hInput->sessionId] = hInput;
            uv_rwlock_wrunlock(&lockMapSession);
            break;
        default:
            break;
    }
    return hRet;
}

// All in the corresponding sub-thread, no need locks
HTaskInfo HdcSessionBase::AdminTask(const uint8_t op, HSession hSession, const uint32_t channelId, HTaskInfo hInput)
{
    HTaskInfo hRet = nullptr;
    map<uint32_t, HTaskInfo> &mapTask = *hSession->mapTask;
    switch (op) {
        case OP_ADD:
            mapTask[channelId] = hInput;
            break;
        case OP_REMOVE:
            mapTask.erase(channelId);
            break;
        case OP_QUERY:
            if (mapTask.count(channelId)) {
                hRet = mapTask[channelId];
            }
            break;
        default:
            break;
    }
    return hRet;
}

int HdcSessionBase::SendByProtocol(HSession hSession, uint8_t *bufPtr, const int bufLen)
{
    if (hSession->isDead) {
        return ERR_SESSION_NOFOUND;
    }
    int ret = 0;
    ++hSession->sendRef;
    switch (hSession->connType) {
        case CONN_TCP: {
            if (hSession->hWorkThread == uv_thread_self()) {
                ret = Base::SendToStreamEx((uv_stream_t *)&hSession->hWorkTCP, bufPtr, bufLen, nullptr,
                                           (void *)FinishWriteSessionTCP, bufPtr);
            } else if (hSession->hWorkChildThread == uv_thread_self()) {
                ret = Base::SendToStreamEx((uv_stream_t *)&hSession->hChildWorkTCP, bufPtr, bufLen, nullptr,
                                           (void *)FinishWriteSessionTCP, bufPtr);
            } else {
                WRITE_LOG(LOG_FATAL, "SendByProtocol uncontrol send");
                ret = ERR_API_FAIL;
            }
            break;
        }
        case CONN_USB: {
            HdcUSBBase *pUSB = ((HdcUSBBase *)hSession->classModule);
            ret = pUSB->SendUSBBlock(hSession, bufPtr, bufLen);
            delete[] bufPtr;
            break;
        }
        default:
            break;
    }
    return ret;
}

int HdcSessionBase::Send(const uint32_t sessionId, const uint32_t channelId, const uint16_t commandFlag,
                         const uint8_t *data, const int dataSize)
{
    HSession hSession = AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hSession) {
        WRITE_LOG(LOG_DEBUG, "Send to offline device, drop it, sessionId:%u", sessionId);
        return ERR_SESSION_NOFOUND;
    }
    PayloadProtect protectBuf;  // noneed convert to big-endian
    protectBuf.channelId = channelId;
    protectBuf.commandFlag = commandFlag;
    protectBuf.checkSum = (ENABLE_IO_CHECKSUM && dataSize > 0) ? Base::CalcCheckSum(data, dataSize) : 0;
    protectBuf.vCode = payloadProtectStaticVcode;
    string s = SerialStruct::SerializeToString(protectBuf);
    // reserve for encrypt here
    // xx-encrypt

    PayloadHead payloadHead;  // need convert to big-endian
    Base::ZeroStruct(payloadHead);
    payloadHead.flag[0] = PACKET_FLAG.at(0);
    payloadHead.flag[1] = PACKET_FLAG.at(1);
    payloadHead.protocolVer = VER_PROTOCOL;
    payloadHead.headSize = htons(s.size());
    payloadHead.dataSize = htonl(dataSize);
    int finalBufSize = sizeof(PayloadHead) + s.size() + dataSize;
    uint8_t *finayBuf = new uint8_t[finalBufSize]();
    if (finayBuf == nullptr) {
        return ERR_BUF_ALLOC;
    }
    bool bufRet = false;
    do {
        if (memcpy_s(finayBuf, sizeof(PayloadHead), reinterpret_cast<uint8_t *>(&payloadHead), sizeof(PayloadHead))) {
            break;
        }
        if (memcpy_s(finayBuf + sizeof(PayloadHead), s.size(),
                     reinterpret_cast<uint8_t *>(const_cast<char *>(s.c_str())), s.size())) {
            break;
        }
        if (dataSize > 0 && memcpy_s(finayBuf + sizeof(PayloadHead) + s.size(), dataSize, data, dataSize)) {
            break;
        }
        bufRet = true;
    } while (false);
    if (!bufRet) {
        delete[] finayBuf;
        return ERR_BUF_COPY;
    }
    return SendByProtocol(hSession, finayBuf, finalBufSize);
}

int HdcSessionBase::DecryptPayload(HSession hSession, PayloadHead *payloadHeadBe, uint8_t *encBuf)
{
    PayloadProtect protectBuf;
    Base::ZeroStruct(protectBuf);
    uint16_t headSize = ntohs(payloadHeadBe->headSize);
    int dataSize = ntohl(payloadHeadBe->dataSize);
    string encString(reinterpret_cast<char *>(encBuf), headSize);
    SerialStruct::ParseFromString(protectBuf, encString);
    if (protectBuf.vCode != payloadProtectStaticVcode) {
        WRITE_LOG(LOG_FATAL, "Session recv static vcode failed");
        return ERR_BUF_CHECK;
    }
    uint8_t *data = encBuf + headSize;
    if (protectBuf.checkSum != 0 && (protectBuf.checkSum != Base::CalcCheckSum(data, dataSize))) {
        WRITE_LOG(LOG_FATAL, "Session recv CalcCheckSum failed");
        return ERR_BUF_CHECK;
    }
    if (!FetchCommand(hSession, protectBuf.channelId, protectBuf.commandFlag, data, dataSize)) {
        WRITE_LOG(LOG_WARN, "FetchCommand failed");
        return ERR_GENERIC;
    }
    return RET_SUCCESS;
}

int HdcSessionBase::OnRead(HSession hSession, uint8_t *bufPtr, const int bufLen)
{
    int ret = ERR_GENERIC;
    if (memcmp(bufPtr, PACKET_FLAG.c_str(), 2)) {
        return ERR_BUF_CHECK;
    }
    struct PayloadHead *payloadHead = (struct PayloadHead *)bufPtr;
    int tobeReadLen = ntohl(payloadHead->dataSize) + ntohs(payloadHead->headSize);
    int packetHeadSize = sizeof(struct PayloadHead);
    if (tobeReadLen <= 0 || (uint32_t)tobeReadLen > HDC_BUF_MAX_BYTES) {
        // max 1G
        return ERR_BUF_CHECK;
    }
    if (bufLen - packetHeadSize < tobeReadLen) {
        return 0;
    }
    if (DecryptPayload(hSession, payloadHead, bufPtr + packetHeadSize)) {
        return ERR_BUF_CHECK;
    }
    ret = packetHeadSize + tobeReadLen;
    return ret;
}

// Returns <0 error;> 0 receives the number of bytes; 0 untreated
int HdcSessionBase::FetchIOBuf(HSession hSession, uint8_t *ioBuf, int read)
{
    HdcSessionBase *ptrConnect = (HdcSessionBase *)hSession->classInstance;
    int indexBuf = 0;
    int childRet = 0;
    if (read < 0) {
        return ERR_IO_FAIL;
    }
    hSession->availTailIndex += read;
    while (!hSession->isDead && hSession->availTailIndex > static_cast<int>(sizeof(PayloadHead))) {
        childRet = ptrConnect->OnRead(hSession, ioBuf + indexBuf, hSession->availTailIndex);
        if (childRet > 0) {
            hSession->availTailIndex -= childRet;
            indexBuf += childRet;
        } else if (childRet == 0) {
            // Not enough a IO
            break;
        } else {
            // <0
            hSession->availTailIndex = 0;  // Preventing malicious data packages
            indexBuf = ERR_BUF_SIZE;
            break;
        }
        // It may be multi-time IO to merge in a BUF, need to loop processing
    }
    if (indexBuf > 0 && hSession->availTailIndex > 0) {
        memmove_s(hSession->ioBuf, hSession->bufSize, hSession->ioBuf + indexBuf, hSession->availTailIndex);
        uint8_t *bufToZero = (uint8_t *)(hSession->ioBuf + hSession->availTailIndex);
        Base::ZeroBuf(bufToZero, hSession->bufSize - hSession->availTailIndex);
    }
    return indexBuf;
}

void HdcSessionBase::AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    HSession context = (HSession)handle->data;
    Base::ReallocBuf(&context->ioBuf, &context->bufSize, context->availTailIndex, sizeWanted);
    buf->base = (char *)context->ioBuf + context->availTailIndex;
    buf->len = context->bufSize - context->availTailIndex - 1;  // 16Bytes are retained to prevent memory sticking
    assert(buf->len >= 0);
}

void HdcSessionBase::FinishWriteSessionTCP(uv_write_t *req, int status)
{
    HSession hSession = (HSession)req->handle->data;
    --hSession->sendRef;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    if (status < 0) {
        Base::TryCloseHandle((uv_handle_t *)req->handle);
        if (!hSession->isDead && !hSession->sendRef) {
            WRITE_LOG(LOG_DEBUG, "FinishWriteSessionTCP freesession :%p", hSession);
            thisClass->FreeSession(hSession->sessionId);
        }
    }
    delete[]((uint8_t *)req->data);
    delete req;
}

bool HdcSessionBase::DispatchSessionThreadCommand(uv_stream_t *uvpipe, HSession hSession, const uint8_t *baseBuf,
                                                  const int bytesIO)
{
    bool ret = true;
    uint8_t flag = *(uint8_t *)baseBuf;

    switch (flag) {
        case SP_JDWP_NEWFD: {
            JdwpNewFileDescriptor(baseBuf, bytesIO);
            break;
        }
        default:
            WRITE_LOG(LOG_WARN, "Not support session command");
            break;
    }
    return ret;
}

void HdcSessionBase::ReadCtrlFromSession(uv_stream_t *uvpipe, ssize_t nread, const uv_buf_t *buf)
{
    HSession hSession = (HSession)uvpipe->data;
    HdcSessionBase *hSessionBase = (HdcSessionBase *)hSession->classInstance;
    while (true) {
        if (nread < 0) {
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed,%s", uv_strerror(nread));
            break;
        }
        if (nread > 64) {
            WRITE_LOG(LOG_WARN, "HdcSessionBase read overlap data");
            break;
        }
        // only one command, no need to split command from stream
        // if add more commands, consider the format command
        hSessionBase->DispatchSessionThreadCommand(uvpipe, hSession, (uint8_t *)buf->base, nread);
        break;
    }
    delete[] buf->base;
}

bool HdcSessionBase::WorkThreadStartSession(HSession hSession)
{
    bool regOK = false;
    int childRet = 0;
    if (hSession->connType == CONN_TCP) {
        HdcTCPBase *pTCPBase = (HdcTCPBase *)hSession->classModule;
        hSession->hChildWorkTCP.data = hSession;
        if ((childRet = uv_tcp_init(&hSession->childLoop, &hSession->hChildWorkTCP)) < 0) {
            WRITE_LOG(LOG_DEBUG, "HdcSessionBase SessionCtrl failed 1");
            return false;
        }
        if ((childRet = uv_tcp_open(&hSession->hChildWorkTCP, hSession->fdChildWorkTCP)) < 0) {
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed 2,fd:%d,str:%s", hSession->fdChildWorkTCP, uv_strerror(childRet));
            return false;
        }
        Base::SetTcpOptions((uv_tcp_t *)&hSession->hChildWorkTCP);
        uv_read_start((uv_stream_t *)&hSession->hChildWorkTCP, AllocCallback, pTCPBase->ReadStream);
        regOK = true;
    } else {  // USB
        HdcUSBBase *pUSBBase = (HdcUSBBase *)hSession->classModule;
        WRITE_LOG(LOG_DEBUG, "USB ReadyForWorkThread");
        regOK = pUSBBase->ReadyForWorkThread(hSession);
    }
    if (regOK && hSession->serverOrDaemon) {
        // session handshake step1
        SessionHandShake handshake;
        Base::ZeroStruct(handshake);
        handshake.banner = HANDSHAKE_MESSAGE;
        handshake.sessionId = hSession->sessionId;
        handshake.connectKey = hSession->connectKey;
        handshake.authType = AUTH_NONE;
        string hs = SerialStruct::SerializeToString(handshake);
        Send(hSession->sessionId, 0, CMD_KERNEL_HANDSHAKE, (uint8_t *)hs.c_str(), hs.size());
    }
    return regOK;
}

vector<uint8_t> HdcSessionBase::BuildCtrlString(InnerCtrlCommand command, uint32_t channelId, uint8_t *data,
                                                int dataSize)
{
    vector<uint8_t> ret;
    while (true) {
        if (dataSize > BUF_SIZE_MICRO) {
            break;
        }
        CtrlStruct ctrl;
        Base::ZeroStruct(ctrl);
        ctrl.command = command;
        ctrl.channelId = channelId;
        ctrl.dataSize = dataSize;
        if (dataSize > 0 && data != nullptr && memcpy_s(ctrl.data, sizeof(ctrl.data), data, dataSize) != EOK) {
            break;
        }
        uint8_t *buf = reinterpret_cast<uint8_t *>(&ctrl);
        ret.insert(ret.end(), buf, buf + sizeof(CtrlStruct));
        break;
    }
    return ret;
}

bool HdcSessionBase::DispatchMainThreadCommand(HSession hSession, const CtrlStruct *ctrl)
{
    bool ret = true;
    uint32_t channelId = ctrl->channelId;  // if send not set, it is zero
    switch (ctrl->command) {
        case SP_START_SESSION: {
            WRITE_LOG(LOG_DEBUG, "Dispatch MainThreadCommand  START_SESSION sessionId:%u instance:%s",
                      hSession->sessionId, hSession->serverOrDaemon ? "server" : "daemon");
            ret = WorkThreadStartSession(hSession);
            break;
        }
        case SP_STOP_SESSION: {
            WRITE_LOG(LOG_DEBUG, "Dispatch MainThreadCommand STOP_SESSION sessionId:%u", hSession->sessionId);
            auto closeSessionChildThreadTCPHandle = [](uv_handle_t *handle) -> void {
                HSession hSession = (HSession)handle->data;
                Base::TryCloseHandle((uv_handle_t *)handle);
                if (--hSession->uvChildRef == 0) {
                    uv_stop(&hSession->childLoop);
                };
            };
            hSession->uvChildRef += 2;
            if (hSession->hChildWorkTCP.loop) {  // maybe not use it
                ++hSession->uvChildRef;
                Base::TryCloseHandle((uv_handle_t *)&hSession->hChildWorkTCP, true, closeSessionChildThreadTCPHandle);
            }
            Base::TryCloseHandle((uv_handle_t *)&hSession->ctrlPipe[STREAM_WORK], true,
                                 closeSessionChildThreadTCPHandle);
            Base::TryCloseHandle((uv_handle_t *)&hSession->dataPipe[STREAM_WORK], true,
                                 closeSessionChildThreadTCPHandle);
            break;
        }
        case SP_ATTACH_CHANNEL: {
            if (!serverOrDaemon) {
                break;  // Only Server has this feature
            }
            AttachChannel(hSession, channelId);
            break;
        }
        case SP_DEATCH_CHANNEL: {
            if (!serverOrDaemon) {
                break;  // Only Server has this feature
            }
            DeatchChannel(hSession, channelId);
            break;
        }
        default:
            WRITE_LOG(LOG_WARN, "Not support main command");
            ret = false;
            break;
    }
    return ret;
}

// Several bytes of control instructions, generally do not stick
void HdcSessionBase::ReadCtrlFromMain(uv_stream_t *uvpipe, ssize_t nread, const uv_buf_t *buf)
{
    HSession hSession = (HSession)uvpipe->data;
    HdcSessionBase *hSessionBase = (HdcSessionBase *)hSession->classInstance;
    int formatCommandSize = sizeof(CtrlStruct);
    int index = 0;
    bool ret = true;
    while (true) {
        if (nread < 0) {
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed,%s", uv_strerror(nread));
            ret = false;
            break;
        }
        if (nread % formatCommandSize != 0) {
            WRITE_LOG(LOG_FATAL, "ReadCtrlFromMain size failed, nread == %d", nread);
            ret = false;
            break;
        }
        CtrlStruct *ctrl = reinterpret_cast<CtrlStruct *>(buf->base + index);
        if (!(ret = hSessionBase->DispatchMainThreadCommand(hSession, ctrl))) {
            ret = false;
            break;
        }
        index += sizeof(CtrlStruct);
        if (index >= nread) {
            break;
        }
    }
    delete[] buf->base;
}

void HdcSessionBase::ReChildLoopForSessionClear(HSession hSession)
{
    // Restart loop close task
    ClearOwnTasks(hSession, 0);
    auto clearTaskForSessionFinish = [](uv_idle_t *handle) -> void {
        HSession hSession = (HSession)handle->data;
        for (auto v : *hSession->mapTask) {
            HTaskInfo hTask = (HTaskInfo)v.second;
            if (!hTask->taskFree)
                return;
        }
        // all task has been free
        uv_close((uv_handle_t *)handle, Base::CloseIdleCallback);
        uv_stop(&hSession->childLoop);  // stop ReChildLoopForSessionClear pendding
    };
    Base::IdleUvTask(&hSession->childLoop, hSession, clearTaskForSessionFinish);
    uv_run(&hSession->childLoop, UV_RUN_DEFAULT);
    // clear
    Base::TryCloseLoop(&hSession->childLoop, "Session childUV");
}

void HdcSessionBase::SessionWorkThread(uv_work_t *arg)
{
    int childRet = 0;
    HSession hSession = (HSession)arg->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    uv_loop_init(&hSession->childLoop);
    hSession->hWorkChildThread = uv_thread_self();
    if ((childRet = uv_tcp_init(&hSession->childLoop, &hSession->ctrlPipe[STREAM_WORK])) < 0) {
        WRITE_LOG(LOG_DEBUG, "SessionCtrl err1, %s", uv_strerror(childRet));
    }
    if ((childRet = uv_tcp_open(&hSession->ctrlPipe[STREAM_WORK], hSession->ctrlFd[STREAM_WORK])) < 0) {
        WRITE_LOG(LOG_DEBUG, "SessionCtrl err2, %s fd:%d", uv_strerror(childRet), hSession->ctrlFd[STREAM_WORK]);
    }
    uv_read_start((uv_stream_t *)&hSession->ctrlPipe[STREAM_WORK], Base::AllocBufferCallback, ReadCtrlFromMain);
    WRITE_LOG(LOG_DEBUG, "!!!Workthread run begin, sessionId:%u instance:%s", hSession->sessionId,
              thisClass->serverOrDaemon ? "server" : "daemon");
    uv_run(&hSession->childLoop, UV_RUN_DEFAULT);  // work pendding
    WRITE_LOG(LOG_DEBUG, "!!!Workthread run again, sessionId:%u", hSession->sessionId);
    // main loop has exit
    thisClass->ReChildLoopForSessionClear(hSession);  // work pending again
    hSession->childCleared = true;
    WRITE_LOG(LOG_DEBUG, "!!!Workthread run finish, sessionId:%u workthread:%p", hSession->sessionId, uv_thread_self());
}

// clang-format off
void HdcSessionBase::LogMsg(const uint32_t sessionId, const uint32_t channelId,
                            MessageLevel level, const char *msg, ...)
// clang-format on
{
    va_list vaArgs;
    va_start(vaArgs, msg);
    string log = Base::StringFormat(msg, vaArgs);
    va_end(vaArgs);
    vector<uint8_t> buf;
    buf.push_back(level);
    buf.insert(buf.end(), log.c_str(), log.c_str() + log.size());
    ServerCommand(sessionId, channelId, CMD_KERNEL_ECHO, buf.data(), buf.size());
}

// Heavy and time-consuming work was putted in the new thread to do, and does
// not occupy the main thread
bool HdcSessionBase::DispatchTaskData(HSession hSession, const uint32_t channelId, const uint16_t command,
                                      uint8_t *payload, int payloadSize)
{
    bool ret = false;
    while (true) {
        HTaskInfo hTaskInfo = AdminTask(OP_QUERY, hSession, channelId, nullptr);
        if (!hTaskInfo) {
            WRITE_LOG(LOG_DEBUG, "New HTaskInfo");
            hTaskInfo = new TaskInformation();
            hTaskInfo->channelId = channelId;
            hTaskInfo->sessionId = hSession->sessionId;
            hTaskInfo->runLoop = &hSession->childLoop;
            hTaskInfo->serverOrDaemon = serverOrDaemon;
        }
        if (hTaskInfo->taskStop) {
            WRITE_LOG(LOG_DEBUG, "RedirectToTask jump stopped task:%d", channelId);
            break;
        }
        if (hTaskInfo->taskFree) {
            WRITE_LOG(LOG_DEBUG, "Jump delete HTaskInfo");
            break;
        }
        bool result = RedirectToTask(hTaskInfo, hSession, channelId, command, payload, payloadSize);
        if (!hTaskInfo->hasInitial) {
            AdminTask(OP_ADD, hSession, channelId, hTaskInfo);
            hTaskInfo->hasInitial = true;
        }
        if (result) {
            ret = true;
        }
        break;
    }
    return ret;
}

void HdcSessionBase::PostStopInstanceMessage(bool restart)
{
    PushAsyncMessage(0, ASYNC_STOP_MAINLOOP, nullptr, 0);
    WRITE_LOG(LOG_DEBUG, "StopDaemon has sended");
    wantRestart = restart;
}

}  // namespace Hdc
