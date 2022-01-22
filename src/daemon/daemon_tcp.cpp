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
#include "daemon_tcp.h"

namespace Hdc {
HdcDaemonTCP::HdcDaemonTCP(const bool serverOrDaemonIn, void *ptrMainBase)
    : HdcTCPBase(serverOrDaemonIn, ptrMainBase)
{
    // If the listening value for the property setting is obtained, it will be 0 randomly assigned.
    string strTCPPort;
    SystemDepend::GetDevItem("persist.hdc.port", strTCPPort);
    tcpListenPort = atoi(strTCPPort.c_str());
    if (tcpListenPort <= 0) {
        tcpListenPort = 0;
    }
#ifdef HDC_DEBUG
    const uint16_t DEBUG_TCP_PORT = 10178;
    tcpListenPort = DEBUG_TCP_PORT;
#endif
}

HdcDaemonTCP::~HdcDaemonTCP()
{
}

void HdcDaemonTCP::Stop()
{
    Base::TryCloseHandle((const uv_handle_t *)&servUDP);
    Base::TryCloseHandle((const uv_handle_t *)&servTCP);
    WRITE_LOG(LOG_DEBUG, "~HdcDaemonTCP");
}

void HdcDaemonTCP::TransmitConfig(const sockaddr *addrSrc, uv_udp_t *handle)
{
    char srcIP[BUF_SIZE_TINY] = "";
    struct sockaddr addrSrcIPPort;
    uv_udp_send_t *req = new uv_udp_send_t();
    if (!req) {
        return;
    }
    string sendBuf = Base::StringFormat("%s-%d", HANDSHAKE_MESSAGE.c_str(), tcpListenPort);
    uv_buf_t sndbuf = uv_buf_init((char *)sendBuf.c_str(), sendBuf.size());
    uv_ip4_name((sockaddr_in *)addrSrc, srcIP, sizeof(srcIP));
    uv_ip4_addr(srcIP, DEFAULT_PORT, (sockaddr_in *)&addrSrcIPPort);
    uv_udp_send(req, handle, &sndbuf, 1, &addrSrcIPPort, SendUDPFinish);
}

void HdcDaemonTCP::AcceptClient(uv_stream_t *server, int status)
{
    uv_loop_t *ptrLoop = server->loop;
    uv_tcp_t *pServTCP = (uv_tcp_t *)server;
    HdcDaemonTCP *thisClass = (HdcDaemonTCP *)pServTCP->data;
    HdcSessionBase *ptrConnect = (HdcSessionBase *)thisClass->clsMainBase;
    HdcSessionBase *daemon = reinterpret_cast<HdcSessionBase *>(thisClass->clsMainBase);
    const uint16_t maxWaitTime = UV_DEFAULT_INTERVAL;
    auto ctrl = daemon->BuildCtrlString(SP_START_SESSION, 0, nullptr, 0);
    HSession hSession = ptrConnect->MallocSession(false, CONN_TCP, thisClass);
    if (!hSession) {
        return;
    }
    if (uv_accept(server, (uv_stream_t *)&hSession->hWorkTCP) < 0) {
        goto Finish;
    }
    if ((hSession->fdChildWorkTCP = Base::DuplicateUvSocket(&hSession->hWorkTCP)) < 0) {
        goto Finish;
    };
    Base::TryCloseHandle((uv_handle_t *)&hSession->hWorkTCP);
    Base::StartWorkThread(ptrLoop, ptrConnect->SessionWorkThread, Base::FinishWorkThread, hSession);
    // wait for thread up
    while (hSession->childLoop.active_handles == 0) {
        usleep(maxWaitTime);
    }
    Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
    return;
Finish:
    ptrConnect->FreeSession(hSession->sessionId);
}

void HdcDaemonTCP::RecvUDPEntry(const sockaddr *addrSrc, uv_udp_t *handle, const uv_buf_t *rcvbuf)
{
    TransmitConfig(addrSrc, handle);
}

void HdcDaemonTCP::SetUDPListen()
{
    struct sockaddr_in addr;
    HdcSessionBase *ptrConnect = (HdcSessionBase *)clsMainBase;
    // udp broadcast
    servUDP.data = this;
    uv_udp_init(&ptrConnect->loopMain, &servUDP);
    uv_ip4_addr("0.0.0.0", DEFAULT_PORT, &addr);
    uv_udp_bind(&servUDP, (const struct sockaddr *)&addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&servUDP, AllocStreamUDP, RecvUDP);
}

// Set the daemon-side TCP listening
int HdcDaemonTCP::SetTCPListen()
{
    // tcp listen
    HdcSessionBase *ptrConnect = (HdcSessionBase *)clsMainBase;
    servTCP.data = this;
    struct sockaddr_in addr = {};
    int namelen;
    const int DEFAULT_BACKLOG = 128;

    uv_tcp_init(&ptrConnect->loopMain, &servTCP);
    uv_ip4_addr("0.0.0.0", tcpListenPort, &addr);  // tcpListenPort == 0
    uv_tcp_bind(&servTCP, (const struct sockaddr *)&addr, 0);
    if (uv_listen((uv_stream_t *)&servTCP, DEFAULT_BACKLOG, (uv_connection_cb)AcceptClient)) {
        return ERR_API_FAIL;
    }
    // Get listen port
    Base::ZeroStruct(addr);
    namelen = sizeof(addr);
    if (uv_tcp_getsockname(&servTCP, (sockaddr *)&addr, &namelen)) {
        return ERR_API_FAIL;
    }
    tcpListenPort = ntohs(addr.sin_port);
    return RET_SUCCESS;
}

int HdcDaemonTCP::Initial()
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemonTCP init");
    SetUDPListen();
    if (SetTCPListen() != RET_SUCCESS) {
        WRITE_LOG(LOG_FATAL, "TCP listen failed");
        return ERR_GENERIC;
    }
#ifndef UNIT_TEST
    WRITE_LOG(LOG_INFO, "TCP listen on port:[%d]", tcpListenPort);
#endif
    return RET_SUCCESS;
}
}  // namespace Hdc