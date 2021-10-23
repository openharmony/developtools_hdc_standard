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
    bool FetchCommand(HSession hSession, const uint32_t channelId, const uint16_t command, uint8_t *payload,
                      const int payloadSize);
    string AdminDaemonMap(uint8_t opType, const string &connectKey, HDaemonInfo &hDaemonInfoInOut);
    string AdminForwardMap(uint8_t opType, const string &taskString, HForwardInfo &hForwardInfoInOut);
    int CreateConnect(const string &connectKey);
    bool Initial(const char *listenString);
    void AttachChannel(HSession hSession, const uint32_t channelId);
    void DeatchChannel(HSession hSession, const uint32_t channelId);
    static bool PullupServer(const char *listenString);
    static void UsbPreConnect(uv_timer_t *handle);
    void NotifyInstanceSessionFree(HSession hSession, bool freeOrClear);

    HdcHostTCP *clsTCPClt;
    HdcHostUSB *clsUSBClt;
    void *clsServerForClient;

private:
    void ClearInstanceResource();
    void BuildDaemonVisableLine(HDaemonInfo hDaemonInfoInOut, bool fullDisplay, string &out);
    void BuildForwardVisableLine(bool fullOrSimble, HForwardInfo hfi, string &echo);
    void ClearMapDaemonInfo();
    bool ServerCommand(const uint32_t sessionId, const uint32_t channelId, const uint16_t command, uint8_t *bufPtr,
                       const int size);
    bool RedirectToTask(HTaskInfo hTaskInfo, HSession hSession, const uint32_t channelId, const uint16_t command,
                        uint8_t *payload, const int payloadSize);
    bool RemoveInstanceTask(const uint8_t op, HTaskInfo hTask);
    void BuildForwardVisableLine(HDaemonInfo hdi, char *out, int sizeOutBuf);
    bool HandServerAuth(HSession hSession, SessionHandShake &handshake);
    string GetDaemonMapList(uint8_t opType);
    bool ServerSessionHandshake(HSession hSession, uint8_t *payload, int payloadSize);
    void GetDaemonMapOnlyOne(HDaemonInfo &hDaemonInfoInOut);
    void TryStopInstance();
    static bool PullupServerWin32(const char *path, const char *listenString);

    uv_rwlock_t daemonAdmin;
    map<string, HDaemonInfo> mapDaemon;
    uv_rwlock_t forwardAdmin;
    map<string, HForwardInfo> mapForward;
};
}  // namespace Hdc
#endif