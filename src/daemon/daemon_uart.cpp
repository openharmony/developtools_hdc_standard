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
#include "daemon_uart.h"

#include <thread>
#include <fcntl.h>
#include <file_ex.h>
#include <string_ex.h>

#include <sys/file.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/time.h>

namespace Hdc {
HdcDaemonUART::HdcDaemonUART(HdcDaemon &daemonSessionIn, ExternInterface &externInterface)
    : HdcUARTBase(daemonSessionIn, externInterface), daemon(daemonSessionIn)
{
    checkSerialPort.data = nullptr;
}

int HdcDaemonUART::Initial(const std::string &devPathIn)
{
    int ret = 0;
    devPath = devPathIn;
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUART init");
    if (access(devPath.c_str(), F_OK) != 0) {
        WRITE_LOG(LOG_DEBUG, "uartMod Disable");
        return -1;
    }
#ifndef HDC_UT
    std::string consoleActive;
    if (OHOS::LoadStringFromFile(CONSOLE_ACTIVE_NODE, consoleActive)) {
        consoleActive = OHOS::TrimStr(consoleActive,'\n');
        WRITE_LOG(LOG_DEBUG, "consoleActive (%d):%s", consoleActive.length(),
                  consoleActive.c_str());
        if (devPathIn.find(consoleActive.c_str()) != std::string::npos) {
            WRITE_LOG(LOG_FATAL,
                      "kernel use this dev(%s) as console , we can't open it as hdc uart dev",
                      devPathIn.c_str());
            return -1;
        }
    }
#endif
    constexpr int bufSize = 1024;
    char buf[bufSize] = { 0 };
    const uint16_t uartScanInterval = 1500;
    ret = uv_timer_init(&daemon.loopMain, &checkSerialPort);
    if (ret != 0) {
        uv_err_name_r(ret, buf, bufSize);
        WRITE_LOG(LOG_FATAL, "uv_timer_init failed %s", buf);
    } else {
        checkSerialPort.data = this;
        ret = uv_timer_start(&checkSerialPort, UvWatchTimer, 0, uartScanInterval);
        if (ret != 0) {
            uv_err_name_r(ret, buf, bufSize);
            WRITE_LOG(LOG_FATAL, "uv_timer_start failed %s", buf);
        } else {
            return 0;
        }
    }
    return -1;
}

int HdcDaemonUART::PrepareBufForRead()
{
    constexpr int bufCoefficient = 1;
    int readMax = MAX_UART_SIZE_IOBUF * bufCoefficient;
    dataReadBuf.clear();
    dataReadBuf.reserve(readMax);
    return RET_SUCCESS;
}

void HdcDaemonUART::WatcherTimerCallBack()
{
    // try reanbel the uart device (reopen)
    if (isAlive) {
        return;
    }
    do {
        if (uartHandle >= 0) {
            if (CloseUartDevice() != RET_SUCCESS) {
                break;
            }
        }
        if ((OpenUartDevice() != RET_SUCCESS)) {
            WRITE_LOG(LOG_DEBUG, "OpenUartdevice fail ! ");
            break;
        }
        if ((PrepareBufForRead() != RET_SUCCESS)) {
            WRITE_LOG(LOG_DEBUG, "PrepareBufForRead fail ! ");
            break;
        }
        // read and write thread need this flag
        isAlive = true;
        if ((LoopUARTRead() != RET_SUCCESS)) {
            WRITE_LOG(LOG_DEBUG, "LoopUARTRead fail ! ");
            break;
        }
        if ((LoopUARTWrite() != RET_SUCCESS)) {
            WRITE_LOG(LOG_DEBUG, "LoopUARTWrite fail ! ");
            break;
        }
        return;
    } while (false);
    WRITE_LOG(LOG_FATAL, "WatcherTimerCallBack found some issue");
    isAlive = false;
}

int HdcDaemonUART::CloseUartDevice()
{
    int ret = close(uartHandle);
    if (ret < 0) {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        strerror_r(errno, buf, bufSize);
        WRITE_LOG(LOG_FATAL, "DaemonUART stop for CloseBulkSpErrno: %d:%s\n", errno, buf);
    } else {
        uartHandle = -1;
    }
    isAlive = false;
    return ret;
}

int HdcDaemonUART::OpenUartDevice()
{
    int ret = ERR_GENERIC;
    while (true) {
        if ((uartHandle = open(devPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY)) < 0) {
            WRITE_LOG(LOG_WARN, "%s: cannot open uartHandle: errno=%d", devPath.c_str(), errno);
            break;
        }
        uv_sleep(uartIOWaitTime100);
        // cannot open with O_CLOEXEC, must fcntl
        fcntl(uartHandle, F_SETFD, FD_CLOEXEC);
        int flag = fcntl(uartHandle, F_GETFL);
        flag &= ~O_NONBLOCK;
        fcntl(uartHandle, F_SETFL, flag);
        WRITE_LOG(LOG_DEBUG, "Set SetSerial ");
        if (SetSerial(uartHandle, DEFAULT_BAUD_RATE_VALUE, UART_BIT2, 'N', 1) != RET_SUCCESS) {
            break;
        }
        ret = RET_SUCCESS;
        break;
    }
    if (ret != RET_SUCCESS) {
        WRITE_LOG(LOG_DEBUG, "OpenUartdevice SerialHandle:%d fail.", uartHandle);
    }
    return ret;
}

void HdcDaemonUART::ResetOldSession(uint32_t sessionId)
{
    if (sessionId == 0) {
        sessionId = currentSessionId;
    }
    HSession hSession = daemon.AdminSession(OP_QUERY, sessionId, nullptr);
    if (hSession == nullptr) {
        return;
    }
    if (hSession->hUART != nullptr) {
        hSession->hUART->resetIO = true;
    }
    // The Host side is restarted, but the USB cable is still connected
    WRITE_LOG(LOG_WARN, "Hostside softreset to restart daemon, old sessionId:%u", sessionId);
    OnTransferError(hSession);
}

HSession HdcDaemonUART::GetSession(const uint32_t sessionId, bool create = false)
{
    HSession hSession = daemon.AdminSession(OP_QUERY, sessionId, nullptr);
    if (hSession == nullptr and create) {
        hSession = PrepareNewSession(sessionId);
    }
    return hSession;
}

void HdcDaemonUART::OnTransferError(const HSession session)
{
    // review maybe we can do something more ?
    if (session != nullptr) {
        WRITE_LOG(LOG_FATAL, "%s %s", __FUNCTION__, session->ToDebugString().c_str());
        daemon.FreeSession(session->sessionId);
        ClearUARTOutMap(session->sessionId);
    }
}

void HdcDaemonUART::OnNewHandshakeOK(const uint32_t sessionId)
{
    currentSessionId = sessionId;
}

HSession HdcDaemonUART::PrepareNewSession(uint32_t sessionId)
{
    WRITE_LOG(LOG_FATAL, "%s sessionId:%u", __FUNCTION__, sessionId);
    HSession hSession = daemon.MallocSession(false, CONN_SERIAL, this, sessionId);
    if (!hSession) {
        WRITE_LOG(LOG_FATAL, "new session malloc failed for sessionId:%u", sessionId);
        return nullptr;
    }
    if (currentSessionId != 0) {
        // reset old session
        // The Host side is restarted, but the cable is still connected
        WRITE_LOG(LOG_WARN, "New session coming, restart old sessionId:%u", currentSessionId);
        daemon.PushAsyncMessage(currentSessionId, ASYNC_FREE_SESSION, nullptr, 0);
    }
    externInterface.StartWorkThread(&daemon.loopMain, daemon.SessionWorkThread,
                                    Base::FinishWorkThread, hSession);
    auto funcNewSessionUp = [](uv_timer_t *handle) -> void {
        HSession hSession = reinterpret_cast<HSession>(handle->data);
        HdcDaemon &daemonSession = *reinterpret_cast<HdcDaemon *>(hSession->classInstance);
        if (hSession->childLoop.active_handles == 0) {
            WRITE_LOG(LOG_DEBUG, "No active_handles.");
            return;
        }
        if (!hSession->isDead) {
            auto ctrl = daemonSession.BuildCtrlString(SP_START_SESSION, 0, nullptr, 0);
            Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], ctrl.data(),
                               ctrl.size());
            WRITE_LOG(LOG_DEBUG, "Main thread uartio mirgate finish");
        }
        Base::TryCloseHandle(reinterpret_cast<uv_handle_t *>(handle), Base::CloseTimerCallback);
    };
    externInterface.TimerUvTask(&daemon.loopMain, hSession, funcNewSessionUp);
    return hSession;
}

