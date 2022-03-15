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
    // print version pid
    WRITE_LOG(LOG_INFO, "Program running. %s Pid:%u", Base::GetVersion().c_str(), getpid());
    // server/daemon common initialization code
    threadPoolCount = SIZE_THREAD_POOL;
    string uvThreadEnv("UV_THREADPOOL_SIZE");
    string uvThreadVal = std::to_string(threadPoolCount);
#ifdef _WIN32
    uvThreadEnv += "=";
    uvThreadEnv += uvThreadVal;
    _putenv(uvThreadEnv.c_str());
#else
    setenv(uvThreadEnv.c_str(), uvThreadVal.c_str(), 1);
#endif
    uv_loop_init(&loopMain);
    WRITE_LOG(LOG_DEBUG, "loopMain init");
    uv_rwlock_init(&mainAsync);
    uv_async_init(&loopMain, &asyncMainLoop, MainAsyncCallback);
    uv_rwlock_init(&lockMapSession);
    serverOrDaemon = serverOrDaemonIn;
    ctxUSB = nullptr;
    wantRestart = false;
    threadSessionMain = uv_thread_self();

#ifdef HDC_HOST
    if (serverOrDaemon) {
        if (libusb_init((libusb_context **)&ctxUSB) != 0) {
            ctxUSB = nullptr;
        }
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
    if (serverOrDaemon and ctxUSB != nullptr) {
        libusb_exit((libusb_context *)ctxUSB);
    }
#endif
    WRITE_LOG(LOG_DEBUG, "~HdcSessionBase free sessionRef:%u instance:%s", uint32_t(sessionRef),
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

    WRITE_LOG(LOG_DEBUG, "BeginRemoveTask taskType:%d channelId:%u", hTask->taskType, hTask->channelId);
    ret = RemoveInstanceTask(OP_CLEAR, hTask);
    auto taskClassDeleteRetry = [](uv_timer_t *handle) -> void {
        HTaskInfo hTask = (HTaskInfo)handle->data;
        HdcSessionBase *thisClass = (HdcSessionBase *)hTask->ownerSessionClass;
        WRITE_LOG(LOG_DEBUG, "TaskDelay task remove current try count %d/%d, channelId:%u, sessionId:%u",
                  hTask->closeRetryCount, GLOBAL_TIMEOUT, hTask->channelId, hTask->sessionId);
        if (!thisClass->TryRemoveTask(hTask)) {
            return;
        }
        HSession hSession = thisClass->AdminSession(OP_QUERY, hTask->sessionId, nullptr);
        thisClass->AdminTask(OP_REMOVE, hSession, hTask->channelId, nullptr);
        WRITE_LOG(LOG_DEBUG, "TaskDelay task remove finish, channelId:%u", hTask->channelId);
        delete hTask;
        Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
    };
    Base::TimerUvTask(hTask->runLoop, hTask, taskClassDeleteRetry, (GLOBAL_TIMEOUT * TIME_BASE) / UV_DEFAULT_INTERVAL);

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
            WRITE_LOG(LOG_DEBUG, "ClearOwnTasks OP_CLEAR finish, session:%p channelIDInput:%u", hSession,
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

#ifdef HDC_SUPPORT_UART
void HdcSessionBase::EnumUARTDeviceRegister(UartKickoutZombie kickOut)
{
    uv_rwlock_rdlock(&lockMapSession);
    map<uint32_t, HSession>::iterator i;
    for (i = mapSession.begin(); i != mapSession.end(); ++i) {
        HSession hs = i->second;
        if ((hs->connType != CONN_SERIAL) or (hs->hUART == nullptr)) {
            continue;
        }
        kickOut(hs);
        break;
    }
    uv_rwlock_rdunlock(&lockMapSession);
}
#endif

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
HSession HdcSessionBase::QueryUSBDeviceRegister(void *pDev, uint8_t busIDIn, uint8_t devIDIn)
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
            thisClass->FreeSession(param->sid);
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
            ++hSession->uvHandleRef;
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
            hSession->hUSB->wMaxPacketSizeSend = MAX_PACKET_SIZE_HISPEED;
            break;
        }
#ifdef HDC_SUPPORT_UART
        case CONN_SERIAL: {
            HUART hUART = new HdcUART();
            if (!hUART) {
                ret = -1;
                break;
            }
            hSession->hUART = hUART;
            break;
        }
#endif // HDC_SUPPORT_UART
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
    HSession hInput = nullptr;
    do {
        uid = static_cast<uint32_t>(Base::GetRandom());
    } while ((hInput = AdminSession(OP_QUERY, uid, nullptr)) != nullptr);
    return uid;
}

// when client 0 to automatic generated，when daemon First place 1 followed by
HSession HdcSessionBase::MallocSession(bool serverOrDaemon, const ConnType connType, void *classModule,
                                       uint32_t sessionId)
{
    HSession hSession = new(std::nothrow) HdcSession();
    if (!hSession) {
        WRITE_LOG(LOG_FATAL, "MallocSession new hSession failed");
        return nullptr;
    }
    int ret = 0;
    ++sessionRef;
    (void)memset_s(hSession->ctrlFd, sizeof(hSession->ctrlFd), 0, sizeof(hSession->ctrlFd));
    hSession->classInstance = this;
    hSession->connType = connType;
    hSession->classModule = classModule;
    hSession->isDead = false;
    hSession->sessionId = ((sessionId == 0) ? GetSessionPseudoUid() : sessionId);
    hSession->serverOrDaemon = serverOrDaemon;
    hSession->hWorkThread = uv_thread_self();
    hSession->mapTask = new(std::nothrow) map<uint32_t, HTaskInfo>();
    if (hSession->mapTask == nullptr) {
        WRITE_LOG(LOG_FATAL, "MallocSession new hSession->mapTask failed");
        delete hSession;
        hSession = nullptr;
        return nullptr;
    }
    hSession->listKey = new(std::nothrow) list<void *>;
    if (hSession->listKey == nullptr) {
        WRITE_LOG(LOG_FATAL, "MallocSession new hSession->listKey failed");
        delete hSession;
        hSession = nullptr;
        return nullptr;
    }
    hSession->uvHandleRef = 0;
    // pullup child
    WRITE_LOG(LOG_DEBUG, "HdcSessionBase NewSession, sessionId:%u, connType:%d.",
              hSession->sessionId, hSession->connType);
    uv_tcp_init(&loopMain, &hSession->ctrlPipe[STREAM_MAIN]);
    ++hSession->uvHandleRef;
    Base::CreateSocketPair(hSession->ctrlFd);
    uv_tcp_open(&hSession->ctrlPipe[STREAM_MAIN], hSession->ctrlFd[STREAM_MAIN]);
    uv_read_start((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], Base::AllocBufferCallback, ReadCtrlFromSession);
    hSession->ctrlPipe[STREAM_MAIN].data = hSession;
    hSession->ctrlPipe[STREAM_WORK].data = hSession;
    // Activate USB DAEMON's data channel, may not for use
    uv_tcp_init(&loopMain, &hSession->dataPipe[STREAM_MAIN]);
    ++hSession->uvHandleRef;
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
    WRITE_LOG(LOG_DEBUG, "FreeSessionByConnectType %s", hSession->ToDebugString().c_str());

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
#ifdef HDC_SUPPORT_UART
    if (CONN_SERIAL == hSession->connType) {
        if (!hSession->hUART) {
            return;
        }
        HUART hUART = hSession->hUART;
        if (!hUART) {
            return;
        }
        HdcUARTBase *uartBase = (HdcUARTBase *)hSession->classModule;
        // tell uart session will be free
        uartBase->StopSession(hSession);
#ifdef HDC_HOST
#ifdef HOST_MINGW
        if (hUART->devUartHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(hUART->devUartHandle);
            hUART->devUartHandle = INVALID_HANDLE_VALUE;
        }
#elif defined(HOST_LINUX)
        if (hUART->devUartHandle >= 0) {
            close(hUART->devUartHandle);
            hUART->devUartHandle = -1;
        }
#endif // _WIN32
#endif
        delete hSession->hUART;
        hSession->hUART = nullptr;
    }
#endif
}

// work when libuv-handle at struct of HdcSession has all callback finished
void HdcSessionBase::FreeSessionFinally(uv_idle_t *handle)
{
    HSession hSession = (HSession)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    if (hSession->uvHandleRef > 0) {
        return;
    }
    // Notify Server or Daemon, just UI or display commandline
    thisClass->NotifyInstanceSessionFree(hSession, true);
    // all hsession uv handle has been clear
    thisClass->AdminSession(OP_REMOVE, hSession->sessionId, nullptr);
    WRITE_LOG(LOG_DEBUG, "!!!FreeSessionFinally sessionId:%u finish", hSession->sessionId);
    HdcAuth::FreeKey(!hSession->serverOrDaemon, hSession->listKey);
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
        --hSession->uvHandleRef;
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
    FreeSessionByConnectType(hSession);
    // finish
    Base::IdleUvTask(&loopMain, hSession, FreeSessionFinally);
}

void HdcSessionBase::FreeSessionOpeate(uv_timer_t *handle)
{
    HSession hSession = (HSession)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    if (hSession->ref > 0) {
        return;
    }
    WRITE_LOG(LOG_DEBUG, "FreeSessionOpeate ref:%u", uint32_t(hSession->ref));
#ifdef HDC_HOST
    if (hSession->hUSB != nullptr
        && (!hSession->hUSB->hostBulkIn.isShutdown || !hSession->hUSB->hostBulkOut.isShutdown)) {
        HdcUSBBase *pUSB = ((HdcUSBBase *)hSession->classModule);
        pUSB->CancelUsbIo(hSession);
        return;
    }
#endif
    // wait workthread to free
    if (hSession->ctrlPipe[STREAM_WORK].loop) {
        auto ctrl = BuildCtrlString(SP_STOP_SESSION, 0, nullptr, 0);
        Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
        WRITE_LOG(LOG_DEBUG, "FreeSessionOpeate, send workthread fo free. sessionId:%u", hSession->sessionId);
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
    if (threadSessionMain != uv_thread_self()) {
        PushAsyncMessage(sessionId, ASYNC_FREE_SESSION, nullptr, 0);
        return;
    }
    HSession hSession = AdminSession(OP_QUERY, sessionId, nullptr);
    WRITE_LOG(LOG_DEBUG, "Begin to free session, sessionid:%u", sessionId);
    do {
        if (!hSession || hSession->isDead) {
            break;
        }
        hSession->isDead = true;
        Base::TimerUvTask(&loopMain, hSession, FreeSessionOpeate);
        NotifyInstanceSessionFree(hSession, false);
        WRITE_LOG(LOG_DEBUG, "FreeSession sessionId:%u ref:%u", hSession->sessionId, uint32_t(hSession->ref));
    } while (false);
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
        case OP_QUERY_REF:
            uv_rwlock_wrlock(&lockMapSession);
            if (mapSession.count(sessionId)) {
                hRet = mapSession[sessionId];
                ++hRet->ref;
            }
            uv_rwlock_wrunlock(&lockMapSession);
            break;
        case OP_UPDATE:
            uv_rwlock_wrlock(&lockMapSession);
            // remove old
            mapSession.erase(sessionId);
            mapSession[hInput->sessionId] = hInput;
            uv_rwlock_wrunlock(&lockMapSession);
            break;
        case OP_VOTE_RESET:
            if (mapSession.count(sessionId) == 0) {
                break;
            }
            bool needReset;
            uv_rwlock_wrlock(&lockMapSession);
            hRet = mapSession[sessionId];
            hRet->voteReset = true;
            needReset = true;
            for (auto &kv : mapSession) {
                if (sessionId == kv.first) {
                    continue;
                }
                WRITE_LOG(LOG_DEBUG, "session:%u vote reset, session %u is %s",
                          sessionId, kv.first, kv.second->voteReset ? "YES" : "NO");
                if (!kv.second->voteReset) {
                    needReset = false;
                }
            }
            uv_rwlock_wrunlock(&lockMapSession);
            if (needReset) {
                WRITE_LOG(LOG_FATAL, "!! session:%u vote reset, passed unanimously !!", sessionId);
                abort();
            }
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
#ifndef HDC_HOST
            // uv sub-thread confiured by threadPoolCount, reserve 2 for main & communicate
            if (mapTask.size() >= (threadPoolCount - 2)) {
                WRITE_LOG(LOG_WARN, "mapTask.size:%d, hdc is busy", mapTask.size());
                break;
            }
#endif
            hRet = mapTask[channelId];
            if (hRet != nullptr) {
                delete hRet;
            }
            mapTask[channelId] = hInput;
            hRet = hInput;
            break;
        case OP_REMOVE:
            mapTask.erase(channelId);
            break;
        case OP_QUERY:
            if (mapTask.count(channelId)) {
                hRet = mapTask[channelId];
            }
            break;
        case OP_VOTE_RESET:
            AdminSession(op, hSession->sessionId, nullptr);
            break;
        default:
            break;
    }
    return hRet;
}

int HdcSessionBase::SendByProtocol(HSession hSession, uint8_t *bufPtr, const int bufLen)
{
    if (hSession->isDead) {
        WRITE_LOG(LOG_WARN, "SendByProtocol session dead error");
        return ERR_SESSION_NOFOUND;
    }
    int ret = 0;
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
            if (ret > 0) {
                ++hSession->ref;
            }
            break;
        }
        case CONN_USB: {
            HdcUSBBase *pUSB = ((HdcUSBBase *)hSession->classModule);
            ret = pUSB->SendUSBBlock(hSession, bufPtr, bufLen);
            delete[] bufPtr;
            break;
        }
#ifdef HDC_SUPPORT_UART
        case CONN_SERIAL: {
            HdcUARTBase *pUART = ((HdcUARTBase *)hSession->classModule);
            ret = pUART->SendUARTData(hSession, bufPtr, bufLen);
            delete[] bufPtr;
            break;
        }
#endif
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

    PayloadHead payloadHead = {};  // need convert to big-endian
    payloadHead.flag[0] = PACKET_FLAG.at(0);
    payloadHead.flag[1] = PACKET_FLAG.at(1);
    payloadHead.protocolVer = VER_PROTOCOL;
    payloadHead.headSize = htons(s.size());
    payloadHead.dataSize = htonl(dataSize);
    int finalBufSize = sizeof(PayloadHead) + s.size() + dataSize;
    uint8_t *finayBuf = new uint8_t[finalBufSize]();
    if (finayBuf == nullptr) {
        WRITE_LOG(LOG_WARN, "send allocmem err");
        return ERR_BUF_ALLOC;
    }
    bool bufRet = false;
    do {
        if (memcpy_s(finayBuf, sizeof(PayloadHead), reinterpret_cast<uint8_t *>(&payloadHead), sizeof(PayloadHead))) {
            WRITE_LOG(LOG_WARN, "send copyhead err for dataSize:%d", dataSize);
            break;
        }
        if (memcpy_s(finayBuf + sizeof(PayloadHead), s.size(),
                     reinterpret_cast<uint8_t *>(const_cast<char *>(s.c_str())), s.size())) {
            WRITE_LOG(LOG_WARN, "send copyProtbuf err for dataSize:%d", dataSize);
            break;
        }
        if (dataSize > 0 && memcpy_s(finayBuf + sizeof(PayloadHead) + s.size(), dataSize, data, dataSize)) {
            WRITE_LOG(LOG_WARN, "send copyDatabuf err for dataSize:%d", dataSize);
            break;
        }
        bufRet = true;
    } while (false);
    if (!bufRet) {
        delete[] finayBuf;
        WRITE_LOG(LOG_WARN, "send copywholedata err for dataSize:%d", dataSize);
        return ERR_BUF_COPY;
    }
    return SendByProtocol(hSession, finayBuf, finalBufSize);
}

