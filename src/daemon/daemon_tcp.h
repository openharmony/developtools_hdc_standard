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
#ifndef HDC_DAEMON_TCP_H
#define HDC_DAEMON_TCP_H
#include "daemon_common.h"

namespace Hdc {
class HdcDaemonTCP : public HdcTCPBase {
public:
    HdcDaemonTCP(const bool serverOrDaemonIn, void *ptrMainBase);
    virtual ~HdcDaemonTCP();
    void RecvUDPEntry(const sockaddr *addrSrc, uv_udp_t *handle, const uv_buf_t *rcvbuf);
    uint16_t tcpListenPort;
    int Initial();
    void Stop();

private:
    static void AcceptClient(uv_stream_t *server, int status);
    void TransmitConfig(const sockaddr *addrSrc, uv_udp_t *handle);
    int SetTCPListen();
    void SetUDPListen();

    uv_tcp_t servTCP = {};
    uv_udp_t servUDP = {};
};
}  // namespace Hdc

#endif