// review Merge this with Host side
void HdcDaemonUART::DeamonReadThread()
{
    HdcUART deamonUart;
    deamonUart.devUartHandle = uartHandle;
    dataReadBuf.clear();
    // after we got the head or something , we will expected some size
    size_t expectedSize = 0;
    // use < not <= because if it full , should not read again
    while (isAlive && dataReadBuf.size() < MAX_READ_BUFFER) {
        ssize_t bytesRead = ReadUartDev(dataReadBuf, expectedSize, deamonUart);
        if (bytesRead == 0) {
            WRITE_LOG(LOG_DEBUG, "%s read %zd, clean the data try read again.", __FUNCTION__,
                      bytesRead);
            // drop current cache
            expectedSize = 0;
            dataReadBuf.clear();
            continue;
        } else if (bytesRead < 0) {
            WRITE_LOG(LOG_DEBUG, "%s read abnormal, stop uart module.", __FUNCTION__);
            Stop();
            break;
        }
        WRITE_LOG(LOG_DEBUG, "DeamonReadThread bytesRead:%d, totalReadBytes.size():%d.", bytesRead,
                  dataReadBuf.size());

        if (dataReadBuf.size() < sizeof(UartHead)) {
            continue; // no enough ,read again
        }
        expectedSize = PackageProcess(dataReadBuf);
    }
    if (isAlive) {
        WRITE_LOG(LOG_WARN, "totalReadSize is full %zu/%zu, DeamonReadThread exit..",
                  dataReadBuf.size());
    } else {
        WRITE_LOG(LOG_WARN, "dev is not alive, DeamonReadThread exit..");
    }
    // why not free session here
    isAlive = false;
    return;
}

