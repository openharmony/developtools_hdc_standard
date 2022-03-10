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
#ifndef HDC_HOST_TCP_H
#define HDC_HOST_TCP_H
#include "host_common.h"

namespace Hdc {
class HdcHostTCP : public HdcTCPBase {
public:
    HdcHostTCP(const bool serverOrDaemonIn, void *ptrMainBase);
    virtual ~HdcHostTCP();
    void FindLanDaemon();
    HSessionPtr ConnectDaemon(const string &connectKey);
    void Stop();
    list<string> lstDaemonResult;

private:
    static void BroadcastTimer(uv_idle_t *handle);
    static void Connect(uv_connect_t *connection, int status);
    void BroadcatFindDaemon(const char *broadcastLanIP);
    void RecvUDPEntry(const sockaddr *addrSrc, uv_udp_t *handle, const uv_buf_t *rcvbuf);

    bool broadcastFindWorking;
};
}  // namespace Hdc
#endif