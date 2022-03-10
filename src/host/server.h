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
#ifndef HDC_SERVER_H
#define HDC_SERVER_H
#include "host_common.h"

namespace Hdc {
class HdcServer : public HdcSessionBase {
public:
    HdcServer(bool serverOrDaemonIn);
    virtual ~HdcServer();
    bool FetchCommand(HSessionPtr hSessionPtr, const uint32_t channelId, const uint16_t command, uint8_t *payload,
                      const int payloadSize);
    virtual string AdminDaemonMap(uint8_t opType, const string &connectKey, HDaemonInfoPtr &hDaemonInfoInOut);
    string AdminForwardMap(uint8_t opType, const string &taskString, HForwardInfoPtr &hForwardInfoInOut);
    int CreateConnect(const string &connectKey);
    bool Initial(const char *listenString);
    void AttachChannel(HSessionPtr hSessionPtr, const uint32_t channelId);
    void DeatchChannel(HSessionPtr hSessionPtr, const uint32_t channelId);
    virtual void EchoToClientsForSession(uint32_t targetSessionId, const string &echo);
    static bool PullupServer(const char *listenString);
    static void UsbPreConnect(uv_timer_t *handle);
    void NotifyInstanceSessionFree(HSessionPtr hSessionPtr, bool freeOrClear);

    HdcHostTCP *clsTCPClt;
    HdcHostUSB *clsUSBClt;
#ifdef HDC_SUPPORT_UART
    void CreatConnectUart(HSessionPtr hSessionPtr);
    static void UartPreConnect(uv_timer_t *handle);
    HdcHostUART *clsUARTClt = nullptr;
#endif
    void *clsServerForClient;

private:
    void ClearInstanceResource();
    void BuildDaemonVisableLine(HDaemonInfoPtr hdi, bool fullDisplay, string &out);
    void BuildForwardVisableLine(bool fullOrSimble, HForwardInfoPtr hfi, string &echo);
    void ClearMapDaemonInfo();
    bool ServerCommand(const uint32_t sessionId, const uint32_t channelId, const uint16_t command, uint8_t *bufPtr,
                       const int size);
    bool RedirectToTask(HTaskInfoPtr hTaskInfo, HSessionPtr hSessionPtr, const uint32_t channelId, const uint16_t command,
                        uint8_t *payload, const int payloadSize);
    bool RemoveInstanceTask(const uint8_t op, HTaskInfoPtr hTask);
    void BuildForwardVisableLine(HDaemonInfoPtr hdi, char *out, int sizeOutBuf);
    bool HandServerAuth(HSessionPtr hSessionPtr, SessionHandShake &handshake);
    string GetDaemonMapList(uint8_t opType);
    bool ServerSessionHandshake(HSessionPtr hSessionPtr, uint8_t *payload, int payloadSize);
    void GetDaemonMapOnlyOne(HDaemonInfoPtr &hDaemonInfoInOut);
    void TryStopInstance();
    static bool PullupServerWin32(const char *path, const char *listenString);

    uv_rwlock_t daemonAdmin;
    map<string, HDaemonInfoPtr> mapDaemon;
    uv_rwlock_t forwardAdmin;
    map<string, HForwardInfoPtr> mapForward;
};
}  // namespace Hdc
#endif