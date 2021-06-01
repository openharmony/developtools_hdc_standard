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
    HChannel hChannel = nullptr;
    uint32_t uid = thisClass->MallocChannel(&hChannel);
    if (!hChannel) {
        return;
    }
    if (uv_accept(server, (uv_stream_t *)&hChannel->hWorkTCP) < 0) {
        thisClass->FreeChannel(uid);
        return;
    }
    Base::SetTcpOptions(&hChannel->hWorkTCP);
    uv_read_start((uv_stream_t *)&hChannel->hWorkTCP, AllocCallback, ReadStream);
    // channel handshake step1
    ChannelHandShake handShake;
    Base::ZeroStruct(handShake);
    if (EOK == strcpy_s(handShake.banner, sizeof(handShake.banner), HANDSHAKE_MESSAGE.c_str())) {
        handShake.channelId = htonl(hChannel->channelId);
        thisClass->Send(hChannel->channelId, (uint8_t *)&handShake, sizeof(ChannelHandShake));
    }
}

// https://andycong.top/2020/03/27/libuv%E5%A4%9A%E7%BA%BF%E7%A8%8B%E4%B8%AD%E4%BD%BF%E7%94%A8uv-accept/
void HdcServerForClient::SetTCPListen()
{
    tcpListen.data = this;
    struct sockaddr_in addr;
    uv_tcp_init(loopMain, &tcpListen);
    uv_ip4_addr(channelHost.c_str(), channelPort, &addr);
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

void HdcServerForClient::EchoClient(HChannel hChannel, MessageLevel level, const char *msg, ...)
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
        log += "\n";
    }
    Send(hChannel->channelId, (uint8_t *)log.c_str(), log.size());
}

void HdcServerForClient::EchoClientRaw(const uint32_t channelId, uint8_t *payload, const int payloadSize)
{
    Send(channelId, payload, payloadSize);
}

bool HdcServerForClient::SendToDaemon(HChannel hChannel, const uint16_t commandFlag, uint8_t *bufPtr, const int bufSize)
{
    HDaemonInfo hdi = nullptr;
    bool ret = false;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    while (true) {
        ptrServer->AdminDaemonMap(OP_QUERY, hChannel->connectKey, hdi);
        if (hdi == nullptr) {
            break;
        }
        if (hdi->connStatus != STATUS_CONNECTED) {
            break;
        }
        if (!hdi->hSession) {
            break;
        }
        if (ptrServer->Send(hdi->hSession->sessionId, hChannel->channelId, commandFlag, bufPtr, bufSize) < 0) {
            break;
        }
        ret = true;
        break;
    }
    return ret;
}

void HdcServerForClient::OrderFindTargets(HChannel hChannel)
{
    int count = 0;
    EchoClient(hChannel, MSG_INFO, "Please add HDC server's firewall ruler to allow udp incoming, udpport:%d",
               DEFAULT_PORT);
    HdcServer *ptrServer = (HdcServer *)clsServer;
    ptrServer->clsTCPClt->FindLanDaemon();
    list<string> &lst = ptrServer->clsTCPClt->lstDaemonResult;
    // refresh main list
    HdcDaemonInformation di;
    while (!lst.empty()) {
        Base::ZeroStruct(di);
        count++;
        di.connectKey = lst.front();
        di.connType = CONN_TCP;
        di.connStatus = STATUS_READY;
        HDaemonInfo pDi = (HDaemonInfo)&di;
        ptrServer->AdminDaemonMap(OP_ADD, STRING_EMPTY, pDi);
        lst.pop_front();
    }
    EchoClient(hChannel, MSG_INFO, "Broadcast find daemon, total:%d", count);
#ifdef UNIT_TEST
    string bufString = std::to_string(count);
    Base::WriteBinFile((UT_TMP_PATH + "/base.result").c_str(), (uint8_t *)bufString.c_str(), bufString.size(), false);
#endif
}

void HdcServerForClient::FinishMainThreadTimer(uv_handle_t *handle)
{
    uv_timer_t *req = (uv_timer_t *)handle;
    delete req;
}

