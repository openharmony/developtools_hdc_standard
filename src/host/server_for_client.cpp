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
#include "server_for_client.h"
#include "server.h"

namespace Hdc {
HdcServerForClient::HdcServerForClient(const bool serverOrClient, const string &addrString, void *pClsServer,
                                       uv_loop_t *loopMainIn)
    : HdcChannelBase(serverOrClient, addrString, loopMainIn)
{
    clsServer = pClsServer;
}

HdcServerForClient::~HdcServerForClient()
{
    WRITE_LOG(LOG_DEBUG, "~HdcServerForClient");
}

void HdcServerForClient::Stop()
{
    Base::TryCloseHandle((uv_handle_t *)&tcpListen);
}

uint16_t HdcServerForClient::GetTCPListenPort()
{
    return channelPort;
}

void HdcServerForClient::AcceptClient(uv_stream_t *server, int status)
{
    uv_tcp_t *pServTCP = (uv_tcp_t *)server;
    HdcServerForClient *thisClass = (HdcServerForClient *)pServTCP->data;
    HChannelPtr hChannelPtr = nullptr;
    uint32_t uid = thisClass->MallocChannel(&hChannelPtr);
    if (!hChannelPtr) {
        return;
    }
    if (uv_accept(server, (uv_stream_t *)&hChannelPtr->hWorkTCP) < 0) {
        thisClass->FreeChannel(uid);
        return;
    }
    WRITE_LOG(LOG_DEBUG, "HdcServerForClient acceptClient");
    // limit first recv
    int bufMaxSize = 0;
    uv_recv_buffer_size((uv_handle_t *)&hChannelPtr->hWorkTCP, &bufMaxSize);
    auto funcChannelHeaderAlloc = [](uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf) -> void {
        HChannelPtr context = (HChannelPtr)handle->data;
        Base::ReallocBuf(&context->ioBuf, &context->bufSize, sizeWanted);  // sizeWanted default 6k
        buf->base = (char *)context->ioBuf + context->availTailIndex;
        buf->len = sizeof(struct ChannelHandShake) + DWORD_SERIALIZE_SIZE;  // only recv static size
    };
    // first packet static size, after this packet will be dup for normal recv
    uv_read_start((uv_stream_t *)&hChannelPtr->hWorkTCP, funcChannelHeaderAlloc, ReadStream);
    // channel handshake step1
    struct ChannelHandShake handShake = {};
    if (EOK == strcpy_s(handShake.banner, sizeof(handShake.banner), HANDSHAKE_MESSAGE.c_str())) {
        handShake.channelId = htonl(hChannelPtr->channelId);
        thisClass->Send(hChannelPtr->channelId, (uint8_t *)&handShake, sizeof(struct ChannelHandShake));
    }
}

void HdcServerForClient::SetTCPListen()
{
    tcpListen.data = this;
    struct sockaddr_in6 addr;
    uv_tcp_init(loopMain, &tcpListen);
    uv_ip6_addr(channelHost.c_str(), channelPort, &addr);
    uv_tcp_bind(&tcpListen, (const struct sockaddr *)&addr, 0);
    uv_listen((uv_stream_t *)&tcpListen, 128, (uv_connection_cb)AcceptClient);
}

int HdcServerForClient::Initial()
{
    if (!clsServer) {
        WRITE_LOG(LOG_FATAL, "Module client initial failed");
        return -1;
    }
    if (!channelHostPort.size() || !channelHost.size() || !channelPort) {
        WRITE_LOG(LOG_FATAL, "Listen string initial failed");
        return -2;
    }
    SetTCPListen();
    return 0;
}

void HdcServerForClient::EchoClient(HChannelPtr hChannelPtr, MessageLevel level, const char *msg, ...)
{
    string logInfo = "";
    switch (level) {
        case MSG_FAIL:
            logInfo = MESSAGE_FAIL;
            break;
        case MSG_INFO:
            logInfo = MESSAGE_INFO;
            break;
        default:  // successful, not append extra info
            break;
    }
    va_list vaArgs;
    va_start(vaArgs, msg);
    string log = logInfo + Base::StringFormat(msg, vaArgs);
    va_end(vaArgs);
    if (log.back() != '\n') {
        log += "\r\n";
    }
    SendChannel(hChannelPtr, (uint8_t *)log.c_str(), log.size());
}

void HdcServerForClient::EchoClientRaw(const HChannelPtr hChannelPtr, uint8_t *payload, const int payloadSize)
{
    SendChannel(hChannelPtr, payload, payloadSize);
}

bool HdcServerForClient::SendToDaemon(HChannelPtr hChannelPtr, const uint16_t commandFlag, uint8_t *bufPtr, const int bufSize)
{
    HDaemonInfoPtr hdi = nullptr;
    bool ret = false;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    while (true) {
        ptrServer->AdminDaemonMap(OP_QUERY, hChannelPtr->connectKey, hdi);
        if (hdi == nullptr) {
            break;
        }
        if (hdi->connStatus != STATUS_CONNECTED) {
            break;
        }
        if (!hdi->hSessionPtr) {
            break;
        }
        if (ptrServer->Send(hdi->hSessionPtr->sessionId, hChannelPtr->channelId, commandFlag, bufPtr, bufSize) < 0) {
            break;
        }
        ret = true;
        break;
    }
    return ret;
}

void HdcServerForClient::OrderFindTargets(HChannelPtr hChannelPtr)
{
    int count = 0;
    EchoClient(hChannelPtr, MSG_INFO, "Please add HDC server's firewall ruler to allow udp incoming, udpport:%d",
               DEFAULT_PORT);
    HdcServer *ptrServer = (HdcServer *)clsServer;
    ptrServer->clsTCPClt->FindLanDaemon();
    list<string> &lst = ptrServer->clsTCPClt->lstDaemonResult;
    // refresh main list
    HdcDaemonInformation di;
    while (!lst.empty()) {
        di = {};
        ++count;
        di.connectKey = lst.front();
        di.connType = CONN_TCP;
        di.connStatus = STATUS_READY;
        HDaemonInfoPtr pDi = (HDaemonInfoPtr)&di;
        ptrServer->AdminDaemonMap(OP_ADD, STRING_EMPTY, pDi);
        lst.pop_front();
    }
    EchoClient(hChannelPtr, MSG_INFO, "Broadcast find daemon, total:%d", count);
#ifdef UNIT_TEST
    string bufString = std::to_string(count);
    Base::WriteBinFile((UT_TMP_PATH + "/base-discover.result").c_str(), (uint8_t *)bufString.c_str(), bufString.size(),
                       true);
#endif
}

void HdcServerForClient::OrderConnecTargetResult(uv_timer_t *req)
{
    HChannelPtr hChannelPtr = (HChannelPtr)req->data;
    HdcServerForClient *thisClass = (HdcServerForClient *)hChannelPtr->clsChannel;
    HdcServer *ptrServer = (HdcServer *)thisClass->clsServer;
    bool bConnectOK = false;
    bool bExitRepet = false;
    HDaemonInfoPtr hdi = nullptr;
    string sRet;
    string target = std::string(hChannelPtr->bufStd + 2);
    if (target == "any") {
        ptrServer->AdminDaemonMap(OP_GET_ANY, target, hdi);
    } else {
        ptrServer->AdminDaemonMap(OP_QUERY, target, hdi);
    }
    if (hdi && STATUS_CONNECTED == hdi->connStatus) {
        bConnectOK = true;
    }
    while (true) {
        if (bConnectOK) {
            bExitRepet = true;
            sRet = "Connect OK";
            thisClass->EchoClient(hChannelPtr, MSG_OK, (char *)sRet.c_str());
            break;
        } else {
            uint16_t *bRetryCount = (uint16_t *)hChannelPtr->bufStd;
            ++(*bRetryCount);
            if (*bRetryCount > 500) {
                // 5s
                bExitRepet = true;
                sRet = "Connect failed";
                thisClass->EchoClient(hChannelPtr, MSG_FAIL, (char *)sRet.c_str());
                break;
            }
        }
        break;
    }
    if (bExitRepet) {
        thisClass->FreeChannel(hChannelPtr->channelId);
        Base::TryCloseHandle((const uv_handle_t *)req, Base::CloseTimerCallback);
    }
}

bool HdcServerForClient::NewConnectTry(void *ptrServer, HChannelPtr hChannelPtr, const string &connectKey)
{
#ifdef HDC_DEBUG
    WRITE_LOG(LOG_ALL, "%s %s", __FUNCTION__, connectKey.c_str());
#endif
    int childRet = ((HdcServer *)ptrServer)->CreateConnect(connectKey);
    bool ret = false;
    if (-1 == childRet) {
        EchoClient(hChannelPtr, MSG_INFO, "Target is connected, repeat operation");
    } else if (-2 == childRet) {
        EchoClient(hChannelPtr, MSG_FAIL, "CreateConnect failed");
        WRITE_LOG(LOG_FATAL, "CreateConnect failed");
    } else {
        Base::ZeroBuf(hChannelPtr->bufStd, 2);
        childRet = snprintf_s(hChannelPtr->bufStd + 2, sizeof(hChannelPtr->bufStd) - 2, sizeof(hChannelPtr->bufStd) - 3, "%s",
                              (char *)connectKey.c_str());
        if (childRet > 0) {
            Base::TimerUvTask(loopMain, hChannelPtr, OrderConnecTargetResult, 10);
            ret = true;
        }
    }
    return ret;
}

bool HdcServerForClient::CommandRemoveSession(HChannelPtr hChannelPtr, const char *connectKey)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HDaemonInfoPtr hdiOld = nullptr;
    ((HdcServer *)ptrServer)->AdminDaemonMap(OP_QUERY, connectKey, hdiOld);
    if (!hdiOld) {
        EchoClient(hChannelPtr, MSG_FAIL, "No target available");
        return false;
    }
    ((HdcServer *)ptrServer)->FreeSession(hdiOld->hSessionPtr->sessionId);
    return true;
}