int HdcSessionBase::DecryptPayload(HSession hSession, PayloadHead *payloadHeadBe, uint8_t *encBuf)
{
    PayloadProtect protectBuf = {};
    uint16_t headSize = ntohs(payloadHeadBe->headSize);
    int dataSize = ntohl(payloadHeadBe->dataSize);
    string encString(reinterpret_cast<char *>(encBuf), headSize);
    SerialStruct::ParseFromString(protectBuf, encString);
    if (protectBuf.vCode != payloadProtectStaticVcode) {
        WRITE_LOG(LOG_FATAL, "Session recv static vcode failed");
        return ERR_BUF_CHECK;
    }
    uint8_t *data = encBuf + headSize;
    if (ENABLE_IO_CHECKSUM && protectBuf.checkSum != 0 && (protectBuf.checkSum != Base::CalcCheckSum(data, dataSize))) {
        WRITE_LOG(LOG_FATAL, "Session recv CalcCheckSum failed");
        return ERR_BUF_CHECK;
    }
    if (!FetchCommand(hSession, protectBuf.channelId, protectBuf.commandFlag, data, dataSize)) {
        WRITE_LOG(LOG_WARN, "FetchCommand failed: channelId %x commandFlag %x",
                  protectBuf.channelId, protectBuf.commandFlag);
        return ERR_GENERIC;
    }
    return RET_SUCCESS;
}

