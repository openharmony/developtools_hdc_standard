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
#include "client.h"
#include "server.h"

namespace Hdc {
HdcClient::HdcClient(const bool serverOrClient, const string &addrString, uv_loop_t *loopMainIn)
    : HdcChannelBase(serverOrClient, addrString, loopMainIn)
{
    MallocChannel(&channel);  // free by logic
    debugRetryCount = 0;
}

HdcClient::~HdcClient()
{
    Base::TryCloseLoop(loopMain, "ExecuteCommand finish");
}

void HdcClient::NotifyInstanceChannelFree(HChannel hChannel)
{
    if (bShellInteractive) {
        WRITE_LOG(LOG_DEBUG, "Restore tty");
        ModifyTty(false, &hChannel->stdinTty);
    }
}

uint32_t HdcClient::GetLastPID()
{
    char bufPath[BUF_SIZE_MEDIUM] = "";
    size_t size = BUF_SIZE_MEDIUM;
    char pidBuf[BUF_SIZE_TINY] = "";
    // get running pid to kill it
    if (uv_os_tmpdir(bufPath, &size) < 0) {
        WRITE_LOG(LOG_FATAL, "Tmppath failed");
        return 0;
    }
    string path = Base::StringFormat("%s%c.%s.pid", bufPath, Base::GetPathSep(), SERVER_NAME.c_str());
    Base::ReadBinFile(path.c_str(), (void **)&pidBuf, BUF_SIZE_TINY);
    int pid = atoi(pidBuf);  // pid  maybe 0
    return pid;
}

bool HdcClient::StartKillServer(const char *cmd, bool startOrKill)
{
    bool isNowRunning = Base::ProgramMutex(SERVER_NAME.c_str(), true) != 0;
    const int SIGN_NUM = 9;
    uint32_t pid = GetLastPID();
    if (!pid) {
        return false;
    }
    if (startOrKill) {
        if (isNowRunning) {
            // already running
            if (!strstr(cmd, " -r")) {
                return true;
            }
            if (pid) {
                uv_kill(pid, SIGN_NUM);
            }
        }
        HdcServer::PullupServer(channelHostPort.c_str());
    } else {
        if (isNowRunning && pid) {
            uv_kill(pid, SIGN_NUM);
            Base::PrintMessage("Kill server finish");
        }
        // already running
        if (!strstr(cmd, " -r")) {
            return true;
        }
        HdcServer::PullupServer(channelHostPort.c_str());
    }
    return true;
}

void HdcClient::DoCtrlServiceWork(uv_check_t *handle)
{
    HdcClient *thisClass = (HdcClient *)handle->data;
    const char *cmd = thisClass->command.c_str();
    string &strCmd = thisClass->command;
    while (true) {
        if (!strncmp(cmd, CMDSTR_SERVICE_START.c_str(), CMDSTR_SERVICE_START.size())) {
            thisClass->StartKillServer(cmd, true);
        } else if (!strncmp(cmd, CMDSTR_SERVICE_KILL.c_str(), CMDSTR_SERVICE_KILL.size())) {
            thisClass->StartKillServer(cmd, false);
            // clang-format off
        } else if (!strncmp(cmd, CMDSTR_GENERATE_KEY.c_str(), CMDSTR_GENERATE_KEY.size()) &&
                   strCmd.find(" ") != std::string::npos) {
            // clang-format on
            string keyPath = strCmd.substr(CMDSTR_GENERATE_KEY.size() + 1, strCmd.size());
            HdcAuth::GenerateKey(keyPath.c_str());
        } else {
            Base::PrintMessage("Unknow command");
        }
        break;
    }
    Base::TryCloseHandle((const uv_handle_t *)handle);
}

int HdcClient::CtrlServiceWork(const char *commandIn)
{
    command = commandIn;
    ctrlServerWork.data = this;
    uv_check_init(loopMain, &ctrlServerWork);
    uv_check_start(&ctrlServerWork, DoCtrlServiceWork);
    uv_run(loopMain, UV_RUN_NOWAIT);
    return 0;
}

string HdcClient::AutoConnectKey(string &doCommand, const string &preConnectKey) const
{
    string key = preConnectKey;
    bool isNoTargetCommand = false;
    vector<string> vecNoConnectKeyCommand;
    vecNoConnectKeyCommand.push_back(CMDSTR_SOFTWARE_VERSION);
    vecNoConnectKeyCommand.push_back(CMDSTR_SOFTWARE_HELP);
    vecNoConnectKeyCommand.push_back(CMDSTR_TARGET_DISCOVER);
    vecNoConnectKeyCommand.push_back(CMDSTR_LIST_TARGETS);
    vecNoConnectKeyCommand.push_back(CMDSTR_CONNECT_TARGET);
    vecNoConnectKeyCommand.push_back(CMDSTR_KILL_SERVER);
    vecNoConnectKeyCommand.push_back(CMDSTR_FORWARD_FPORT + " ls");
    vecNoConnectKeyCommand.push_back(CMDSTR_FORWARD_FPORT + " rm");
    for (string v : vecNoConnectKeyCommand) {
        if (!doCommand.compare(0, v.size(), v)) {
            isNoTargetCommand = true;
            break;
        }
    }
    if (isNoTargetCommand) {
        key = "";
    } else {
        if (!preConnectKey.size()) {
            key = CMDSTR_CONNECT_ANY;
        }
    }
    return key;
}

int HdcClient::ExecuteCommand(const string &commandIn)
{
    char ip[BUF_SIZE_TINY] = "";
    uint16_t port = 0;
    if (Base::ConnectKey2IPPort(channelHostPort.c_str(), ip, &port) < 0) {
        return -1;
    }
    command = commandIn;
    connectKey = AutoConnectKey(command, connectKey);
    ConnectServerForClient(ip, port);
    uv_timer_init(loopMain, &waitTimeDoCmd);
    waitTimeDoCmd.data = this;
    uv_timer_start(&waitTimeDoCmd, CommandWorker, 10, 10);
    WorkerPendding();
    return 0;
}

int HdcClient::Initial(const string &connectKeyIn)
{
    connectKey = connectKeyIn;
    if (!channelHostPort.size() || !channelHost.size() || !channelPort) {
        WRITE_LOG(LOG_FATAL, "Listen string initial failed");
        return ERR_PARM_FAIL;
    }
    return 0;
}

int HdcClient::ConnectServerForClient(const char *ip, uint16_t port)
{
    if (uv_is_closing((const uv_handle_t *)&channel->hWorkTCP)) {
        return ERR_SOCKET_FAIL;
    }
    WRITE_LOG(LOG_DEBUG, "Try to connect %s:%d", ip, port);
    struct sockaddr_in dest;
    uv_ip4_addr(ip, port, &dest);
    uv_connect_t *conn = new uv_connect_t();
    conn->data = this;
    uv_tcp_connect(conn, (uv_tcp_t *)&channel->hWorkTCP, (const struct sockaddr *)&dest, Connect);
    return 0;
}

void HdcClient::CommandWorker(uv_timer_t *handle)
{
    const uint16_t maxWaitRetry = 500;
    HdcClient *thisClass = (HdcClient *)handle->data;
    if (++thisClass->debugRetryCount > maxWaitRetry) {
        uv_timer_stop(handle);
        uv_stop(thisClass->loopMain);
        WRITE_LOG(LOG_DEBUG, "Connect server failed");
        return;
    }
    if (!thisClass->channel->handshakeOK) {
        return;
    }
    uv_timer_stop(handle);
    WRITE_LOG(LOG_DEBUG, "Connect server successful");
    thisClass->Send(thisClass->channel->channelId, (uint8_t *)thisClass->command.c_str(),
                    thisClass->command.size() + 1);
}

void HdcClient::AllocStdbuf(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf)
{
    if (sizeWanted <= 0) {
        return;
    }
    HChannel context = (HChannel)handle->data;
    int availSize = strlen(context->bufStd);
    buf->base = (char *)context->bufStd + availSize;
    buf->len = sizeof(context->bufStd) - availSize - 2;  // reserve 2bytes
}

void HdcClient::ReadStd(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    HChannel hChannel = (HChannel)stream->data;
    HdcClient *thisClass = (HdcClient *)hChannel->clsChannel;
    char *command = hChannel->bufStd;
    if (nread <= 0) {
        return;  // error
    }
    thisClass->Send(hChannel->channelId, (uint8_t *)command, strlen(command));
    Base::ZeroArray(hChannel->bufStd);
}

void HdcClient::ModifyTty(bool setOrRestore, uv_tty_t *tty)
{
    if (setOrRestore) {
#ifdef _WIN32
        uv_tty_set_mode(tty, UV_TTY_MODE_RAW);
#else
        if (tcgetattr(STDIN_FILENO, &terminalState))
            return;
        termios tio;
        if (tcgetattr(STDIN_FILENO, &tio))
            return;
        cfmakeraw(&tio);
        tio.c_cc[VTIME] = 0;
        tio.c_cc[VMIN] = 1;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio);
#endif
    } else {
#ifndef _WIN32
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminalState);
#endif
    }
}