bool HdcServerForClient::CommandRemoveForward(const string &forwardKey)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HForwardInfoPtr hfi = nullptr;
    ptrServer->AdminForwardMap(OP_QUERY, forwardKey, hfi);
    if (!hfi) {
        return false;
    }
    HSessionPtr hSessionPtr = ptrServer->AdminSession(OP_QUERY, hfi->sessionId, nullptr);
    if (!hSessionPtr) {
        return false;
    }
    ptrServer->ClearOwnTasks(hSessionPtr, hfi->channelId);
    FreeChannel(hfi->channelId);
    hfi = nullptr;
    ptrServer->AdminForwardMap(OP_REMOVE, forwardKey, hfi);
    return true;
}

void HdcServerForClient::GetTargetList(HChannelPtr hChannelPtr, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    uint16_t cmd = OP_GET_STRLIST;
    if (formatCommand->parameters == "v") {
        cmd = OP_GET_STRLIST_FULL;
    }
    HDaemonInfoPtr hdi = nullptr;
    string sRet = ptrServer->AdminDaemonMap(cmd, STRING_EMPTY, hdi);
    if (!sRet.length()) {
        sRet = EMPTY_ECHO;
    }
    EchoClient(hChannelPtr, MSG_OK, (char *)sRet.c_str());
#ifdef UNIT_TEST
    Base::WriteBinFile((UT_TMP_PATH + "/base-list.result").c_str(), (uint8_t *)MESSAGE_SUCCESS.c_str(),
                       MESSAGE_SUCCESS.size(), true);
#endif
}

