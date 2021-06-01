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
    char strTCPPort[BUF_SIZE_TINY] = "";
    const uint16_t BUFF_SIZE = 8;
    Base::GetHdcProperty("persist.hdc.port", strTCPPort, BUFF_SIZE);
    tcpListenPort = atoi(strTCPPort);
    if (tcpListenPort <= 0) {
        tcpListenPort = 0;
    }
    Base::ZeroStruct(servUDP);
    Base::ZeroStruct(servTCP);
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
    uint8_t *byteFlag = nullptr;
    uv_loop_t *ptrLoop = server->loop;
    uv_tcp_t *pServTCP = (uv_tcp_t *)server;
    HdcDaemonTCP *thisClass = (HdcDaemonTCP *)pServTCP->data;
    HdcSessionBase *ptrConnect = (HdcSessionBase *)thisClass->clsMainBase;
    HSession hSession = ptrConnect->MallocSession(false, CONN_TCP, thisClass);
    if (!hSession) {
        return;
    }
    if (uv_accept(server, (uv_stream_t *)&hSession->hWorkTCP) < 0) {
        goto Finish;
    }
    Base::SetTcpOptions(&hSession->hWorkTCP);
    Base::StartWorkThread(ptrLoop, ptrConnect->SessionWorkThread, Base::FinishWorkThread, hSession);
    // wait for thread up
    while (hSession->childLoop.active_handles == 0) {
        usleep(1000);
    }
    if (uv_fileno((const uv_handle_t *)&hSession->hWorkTCP, &hSession->fdChildWorkTCP) < 0) {
        goto Finish;
    }
#ifdef UNIT_TEST
    hSession->fdChildWorkTCP = dup(hSession->fdChildWorkTCP);
#endif

    uv_read_stop((uv_stream_t *)&hSession->hWorkTCP);
    // Send a HWORKTCP handle via PIPE[0]
    byteFlag = new uint8_t[1];
    *byteFlag = SP_START_SESSION;
    Base::SendToStreamEx((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], (uint8_t *)byteFlag, 1, nullptr,
        (void *)Base::SendCallback, byteFlag);
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
    int r;
    HdcSessionBase *ptrConnect = (HdcSessionBase *)clsMainBase;
    // udp broadcast
    servUDP.data = this;
    r = uv_udp_init(&ptrConnect->loopMain, &servUDP);
    r = uv_ip4_addr("0.0.0.0", DEFAULT_PORT, &addr);
    r = uv_udp_bind(&servUDP, (const struct sockaddr *)&addr, UV_UDP_REUSEADDR);
    r = uv_udp_recv_start(&servUDP, AllocStreamUDP, RecvUDP);
}

// Set the daemon-side TCP listening
int HdcDaemonTCP::SetTCPListen()
{
    // tcp listen
    HdcSessionBase *ptrConnect = (HdcSessionBase *)clsMainBase;
    servTCP.data = this;
    struct sockaddr_in addr;
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
    return ERR_SUCCESS;
}

int HdcDaemonTCP::Initial()
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemonTCP init");
    SetUDPListen();
    if (SetTCPListen() != ERR_SUCCESS) {
        WRITE_LOG(LOG_FATAL, "TCP listen failed");
        return ERR_GENERIC;
    }
#ifndef UNIT_TEST
    WRITE_LOG(LOG_INFO, "TCP listen on port:[%d]", tcpListenPort);
#endif
    return ERR_SUCCESS;
}
}  // namespace Hdc