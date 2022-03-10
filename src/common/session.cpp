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
bool HdcSessionBase::TryRemoveTask(HTaskInfoPtr hTask)
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
        WRITE_LOG(LOG_DEBUG, "task taskType:%d channelId:%u has not been released.", hTask->taskType, hTask->channelId);
    }
    return ret;
}

// remove step1
bool HdcSessionBase::BeginRemoveTask(HTaskInfoPtr hTask)
{
    bool ret = true;
    if (hTask->taskStop || hTask->taskFree || !hTask->taskClass) {
        return true;
    }

    WRITE_LOG(LOG_DEBUG, "BeginRemoveTask taskType:%d channelId:%u", hTask->taskType, hTask->channelId);
    ret = RemoveInstanceTask(OP_CLEAR, hTask);
    auto taskClassDeleteRetry = [](uv_timer_t *handle) -> void {
        HTaskInfoPtr hTask = (HTaskInfoPtr)handle->data;
        HdcSessionBase *thisClass = (HdcSessionBase *)hTask->ownerSessionClass;
        WRITE_LOG(LOG_DEBUG, "TaskDelay task remove current try count %d/%d, channelId:%u, sessionId:%u",
                  hTask->closeRetryCount, GLOBAL_TIMEOUT, hTask->channelId, hTask->sessionId);
        if (!thisClass->TryRemoveTask(hTask)) {
            return;
        }
        HSessionPtr hSessionPtr = thisClass->AdminSession(OP_QUERY, hTask->sessionId, nullptr);
        thisClass->AdminTask(OP_REMOVE, hSessionPtr, hTask->channelId, nullptr);
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
void HdcSessionBase::ClearOwnTasks(HSessionPtr hSessionPtr, const uint32_t channelIDInput)
{
    // First case: normal task cleanup process (STOP Remove)
    // Second: The task is cleaned up, the session ends
    // Third: The task is cleaned up, and the session is directly over the session.
    map<uint32_t, HTaskInfoPtr>::iterator iter;
    for (iter = hSessionPtr->mapTask->begin(); iter != hSessionPtr->mapTask->end();++iter) {
        uint32_t channelId = iter->first;
        HTaskInfoPtr hTask = iter->second;
        if (hTask == nullptr) {
            continue;
        }
        if (channelIDInput != 0) {  // single
            if (channelIDInput != channelId) {
                continue;
            }
            BeginRemoveTask(hTask);
            WRITE_LOG(LOG_DEBUG, "ClearOwnTasks OP_CLEAR finish, session:%p channelIDInput:%u", hSessionPtr,
                      channelIDInput);
            break;
        }
        // multi
        BeginRemoveTask(hTask);
    }
}

void HdcSessionBase::ClearSessions()
{
    // no need to lock mapSession
    // broadcast free singal
    for (auto v : mapSession) {
        HSessionPtr hSessionPtr = (HSessionPtr)v.second;
        if (hSessionPtr != nullptr && !hSessionPtr->isDead) {
            FreeSession(hSessionPtr->sessionId);
        }
    }
}

void HdcSessionBase::ReMainLoopForInstanceClear()
{  // reloop
    auto clearSessionsForFinish = [](uv_idle_t *handle) -> void {
        if (handle == nullptr || handle == nullptr) {
            return;
        }
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
    map<uint32_t, HSessionPtr>::iterator i;
    for (i = mapSession.begin(); i != mapSession.end(); ++i) {
        HSessionPtr hs = i->second;
        if ((hs == nullptr) or (hs->connType != CONN_SERIAL) or (hs->hUART == nullptr)) {
            continue;
        }
        kickOut(hs);
        break;
    }
    uv_rwlock_rdunlock(&lockMapSession);
}
#endif

void HdcSessionBase::EnumUSBDeviceRegister(void (*pCallBack)(HSessionPtr hSessionPtr))
{
    if (!pCallBack) {
        return;
    }
    uv_rwlock_rdlock(&lockMapSession);
    map<uint32_t, HSessionPtr>::iterator i;
    for (i = mapSession.begin(); i != mapSession.end(); ++i) {
        HSessionPtr hs = i->second;
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
HSessionPtr HdcSessionBase::QueryUSBDeviceRegister(void *pDev, uint8_t busIDIn, uint8_t devIDIn)
{
#ifdef HDC_HOST
    libusb_device *dev = (libusb_device *)pDev;
    HSessionPtr hResult = nullptr;
    if (mapSession.empty()) {
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
    map<uint32_t, HSessionPtr>::iterator i;
    for (i = mapSession.begin(); i != mapSession.end(); ++i) {
        HSessionPtr hs = i->second;
        if (hs == nullptr || hs->connType == CONN_USB || hs->hUSB == nullptr ||
            hs->hUSB->devId != devId || hs->hUSB->busId != busId) {
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
    if (handle == nullptr || handle->data == nullptr) {
        return;
    }
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
    if (data == nullptr) {
        return;
    }
    AsyncParam *param = new(std::nothrow) AsyncParam();
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

void HdcSessionBase::WorkerPendding()
{
    uv_run(&loopMain, UV_RUN_DEFAULT);
    ClearInstanceResource();
}

int HdcSessionBase::MallocSessionByConnectType(HSessionPtr hSessionPtr)
{
    int ret = 0;
    switch (hSessionPtr->connType) {
        case CONN_TCP: {
            uv_tcp_init(&loopMain, &hSessionPtr->hWorkTCP);
            ++hSessionPtr->uvHandleRef;
            hSessionPtr->hWorkTCP.data = hSessionPtr;
            break;
        }
        case CONN_USB: {
            // Some members need to be placed at the primary thread
            HUSBPtr hUSB = new HdcUSB();
            if (!hUSB) {
                ret = -1;
                break;
            }
            hSessionPtr->hUSB = hUSB;
            hSessionPtr->hUSB->wMaxPacketSizeSend = MAX_PACKET_SIZE_HISPEED;
            break;
        }
#ifdef HDC_SUPPORT_UART
        case CONN_SERIAL: {
            HUARTPtr hUART = new HdcUART();
            if (!hUART) {
                ret = -1;
                break;
            }
            hSessionPtr->hUART = hUART;
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
    HSessionPtr hInput = nullptr;
    do {
        uid = static_cast<uint32_t>(Base::GetRandom());
    } while ((hInput = AdminSession(OP_QUERY, uid, nullptr)) != nullptr);
    return uid;
}

// when client 0 to automatic generated，when daemon First place 1 followed by
HSessionPtr HdcSessionBase::MallocSession(bool serverOrDaemon, const ConnType connType, void *classModule,
                                       uint32_t sessionId)
{
    HSessionPtr hSessionPtr = new(std::nothrow) HdcSession();
    if (!hSessionPtr) {
        WRITE_LOG(LOG_FATAL, "MallocSession new hSessionPtr failed");
        return nullptr;
    }
    int ret = 0;
    ++sessionRef;
    (void)memset_s(hSessionPtr->ctrlFd, sizeof(hSessionPtr->ctrlFd), 0, sizeof(hSessionPtr->ctrlFd));
    hSessionPtr->classInstance = this;
    hSessionPtr->connType = connType;
    hSessionPtr->classModule = classModule;
    hSessionPtr->isDead = false;
    hSessionPtr->sessionId = ((sessionId == 0) ? GetSessionPseudoUid() : sessionId);
    hSessionPtr->serverOrDaemon = serverOrDaemon;
    hSessionPtr->hWorkThread = uv_thread_self();
    hSessionPtr->mapTask = new(std::nothrow) map<uint32_t, HTaskInfoPtr>();
    if (hSessionPtr->mapTask == nullptr) {
        WRITE_LOG(LOG_FATAL, "MallocSession new hSessionPtr->mapTask failed");
        delete hSessionPtr;
        hSessionPtr = nullptr;
        return nullptr;
    }
    hSessionPtr->listKey = new(std::nothrow) list<void *>;
    if (hSessionPtr->listKey == nullptr) {
        WRITE_LOG(LOG_FATAL, "MallocSession new hSessionPtr->listKey failed");
        delete hSessionPtr->mapTask;
        delete hSessionPtr;
        hSessionPtr = nullptr;
        return nullptr;
    }
    hSessionPtr->uvHandleRef = 0;
    // pullup child
    WRITE_LOG(LOG_DEBUG, "HdcSessionBase NewSession, sessionId:%u, connType:%d.",
              hSessionPtr->sessionId, hSessionPtr->connType);
    uv_tcp_init(&loopMain, &hSessionPtr->ctrlPipe[STREAM_MAIN]);
    ++hSessionPtr->uvHandleRef;
    Base::CreateSocketPair(hSessionPtr->ctrlFd);
    uv_tcp_open(&hSessionPtr->ctrlPipe[STREAM_MAIN], hSessionPtr->ctrlFd[STREAM_MAIN]);
    uv_read_start((uv_stream_t *)&hSessionPtr->ctrlPipe[STREAM_MAIN], Base::AllocBufferCallback, ReadCtrlFromSession);
    hSessionPtr->ctrlPipe[STREAM_MAIN].data = hSessionPtr;
    hSessionPtr->ctrlPipe[STREAM_WORK].data = hSessionPtr;
    // Activate USB DAEMON's data channel, may not for use
    uv_tcp_init(&loopMain, &hSessionPtr->dataPipe[STREAM_MAIN]);
    ++hSessionPtr->uvHandleRef;
    Base::CreateSocketPair(hSessionPtr->dataFd);
    uv_tcp_open(&hSessionPtr->dataPipe[STREAM_MAIN], hSessionPtr->dataFd[STREAM_MAIN]);
    hSessionPtr->dataPipe[STREAM_MAIN].data = hSessionPtr;
    hSessionPtr->dataPipe[STREAM_WORK].data = hSessionPtr;
    Base::SetTcpOptions(&hSessionPtr->dataPipe[STREAM_MAIN]);
    ret = MallocSessionByConnectType(hSessionPtr);
    if (ret) {
        delete hSessionPtr->mapTask;
        delete hSessionPtr->listKey;
        delete hSessionPtr;
        hSessionPtr = nullptr;
    } else {
        AdminSession(OP_ADD, hSessionPtr->sessionId, hSessionPtr);
    }
    return hSessionPtr;
}

void HdcSessionBase::FreeSessionByConnectType(HSessionPtr hSessionPtr)
{
    WRITE_LOG(LOG_DEBUG, "FreeSessionByConnectType %s", hSessionPtr->ToDebugString().c_str());
    if (hSessionPtr == nullptr) {
        return;
    }

    if (CONN_USB == hSessionPtr->connType) {
        // ibusb All context is applied for sub-threaded, so it needs to be destroyed in the subline
        if (!hSessionPtr->hUSB) {
            return;
        }
        HUSBPtr hUSB = hSessionPtr->hUSB;
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
        delete hSessionPtr->hUSB;
        hSessionPtr->hUSB = nullptr;
    }
#ifdef HDC_SUPPORT_UART
    if (CONN_SERIAL == hSessionPtr->connType) {
        if (!hSessionPtr->hUART) {
            return;
        }
        HUARTPtr hUART = hSessionPtr->hUART;
        if (!hUART) {
            return;
        }
        HdcUARTBase *uartBase = (HdcUARTBase *)hSessionPtr->classModule;
        // tell uart session will be free
        uartBase->StopSession(hSessionPtr);
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
        delete hSessionPtr->hUART;
        hSessionPtr->hUART = nullptr;
    }
#endif
}

// work when libuv-handle at struct of HdcSession has all callback finished
void HdcSessionBase::FreeSessionFinally(uv_idle_t *handle)
{
    HSessionPtr hSessionPtr = (HSessionPtr)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSessionPtr->classInstance;
    if (hSessionPtr->uvHandleRef > 0 || thisClass == nullptr) {
        return;
    }
    // Notify Server or Daemon, just UI or display commandline
    thisClass->NotifyInstanceSessionFree(hSessionPtr, true);
    // all hsession uv handle has been clear
    thisClass->AdminSession(OP_REMOVE, hSessionPtr->sessionId, nullptr);
    WRITE_LOG(LOG_DEBUG, "!!!FreeSessionFinally sessionId:%u finish", hSessionPtr->sessionId);
    delete hSessionPtr;
    hSessionPtr = nullptr;  // fix CodeMars SetNullAfterFree issue
    Base::TryCloseHandle((const uv_handle_t *)handle, Base::CloseIdleCallback);
    --thisClass->sessionRef;
}

// work when child-work thread finish
void HdcSessionBase::FreeSessionContinue(HSessionPtr hSessionPtr)
{
    auto closeSessionTCPHandle = [](uv_handle_t *handle) -> void {
        HSessionPtr hSessionPtr = (HSessionPtr)handle->data;
        --hSessionPtr->uvHandleRef;
        Base::TryCloseHandle((uv_handle_t *)handle);
    };
    if (CONN_TCP == hSessionPtr->connType) {
        // Turn off TCP to prevent continuing writing
        Base::TryCloseHandle((uv_handle_t *)&hSessionPtr->hWorkTCP, true, closeSessionTCPHandle);
    }
    hSessionPtr->availTailIndex = 0;
    if (hSessionPtr->ioBuf) {
        delete[] hSessionPtr->ioBuf;
        hSessionPtr->ioBuf = nullptr;
    }
    Base::TryCloseHandle((uv_handle_t *)&hSessionPtr->ctrlPipe[STREAM_MAIN], true, closeSessionTCPHandle);
    Base::TryCloseHandle((uv_handle_t *)&hSessionPtr->dataPipe[STREAM_MAIN], true, closeSessionTCPHandle);
    delete hSessionPtr->mapTask;
    hSessionPtr->mapTask = nullptr;
    HdcAuth::FreeKey(!hSessionPtr->serverOrDaemon, hSessionPtr->listKey);
    delete hSessionPtr->listKey;  // to clear
    hSessionPtr->listKey = nullptr;
    FreeSessionByConnectType(hSessionPtr);
    // finish
    Base::IdleUvTask(&loopMain, hSessionPtr, FreeSessionFinally);
}

void HdcSessionBase::FreeSessionOpeate(uv_timer_t *handle)
{
    HSessionPtr hSessionPtr = (HSessionPtr)handle->data;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSessionPtr->classInstance;
    if (hSessionPtr->ref > 0) {
        return;
    }
    WRITE_LOG(LOG_DEBUG, "FreeSessionOpeate ref:%u", uint32_t(hSessionPtr->ref));
#ifdef HDC_HOST
    if (hSessionPtr->hUSB != nullptr
        && (!hSessionPtr->hUSB->hostBulkIn.isShutdown || !hSessionPtr->hUSB->hostBulkOut.isShutdown)) {
        HdcUSBBase *pUSB = ((HdcUSBBase *)hSessionPtr->classModule);
        pUSB->CancelUsbIo(hSessionPtr);
        return;
    }
#endif
    // wait workthread to free
    if (hSessionPtr->ctrlPipe[STREAM_WORK].loop) {
        auto ctrl = BuildCtrlString(SP_STOP_SESSION, 0, nullptr, 0);
        Base::SendToStream((uv_stream_t *)&hSessionPtr->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
        WRITE_LOG(LOG_DEBUG, "FreeSessionOpeate, send workthread fo free. sessionId:%u", hSessionPtr->sessionId);
        auto callbackCheckFreeSessionContinue = [](uv_timer_t *handle) -> void {
            HSessionPtr hSessionPtr = (HSessionPtr)handle->data;
            HdcSessionBase *thisClass = (HdcSessionBase *)hSessionPtr->classInstance;
            if (!hSessionPtr->childCleared) {
                return;
            }
            Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
            thisClass->FreeSessionContinue(hSessionPtr);
        };
        Base::TimerUvTask(&thisClass->loopMain, hSessionPtr, callbackCheckFreeSessionContinue);
    } else {
        thisClass->FreeSessionContinue(hSessionPtr);
    }
    Base::TryCloseHandle((uv_handle_t *)handle, Base::CloseTimerCallback);
}

void HdcSessionBase::FreeSession(const uint32_t sessionId)
{
    if (threadSessionMain != uv_thread_self()) {
        PushAsyncMessage(sessionId, ASYNC_FREE_SESSION, nullptr, 0);
        return;
    }
    HSessionPtr hSessionPtr = AdminSession(OP_QUERY, sessionId, nullptr);
    WRITE_LOG(LOG_DEBUG, "Begin to free session, sessionid:%u", sessionId);
    do {
        if (!hSessionPtr || hSessionPtr->isDead) {
            break;
        }
        hSessionPtr->isDead = true;
        Base::TimerUvTask(&loopMain, hSessionPtr, FreeSessionOpeate);
        NotifyInstanceSessionFree(hSessionPtr, false);
        WRITE_LOG(LOG_DEBUG, "FreeSession sessionId:%u ref:%u", hSessionPtr->sessionId, uint32_t(hSessionPtr->ref));
    } while (false);
}

HSessionPtr HdcSessionBase::AdminSession(const uint8_t op, const uint32_t sessionId, HSessionPtr hInput)
{
    HSessionPtr hRet = nullptr;
    switch (op) {
        case OP_ADD:
            if (hInput == nullptr) {
                return nullptr;
            }
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
            if (hInput == nullptr) {
                return nullptr;
            }
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
HTaskInfoPtr HdcSessionBase::AdminTask(const uint8_t op, HSessionPtr hSessionPtr, const uint32_t channelId, HTaskInfoPtr hInput)
{
    HTaskInfoPtr hRet = nullptr;
    map<uint32_t, HTaskInfoPtr> &mapTask = *hSessionPtr->mapTask;
    switch (op) {
        case OP_ADD:
#ifndef HDC_HOST
            // uv sub-thread confiured by threadPoolCount, reserve 2 for main & communicate
            if (mapTask.size() >= (threadPoolCount - 2)) {
                WRITE_LOG(LOG_WARN, "mapTask.size:%d, hdc is busy", mapTask.size());
                break;
            }
#endif
            if (hInput == nullptr) {
                return nullptr;
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
            AdminSession(op, hSessionPtr->sessionId, nullptr);
            break;
        default:
            break;
    }
    return hRet;
}

int HdcSessionBase::SendByProtocol(HSessionPtr hSessionPtr, uint8_t *bufPtr, const int bufLen)
{
    if (hSessionPtr->isDead) {
        WRITE_LOG(LOG_WARN, "SendByProtocol session dead error");
        return ERR_SESSION_NOFOUND;
    }
    int ret = 0;
    switch (hSessionPtr->connType) {
        case CONN_TCP: {
            if (hSessionPtr->hWorkThread == uv_thread_self()) {
                ret = Base::SendToStreamEx((uv_stream_t *)&hSessionPtr->hWorkTCP, bufPtr, bufLen, nullptr,
                                           (void *)FinishWriteSessionTCP, bufPtr);
            } else if (hSessionPtr->hWorkChildThread == uv_thread_self()) {
                ret = Base::SendToStreamEx((uv_stream_t *)&hSessionPtr->hChildWorkTCP, bufPtr, bufLen, nullptr,
                                           (void *)FinishWriteSessionTCP, bufPtr);
            } else {
                WRITE_LOG(LOG_FATAL, "SendByProtocol uncontrol send");
                ret = ERR_API_FAIL;
            }
            if (ret > 0) {
                ++hSessionPtr->ref;
            }
            break;
        }
        case CONN_USB: {
            HdcUSBBase *pUSB = ((HdcUSBBase *)hSessionPtr->classModule);
            if (pUSB == nullptr) {
                return 0;
            }
            ret = pUSB->SendUSBBlock(hSessionPtr, bufPtr, bufLen);
            delete[] bufPtr;
            break;
        }
#ifdef HDC_SUPPORT_UART
        case CONN_SERIAL: {
            HdcUARTBase *pUART = ((HdcUARTBase *)hSessionPtr->classModule);
            ret = pUART->SendUARTData(hSessionPtr, bufPtr, bufLen);
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
    HSessionPtr hSessionPtr = AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hSessionPtr) {
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
    uint8_t *finayBuf = new(std::nothrow) uint8_t[finalBufSize]();
    if (finayBuf == nullptr) {
        WRITE_LOG(LOG_WARN, "send allocmem err");
        return ERR_BUF_ALLOC;
    }
    bool bufRet = false;
    do {
        if (memcpy_s(finayBuf, sizeof(PayloadHead), reinterpret_cast<uint8_t *>(&payloadHead), sizeof(PayloadHead)) != EOK) {
            WRITE_LOG(LOG_WARN, "send copyhead err for dataSize:%d", dataSize);
            break;
        }
        if (memcpy_s(finayBuf + sizeof(PayloadHead), s.size(),
                     reinterpret_cast<uint8_t *>(const_cast<char *>(s.c_str())), s.size()) != EOK) {
            WRITE_LOG(LOG_WARN, "send copyProtbuf err for dataSize:%d", dataSize);
            break;
        }
        if (dataSize > 0 && memcpy_s(finayBuf + sizeof(PayloadHead) + s.size(), dataSize, data, dataSize) != EOK) {
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
    return SendByProtocol(hSessionPtr, finayBuf, finalBufSize);
}

int HdcSessionBase::DecryptPayload(HSessionPtr hSessionPtr, PayloadHead *payloadHeadBe, uint8_t *encBuf)
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
    if (!FetchCommand(hSessionPtr, protectBuf.channelId, protectBuf.commandFlag, data, dataSize)) {
        WRITE_LOG(LOG_WARN, "FetchCommand failed: channelId %x commandFlag %x",
                  protectBuf.channelId, protectBuf.commandFlag);
        return ERR_GENERIC;
    }
    return RET_SUCCESS;
}

int HdcSessionBase::OnRead(HSessionPtr hSessionPtr, uint8_t *bufPtr, const int bufLen)
{
    int ret = ERR_GENERIC;
    if (memcmp(bufPtr, PACKET_FLAG.c_str(), PACKET_FLAG.size()) != EOK) {
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
    if (DecryptPayload(hSessionPtr, payloadHead, bufPtr + packetHeadSize)) {
        WRITE_LOG(LOG_WARN, "decrypt plhead error");
        return ERR_BUF_CHECK;
    }
    ret = packetHeadSize + tobeReadLen;
    return ret;
}

// Returns <0 error;> 0 receives the number of bytes; 0 untreated
int HdcSessionBase::FetchIOBuf(HSessionPtr hSessionPtr, uint8_t *ioBuf, int read)
{
    if (hSessionPtr == nullptr || hSessionPtr->classInstance == nullptr || ioBuf == nullptr) {
        return ERR_GENERIC;
    }
    HdcSessionBase *ptrConnect = (HdcSessionBase *)hSessionPtr->classInstance;
    int indexBuf = 0;
    int childRet = 0;
    if (read < 0) {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r(read, buf, bufSize);
        WRITE_LOG(LOG_FATAL, "HdcSessionBase read io failed,%s", buf);
        return ERR_IO_FAIL;
    }
    hSessionPtr->availTailIndex += read;
    while (!hSessionPtr->isDead && hSessionPtr->availTailIndex > static_cast<int>(sizeof(PayloadHead))) {
        childRet = ptrConnect->OnRead(hSessionPtr, ioBuf + indexBuf, hSessionPtr->availTailIndex);
        if (childRet > 0) {
            hSessionPtr->availTailIndex -= childRet;
            indexBuf += childRet;
        } else if (childRet == 0) {
            // Not enough a IO
            break;
        } else {                           // <0
            hSessionPtr->availTailIndex = 0;  // Preventing malicious data packages
            indexBuf = ERR_BUF_SIZE;
            break;
        }
        // It may be multi-time IO to merge in a BUF, need to loop processing
    }
    if (indexBuf > 0 && hSessionPtr->availTailIndex > 0) {
        if (memmove_s(hSessionPtr->ioBuf, hSessionPtr->bufSize, hSessionPtr->ioBuf + indexBuf, hSessionPtr->availTailIndex)
            != EOK) {
            return ERR_BUF_COPY;
        };
        uint8_t *bufToZero = (uint8_t *)(hSessionPtr->ioBuf + hSessionPtr->availTailIndex);
        Base::ZeroBuf(bufToZero, hSessionPtr->bufSize - hSessionPtr->availTailIndex);
    }
    return indexBuf;
}

void HdcSessionBase::AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    if (handle == nullptr || handle->data == nullptr || buf == nullptr) {
        return;
    }
    HSessionPtr context = (HSessionPtr)handle->data;
    Base::ReallocBuf(&context->ioBuf, &context->bufSize, HDC_SOCKETPAIR_SIZE);
    buf->base = (char *)context->ioBuf + context->availTailIndex;
    int size = context->bufSize - context->availTailIndex;
    buf->len = std::min(size, static_cast<int>(sizeWanted));
}

void HdcSessionBase::FinishWriteSessionTCP(uv_write_t *req, int status)
{
    if (req == nullptr || req->handle == nullptr || req->handle->data == nullptr) {
        return;
    }
    HSessionPtr hSessionPtr = (HSessionPtr)req->handle->data;
    --hSessionPtr->ref;
    HdcSessionBase *thisClass = (HdcSessionBase *)hSessionPtr->classInstance;
    if (status < 0) {
        Base::TryCloseHandle((uv_handle_t *)req->handle);
        if (thisClass != nullptr && !hSessionPtr->isDead && !hSessionPtr->ref) {
            WRITE_LOG(LOG_DEBUG, "FinishWriteSessionTCP freesession :%p", hSessionPtr);
            thisClass->FreeSession(hSessionPtr->sessionId);
        }
    }
    if (req->data == nullptr) {
        delete[]((uint8_t *)req->data);
    }
    delete req;
}

bool HdcSessionBase::DispatchSessionThreadCommand(uv_stream_t *uvpipe, HSessionPtr hSessionPtr, const uint8_t *baseBuf,
                                                  const int bytesIO)
{
    if (baseBuf == nullptr) {
        return false;
    }
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
    if (uvpipe == nullptr || uvpipe->data == nullptr) {
        return;
    }
    HSessionPtr hSessionPtr = (HSessionPtr)uvpipe->data;
    HdcSessionBase *hSessionBase = (HdcSessionBase *)hSessionPtr->classInstance;
    if (hSessionBase == nullptr) {
        return;
    }
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
        hSessionBase->DispatchSessionThreadCommand(uvpipe, hSessionPtr, (uint8_t *)buf->base, nread);
        break;
    }
    if (buf->base != nullptr) {
        delete[] buf->base;
    }
}

bool HdcSessionBase::WorkThreadStartSession(HSessionPtr hSessionPtr)
{
    bool regOK = false;
    int childRet = 0;
    if (hSessionPtr->connType == CONN_TCP) {
        HdcTCPBase *pTCPBase = (HdcTCPBase *)hSessionPtr->classModule;
        hSessionPtr->hChildWorkTCP.data = hSessionPtr;
        if ((childRet = uv_tcp_init(&hSessionPtr->childLoop, &hSessionPtr->hChildWorkTCP)) < 0) {
            WRITE_LOG(LOG_DEBUG, "HdcSessionBase SessionCtrl failed 1");
            return false;
        }
        if ((childRet = uv_tcp_open(&hSessionPtr->hChildWorkTCP, hSessionPtr->fdChildWorkTCP)) < 0) {
            constexpr int bufSize = 1024;
            char buf[bufSize] = { 0 };
            uv_strerror_r(childRet, buf, bufSize);
            WRITE_LOG(LOG_DEBUG, "SessionCtrl failed 2,fd:%d,str:%s", hSessionPtr->fdChildWorkTCP, buf);
            return false;
        }
        Base::SetTcpOptions((uv_tcp_t *)&hSessionPtr->hChildWorkTCP);
        uv_read_start((uv_stream_t *)&hSessionPtr->hChildWorkTCP, AllocCallback, pTCPBase->ReadStream);
        regOK = true;
#ifdef HDC_SUPPORT_UART
    } else if (hSessionPtr->connType == CONN_SERIAL) { // UART
        HdcUARTBase *pUARTBase = (HdcUARTBase *)hSessionPtr->classModule;
        WRITE_LOG(LOG_DEBUG, "UART ReadyForWorkThread");
        regOK = pUARTBase->ReadyForWorkThread(hSessionPtr);
#endif
    } else {  // USB
        HdcUSBBase *pUSBBase = (HdcUSBBase *)hSessionPtr->classModule;
        WRITE_LOG(LOG_DEBUG, "USB ReadyForWorkThread");
        regOK = pUSBBase->ReadyForWorkThread(hSessionPtr);
    }

    if (regOK && hSessionPtr->serverOrDaemon) {
        // session handshake step1
        SessionHandShake handshake = {};
        handshake.banner = HANDSHAKE_MESSAGE;
        handshake.sessionId = hSessionPtr->sessionId;
        handshake.connectKey = hSessionPtr->connectKey;
        handshake.authType = AUTH_NONE;
        string hs = SerialStruct::SerializeToString(handshake);
#ifdef HDC_SUPPORT_UART
        WRITE_LOG(LOG_DEBUG, "WorkThreadStartSession session %u auth %u send handshake hs:",
                  hSessionPtr->sessionId, handshake.authType, hs.c_str());
#endif
        Send(hSessionPtr->sessionId, 0, CMD_KERNEL_HANDSHAKE, (uint8_t *)hs.c_str(), hs.size());
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

bool HdcSessionBase::DispatchMainThreadCommand(HSessionPtr hSessionPtr, const CtrlStruct *ctrl)
{
    bool ret = true;
    uint32_t channelId = ctrl->channelId;  // if send not set, it is zero
    switch (ctrl->command) {
        case SP_START_SESSION: {
            WRITE_LOG(LOG_DEBUG, "Dispatch MainThreadCommand  START_SESSION sessionId:%u instance:%s",
                      hSessionPtr->sessionId, hSessionPtr->serverOrDaemon ? "server" : "daemon");
            ret = WorkThreadStartSession(hSessionPtr);
            break;
        }
        case SP_STOP_SESSION: {
            WRITE_LOG(LOG_DEBUG, "Dispatch MainThreadCommand STOP_SESSION sessionId:%u", hSessionPtr->sessionId);
            auto closeSessionChildThreadTCPHandle = [](uv_handle_t *handle) -> void {
                HSessionPtr hSessionPtr = (HSessionPtr)handle->data;
                Base::TryCloseHandle((uv_handle_t *)handle);
                if (--hSessionPtr->uvChildRef == 0) {
                    uv_stop(&hSessionPtr->childLoop);
                };
            };
            hSessionPtr->uvChildRef += 2;
            if (hSessionPtr->hChildWorkTCP.loop) {  // maybe not use it
                ++hSessionPtr->uvChildRef;
                Base::TryCloseHandle((uv_handle_t *)&hSessionPtr->hChildWorkTCP, true, closeSessionChildThreadTCPHandle);
            }
            Base::TryCloseHandle((uv_handle_t *)&hSessionPtr->ctrlPipe[STREAM_WORK], true,
                                 closeSessionChildThreadTCPHandle);
            Base::TryCloseHandle((uv_handle_t *)&hSessionPtr->dataPipe[STREAM_WORK], true,
                                 closeSessionChildThreadTCPHandle);
            break;
        }
        case SP_ATTACH_CHANNEL: {
            if (!serverOrDaemon) {
                break;  // Only Server has this feature
            }
            AttachChannel(hSessionPtr, channelId);
            break;
        }
        case SP_DEATCH_CHANNEL: {
            if (!serverOrDaemon) {
                break;  // Only Server has this feature
            }
            DeatchChannel(hSessionPtr, channelId);
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
    if (uvpipe == nullptr || uvpipe->data == nullptr || buf == nullptr || buf->base == nullptr) {
        return;
    }
    HSessionPtr hSessionPtr = (HSessionPtr)uvpipe->data;
    HdcSessionBase *hSessionBase = (HdcSessionBase *)hSessionPtr->classInstance;
    if (hSessionBase == nullptr) {
        return;
    }
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
        if (!(ret = hSessionBase->DispatchMainThreadCommand(hSessionPtr, ctrl))) {
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

void HdcSessionBase::ReChildLoopForSessionClear(HSessionPtr hSessionPtr)
{
    // Restart loop close task
    ClearOwnTasks(hSessionPtr, 0);
    WRITE_LOG(LOG_INFO, "ReChildLoopForSessionClear sessionId:%u", hSessionPtr->sessionId);
    auto clearTaskForSessionFinish = [](uv_timer_t *handle) -> void {
        HSessionPtr hSessionPtr = (HSessionPtr)handle->data;
        for (auto v : *hSessionPtr->mapTask) {
            HTaskInfoPtr hTask = (HTaskInfoPtr)v.second;
            if (hTask == nullptr) {
                continue;
            }
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
                if (thisClass == nullptr) {
                    continue;
                }
                hSessionPtr = thisClass->AdminSession(OP_QUERY, hTask->sessionId, nullptr);
                thisClass->AdminTask(OP_VOTE_RESET, hSessionPtr, hTask->channelId, nullptr);
            }
            if (!hTask->taskFree)
                return;
        }
        // all task has been free
        uv_close((uv_handle_t *)handle, Base::CloseTimerCallback);
        uv_stop(&hSessionPtr->childLoop);  // stop ReChildLoopForSessionClear pendding
    };
    Base::TimerUvTask(&hSessionPtr->childLoop, hSessionPtr, clearTaskForSessionFinish, (GLOBAL_TIMEOUT * TIME_BASE) / UV_DEFAULT_INTERVAL);
    uv_run(&hSessionPtr->childLoop, UV_RUN_DEFAULT);
    // clear
    Base::TryCloseLoop(&hSessionPtr->childLoop, "Session childUV");
}

void HdcSessionBase::SessionWorkThread(uv_work_t *arg)
{
    int childRet = 0;
    HSessionPtr hSessionPtr = (HSessionPtr)arg->data;
    if (hSessionPtr == nullptr) {
        return;
    }
    HdcSessionBase *thisClass = (HdcSessionBase *)hSessionPtr->classInstance;
    uv_loop_init(&hSessionPtr->childLoop);
    hSessionPtr->hWorkChildThread = uv_thread_self();
    if ((childRet = uv_tcp_init(&hSessionPtr->childLoop, &hSessionPtr->ctrlPipe[STREAM_WORK])) < 0) {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r(childRet, buf, bufSize);
        WRITE_LOG(LOG_DEBUG, "SessionCtrl err1, %s", buf);
    }
    if ((childRet = uv_tcp_open(&hSessionPtr->ctrlPipe[STREAM_WORK], hSessionPtr->ctrlFd[STREAM_WORK])) < 0) {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r(childRet, buf, bufSize);
        WRITE_LOG(LOG_DEBUG, "SessionCtrl err2, %s fd:%d", buf, hSessionPtr->ctrlFd[STREAM_WORK]);
    }
    uv_read_start((uv_stream_t *)&hSessionPtr->ctrlPipe[STREAM_WORK], Base::AllocBufferCallback, ReadCtrlFromMain);
    WRITE_LOG(LOG_DEBUG, "!!!Workthread run begin, sessionId:%u instance:%s", hSessionPtr->sessionId,
              thisClass->serverOrDaemon ? "server" : "daemon");
    uv_run(&hSessionPtr->childLoop, UV_RUN_DEFAULT);  // work pendding
    WRITE_LOG(LOG_DEBUG, "!!!Workthread run again, sessionId:%u", hSessionPtr->sessionId);
    // main loop has exit
    thisClass->ReChildLoopForSessionClear(hSessionPtr);  // work pending again
    hSessionPtr->childCleared = true;
    WRITE_LOG(LOG_DEBUG, "!!!Workthread run finish, sessionId:%u workthread:%p", hSessionPtr->sessionId, uv_thread_self());
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
    } else if (taskMasterInit) {
        // task init command
        masterTask = true;
        ret = true;
    }
    return ret;
}
// Heavy and time-consuming work was putted in the new thread to do, and does
// not occupy the main thread
bool HdcSessionBase::DispatchTaskData(HSessionPtr hSessionPtr, const uint32_t channelId, const uint16_t command,
                                      uint8_t *payload, int payloadSize)
{
    bool ret = true;
    HTaskInfoPtr hTaskInfo = nullptr;
    bool masterTask = false;
    while (true) {
        // Some basic commands do not have a local task constructor. example: Interactive shell, some uinty commands
        if (NeedNewTaskInfo(command, masterTask)) {
            WRITE_LOG(LOG_DEBUG, "New HTaskInfoPtr channelId:%u command:%u", channelId, command);
            hTaskInfo = new(std::nothrow) TaskInformation();
            if (hTaskInfo == nullptr || hSessionPtr == nullptr) {
                WRITE_LOG(LOG_FATAL, "DispatchTaskData new hTaskInfo failed");
                break;
            }
            hTaskInfo->channelId = channelId;
            hTaskInfo->sessionId = hSessionPtr->sessionId;
            hTaskInfo->runLoop = &hSessionPtr->childLoop;
            hTaskInfo->serverOrDaemon = serverOrDaemon;
            hTaskInfo->masterSlave = masterTask;
            hTaskInfo->closeRetryCount = 0;

            int addTaskRetry = 3; // try 3 time
            while (addTaskRetry > 0) {
                if (AdminTask(OP_ADD, hSessionPtr, channelId, hTaskInfo)) {
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
            hTaskInfo = AdminTask(OP_QUERY, hSessionPtr, channelId, nullptr);
        }
        if (!hTaskInfo || hTaskInfo->taskStop || hTaskInfo->taskFree) {
            WRITE_LOG(LOG_ALL, "Dead HTaskInfoPtr, ignore, channelId:%u command:%u", channelId, command);
            break;
        }
        ret = RedirectToTask(hTaskInfo, hSessionPtr, channelId, command, payload, payloadSize);
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
