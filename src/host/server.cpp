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
#include "server.h"

namespace Hdc {
HdcServer::HdcServer(bool serverOrDaemonIn)
    : HdcSessionBase(serverOrDaemonIn)
{
    clsTCPClt = nullptr;
    clsUSBClt = nullptr;
    clsServerForClient = nullptr;
    uv_rwlock_init(&daemonAdmin);
    uv_rwlock_init(&forwardAdmin);
}

HdcServer::~HdcServer()
{
    WRITE_LOG(LOG_DEBUG, "~HdcServer");
    uv_rwlock_destroy(&daemonAdmin);
    uv_rwlock_destroy(&forwardAdmin);
}

void HdcServer::ClearInstanceResource()
{
    TryStopInstance();
    Base::TryCloseLoop(&loopMain, "HdcServer::~HdcServer");
    if (clsTCPClt) {
        delete clsTCPClt;
    }
    if (clsUSBClt) {
        delete clsUSBClt;
    }
    if (clsServerForClient) {
        delete (static_cast<HdcServerForClient *>(clsServerForClient));
    }
}

void HdcServer::TryStopInstance()
{
    ClearSessions();
    if (clsTCPClt) {
        clsTCPClt->Stop();
    }
    if (clsUSBClt) {
        clsUSBClt->Stop();
    }
    if (clsServerForClient) {
        ((HdcServerForClient *)clsServerForClient)->Stop();
    }
    ReMainLoopForInstanceClear();
    ClearMapDaemonInfo();
}

bool HdcServer::Initial(const char *listenString)
{
    if (Base::ProgramMutex(SERVER_NAME.c_str(), false) != 0) {
        WRITE_LOG(LOG_FATAL, "Other instance already running, program mutex failed");
        return false;
    }
    clsServerForClient = new HdcServerForClient(true, listenString, this, &loopMain);
    clsTCPClt = new HdcHostTCP(true, this);
    clsUSBClt = new HdcHostUSB(true, this, ctxUSB);
    if (!clsServerForClient || !clsTCPClt || !clsUSBClt) {
        WRITE_LOG(LOG_FATAL, "Class init failed");
        return false;
    }
    (static_cast<HdcServerForClient *>(clsServerForClient))->Initial();
    clsUSBClt->Initial();
    return true;
}

bool HdcServer::PullupServerWin32(const char *path, const char *listenString)
{
#ifdef _WIN32
    char buf[BUF_SIZE_SMALL] = "";
    char shortPath[MAX_PATH] = "";
    int ret = GetShortPathName(path, shortPath, MAX_PATH);
    std::string runPath = shortPath;
    if (ret == 0) {
        int err = GetLastError();
        WRITE_LOG(LOG_WARN, "GetShortPath path:[%s] err:%d errmsg:%s", path, err, strerror(err));
        string uvPath = path;
        runPath = uvPath.substr(uvPath.find_last_of("/\\") + 1);
    }
    WRITE_LOG(LOG_DEBUG, "server shortpath:[%s] runPath:[%s]", shortPath, runPath.c_str());
    if (sprintf_s(buf, sizeof(buf), "%s -l5 -s %s -m", runPath.c_str(), listenString) < 0) {
        return false;
    }
    WRITE_LOG(LOG_DEBUG, "Run server in debug-forground, cmd:%s", buf);
    STARTUPINFO si;
    Base::ZeroStruct(si);
    si.cb = sizeof(STARTUPINFO);
    PROCESS_INFORMATION pi;
    Base::ZeroStruct(pi);
#ifndef HDC_DEBUG
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
#endif
    CreateProcess(nullptr, buf, nullptr, nullptr, true, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#endif
    return true;
}

// Only detects that the default call is in the loop address, the other tubes are not
bool HdcServer::PullupServer(const char *listenString)
{
    char path[BUF_SIZE_SMALL] = "";
    size_t nPathSize = sizeof(path);
    int ret = uv_exepath(path, &nPathSize);
    if (ret < 0) {
        WRITE_LOG(LOG_WARN, "uvexepath ret:%d error:%s", ret, uv_err_name(ret));
        return false;
    }

#ifdef _WIN32
    if (!PullupServerWin32(path, listenString)) {
        return false;
    }
#else
    pid_t pc = fork();  // create process as daemon process
    if (pc < 0) {
        return false;
    } else if (!pc) {
        int i;
        const int maxFD = 1024;
        for (i = 0; i < maxFD; ++i) {
            // close file pipe
            close(i);
        }
        execl(path, "hdc", "-m", "-s", listenString, nullptr);
        exit(0);
        return true;
    }
    // orig process
#endif
    // wait little time, util backend-server work ready
    uv_sleep(TIME_BASE);
    return true;
}

void HdcServer::ClearMapDaemonInfo()
{
    map<string, HDaemonInfo>::iterator iter;
    uv_rwlock_rdlock(&daemonAdmin);
    for (iter = mapDaemon.begin(); iter != mapDaemon.end();) {
        string sKey = iter->first;
        HDaemonInfo hDi = iter->second;
        delete hDi;
        ++iter;
    }
    uv_rwlock_rdunlock(&daemonAdmin);
    uv_rwlock_wrlock(&daemonAdmin);
    mapDaemon.clear();
    uv_rwlock_wrunlock(&daemonAdmin);
}

void HdcServer::BuildDaemonVisableLine(HDaemonInfo hdi, bool fullDisplay, string &out)
{
    if (fullDisplay) {
        string sConn;
        string sStatus;
        switch (hdi->connType) {
            case CONN_TCP:
                sConn = "TCP";
                break;
            case CONN_USB:
                sConn = "USB";
                break;

            case CONN_BT:
                sConn = "BT";
                break;
            default:
                sConn = "UNKNOW";
                break;
        }
        switch (hdi->connStatus) {
            case STATUS_READY:
                sStatus = "Ready";
                break;
            case STATUS_CONNECTED:
                sStatus = "Connected";
                break;
            case STATUS_OFFLINE:
                sStatus = "Offline";
                break;
            default:
                sStatus = "UNKNOW";
                break;
        }
        out = Base::StringFormat("%s\t\t%s\t%s\t%s\n", hdi->connectKey.c_str(), sConn.c_str(), sStatus.c_str(),
                                 hdi->devName.c_str());
    } else {
        if (hdi->connStatus == STATUS_CONNECTED) {
            out = Base::StringFormat("%s\n", hdi->connectKey.c_str());
        }
    }
}

string HdcServer::GetDaemonMapList(uint8_t opType)
{
    string ret;
    bool fullDisplay = false;
    if (OP_GET_STRLIST_FULL == opType) {
        fullDisplay = true;
    }
    uv_rwlock_rdlock(&daemonAdmin);
    map<string, HDaemonInfo>::iterator iter;
    string echoLine;
    for (iter = mapDaemon.begin(); iter != mapDaemon.end(); ++iter) {
        HDaemonInfo di = iter->second;
        if (!di) {
            continue;
        }
        echoLine = "";
        BuildDaemonVisableLine(di, fullDisplay, echoLine);
        ret += echoLine;
    }
    uv_rwlock_rdunlock(&daemonAdmin);
    return ret;
}

void HdcServer::GetDaemonMapOnlyOne(HDaemonInfo &hDaemonInfoInOut)
{
    uv_rwlock_rdlock(&daemonAdmin);
    string key;
    for (auto &i : mapDaemon) {
        if (i.second->connStatus == STATUS_CONNECTED) {
            if (key == STRING_EMPTY) {
                key = i.first;
            } else {
                key = STRING_EMPTY;
                break;
            }
        }
    }
    if (key.size() > 0) {
        hDaemonInfoInOut = mapDaemon[key];
    }
    uv_rwlock_rdunlock(&daemonAdmin);
}

string HdcServer::AdminDaemonMap(uint8_t opType, const string &connectKey, HDaemonInfo &hDaemonInfoInOut)
{
    string sRet;
    switch (opType) {
        case OP_ADD: {
            HDaemonInfo pdiNew = new HdcDaemonInformation();
            *pdiNew = *hDaemonInfoInOut;
            uv_rwlock_wrlock(&daemonAdmin);
            if (!mapDaemon[hDaemonInfoInOut->connectKey]) {
                mapDaemon[hDaemonInfoInOut->connectKey] = pdiNew;
            }
            uv_rwlock_wrunlock(&daemonAdmin);
            break;
        }
        case OP_GET_STRLIST:
        case OP_GET_STRLIST_FULL: {
            sRet = GetDaemonMapList(opType);
            break;
        }
        case OP_QUERY: {
            uv_rwlock_rdlock(&daemonAdmin);
            if (mapDaemon.count(connectKey)) {
                hDaemonInfoInOut = mapDaemon[connectKey];
            }
            uv_rwlock_rdunlock(&daemonAdmin);
            break;
        }
        case OP_REMOVE: {
            uv_rwlock_wrlock(&daemonAdmin);
            if (mapDaemon.count(connectKey)) {
                mapDaemon.erase(connectKey);
            }
            uv_rwlock_wrunlock(&daemonAdmin);
            break;
        }
        case OP_GET_ANY: {
            uv_rwlock_rdlock(&daemonAdmin);
            map<string, HDaemonInfo>::iterator iter;
            for (iter = mapDaemon.begin(); iter != mapDaemon.end(); ++iter) {
                HDaemonInfo di = iter->second;
                // usb will be auto connected
                if (di->connStatus == STATUS_READY || di->connStatus == STATUS_CONNECTED) {
                    hDaemonInfoInOut = di;
                    break;
                }
            }
            uv_rwlock_rdunlock(&daemonAdmin);
            break;
        }
        case OP_GET_ONLY: {
            GetDaemonMapOnlyOne(hDaemonInfoInOut);
            break;
        }
        case OP_UPDATE: {  // Cannot update the Object HDi lower key value by direct value
            uv_rwlock_wrlock(&daemonAdmin);
            HDaemonInfo hdi = mapDaemon[hDaemonInfoInOut->connectKey];
            if (hdi) {
                *mapDaemon[hDaemonInfoInOut->connectKey] = *hDaemonInfoInOut;
            }
            uv_rwlock_wrunlock(&daemonAdmin);
            break;
        }
        default:
            break;
    }
    return sRet;
}

void HdcServer::NotifyInstanceSessionFree(HSession hSession, bool freeOrClear)
{
    HDaemonInfo hdiOld = nullptr;
    AdminDaemonMap(OP_QUERY, hSession->connectKey, hdiOld);
    if (hdiOld == nullptr) {
        return;
    }
    if (!freeOrClear) {  // step1
        // update
        HdcDaemonInformation diNew = *hdiOld;
        diNew.connStatus = STATUS_OFFLINE;
        HDaemonInfo hdiNew = &diNew;
        AdminDaemonMap(OP_UPDATE, hSession->connectKey, hdiNew);
    } else {  // step2
        string usbMountPoint = hdiOld->usbMountPoint;
        // The waiting time must be longer than DEVICE_CHECK_INTERVAL. Wait the method WatchUsbNodeChange
        // to finish execution. Otherwise, the main thread and the session worker thread will conflict
        constexpr int waitDaemonReconnect = DEVICE_CHECK_INTERVAL + DEVICE_CHECK_INTERVAL;
        auto funcDelayUsbNotify = [this, usbMountPoint](const uint8_t flag, string &msg, const void *) -> void {
            string s = usbMountPoint;
            clsUSBClt->RemoveIgnoreDevice(s);
        };
        if (usbMountPoint.size() > 0) {
            // wait time for daemon reconnect
            // If removed from maplist, the USB module will be reconnected, so it needs to wait for a while
            Base::DelayDoSimple(&loopMain, waitDaemonReconnect, funcDelayUsbNotify);
        }
    }
}

bool HdcServer::HandServerAuth(HSession hSession, SessionHandShake &handshake)
{
    bool ret = false;
    int retChild = 0;
    string bufString;
    switch (handshake.authType) {
        case AUTH_TOKEN: {
            void *ptr = nullptr;
            bool retChild = HdcAuth::KeylistIncrement(hSession->listKey, hSession->authKeyIndex, &ptr);
            // HdcAuth::FreeKey will be effect at funciton 'FreeSession'
            if (!retChild) {
                // Iteration call certificate authentication
                handshake.authType = AUTH_PUBLICKEY;
                ret = HandServerAuth(hSession, handshake);
                break;
            }
            char sign[BUF_SIZE_DEFAULT2] = { 0 };
            retChild = HdcAuth::AuthSign(ptr, (const unsigned char *)handshake.buf.c_str(), handshake.buf.size(), sign);
            if (!retChild) {
                break;
            }
            handshake.buf = string(sign, retChild);
            handshake.authType = AUTH_SIGNATURE;
            bufString = SerialStruct::SerializeToString(handshake);
            Send(hSession->sessionId, 0, CMD_KERNEL_HANDSHAKE, (uint8_t *)bufString.c_str(), bufString.size());
            ret = true;
            break;
        }
        case AUTH_PUBLICKEY: {
            char bufPrivateKey[BUF_SIZE_DEFAULT2] = "";
            retChild = HdcAuth::GetPublicKeyFileBuf((unsigned char *)bufPrivateKey, sizeof(bufPrivateKey));
            if (!retChild) {
                break;
            }
            handshake.buf = string(bufPrivateKey, retChild);
            handshake.authType = AUTH_PUBLICKEY;
            bufString = SerialStruct::SerializeToString(handshake);
            Send(hSession->sessionId, 0, CMD_KERNEL_HANDSHAKE, (uint8_t *)bufString.c_str(), bufString.size());
            ret = true;
            break;
        }
        default:
            break;
    }
    return ret;
}

bool HdcServer::ServerSessionHandshake(HSession hSession, uint8_t *payload, int payloadSize)
{
    // session handshake step3
    string s = string((char *)payload, payloadSize);
    Hdc::HdcSessionBase::SessionHandShake handshake;
    SerialStruct::ParseFromString(handshake, s);
    if (handshake.banner != HANDSHAKE_MESSAGE.c_str()) {
        WRITE_LOG(LOG_DEBUG, "Hello failed");
        return false;
    }
    if (handshake.authType != AUTH_OK) {
        if (!HandServerAuth(hSession, handshake)) {
            WRITE_LOG(LOG_DEBUG, "Auth failed");
            return false;
        }
        return true;
    }
    // handshake auth OK
    HDaemonInfo hdiOld = nullptr;
    AdminDaemonMap(OP_QUERY, hSession->connectKey, hdiOld);
    if (!hdiOld) {
        return false;
    }
    HdcDaemonInformation diNew = *hdiOld;
    HDaemonInfo hdiNew = &diNew;
    // update
    hdiNew->connStatus = STATUS_CONNECTED;
    if (handshake.buf.size() > sizeof(hdiNew->devName) || !handshake.buf.size()) {
        hdiNew->devName = "unknow...";
    } else {
        hdiNew->devName = handshake.buf;
    }
    AdminDaemonMap(OP_UPDATE, hSession->connectKey, hdiNew);
    hSession->handshakeOK = true;
    return true;
}

// call in child thread
bool HdcServer::FetchCommand(HSession hSession, const uint32_t channelId, const uint16_t command, uint8_t *payload,
                             const int payloadSize)
{
    bool ret = true;
    HdcServerForClient *sfc = static_cast<HdcServerForClient *>(clsServerForClient);
    if (CMD_KERNEL_HANDSHAKE == command) {
        ret = ServerSessionHandshake(hSession, payload, payloadSize);
        WRITE_LOG(LOG_DEBUG, "Session handshake %s", ret ? "successful" : "failed");
        return ret;
    }
    // When you first initialize, ChannelID may be 0
    HChannel hChannel = sfc->AdminChannel(OP_QUERY_REF, channelId, nullptr);
    if (!hChannel) {
        if (command == CMD_KERNEL_CHANNEL_CLOSE) {
            // Daemon close channel and want to notify server close channel also, but it may has been
            // closed by herself
        } else {
            // Client may be ctrl+c and Server remove channel. notify server async
        }
        uint8_t flag = 0;
        Send(hSession->sessionId, channelId, CMD_KERNEL_CHANNEL_CLOSE, &flag, 1);
        return ret;
    }
    if (hChannel->isDead) {
        --hChannel->ref;
        return ret;
    }
    switch (command) {
        case CMD_KERNEL_ECHO_RAW: {  // Native shell data output
            sfc->EchoClientRaw(hChannel, payload, payloadSize);
            break;
        }
        case CMD_KERNEL_ECHO: {
            MessageLevel level = (MessageLevel)*payload;
            string s(reinterpret_cast<char *>(payload + 1), payloadSize - 1);
            sfc->EchoClient(hChannel, level, s.c_str());
            WRITE_LOG(LOG_DEBUG, "CMD_KERNEL_ECHO size:%d", payloadSize - 1);
            break;
        }
        case CMD_KERNEL_CHANNEL_CLOSE: {
            WRITE_LOG(LOG_DEBUG, "CMD_KERNEL_CHANNEL_CLOSE channelid:%u", channelId);
            // Forcibly closing the tcp handle here may result in incomplete data reception on the client side
            ClearOwnTasks(hSession, channelId);
            // crossthread free
            sfc->PushAsyncMessage(channelId, ASYNC_FREE_CHANNEL, nullptr, 0);
            if (*payload != 0) {
                --(*payload);
                Send(hSession->sessionId, channelId, CMD_KERNEL_CHANNEL_CLOSE, payload, 1);
            }
            break;
        }
        case CMD_FORWARD_SUCCESS: {
            // add to local
            HdcForwardInformation di;
            HForwardInfo pdiNew = &di;
            pdiNew->channelId = channelId;
            pdiNew->sessionId = hSession->sessionId;
            pdiNew->forwardDirection = ((char *)payload)[0] == '1';
            pdiNew->taskString = (char *)payload + 2;
            AdminForwardMap(OP_ADD, STRING_EMPTY, pdiNew);
            Base::TryCloseHandle((uv_handle_t *)&hChannel->hChildWorkTCP);  // detch client channel
            break;
        }
        default: {
            HSession hSession = AdminSession(OP_QUERY, hChannel->targetSessionId, nullptr);
            if (!hSession) {
                ret = false;
                break;
            }
            ret = DispatchTaskData(hSession, channelId, command, payload, payloadSize);
            break;
        }
    }
    --hChannel->ref;
    return ret;
}

void HdcServer::BuildForwardVisableLine(bool fullOrSimble, HForwardInfo hfi, string &echo)
{
    string buf;
    if (fullOrSimble) {
        buf = Base::StringFormat("'%s'\t%s\n", hfi->taskString.c_str(),
                                 hfi->forwardDirection ? "[Forward]" : "[Reverse]");
    } else {
        buf = Base::StringFormat("%s\n", hfi->taskString.c_str());
    }
    echo += buf;
}

string HdcServer::AdminForwardMap(uint8_t opType, const string &taskString, HForwardInfo &hForwardInfoInOut)
{
    string sRet;
    switch (opType) {
        case OP_ADD: {
            HForwardInfo pfiNew = new HdcForwardInformation();
            *pfiNew = *hForwardInfoInOut;
            uv_rwlock_wrlock(&forwardAdmin);
            if (!mapForward[hForwardInfoInOut->taskString]) {
                mapForward[hForwardInfoInOut->taskString] = pfiNew;
            }
            uv_rwlock_wrunlock(&forwardAdmin);
            break;
        }
        case OP_GET_STRLIST:
        case OP_GET_STRLIST_FULL: {
            uv_rwlock_rdlock(&forwardAdmin);
            map<string, HForwardInfo>::iterator iter;
            for (iter = mapForward.begin(); iter != mapForward.end(); ++iter) {
                HForwardInfo di = iter->second;
                if (!di) {
                    continue;
                }
                BuildForwardVisableLine(opType == OP_GET_STRLIST_FULL, di, sRet);
            }
            uv_rwlock_rdunlock(&forwardAdmin);
            break;
        }
        case OP_QUERY: {
            uv_rwlock_rdlock(&forwardAdmin);
            if (mapForward.count(taskString)) {
                hForwardInfoInOut = mapForward[taskString];
            }
            uv_rwlock_rdunlock(&forwardAdmin);
            break;
        }
        case OP_REMOVE: {
            uv_rwlock_wrlock(&forwardAdmin);
            if (mapForward.count(taskString)) {
                mapForward.erase(taskString);
            }
            uv_rwlock_wrunlock(&forwardAdmin);
            break;
        }
        default:
            break;
    }
    return sRet;
}

void HdcServer::UsbPreConnect(uv_timer_t *handle)
{
    HSession hSession = (HSession)handle->data;
    bool stopLoop = false;
    HdcServer *hdcServer = (HdcServer *)hSession->classInstance;
    const int usbConnectRetryMax = 5;
    while (true) {
        WRITE_LOG(LOG_DEBUG, "HdcServer::UsbPreConnect");
        if (++hSession->hUSB->retryCount > usbConnectRetryMax) {  // max 15s
            hdcServer->FreeSession(hSession->sessionId);
            stopLoop = true;
            break;
        }
        HDaemonInfo pDi = nullptr;
        if (hSession->connectKey == "any") {
            hdcServer->AdminDaemonMap(OP_GET_ANY, hSession->connectKey, pDi);
        } else {
            hdcServer->AdminDaemonMap(OP_QUERY, hSession->connectKey, pDi);
        }
        if (!pDi || !pDi->usbMountPoint.size()) {
            break;
        }
        HdcHostUSB *hdcHostUSB = (HdcHostUSB *)hSession->classModule;
        hdcHostUSB->ConnectDetectDaemon(hSession, pDi);
        stopLoop = true;
        break;
    }
    if (stopLoop && !uv_is_closing((const uv_handle_t *)handle)) {
        uv_close((uv_handle_t *)handle, Base::CloseTimerCallback);
    }
}

// -1,has old,-2 error
int HdcServer::CreateConnect(const string &connectKey)
{
    uint8_t connType = 0;
    if (connectKey.find(":") != std::string::npos) {  // TCP
        connType = CONN_TCP;
    } else {  // USB
        connType = CONN_USB;
    }
    HDaemonInfo hdi = nullptr;
    if (connectKey == "any") {
        return RET_SUCCESS;
    }
    AdminDaemonMap(OP_QUERY, connectKey, hdi);
    if (hdi == nullptr) {
        HdcDaemonInformation di;
        Base::ZeroStruct(di);
        di.connectKey = connectKey;
        di.connType = connType;
        di.connStatus = STATUS_UNKNOW;
        HDaemonInfo pDi = (HDaemonInfo)&di;
        AdminDaemonMap(OP_ADD, "", pDi);
        AdminDaemonMap(OP_QUERY, connectKey, hdi);
    }
    if (!hdi || hdi->connStatus == STATUS_CONNECTED) {
        return ERR_GENERIC;
    }
    HSession hSession = nullptr;
    if (CONN_TCP == connType) {
        hSession = clsTCPClt->ConnectDaemon(connectKey);
    } else {
        hSession = MallocSession(true, CONN_USB, clsUSBClt);
        hSession->connectKey = connectKey;
        uv_timer_t *waitTimeDoCmd = new uv_timer_t;
        uv_timer_init(&loopMain, waitTimeDoCmd);
        waitTimeDoCmd->data = hSession;
        uv_timer_start(waitTimeDoCmd, UsbPreConnect, 10, 100);
    }
    if (!hSession) {
        return ERR_BUF_ALLOC;
    }
    HDaemonInfo hdiQuery = nullptr;
    AdminDaemonMap(OP_QUERY, connectKey, hdiQuery);
    if (hdiQuery) {
        HdcDaemonInformation diNew = *hdiQuery;
        diNew.hSession = hSession;
        HDaemonInfo hdiNew = &diNew;
        AdminDaemonMap(OP_UPDATE, hdiQuery->connectKey, hdiNew);
    }
    return RET_SUCCESS;
}

void HdcServer::AttachChannel(HSession hSession, const uint32_t channelId)
{
    int ret = 0;
    HdcServerForClient *hSfc = static_cast<HdcServerForClient *>(clsServerForClient);
    HChannel hChannel = hSfc->AdminChannel(OP_QUERY_REF, channelId, nullptr);
    if (!hChannel) {
        return;
    }
    uv_tcp_init(&hSession->childLoop, &hChannel->hChildWorkTCP);
    hChannel->hChildWorkTCP.data = hChannel;
    hChannel->targetSessionId = hSession->sessionId;
    if ((ret = uv_tcp_open((uv_tcp_t *)&hChannel->hChildWorkTCP, hChannel->fdChildWorkTCP)) < 0) {
        WRITE_LOG(LOG_DEBUG, "Hdcserver AttachChannel uv_tcp_open failed %s, channelid:%d fdChildWorkTCP:%d",
                  uv_err_name(ret), hChannel->channelId, hChannel->fdChildWorkTCP);
        Base::TryCloseHandle((uv_handle_t *)&hChannel->hChildWorkTCP);
        --hChannel->ref;
        return;
    }
    Base::SetTcpOptions((uv_tcp_t *)&hChannel->hChildWorkTCP);
    uv_read_start((uv_stream_t *)&hChannel->hChildWorkTCP, hSfc->AllocCallback, hSfc->ReadStream);
    --hChannel->ref;
};

void HdcServer::DeatchChannel(HSession hSession, const uint32_t channelId)
{
    HdcServerForClient *hSfc = static_cast<HdcServerForClient *>(clsServerForClient);
    // childCleared has not set, no need OP_QUERY_REF
    HChannel hChannel = hSfc->AdminChannel(OP_QUERY, channelId, nullptr);
    if (!hChannel) {
        return;
    }
    if (hChannel->childCleared) {
        WRITE_LOG(LOG_DEBUG, "Childchannel has already freed, cid:%u", channelId);
        return;
    }
    uint8_t count = 0;
    Send(hSession->sessionId, hChannel->channelId, CMD_KERNEL_CHANNEL_CLOSE, &count, 1);
    if (uv_is_closing((const uv_handle_t *)&hChannel->hChildWorkTCP)) {
        Base::DoNextLoop(&hSession->childLoop, hChannel, [](const uint8_t flag, string &msg, const void *data) {
            HChannel hChannel = (HChannel)data;
            hChannel->childCleared = true;
            WRITE_LOG(LOG_DEBUG, "Childchannel free direct, cid:%u", hChannel->channelId);
        });
    } else {
        Base::TryCloseHandle((uv_handle_t *)&hChannel->hChildWorkTCP, [](uv_handle_t *handle) -> void {
            HChannel hChannel = (HChannel)handle->data;
            hChannel->childCleared = true;
            WRITE_LOG(LOG_DEBUG, "Childchannel free callback, cid:%u", hChannel->channelId);
        });
    }
};

bool HdcServer::ServerCommand(const uint32_t sessionId, const uint32_t channelId, const uint16_t command,
                              uint8_t *bufPtr, const int size)
{
    HdcServerForClient *hSfc = static_cast<HdcServerForClient *>(clsServerForClient);
    HChannel hChannel = hSfc->AdminChannel(OP_QUERY, channelId, nullptr);
    HSession hSession = AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hChannel || !hSession) {
        return false;
    }
    return FetchCommand(hSession, channelId, command, bufPtr, size);
}

// clang-format off
bool HdcServer::RedirectToTask(HTaskInfo hTaskInfo, HSession hSession, const uint32_t channelId,
                               const uint16_t command, uint8_t *payload, const int payloadSize)
// clang-format on
{
    bool ret = true;
    hTaskInfo->ownerSessionClass = this;
    switch (command) {
        case CMD_UNITY_BUGREPORT_INIT:
        case CMD_UNITY_BUGREPORT_DATA:
            ret = TaskCommandDispatch<HdcHostUnity>(hTaskInfo, TYPE_UNITY, command, payload, payloadSize);
            break;
        case CMD_FILE_INIT:
        case CMD_FILE_BEGIN:
        case CMD_FILE_CHECK:
        case CMD_FILE_DATA:
        case CMD_FILE_FINISH:
            ret = TaskCommandDispatch<HdcFile>(hTaskInfo, TASK_FILE, command, payload, payloadSize);
            break;
        case CMD_FORWARD_INIT:
        case CMD_FORWARD_CHECK:
        case CMD_FORWARD_CHECK_RESULT:
        case CMD_FORWARD_ACTIVE_MASTER:
        case CMD_FORWARD_ACTIVE_SLAVE:
        case CMD_FORWARD_DATA:
        case CMD_FORWARD_FREE_CONTEXT:
            ret = TaskCommandDispatch<HdcHostForward>(hTaskInfo, TASK_FORWARD, command, payload, payloadSize);
            break;
        case CMD_APP_INIT:
        case CMD_APP_SIDELOAD:
        case CMD_APP_BEGIN:
        case CMD_APP_FINISH:
        case CMD_APP_UNINSTALL:
            ret = TaskCommandDispatch<HdcHostApp>(hTaskInfo, TASK_APP, command, payload, payloadSize);
            break;
        default:
            ret = false;
            break;
    }
    return ret;
}

bool HdcServer::RemoveInstanceTask(const uint8_t op, HTaskInfo hTask)
{
    bool ret = true;
    switch (hTask->taskType) {
        case TYPE_SHELL:
            WRITE_LOG(LOG_DEBUG, "Server not enable unity/shell");
            break;
        case TYPE_UNITY:
            ret = DoTaskRemove<HdcHostUnity>(hTask, op);
            break;
        case TASK_FILE:
            ret = DoTaskRemove<HdcFile>(hTask, op);
            break;
        case TASK_FORWARD:
            ret = DoTaskRemove<HdcHostForward>(hTask, op);
            break;
        case TASK_APP:
            ret = DoTaskRemove<HdcHostApp>(hTask, op);
            break;
        default:
            ret = false;
            break;
    }
    return ret;
}
}  // namespace Hdc