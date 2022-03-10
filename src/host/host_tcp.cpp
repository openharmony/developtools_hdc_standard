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
#include "host_tcp.h"
#include "server.h"

namespace Hdc {
HdcHostTCP::HdcHostTCP(const bool serverOrDaemonIn, void *ptrMainBase)
    : HdcTCPBase(serverOrDaemonIn, ptrMainBase)
{
    broadcastFindWorking = false;
}

HdcHostTCP::~HdcHostTCP()
{
    WRITE_LOG(LOG_DEBUG, "~HdcHostTCP");
}

void HdcHostTCP::Stop()
{
}

void HdcHostTCP::RecvUDPEntry(const sockaddr *addrSrc, uv_udp_t *handle, const uv_buf_t *rcvbuf)
{
    char bufString[BUF_SIZE_TINY];
    int port = 0;
    char *p = strstr(rcvbuf->base, "-");
    if (!p) {
        return;
    }
    port = atoi(p + 1);
    if (!port) {
        return;
    }
    uv_ip6_name((sockaddr_in6 *)addrSrc, bufString, sizeof(bufString));
    string addrPort = string(bufString);
    addrPort += string(":") + std::to_string(port);
    lstDaemonResult.push_back(addrPort);
}

void HdcHostTCP::BroadcastTimer(uv_idle_t *handle)
{
    uv_stop(handle->loop);
}

// Executive Administration Network Broadcast Discovery, broadcastLanIP==which interface to broadcast
void HdcHostTCP::BroadcatFindDaemon(const char *broadcastLanIP)
{
    if (broadcastFindWorking) {
        return;
    }
    broadcastFindWorking = true;
    lstDaemonResult.clear();

    uv_loop_t loopBroadcast;
    uv_loop_init(&loopBroadcast);
    struct sockaddr_in6 addr;
    uv_udp_send_t req;
    uv_udp_t client;
    // send
    uv_ip6_addr(broadcastLanIP, 0, &addr);
    uv_udp_init(&loopBroadcast, &client);
    uv_udp_bind(&client, (const struct sockaddr *)&addr, 0);
    uv_udp_set_broadcast(&client, 1);
    uv_ip6_addr("FFFF:FFFF:FFFF", DEFAULT_PORT, &addr);
    uv_buf_t buf = uv_buf_init((char *)HANDSHAKE_MESSAGE.c_str(), HANDSHAKE_MESSAGE.size());
    uv_udp_send(&req, &client, &buf, 1, (const struct sockaddr *)&addr, nullptr);
    // recv
    uv_udp_t server;
    server.data = this;
    uv_ip6_addr(broadcastLanIP, DEFAULT_PORT, &addr);
    uv_udp_init(&loopBroadcast, &server);
    uv_udp_bind(&server, (const struct sockaddr *)&addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&server, AllocStreamUDP, RecvUDP);
    // find timeout
    uv_timer_t tLastCheck;
    uv_timer_init(&loopBroadcast, &tLastCheck);
    uv_timer_start(&tLastCheck, (uv_timer_cb)BroadcastTimer, TIME_BASE, 0);  // timeout debug 1s

    uv_run(&loopBroadcast, UV_RUN_DEFAULT);
    uv_loop_close(&loopBroadcast);
    broadcastFindWorking = false;
}

void HdcHostTCP::Connect(uv_connect_t *connection, int status)
{
    HSession hSession = (HSession)connection->data;
    delete connection;
    HdcSessionBase *ptrConnect = (HdcSessionBase *)hSession->classInstance;
    auto ctrl = ptrConnect->BuildCtrlString(SP_START_SESSION, 0, nullptr, 0);
    if (status < 0) {
        goto Finish;
    }
    if ((hSession->fdChildWorkTCP = Base::DuplicateUvSocket(&hSession->hWorkTCP)) < 0) {
        goto Finish;
    }
    uv_read_stop((uv_stream_t *)&hSession->hWorkTCP);
    Base::SetTcpOptions((uv_tcp_t *)&hSession->hWorkTCP);
    WRITE_LOG(LOG_DEBUG, "HdcHostTCP::Connect");
    Base::StartWorkThread(&ptrConnect->loopMain, ptrConnect->SessionWorkThread, Base::FinishWorkThread, hSession);
    // wait for thread up
    while (hSession->childLoop.active_handles == 0) {
        uv_sleep(MINOR_TIMEOUT);
    }
    Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
    return;
Finish:
    WRITE_LOG(LOG_FATAL, "Connect failed");
    ptrConnect->FreeSession(hSession->sessionId);
}

HSession HdcHostTCP::ConnectDaemon(const string &connectKey)
{
    char ip[BUF_SIZE_TINY] = "";
    uint16_t port = 0;
    if (Base::ConnectKey2IPPort(connectKey.c_str(), ip, &port) < 0) {
        return nullptr;
    }

    HdcSessionBase *ptrConnect = (HdcSessionBase *)clsMainBase;
    HSession hSession = ptrConnect->MallocSession(true, CONN_TCP, this);
    if (!hSession) {
        return nullptr;
    }
    hSession->connectKey = connectKey;
    struct sockaddr_in dest;
    uv_ip4_addr(ip, port, &dest);
    uv_connect_t *conn = new(std::nothrow) uv_connect_t();
    if (conn == nullptr) {
        WRITE_LOG(LOG_FATAL, "ConnectDaemon new conn failed");
        delete hSession;
        hSession = nullptr;
        return nullptr;
    }
    conn->data = hSession;
    uv_tcp_connect(conn, (uv_tcp_t *)&hSession->hWorkTCP, (const struct sockaddr *)&dest, Connect);
    return hSession;
}

void HdcHostTCP::FindLanDaemon()
{
    uv_interface_address_t *info;
    int count, i;
    char ipAddr[BUF_SIZE_TINY] = "";
    if (broadcastFindWorking) {
        return;
    }
    lstDaemonResult.clear();
    uv_interface_addresses(&info, &count);
    i = count;
    while (--i) {
        uv_interface_address_t interface = info[i];
        if (interface.address.address6.sin6_family == AF_INET6) {
            continue;
        }
        uv_ip6_name(&interface.address.address6, ipAddr, sizeof(ipAddr));
        BroadcatFindDaemon(ipAddr);
    }
    uv_free_interface_addresses(info, count);
}
}  // namespace Hdc