bool HdcServerForClient::GetAnyTarget(HChannelPtr hChannelPtr)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HDaemonInfoPtr hdi = nullptr;
    ptrServer->AdminDaemonMap(OP_GET_ANY, STRING_EMPTY, hdi);
    if (!hdi) {
        EchoClient(hChannelPtr, MSG_FAIL, "No target available");
        return false;
    }
    // can not use hdi->connectKey.This memory may be released to re-Malloc
    string connectKey = hdi->connectKey;
    bool ret = NewConnectTry(ptrServer, hChannelPtr, connectKey);
#ifdef UNIT_TEST
    Base::WriteBinFile((UT_TMP_PATH + "/base-any.result").c_str(), (uint8_t *)MESSAGE_SUCCESS.c_str(),
                       MESSAGE_SUCCESS.size(), true);
#endif
    return ret;
}

bool HdcServerForClient::RemoveForward(HChannelPtr hChannelPtr, const char *parameterString)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    if (parameterString == nullptr) {  // remove all
        HForwardInfoPtr hfi = nullptr;    // dummy
        string echo = ptrServer->AdminForwardMap(OP_GET_STRLIST, "", hfi);
        if (!echo.length()) {
            return false;
        }
        vector<string> filterStrings;
        Base::SplitString(echo, string("\n"), filterStrings);
        for (auto &&s : filterStrings) {
            if (!CommandRemoveForward(s.c_str())) {
                EchoClient(hChannelPtr, MSG_FAIL, "Remove forward ruler failed,ruler:%s", s.c_str());
            }
        }
    } else {  // remove single
        if (!CommandRemoveForward(parameterString)) {
            EchoClient(hChannelPtr, MSG_FAIL, "Remove forward ruler failed,ruler:%s", parameterString);
        }
    }
    return true;
}