int HdcSessionBase::OnRead(HSession hSession, uint8_t *bufPtr, const int bufLen)
{
    int ret = ERR_GENERIC;
    if (memcmp(bufPtr, PACKET_FLAG.c_str(), PACKET_FLAG.size())) {
        WRITE_LOG(LOG_FATAL, "PACKET_FLAG incorrect %x %x", bufPtr[0], bufPtr[1]);
        return ERR_BUF_CHECK;
    }
    struct PayloadHead *payloadHead = (struct PayloadHead *)bufPtr;
    int tobeReadLen = ntohl(payloadHead->dataSize) + ntohs(payloadHead->headSize);
    int packetHeadSize = sizeof(struct PayloadHead);
    if (tobeReadLen <= 0 || (uint32_t)tobeReadLen > HDC_BUF_MAX_BYTES) {
        WRITE_LOG(LOG_FATAL, "Packet size incorrect");
        return ERR_BUF_CHECK;
    }
    if (bufLen - packetHeadSize < tobeReadLen) {
        return 0;
    }
    if (DecryptPayload(hSession, payloadHead, bufPtr + packetHeadSize)) {
        WRITE_LOG(LOG_WARN, "decrypt plhead error");
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
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r(read, buf, bufSize);
        WRITE_LOG(LOG_FATAL, "HdcSessionBase read io failed,%s", buf);
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
        } else {                           // <0
            hSession->availTailIndex = 0;  // Preventing malicious data packages
            indexBuf = ERR_BUF_SIZE;
            break;
        }
        // It may be multi-time IO to merge in a BUF, need to loop processing
    }
    if (indexBuf > 0 && hSession->availTailIndex > 0) {
        if (memmove_s(hSession->ioBuf, hSession->bufSize, hSession->ioBuf + indexBuf, hSession->availTailIndex)
            != EOK) {
            return ERR_BUF_COPY;
        };
        uint8_t *bufToZero = (uint8_t *)(hSession->ioBuf + hSession->availTailIndex);
        Base::ZeroBuf(bufToZero, hSession->bufSize - hSession->availTailIndex);
    }
    return indexBuf;
}