void HdcServerForClient::OrderConnecTargetResult(uv_timer_t *req)
{
    HChannel hChannel = (HChannel)req->data;
    HdcServerForClient *thisClass = (HdcServerForClient *)hChannel->clsChannel;
    HdcServer *ptrServer = (HdcServer *)thisClass->clsServer;
    bool bConnectOK = false;
    bool bExitRepet = false;
    HDaemonInfo hdi = nullptr;
    string sRet;
    string target = std::string(hChannel->bufStd + 2);
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
            thisClass->EchoClient(hChannel, MSG_OK, (char *)sRet.c_str());
            break;
        } else {
            uint16_t *bRetryCount = (uint16_t *)hChannel->bufStd;
            (*bRetryCount)++;
            if (*bRetryCount > 500) {
                // 5s
                bExitRepet = true;
                sRet = "Connect failed";
                thisClass->EchoClient(hChannel, MSG_FAIL, (char *)sRet.c_str());
                break;
            }
        }
        break;
    }
    if (bExitRepet) {
        thisClass->FreeChannel(hChannel->channelId);
        uv_close((uv_handle_t *)req, FinishMainThreadTimer);
    }
}

bool HdcServerForClient::NewConnectTry(void *ptrServer, HChannel hChannel, const string &connectKey)
{
    int childRet = ((HdcServer *)ptrServer)->CreateConnect(connectKey);
    bool ret = false;
    if (-1 == childRet) {
        EchoClient(hChannel, MSG_INFO, "Target is connected, repeat operation");
    } else if (-2 == childRet) {
        EchoClient(hChannel, MSG_FAIL, "CreateConnect failed");
        WRITE_LOG(LOG_FATAL, "CreateConnect failed");
    } else {
        Base::ZeroBuf(hChannel->bufStd, 2);
        childRet = snprintf_s(hChannel->bufStd + 2, sizeof(hChannel->bufStd) - 2, sizeof(hChannel->bufStd) - 3, "%s",
                              (char *)connectKey.c_str());
        if (childRet > 0) {
            uv_timer_t *waitTimeDoCmd = new uv_timer_t();
            uv_timer_init(loopMain, waitTimeDoCmd);
            waitTimeDoCmd->data = hChannel;
            uv_timer_start(waitTimeDoCmd, OrderConnecTargetResult, 10, 10);
            ret = true;
        }
    }
    return ret;
}

bool HdcServerForClient::CommandRemoveSession(HChannel hChannel, const char *connectKey)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HDaemonInfo hdiOld = nullptr;
    ((HdcServer *)ptrServer)->AdminDaemonMap(OP_QUERY, connectKey, hdiOld);
    if (!hdiOld) {
        EchoClient(hChannel, MSG_FAIL, "No target available");
        return false;
    }
    ((HdcServer *)ptrServer)->FreeSession(hdiOld->hSession->sessionId);
    return true;
}

bool HdcServerForClient::CommandRemoveForward(const string &forwardKey)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HForwardInfo hfi = nullptr;
    ptrServer->AdminForwardMap(OP_QUERY, forwardKey, hfi);
    if (!hfi) {
        return false;
    }
    HSession hSession = ptrServer->AdminSession(OP_QUERY, hfi->sessionId, nullptr);
    if (!hSession) {
        return false;
    }
    ptrServer->ClearOwnTasks(hSession, hfi->channelId);
    FreeChannel(hfi->channelId);
    hfi = nullptr;
    ptrServer->AdminForwardMap(OP_REMOVE, forwardKey, hfi);
    return true;
}

void HdcServerForClient::GetTargetList(HChannel hChannel, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    uint16_t cmd = OP_GET_STRLIST;
    if (formatCommand->paraments == "v") {
        cmd = OP_GET_STRLIST_FULL;
    }
    HDaemonInfo hdi = nullptr;
    string sRet = ptrServer->AdminDaemonMap(cmd, STRING_EMPTY, hdi);
    if (!sRet.length()) {
        sRet = EMPTY_ECHO;
    }
    EchoClient(hChannel, MSG_OK, (char *)sRet.c_str());
#ifdef UNIT_TEST
    Base::WriteBinFile((UT_TMP_PATH + "/base.result").c_str(), (uint8_t *)sRet.c_str(), sRet.size(), false);
#endif
}