void HdcDaemonUART::DeamonWriteThread()
{
    while (isAlive) {
        WRITE_LOG(LOG_DEBUG, "DeamonWriteThread wait sendLock.");
        transfer.Wait();
        SendPkgInUARTOutMap();
    }
    WRITE_LOG(LOG_WARN, "dev is not alive, DeamonWriteThread exit..");
    return;
}

int HdcDaemonUART::LoopUARTRead()
{
    try {
        std::thread deamonReadThread(std::bind(&HdcDaemonUART::DeamonReadThread, this));
        deamonReadThread.detach();
        return 0;
    } catch (...) {
        WRITE_LOG(LOG_WARN, "create thread DeamonReadThread failed");
    }
    return -1;
}

int HdcDaemonUART::LoopUARTWrite()
{
    try {
        std::thread deamonWriteThread(std::bind(&HdcDaemonUART::DeamonWriteThread, this));
        deamonWriteThread.detach();
        return 0;
    } catch (...) {
        WRITE_LOG(LOG_WARN, "create thread DeamonWriteThread failed");
    }
    return -1;
}

bool HdcDaemonUART::IsSendReady(HSession hSession)
{
    if (isAlive and !hSession->isDead and uartHandle >= 0 and !hSession->hUART->resetIO) {
        return true;
    } else {
        if (!isAlive) {
            WRITE_LOG(LOG_WARN, "!isAlive");
        } else if (hSession->isDead) {
            WRITE_LOG(LOG_WARN, "session isDead");
        } else if (uartHandle < 0) {
            WRITE_LOG(LOG_WARN, "uartHandle is not valid");
        } else if (hSession->hUART->resetIO) {
            WRITE_LOG(LOG_WARN, "session have resetIO");
        }
        return false;
    }
};

HdcDaemonUART::~HdcDaemonUART()
{
    Stop();
}

void HdcDaemonUART::Stop()
{
    WRITE_LOG(LOG_DEBUG, "%s run!", __FUNCTION__);
    if (!stopped) {
        stopped = true;
        std::lock_guard<std::mutex> lock(workThreadProcessingData);

        // maybe some data response not back to host
        // like smode need response.
        ResponseUartTrans(currentSessionId, 0, PKG_OPTION_FREE);
        EnsureAllPkgsSent();
        isAlive = false;
        WRITE_LOG(LOG_DEBUG, "%s free main session", __FUNCTION__);
        if (checkSerialPort.data != nullptr) {
            externInterface.TryCloseHandle((uv_handle_t *)&checkSerialPort);
            checkSerialPort.data = nullptr;
        }
        CloseUartDevice();
        WRITE_LOG(LOG_DEBUG, "%s free main session finish", __FUNCTION__);
    }
}
} // namespace Hdc