bool HdcServerForClient::DoCommandLocal(HChannelPtr hChannelPtr, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    const char *parameterString = formatCommand->parameters.c_str();
    bool ret = false;
    // Main thread command, direct Listen main thread
    switch (formatCommand->cmdFlag) {
        case CMD_KERNEL_TARGET_DISCOVER: {
            OrderFindTargets(hChannelPtr);
            ret = false;
            break;
        }
        case CMD_KERNEL_TARGET_LIST: {
            GetTargetList(hChannelPtr, formatCommandInput);
            ret = false;
            break;
        }
        case CMD_KERNEL_TARGET_ANY: {
#ifdef HDC_DEBUG
            WRITE_LOG(LOG_DEBUG, "%s CMD_KERNEL_TARGET_ANY %s", __FUNCTION__, parameterString);
#endif
            ret = GetAnyTarget(hChannelPtr);
            break;
        }
        case CMD_KERNEL_TARGET_CONNECT: {
#ifdef HDC_DEBUG
            WRITE_LOG(LOG_DEBUG, "%s CMD_KERNEL_TARGET_CONNECT %s", __FUNCTION__, parameterString);
#endif
            ret = NewConnectTry(ptrServer, hChannelPtr, parameterString);
            break;
        }
        case CMD_KERNEL_TARGET_DISCONNECT: {
            CommandRemoveSession(hChannelPtr, parameterString);
            break;
        }
        case CMD_KERNEL_SERVER_KILL: {
            WRITE_LOG(LOG_DEBUG, "Recv server kill command");
            uv_stop(loopMain);
            ret = true;
            break;
        }
        // task will be global task，Therefore, it can only be controlled in the global session.
        case CMD_FORWARD_LIST: {
            HForwardInfoPtr hfi = nullptr;  // dummy
            string echo = ptrServer->AdminForwardMap(OP_GET_STRLIST_FULL, "", hfi);
            if (!echo.length()) {
                echo = EMPTY_ECHO;
            }
            EchoClient(hChannelPtr, MSG_OK, (char *)echo.c_str());
            break;
        }
        case CMD_FORWARD_REMOVE: {
            RemoveForward(hChannelPtr, parameterString);
            break;
        }
        case CMD_KERNEL_ENABLE_KEEPALIVE: {
            // just use for 'list targets' now
            hChannelPtr->keepAlive = true;
            ret = true;
            break;
        }
        default: {
            EchoClient(hChannelPtr, MSG_FAIL, "ExecuteCommand need connect-key?");
            break;
        }
    }
    return ret;
}