void HdcClient::BindLocalStd(HChannel hChannel)
{
    if (command == CMDSTR_SHELL) {
        bShellInteractive = true;
    }
    if (uv_guess_handle(STDIN_FILENO) != UV_TTY) {
        WRITE_LOG(LOG_FATAL, "Not support std mode");
        return;
    }

    WRITE_LOG(LOG_DEBUG, "Tty std mode");
    if (uv_tty_init(loopMain, &hChannel->stdoutTty, STDOUT_FILENO, 0)
        || uv_tty_init(loopMain, &hChannel->stdinTty, STDIN_FILENO, 1)) {
        WRITE_LOG(LOG_DEBUG, "uv_tty_init failed");
        return;
    }
    hChannel->stdoutTty.data = hChannel;
    ++hChannel->uvHandleRef;
    hChannel->stdinTty.data = hChannel;
    ++hChannel->uvHandleRef;
    if (bShellInteractive) {
        WRITE_LOG(LOG_DEBUG, "uv_tty_init uv_tty_set_mode");
        ModifyTty(true, &hChannel->stdinTty);
        uv_read_start((uv_stream_t *)&hChannel->stdinTty, AllocStdbuf, ReadStd);
    }
}

void HdcClient::Connect(uv_connect_t *connection, int status)
{
    HdcClient *thisClass = (HdcClient *)connection->data;
    delete connection;
    HChannel hChannel = (HChannel)thisClass->channel;
    if (status < 0 || uv_is_closing((const uv_handle_t *)&hChannel->hWorkTCP)) {
        WRITE_LOG(LOG_FATAL, "connect failed");
        thisClass->FreeChannel(hChannel->channelId);
        return;
    }
    thisClass->BindLocalStd(hChannel);
    Base::SetTcpOptions((uv_tcp_t *)&hChannel->hWorkTCP);
    uv_read_start((uv_stream_t *)&hChannel->hWorkTCP, AllocCallback, ReadStream);
}