void HdcSessionBase::AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    HSession context = (HSession)handle->data;
    Base::ReallocBuf(&context->ioBuf, &context->bufSize, HDC_SOCKETPAIR_SIZE);
    buf->base = (char *)context->ioBuf + context->availTailIndex;
    int size = context->bufSize - context->availTailIndex;
    buf->len = std::min(size, static_cast<int>(sizeWanted));
}

void HdcSessionBase::FinishWriteSessionTCP(uv_write_t *req, int status)
{
    HSession hSession = (HSession)req->handle->data;
    --hSession->ref;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
    if (status < 0) {
        Base::TryCloseHandle((uv_handle_t *)req->handle);
        if (!hSession->isDead && !hSession->ref) {
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
            constexpr int bufSize = 1024;
            char buffer[bufSize] = { 0 };
            uv_strerror_r((int)nread, buffer, bufSize);
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed,%s", buf);
            uv_read_stop(uvpipe);
            break;
        }
        if (nread > 64) {  // 64 : max length
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
            constexpr int bufSize = 1024;
            char buf[bufSize] = { 0 };
            uv_strerror_r(childRet, buf, bufSize);
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed 2,fd:%d,str:%s", hSession->fdChildWorkTCP, buf);
            return false;
        }
        Base::SetTcpOptions((uv_tcp_t *)&hSession->hChildWorkTCP);
        uv_read_start((uv_stream_t *)&hSession->hChildWorkTCP, AllocCallback, pTCPBase->ReadStream);
        regOK = true;
#ifdef HDC_SUPPORT_UART
    } else if (hSession->connType == CONN_SERIAL) { // UART
        HdcUARTBase *pUARTBase = (HdcUARTBase *)hSession->classModule;
        WRITE_LOG(LOG_DEBUG, "UART ReadyForWorkThread");
        regOK = pUARTBase->ReadyForWorkThread(hSession);
#endif
    } else {  // USB
        HdcUSBBase *pUSBBase = (HdcUSBBase *)hSession->classModule;
        WRITE_LOG(LOG_DEBUG, "USB ReadyForWorkThread");
        regOK = pUSBBase->ReadyForWorkThread(hSession);
    }

    if (regOK && hSession->serverOrDaemon) {
        // session handshake step1
        SessionHandShake handshake = {};
        handshake.banner = HANDSHAKE_MESSAGE;
        handshake.sessionId = hSession->sessionId;
        handshake.connectKey = hSession->connectKey;
        handshake.authType = AUTH_NONE;
        string hs = SerialStruct::SerializeToString(handshake);
#ifdef HDC_SUPPORT_UART
        WRITE_LOG(LOG_DEBUG, "WorkThreadStartSession session %u auth %u send handshake hs:",
                  hSession->sessionId, handshake.authType, hs.c_str());
#endif
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
        CtrlStruct ctrl = {};
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
            constexpr int bufSize = 1024;
            char buffer[bufSize] = { 0 };
            uv_strerror_r((int)nread, buffer, bufSize);
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed,%s", buffer);
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
    WRITE_LOG(LOG_INFO, "ReChildLoopForSessionClear sessionId:%u", hSession->sessionId);
    auto clearTaskForSessionFinish = [](uv_timer_t *handle) -> void {
        HSession hSession = (HSession)handle->data;
        for (auto v : *hSession->mapTask) {
            HTaskInfo hTask = (HTaskInfo)v.second;
            uint8_t level;
            if (hTask->closeRetryCount < GLOBAL_TIMEOUT / 2) {
                level = LOG_DEBUG;
            } else {
                level = LOG_WARN;
            }
            WRITE_LOG(level, "wait task free retry %d/%d, channelId:%u, sessionId:%u",
                      hTask->closeRetryCount, GLOBAL_TIMEOUT, hTask->channelId, hTask->sessionId);
            if (hTask->closeRetryCount++ >= GLOBAL_TIMEOUT) {
                HdcSessionBase *thisClass = (HdcSessionBase *)hTask->ownerSessionClass;
                hSession = thisClass->AdminSession(OP_QUERY, hTask->sessionId, nullptr);
                thisClass->AdminTask(OP_VOTE_RESET, hSession, hTask->channelId, nullptr);
            }
            if (!hTask->taskFree)
                return;
        }
        // all task has been free
        uv_close((uv_handle_t *)handle, Base::CloseTimerCallback);
        uv_stop(&hSession->childLoop);  // stop ReChildLoopForSessionClear pendding
    };
    Base::TimerUvTask(&hSession->childLoop, hSession, clearTaskForSessionFinish, (GLOBAL_TIMEOUT * TIME_BASE) / UV_DEFAULT_INTERVAL);
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
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r(childRet, buf, bufSize);
        WRITE_LOG(LOG_DEBUG, "SessionCtrl err1, %s", buf);
    }
    if ((childRet = uv_tcp_open(&hSession->ctrlPipe[STREAM_WORK], hSession->ctrlFd[STREAM_WORK])) < 0) {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r(childRet, buf, bufSize);
        WRITE_LOG(LOG_DEBUG, "SessionCtrl err2, %s fd:%d", buf, hSession->ctrlFd[STREAM_WORK]);
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

bool HdcSessionBase::NeedNewTaskInfo(const uint16_t command, bool &masterTask)
{
    // referer from HdcServerForClient::DoCommandRemote
    bool ret = false;
    bool taskMasterInit = false;
    masterTask = false;
    switch (command) {
        case CMD_FILE_INIT:
        case CMD_FORWARD_INIT:
        case CMD_APP_INIT:
        case CMD_APP_UNINSTALL:
        case CMD_UNITY_BUGREPORT_INIT:
        case CMD_APP_SIDELOAD:
            taskMasterInit = true;
            break;
        default:
            break;
    }
    if (!serverOrDaemon
        && (command == CMD_SHELL_INIT || (command > CMD_UNITY_COMMAND_HEAD && command < CMD_UNITY_COMMAND_TAIL))) {
        // daemon's single side command
        ret = true;
    } else if (command == CMD_KERNEL_WAKEUP_SLAVETASK) {
        // slave tasks
        ret = true;
    } else if (taskMasterInit) {
        // task init command
        masterTask = true;
        ret = true;
    }
    return ret;
}
// Heavy and time-consuming work was putted in the new thread to do, and does
// not occupy the main thread
bool HdcSessionBase::DispatchTaskData(HSession hSession, const uint32_t channelId, const uint16_t command,
                                      uint8_t *payload, int payloadSize)
{
    bool ret = true;
    HTaskInfo hTaskInfo = nullptr;
    bool masterTask = false;
    while (true) {
        // Some basic commands do not have a local task constructor. example: Interactive shell, some uinty commands
        if (NeedNewTaskInfo(command, masterTask)) {
            WRITE_LOG(LOG_DEBUG, "New HTaskInfo channelId:%u command:%u", channelId, command);
            hTaskInfo = new(std::nothrow) TaskInformation();
            if (hTaskInfo == nullptr) {
                WRITE_LOG(LOG_FATAL, "DispatchTaskData new hTaskInfo failed");
                break;
            }
            hTaskInfo->channelId = channelId;
            hTaskInfo->sessionId = hSession->sessionId;
            hTaskInfo->runLoop = &hSession->childLoop;
            hTaskInfo->serverOrDaemon = serverOrDaemon;
            hTaskInfo->masterSlave = masterTask;
            hTaskInfo->closeRetryCount = 0;

            int addTaskRetry = 3; // try 3 time
            while (addTaskRetry > 0) {
                if (AdminTask(OP_ADD, hSession, channelId, hTaskInfo)) {
                    break;
                }
                sleep(1);
                --addTaskRetry;
            }

            if (addTaskRetry == 0) {
#ifndef HDC_HOST
                LogMsg(hTaskInfo->sessionId, hTaskInfo->channelId, MSG_FAIL, "hdc thread pool busy, may cause reset later");
#endif
                delete hTaskInfo;
                hTaskInfo = nullptr;
                ret = false;
                break;
            }
        } else {
            hTaskInfo = AdminTask(OP_QUERY, hSession, channelId, nullptr);
        }
        if (!hTaskInfo || hTaskInfo->taskStop || hTaskInfo->taskFree) {
            WRITE_LOG(LOG_ALL, "Dead HTaskInfo, ignore, channelId:%u command:%u", channelId, command);
            break;
        }
        ret = RedirectToTask(hTaskInfo, hSession, channelId, command, payload, payloadSize);
        break;
    }
    return ret;
}

void HdcSessionBase::PostStopInstanceMessage(bool restart)
{
    PushAsyncMessage(0, ASYNC_STOP_MAINLOOP, nullptr, 0);
    WRITE_LOG(LOG_DEBUG, "StopDaemon has sended restart %d", restart);
    wantRestart = restart;
}
}  // namespace Hdc