bool HdcServerForClient::TaskCommand(HChannelPtr hChannelPtr, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    int sizeSend = formatCommand->parameters.size();
    string cmdFlag;
    uint8_t sizeCmdFlag = 0;
    if (CMD_FILE_INIT == formatCommand->cmdFlag) {
        cmdFlag = "send ";
        sizeCmdFlag = 5;  // 5: cmdFlag send size
    } else if (CMD_FORWARD_INIT == formatCommand->cmdFlag) {
        cmdFlag = "fport ";
        sizeCmdFlag = 6;  // 6: cmdFlag fport size
    } else if (CMD_APP_INIT == formatCommand->cmdFlag) {
        cmdFlag = "install ";
        sizeCmdFlag = 8;  // 8: cmdFlag install size
    } else if (CMD_APP_UNINSTALL == formatCommand->cmdFlag) {
        cmdFlag = "uninstall ";
        sizeCmdFlag = 10;  // 10: cmdFlag uninstall size
    } else if (CMD_UNITY_BUGREPORT_INIT == formatCommand->cmdFlag) {
        cmdFlag = "bugreport ";
        sizeCmdFlag = 10;  // 10: cmdFlag bugreport size
    } else if (CMD_APP_SIDELOAD == formatCommand->cmdFlag) {
        cmdFlag = "sideload ";
        sizeCmdFlag = 9;
    }
    uint8_t *payload = reinterpret_cast<uint8_t *>(const_cast<char *>(formatCommand->parameters.c_str())) + sizeCmdFlag;
    if (!strncmp(formatCommand->parameters.c_str(), cmdFlag.c_str(), sizeCmdFlag)) {  // local do
        HSessionPtr hSessionPtr = FindAliveSession(hChannelPtr->targetSessionId);
        if (!hSessionPtr) {
            return false;
        }
        ptrServer->DispatchTaskData(hSessionPtr, hChannelPtr->channelId, formatCommand->cmdFlag, payload,
                                    sizeSend - sizeCmdFlag);
    } else {  // Send to Daemon-side to do
        SendToDaemon(hChannelPtr, formatCommand->cmdFlag, payload, sizeSend - sizeCmdFlag);
    }
    return true;
}

bool HdcServerForClient::DoCommandRemote(HChannelPtr hChannelPtr, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    bool ret = false;
    int sizeSend = formatCommand->parameters.size();
    string cmdFlag;
    switch (formatCommand->cmdFlag) {
        // Some simple commands only need to forward the instruction, no need to start Task
        case CMD_SHELL_INIT:
        case CMD_SHELL_DATA:
        case CMD_UNITY_EXECUTE:
        case CMD_UNITY_TERMINATE:
        case CMD_UNITY_REMOUNT:
        case CMD_UNITY_REBOOT:
        case CMD_UNITY_RUNMODE:
        case CMD_UNITY_HILOG:
        case CMD_UNITY_ROOTRUN:
        case CMD_JDWP_TRACK:
        case CMD_JDWP_LIST: {
            if (!SendToDaemon(hChannelPtr, formatCommand->cmdFlag,
                              reinterpret_cast<uint8_t *>(const_cast<char *>(formatCommand->parameters.c_str())),
                              sizeSend)) {
                break;
            }
            ret = true;
            if (CMD_SHELL_INIT == formatCommand->cmdFlag) {
                hChannelPtr->interactiveShellMode = true;
            }
            break;
        }
        case CMD_FILE_INIT:
        case CMD_FORWARD_INIT:
        case CMD_APP_INIT:
        case CMD_APP_UNINSTALL:
        case CMD_UNITY_BUGREPORT_INIT:
        case CMD_APP_SIDELOAD: {
            TaskCommand(hChannelPtr, formatCommandInput);
            ret = true;
            break;
        }
        default:
            break;
    }
    if (!ret) {
        EchoClient(hChannelPtr, MSG_FAIL, "Failed to communicate with daemon");
    }
    return ret;
}
// Do not specify Target's operations no longer need to put it in the thread.
bool HdcServerForClient::DoCommand(HChannelPtr hChannelPtr, void *formatCommandInput)
{
    bool ret = false;
    if (!hChannelPtr->hChildWorkTCP.loop) {
        // Main thread command, direct Listen main thread
        ret = DoCommandLocal(hChannelPtr, formatCommandInput);
    } else {  // CONNECT DAEMON's work thread command, non-primary thread
        ret = DoCommandRemote(hChannelPtr, formatCommandInput);
    }
    return ret;
}

