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
#ifndef HDC_DAEMON_UART_H
#define HDC_DAEMON_UART_H
#include <pthread.h>
#include "daemon_common.h"

namespace Hdc {
class HdcDaemon;

class HdcDaemonUART : public HdcUARTBase {
public:
    explicit HdcDaemonUART(HdcDaemon &, ExternInterface & = HdcUARTBase::defaultInterface);
    int Initial(const std::string &devPathIn = UART_HDC_NODE);
    ~HdcDaemonUART();

    void OnNewHandshakeOK(const uint32_t sessionId);
    void Stop();
protected:
    virtual HSession GetSession(const uint32_t sessionId, bool create) override;
    virtual void OnTransferError(const HSession session) override;

private:
    static inline void UvWatchTimer(uv_timer_t *handle)
    {
        if (handle != nullptr) {
            HdcDaemonUART *thisClass = static_cast<HdcDaemonUART *>(handle->data);
            if (thisClass != nullptr) {
                thisClass->WatcherTimerCallBack();
                return;
            }
        }
        WRITE_LOG(LOG_FATAL, "UvWatchTimer have not got correct class parameter");
    };
    virtual void WatcherTimerCallBack(); // ut will overider this
    virtual int CloseUartDevice();
    virtual int OpenUartDevice();
    virtual int LoopUARTRead();
    virtual int LoopUARTWrite();
    virtual bool IsSendReady(HSession hSession);
    virtual int PrepareBufForRead();
    virtual HSession PrepareNewSession(uint32_t sessionId);
    virtual void DeamonReadThread();
    virtual void DeamonWriteThread();
    std::vector<uint8_t> dataReadBuf; // from uart dev
    virtual void ResetOldSession(uint32_t sessionId) override;

    uv_timer_t checkSerialPort; // server-use
    uint32_t currentSessionId = 0;
    bool isAlive = false;
    std::string devPath;

    HdcDaemon &daemon;
};
} // namespace Hdc
#endif