bool HdcServerForClient::GetAnyTarget(HChannel hChannel)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    HDaemonInfo hdi = nullptr;
    ptrServer->AdminDaemonMap(OP_GET_ANY, STRING_EMPTY, hdi);
    if (!hdi) {
        EchoClient(hChannel, MSG_FAIL, "No target available");
        return false;
    }
    // can not use hdi->connectKey.This memory may be released to re-Malloc
    string connectKey = hdi->connectKey;
    bool ret = NewConnectTry(ptrServer, hChannel, connectKey);
#ifdef UNIT_TEST
    Base::WriteBinFile((UT_TMP_PATH + "/base.result").c_str(), (uint8_t *)"OK", 3, false);
#endif
    return ret;
}

bool HdcServerForClient::RemoveForward(HChannel hChannel, const char *paramentString)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    if (paramentString == nullptr) {  // remove all
        HForwardInfo hfi = nullptr;   // dummy
        string echo = ptrServer->AdminForwardMap(OP_GET_STRLIST, "", hfi);
        if (!echo.length()) {
            return false;
        }
        vector<string> filterStrings;
        Base::SplitString(echo, string("\n"), filterStrings);
        for (auto &&s : filterStrings) {
            if (!CommandRemoveForward(s.c_str())) {
                EchoClient(hChannel, MSG_FAIL, "Remove forward ruler failed,ruler:%s", s.c_str());
            }
        }
    } else {  // remove single
        if (!CommandRemoveForward(paramentString)) {
            EchoClient(hChannel, MSG_FAIL, "Remove forward ruler failed,ruler:%s", paramentString);
        }
    }
    return true;
}

bool HdcServerForClient::DoCommandLocal(HChannel hChannel, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    const char *paramentString = formatCommand->paraments.c_str();
    bool ret = false;
    // Main thread command, direct Listen main thread
    switch (formatCommand->cmdFlag) {
        case CMD_KERNEL_TARGET_DISCOVER: {
            OrderFindTargets(hChannel);
            ret = false;
            break;
        }
        case CMD_KERNEL_TARGET_LIST: {
            GetTargetList(hChannel, formatCommandInput);
            ret = false;
            break;
        }
        case CMD_KERNEL_TARGET_ANY: {
            ret = GetAnyTarget(hChannel);
            break;
        }
        case CMD_KERNEL_TARGET_CONNECT: {
            ret = NewConnectTry(ptrServer, hChannel, paramentString);
            break;
        }
        case CMD_KERNEL_TARGET_DISCONNECT: {
            CommandRemoveSession(hChannel, paramentString);
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
            HForwardInfo hfi = nullptr;  // dummy
            string echo = ptrServer->AdminForwardMap(OP_GET_STRLIST_FULL, "", hfi);
            if (!echo.length()) {
                echo = EMPTY_ECHO;
            }
            EchoClient(hChannel, MSG_OK, (char *)echo.c_str());
            break;
        }
        case CMD_FORWARD_REMOVE: {
            RemoveForward(hChannel, paramentString);
            break;
        }
        default: {
            EchoClient(hChannel, MSG_FAIL, "ExecuteCommand need connect-key?");
            break;
        }
    }
    return ret;
}