// just call from BindChannelToSession
HSessionPtr HdcServerForClient::FindAliveSessionFromDaemonMap(const HChannelPtr hChannelPtr)
{
    HSessionPtr hSessionPtr = nullptr;
    HDaemonInfoPtr hdi = nullptr;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    ptrServer->AdminDaemonMap(OP_QUERY, hChannelPtr->connectKey, hdi);
    if (!hdi) {
        EchoClient(hChannelPtr, MSG_FAIL, "Not match target founded, check connect-key please");
        return nullptr;
    }
    if (hdi->connStatus != STATUS_CONNECTED) {
        EchoClient(hChannelPtr, MSG_FAIL, "Device not founded or connected");
        return nullptr;
    }
    if (hdi->hSessionPtr->isDead) {
        EchoClient(hChannelPtr, MSG_FAIL, "Bind tartget session is dead");
        return nullptr;
    }
    hSessionPtr = (HSessionPtr)hdi->hSessionPtr;
    return hSessionPtr;
}

int HdcServerForClient::BindChannelToSession(HChannelPtr hChannelPtr, uint8_t *bufPtr, const int bytesIO)
{
    HSessionPtr hSessionPtr = nullptr;
    if ((hSessionPtr = FindAliveSessionFromDaemonMap(hChannelPtr)) == nullptr) {
        return ERR_SESSION_NOFOUND;
    }
    bool isClosing = uv_is_closing((const uv_handle_t *)&hChannelPtr->hWorkTCP);
    if (!isClosing && (hChannelPtr->fdChildWorkTCP = Base::DuplicateUvSocket(&hChannelPtr->hWorkTCP)) < 0) {
        WRITE_LOG(LOG_FATAL, "Duplicate socket failed, cid:%d", hChannelPtr->channelId);
        return ERR_SOCKET_DUPLICATE;
    }
    uv_close_cb funcWorkTcpClose = [](uv_handle_t *handle) -> void {
        HChannelPtr hChannelPtr = (HChannelPtr)handle->data;
        --hChannelPtr->ref;
    };
    ++hChannelPtr->ref;
    if (!isClosing) {
        uv_close((uv_handle_t *)&hChannelPtr->hWorkTCP, funcWorkTcpClose);
    }
    Base::DoNextLoop(loopMain, hChannelPtr, [](const uint8_t flag, string &msg, const void *data) {
        // Thread message can avoid using thread lock and improve program efficiency
        // If not next loop call, ReadStream will thread conflict
        HChannelPtr hChannelPtr = (HChannelPtr)data;
        auto thisClass = (HdcServerForClient *)hChannelPtr->clsChannel;
        HSessionPtr hSessionPtr = nullptr;
        if ((hSessionPtr = thisClass->FindAliveSessionFromDaemonMap(hChannelPtr)) == nullptr) {
            return;
        }
        auto ctrl = HdcSessionBase::BuildCtrlString(SP_ATTACH_CHANNEL, hChannelPtr->channelId, nullptr, 0);
        Base::SendToStream((uv_stream_t *)&hSessionPtr->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
    });
    return RET_SUCCESS;
}

bool HdcServerForClient::CheckAutoFillTarget(HChannelPtr hChannelPtr)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    if (!hChannelPtr->connectKey.size()) {
        return false;  // Operation of non-bound destination of scanning
    }
    if (hChannelPtr->connectKey == CMDSTR_CONNECT_ANY) {
        HDaemonInfoPtr hdiOld = nullptr;
        ptrServer->AdminDaemonMap(OP_GET_ONLY, "", hdiOld);
        if (!hdiOld) {
            return false;
        }
        hChannelPtr->connectKey = hdiOld->connectKey;
        return true;
    }
    return true;
}