int HdcClient::PreHandshake(HChannel hChannel, const uint8_t *buf)
{
    ChannelHandShake *hShake = (ChannelHandShake *)buf;
    if (strncmp(hShake->banner, HANDSHAKE_MESSAGE.c_str(), HANDSHAKE_MESSAGE.size())) {
        hChannel->availTailIndex = 0;
        WRITE_LOG(LOG_DEBUG, "Channel Hello failed");
        return ERR_BUF_CHECK;
    }
    // sync remote session id to local
    uint32_t unOld = hChannel->channelId;
    hChannel->channelId = ntohl(hShake->channelId);
    AdminChannel(OP_UPDATE, unOld, hChannel);
    WRITE_LOG(LOG_DEBUG, "Client channel handshake finished, use connectkey:%s", connectKey.c_str());
    // send config
    // channel handshake step2
    if (memset_s(hShake->connectKey, sizeof(hShake->connectKey), 0, sizeof(hShake->connectKey)) != EOK
        || memcpy_s(hShake->connectKey, sizeof(hShake->connectKey), connectKey.c_str(), connectKey.size()) != EOK) {
        hChannel->availTailIndex = 0;
        WRITE_LOG(LOG_DEBUG, "Channel Hello failed");
        return ERR_BUF_COPY;
    }
    Send(hChannel->channelId, reinterpret_cast<uint8_t *>(hShake), sizeof(ChannelHandShake));
    hChannel->handshakeOK = true;
#ifdef HDC_CHANNEL_KEEP_ALIVE
    // Evaluation method, non long-term support
    Send(hChannel->channelId, reinterpret_cast<uint8_t *>(const_cast<char*>(CMDSTR_INNER_ENABLE_KEEPALIVE.c_str())),
         CMDSTR_INNER_ENABLE_KEEPALIVE.size());
#endif
    return RET_SUCCESS;
}

// read serverForClient(server)TCP data
int HdcClient::ReadChannel(HChannel hChannel, uint8_t *buf, const int bytesIO)
{
    if (!hChannel->handshakeOK) {
        return PreHandshake(hChannel, buf);
    }
#ifdef UNIT_TEST
    // Do not output to console when the unit test
    return 0;
#endif
    WRITE_LOG(LOG_DEBUG, "Client ReadChannel :%d", bytesIO);
    string s(reinterpret_cast<char *>(buf), bytesIO);
    fprintf(stdout, "%s", s.c_str());
    fflush(stdout);
    return 0;
};
}  // namespace Hdc