bool HdcServerForClient::TaskCommand(HChannel hChannel, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    int sizeSend = formatCommand->paraments.size() == 0 ? 0 : (formatCommand->paraments.size() + 1);
    string cmdFlag;
    uint8_t sizeCmdFlag = 0;
    if (CMD_FILE_INIT == formatCommand->cmdFlag) {
        cmdFlag = "send ";
        sizeCmdFlag = 5;
    } else if (CMD_FORWARD_INIT == formatCommand->cmdFlag) {
        cmdFlag = "fport ";
        sizeCmdFlag = 6;
    } else if (CMD_APP_INIT == formatCommand->cmdFlag) {
        cmdFlag = "install ";
        sizeCmdFlag = 8;
    } else if (CMD_APP_UNINSTALL == formatCommand->cmdFlag) {
        cmdFlag = "uninstall ";
        sizeCmdFlag = 10;
    } else if (CMD_UNITY_BUGREPORT_INIT == formatCommand->cmdFlag) {
        cmdFlag = "bugreport ";
        sizeCmdFlag = 10;
    } else if (CMD_APP_SIDELOAD == formatCommand->cmdFlag) {
        cmdFlag = "sideload ";
        sizeCmdFlag = 9;
    }
    if (!strncmp(formatCommand->paraments.c_str(), cmdFlag.c_str(), sizeCmdFlag)) {  // local do
        ptrServer->DispatchTaskData(hChannel->targetSession, hChannel->channelId, formatCommand->cmdFlag,
                                    (uint8_t *)formatCommand->paraments.c_str() + sizeCmdFlag, sizeSend - sizeCmdFlag);
    } else {  // Send to Daemon-side to do
        SendToDaemon(hChannel, formatCommand->cmdFlag, (uint8_t *)formatCommand->paraments.c_str() + sizeCmdFlag,
                     sizeSend - sizeCmdFlag);
    }
    return true;
}

bool HdcServerForClient::DoCommandRemote(HChannel hChannel, void *formatCommandInput)
{
    TranslateCommand::FormatCommand *formatCommand = (TranslateCommand::FormatCommand *)formatCommandInput;
    bool ret = false;
    int sizeSend = formatCommand->paraments.size() == 0 ? 0 : (formatCommand->paraments.size() + 1);
    string cmdFlag;
    switch (formatCommand->cmdFlag) {
        // Some simple commands only need to forward the instruction, no need to start Task
        case CMD_SHELL_INIT:
        case CMD_KERNEL_ECHO_RAW:
        case CMD_UNITY_EXECUTE:
        case CMD_UNITY_TERMINATE:
        case CMD_UNITY_REMOUNT:
        case CMD_UNITY_REBOOT:
        case CMD_UNITY_RUNMODE:
        case CMD_UNITY_HILOG:
        case CMD_UNITY_ROOTRUN:
        case CMD_UNITY_JPID: {
            // strlen+1 Prevent DAEMON memory sticks from causing Strlen errors
            if (!SendToDaemon(hChannel, formatCommand->cmdFlag, (uint8_t *)formatCommand->paraments.c_str(),
                              sizeSend)) {
                break;
            }
            ret = true;
            if (CMD_SHELL_INIT == formatCommand->cmdFlag) {
                hChannel->interactiveShellMode = true;
            }
            break;
        }
        case CMD_FILE_INIT:
        case CMD_FORWARD_INIT:
        case CMD_APP_INIT:
        case CMD_APP_UNINSTALL:
        case CMD_UNITY_BUGREPORT_INIT:
        case CMD_APP_SIDELOAD: {
            TaskCommand(hChannel, formatCommandInput);
            ret = true;
            break;
        }
        default:
            break;
    }
    if (!ret) {
        EchoClient(hChannel, MSG_FAIL, "Failed to communicate with daemon");
    }
    return ret;
}
// Do not specify Target's operations no longer need to put it in the thread.
bool HdcServerForClient::DoCommand(HChannel hChannel, void *formatCommandInput)
{
    bool ret = false;
    if (!hChannel->hChildWorkTCP.loop) {
        // Main thread command, direct Listen main thread
        ret = DoCommandLocal(hChannel, formatCommandInput);
    } else {  // CONNECT DAEMON's work thread command, non-primary thread
        ret = DoCommandRemote(hChannel, formatCommandInput);
    }
    return ret;
}

int HdcServerForClient::BindChannelToSession(HChannel hChannel, uint8_t *bufPtr, const int bytesIO)
{
    HDaemonInfo hdi = nullptr;
    HdcServer *ptrServer = (HdcServer *)clsServer;
    ptrServer->AdminDaemonMap(OP_QUERY, hChannel->connectKey, hdi);
    if (!hdi) {
        EchoClient(hChannel, MSG_FAIL, "Not match target founded, check connect-key please");
        return -1;
    }
    HSession hSession = (HSession)hdi->hSession;
    if (hdi->connStatus != STATUS_CONNECTED) {
        EchoClient(hChannel, MSG_FAIL, "Device not founded or connected");
        return -2;
    }

    uint8_t flag[5];
    flag[0] = SP_REGISTER_CHANNEL;
    if (memcpy_s(flag + 1, sizeof(flag) - 1, &hChannel->channelId, 4)) {
    }
    Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], flag, 5);
    while (!hChannel->hChildWorkTCP.loop) {
        uv_sleep(1);
    }

    if (uv_fileno((const uv_handle_t *)&hChannel->hWorkTCP, &hChannel->fdChildWorkTCP) < 0) {
        return -3;
    }