int HdcServerForClient::ChannelHandShake(HChannelPtr hChannelPtr, uint8_t *bufPtr, const int bytesIO)
{
    vector<uint8_t> rebuildHandshake;
    rebuildHandshake.insert(rebuildHandshake.end(), bufPtr, bufPtr + bytesIO);
    rebuildHandshake.push_back(0x00);
    struct ChannelHandShake *handShake = reinterpret_cast<struct ChannelHandShake *>(rebuildHandshake.data());
    if (strncmp(handShake->banner, HANDSHAKE_MESSAGE.c_str(), HANDSHAKE_MESSAGE.size())) {
        hChannelPtr->availTailIndex = 0;
        WRITE_LOG(LOG_DEBUG, "Channel Hello failed");
        return ERR_HANDSHAKE_NOTMATCH;
    }
    if (strlen(handShake->connectKey) > sizeof(handShake->connectKey)) {
        hChannelPtr->availTailIndex = 0;
        WRITE_LOG(LOG_DEBUG, "Connectkey's size incorrect");
        return ERR_HANDSHAKE_CONNECTKEY_FAILED;
    }
    // channel handshake step3
    WRITE_LOG(LOG_DEBUG, "ServerForClient channel handshake finished");
    hChannelPtr->connectKey = handShake->connectKey;
    hChannelPtr->handshakeOK = true;
    if (!CheckAutoFillTarget(hChannelPtr)) {
        return 0;
    }
    // channel handshake stBindChannelToSession
    if (BindChannelToSession(hChannelPtr, nullptr, 0)) {
        hChannelPtr->availTailIndex = 0;
        WRITE_LOG(LOG_FATAL, "BindChannelToSession failed");
        return ERR_GENERIC;
    }
    return 0;
}

// Here is Server to get data, the source is the SERVER's ChildWork to send data
int HdcServerForClient::ReadChannel(HChannelPtr hChannelPtr, uint8_t *bufPtr, const int bytesIO)
{
    int ret = 0;
    if (!hChannelPtr->handshakeOK) {
        return ChannelHandShake(hChannelPtr, bufPtr, bytesIO);
    }
    struct TranslateCommand::FormatCommand formatCommand = { 0 };
    if (!hChannelPtr->interactiveShellMode) {
        string retEcho = String2FormatCommand((char *)bufPtr, bytesIO, &formatCommand);
        if (retEcho.length()) {
            if (!strcmp((char *)bufPtr, CMDSTR_SOFTWARE_HELP.c_str())
                || !strcmp((char *)bufPtr, CMDSTR_SOFTWARE_VERSION.c_str())) {
                EchoClient(hChannelPtr, MSG_OK, retEcho.c_str());
            } else {
                EchoClient(hChannelPtr, MSG_FAIL, retEcho.c_str());
            }
        }
        WRITE_LOG(LOG_DEBUG, "ReadChannel command: %s", bufPtr);
        if (formatCommand.bJumpDo) {
            ret = -10;
            return ret;
        }
    } else {
        formatCommand.parameters = string(reinterpret_cast<char *>(bufPtr), bytesIO);
        formatCommand.cmdFlag = CMD_SHELL_DATA;
    }

    if (!DoCommand(hChannelPtr, &formatCommand)) {
        return -3;  // -3: error or want close
    }
    ret = bytesIO;
    return ret;
};

// avoid session dead
HSessionPtr HdcServerForClient::FindAliveSession(uint32_t sessionId)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HSessionPtr hSessionPtr = ptrServer->AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hSessionPtr || hSessionPtr->isDead) {
        return nullptr;
    } else {
        return hSessionPtr;
    }
}

bool HdcServerForClient::ChannelSendSessionCtrlMsg(vector<uint8_t> &ctrlMsg, uint32_t sessionId)
{
    HSessionPtr hSessionPtr = FindAliveSession(sessionId);
    if (!hSessionPtr) {
        return false;
    }
    return Base::SendToStream((uv_stream_t *)&hSessionPtr->ctrlPipe[STREAM_MAIN], ctrlMsg.data(), ctrlMsg.size()) > 0;
}
}  // namespace Hdc