#ifdef UNIT_TEST
    hChannel->fdChildWorkTCP = dup(hChannel->fdChildWorkTCP);
#endif
    uv_read_stop((uv_stream_t *)&hChannel->hWorkTCP);  // disable parent

    // Send work thread enabled listening
    flag[0] = SP_ATTACH_CHANNEL;
    Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], flag, 5);
    return 0;
}

bool HdcServerForClient::CheckAutoFillTarget(HChannel hChannel)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    if (!hChannel->connectKey.size()) {
        return false;  // Operation of non-bound destination of scanning
    }
    if (hChannel->connectKey == CMDSTR_CONNECT_ANY) {
        HDaemonInfo hdiOld = nullptr;
        ptrServer->AdminDaemonMap(OP_GET_ONLY, "", hdiOld);
        if (!hdiOld) {
            return false;
        }
        hChannel->connectKey = hdiOld->connectKey;
        return true;
    }
    return true;
}

// Here is Server to get data, the source is the SERVER's ChildWork to send data
int HdcServerForClient::ReadChannel(HChannel hChannel, uint8_t *bufPtr, const int bytesIO)
{
    int ret = 0;
    if (!hChannel->handshakeOK) {
        ChannelHandShake *handShake = (ChannelHandShake *)bufPtr;
        if (strncmp(handShake->banner, HANDSHAKE_MESSAGE.c_str(), HANDSHAKE_MESSAGE.size())) {
            hChannel->availTailIndex = 0;
            WRITE_LOG(LOG_DEBUG, "Channel Hello failed");
            return -1;
        }
        // channel handshake step3
        WRITE_LOG(LOG_DEBUG, "ServerForClient channel handshake finished");
        hChannel->connectKey = handShake->connectKey;
        hChannel->handshakeOK = true;
        if (!CheckAutoFillTarget(hChannel)) {
            return 0;
        }
        if (BindChannelToSession(hChannel, nullptr, 0)) {
            hChannel->availTailIndex = 0;
            WRITE_LOG(LOG_DEBUG, "BindChannelToSession failed");
            return -2;
        }
        return 0;
    }
    TranslateCommand::FormatCommand formatCommand;
    Base::ZeroStruct(formatCommand);
    if (!hChannel->interactiveShellMode) {
        string retEcho = String2FormatCommand((char *)bufPtr, bytesIO, &formatCommand);
        if (retEcho.length()) {
            if (!strcmp((char *)bufPtr, CMDSTR_SOFTWARE_HELP.c_str())
                || !strcmp((char *)bufPtr, CMDSTR_SOFTWARE_VERSION.c_str())) {
                EchoClient(hChannel, MSG_OK, retEcho.c_str());
            } else {
                EchoClient(hChannel, MSG_FAIL, retEcho.c_str());
            }
        }
        if (formatCommand.bJumpDo) {
            ret = -10;
            return ret;
        }
    } else {
        formatCommand.paraments = (char *)bufPtr;
        formatCommand.cmdFlag = CMD_KERNEL_ECHO_RAW;
    }
    if (!DoCommand(hChannel, &formatCommand)) {
        ret = -3;
        return ret;
    }
    ret = bytesIO;
    return ret;
};

void HdcServerForClient::NotifyInstanceChannelFree(HChannel hChannel)
{
    HdcServer *ptrServer = (HdcServer *)clsServer;
    if (hChannel->targetSession) {
        uint8_t count = 1;
        ptrServer->Send(hChannel->targetSession->sessionId, hChannel->channelId, CMD_KERNEL_CHANNEL_CLOSE, &count, 1);
    }
};
}  // namespace Hdc