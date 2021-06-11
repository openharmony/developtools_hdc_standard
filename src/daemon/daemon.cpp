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
#include "daemon.h"
#include "../common/serial_struct.h"
#include <openssl/sha.h>

namespace Hdc {
HdcDaemon::HdcDaemon(bool serverOrDaemonIn)
    : HdcSessionBase(serverOrDaemonIn)
{
    clsTCPServ = nullptr;
    clsUSBServ = nullptr;
    clsJdwp = nullptr;
    enableSecure = false;
    isStopped = false;
}

HdcDaemon::~HdcDaemon()
{
    WRITE_LOG(LOG_DEBUG, "~HdcDaemon");
}

void HdcDaemon::ClearInstanceResource()
{
    StopInstance();
    Base::TryCloseLoop(&loopMain, "HdcDaemon::~HdcDaemon");
    if (clsTCPServ) {
        delete (HdcDaemonTCP *)clsTCPServ;
        clsTCPServ = nullptr;
    }
    if (clsUSBServ) {
        delete (HdcDaemonUSB *)clsUSBServ;
        clsUSBServ = nullptr;
    }
    if (clsJdwp) {
        delete (HdcJdwp *)clsJdwp;
        clsJdwp = nullptr;
    }
    WRITE_LOG(LOG_DEBUG, "~HdcDaemon finish");
}

void HdcDaemon::StopInstance()
{
    if (isStopped) {
        return;
    }
    ClearSessions();
    if (clsTCPServ) {
        WRITE_LOG(LOG_DEBUG, "Stop TCP");
        ((HdcDaemonTCP *)clsTCPServ)->Stop();
    }
    if (clsUSBServ) {
        WRITE_LOG(LOG_DEBUG, "Stop USB");
        ((HdcDaemonUSB *)clsUSBServ)->Stop();
    }
    ((HdcJdwp *)clsJdwp)->Stop();
    // workaround temply remove MainLoop instance clear
    isStopped = true;
    WRITE_LOG(LOG_DEBUG, "Stop loopmain");
}

void HdcDaemon::InitMod(bool bEnableTCP, bool bEnableUSB)
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemon InitMod");
    if (bEnableTCP) {
        // tcp
        clsTCPServ = new HdcDaemonTCP(false, this);
        ((HdcDaemonTCP *)clsTCPServ)->Initial();
    }
    if (bEnableUSB) {
        // usb
        clsUSBServ = new HdcDaemonUSB(false, this);
        ((HdcDaemonUSB *)clsUSBServ)->Initial();
    }

    clsJdwp = new HdcJdwp(&loopMain);
    ((HdcJdwp *)clsJdwp)->Initial();

    // enable security
    char value[4] = "0";
    Base::GetHdcProperty("ro.hdc.secure", value, 4);
    if (!strcmp("1", value)) {
        enableSecure = true;
    }
}

// clang-format off
bool HdcDaemon::RedirectToTask(HTaskInfo hTaskInfo, HSession hSession, const uint32_t channelId,
                               const uint16_t command, uint8_t *payload, const int payloadSize)
{
    bool ret = true;
    hTaskInfo->ownerSessionClass = this;
    switch (command) {
        case CMD_UNITY_EXECUTE:
        case CMD_UNITY_REMOUNT:
        case CMD_UNITY_REBOOT:
        case CMD_UNITY_RUNMODE:
        case CMD_UNITY_HILOG:
        case CMD_UNITY_ROOTRUN:
        case CMD_UNITY_TERMINATE:
        case CMD_UNITY_BUGREPORT_INIT:
        case CMD_UNITY_JPID:
            ret = TaskCommandDispatch<HdcDaemonUnity>(hTaskInfo, TYPE_UNITY, command, payload, payloadSize);
            break;
        case CMD_SHELL_INIT:
        case CMD_KERNEL_ECHO_RAW:
            ret = TaskCommandDispatch<HdcShell>(hTaskInfo, TYPE_SHELL, command, payload, payloadSize);
            break;
        case CMD_FILE_CHECK:
        case CMD_FILE_DATA:
        case CMD_FILE_FINISH:
        case CMD_FILE_INIT:
        case CMD_FILE_BEGIN:
            ret = TaskCommandDispatch<HdcFile>(hTaskInfo, TASK_FILE, command, payload, payloadSize);
            break;
        // One-way function, so fewer options
        case CMD_APP_CHECK:
        case CMD_APP_DATA:
        case CMD_APP_UNINSTALL:
            ret = TaskCommandDispatch<HdcDaemonApp>(hTaskInfo, TASK_APP, command, payload, payloadSize);
            break;
        case CMD_FORWARD_INIT:
        case CMD_FORWARD_CHECK:
        case CMD_FORWARD_ACTIVE_SLAVE:
        case CMD_FORWARD_DATA:
        case CMD_FORWARD_FREE_CONTEXT:
        case CMD_FORWARD_CHECK_RESULT:
            ret = TaskCommandDispatch<HdcDaemonForward>(hTaskInfo, TASK_FORWARD, command, payload, payloadSize);
            break;
        default:
            ret = false;
            break;
    }
    return ret;
}
// clang-format on

bool HdcDaemon::HandDaemonAuth(HSession hSession, const uint32_t channelId, SessionHandShake &handshake)
{
    bool ret = false;
    switch (handshake.authType) {
        case AUTH_NONE: {  // AUTH_NONE -> AUTH
            hSession->tokenRSA = Base::GetRandomString(SHA_DIGEST_LENGTH);
            handshake.authType = AUTH_TOKEN;
            handshake.buf = hSession->tokenRSA;
            string bufString = SerialStruct::SerializeToString(handshake);
            Send(hSession->sessionId, channelId, CMD_KERNEL_HANDSHAKE, (uint8_t *)bufString.c_str(), bufString.size());
            ret = true;
            break;
        }
        case AUTH_SIGNATURE: {
            // When Host is first connected to the device, the signature authentication is inevitable, and the
            // certificate verification must be triggered.
            //
            // When the certificate is verified, the client sends a public key to the device, triggered the system UI
            // jump out dialog, and click the system, the system will store the Host public key certificate in the
            // device locally, and the signature authentication will be correct when the subsequent connection is
            // connected.
            if (!HdcAuth::AuthVerify((uint8_t *)hSession->tokenRSA.c_str(), (uint8_t *)handshake.buf.c_str(),
                                     handshake.buf.size())) {
                // Next auth
                handshake.authType = AUTH_TOKEN;
                handshake.buf = hSession->tokenRSA;
                string bufString = SerialStruct::SerializeToString(handshake);
                Send(hSession->sessionId, channelId, CMD_KERNEL_HANDSHAKE, (uint8_t *)bufString.c_str(),
                     bufString.size());
                break;
            }
            ret = true;
            break;
        }
        case AUTH_PUBLICKEY: {
            ret = HdcAuth::PostUIConfirm(handshake.buf);
            WRITE_LOG(LOG_DEBUG, "Auth host OK, postUIConfirm");
            break;
        }
        default:
            break;
    }
    return ret;
}

bool HdcDaemon::CheckVersionMatch(string version)
{
    uint32_t nowMajor;
    uint32_t nowMinor;
    uint32_t inMajor;
    uint32_t inMinor;
    uint32_t ignore;
    if (sscanf_s(VERSION_NUMBER.c_str(), "%d.%d.%d", &nowMajor, &nowMinor, &ignore) < 3
        || sscanf_s(VERSION_NUMBER.c_str(), "%d.%d.%d", &inMajor, &inMinor, &ignore) < 3) {
        return false;
    }
    if (nowMajor != inMajor || nowMinor != inMinor) {
        return false;
    }
    return true;
}

bool HdcDaemon::DaemonSessionHandshake(HSession hSession, const uint32_t channelId, uint8_t *payload, int payloadSize)
{
    // session handshake step2
    string s = string((char *)payload, payloadSize);
    SessionHandShake handshake;
    string err;
    SerialStruct::ParseFromString(handshake, s);
    // banner to check is parse ok...
    if (handshake.banner != HANDSHAKE_MESSAGE) {
        hSession->availTailIndex = 0;
        WRITE_LOG(LOG_FATAL, "Recv server-hello failed");
        return false;
    }
    if (handshake.authType == AUTH_NONE) {
        // checkVersionMatch
        // daemon handshake 1st packet
        uint32_t unOld = hSession->sessionId;
        hSession->sessionId = handshake.sessionId;
        hSession->connectKey = handshake.connectKey;
        AdminSession(OP_UPDATE, unOld, hSession);
        if (clsUSBServ != nullptr) {
            (reinterpret_cast<HdcDaemonUSB *>(clsUSBServ))->OnNewHandshakeOK(hSession->sessionId);
        }

        handshake.sessionId = 0;
        handshake.connectKey = "";
    }
    if (enableSecure && !HandDaemonAuth(hSession, channelId, handshake)) {
        return false;
    }
    // handshake auth OK.Can append the sending device information to HOST
    char hostName[BUF_SIZE_MEDIUM] = "";
    size_t len = sizeof(hostName);
    uv_os_gethostname(hostName, &len);
    handshake.authType = AUTH_OK;
    handshake.buf = hostName;
    string bufString = SerialStruct::SerializeToString(handshake);
    Send(hSession->sessionId, channelId, CMD_KERNEL_HANDSHAKE, (uint8_t *)bufString.c_str(), bufString.size());
    hSession->handshakeOK = true;
    return true;
}

bool HdcDaemon::FetchCommand(HSession hSession, const uint32_t channelId, const uint16_t command, uint8_t *payload,
                             int payloadSize)
{
    bool ret = true;
    if (!hSession->handshakeOK && command != CMD_KERNEL_HANDSHAKE) {
        ret = false;
        return ret;
    }
    switch (command) {
        case CMD_KERNEL_HANDSHAKE: {
            // session handshake step2
            ret = DaemonSessionHandshake(hSession, channelId, payload, payloadSize);
            break;
        }
        case CMD_KERNEL_CHANNEL_CLOSE: {  // Daemon is only cleaning up the Channel task
            ClearOwnTasks(hSession, channelId);
            if (*payload) {
                (*payload)--;
                Send(hSession->sessionId, channelId, CMD_KERNEL_CHANNEL_CLOSE, payload, 1);
            }
            ret = true;
            break;
        }
        default:
            ret = DispatchTaskData(hSession, channelId, command, payload, payloadSize);
            break;
    }
    return ret;
}

bool HdcDaemon::RemoveInstanceTask(const uint8_t op, HTaskInfo hTask)
{
    bool ret = true;
    switch (hTask->taskType) {
        case TYPE_UNITY:
            ret = DoTaskRemove<HdcDaemonUnity>(hTask, op);
            break;
        case TYPE_SHELL:
            ret = DoTaskRemove<HdcShell>(hTask, op);
            break;
        case TASK_FILE:
            ret = DoTaskRemove<HdcTransferBase>(hTask, op);
            break;
        case TASK_FORWARD:
            ret = DoTaskRemove<HdcDaemonForward>(hTask, op);
            break;
        case TASK_APP:
            ret = DoTaskRemove<HdcDaemonApp>(hTask, op);
            break;
        default:
            ret = false;
            break;
    }
    return ret;
}

void HdcDaemon::StopDaemon(bool restart)
{
    PushAsyncMessage(0, ASYNC_STOP_MAINLOOP, nullptr, 0);
    WRITE_LOG(LOG_DEBUG, "StopDaemon has sended");
    wantRestart = restart;
}

bool HdcDaemon::ServerCommand(const uint32_t sessionId, const uint32_t channelId, const uint16_t command,
                              uint8_t *bufPtr, const int size)
{
    return Send(sessionId, channelId, command, (uint8_t *)bufPtr, size) > 0;
}

void HdcDaemon::JdwpNewFileDescriptor(const uint8_t *buf, const int bytesIO)
{
    uint32_t pid = *(uint32_t *)(buf + 1);
    uint32_t fd = *(uint32_t *)(buf + 5);
    ((HdcJdwp *)clsJdwp)->SendJdwpNewFD(pid, fd);
};
}  // namespace Hdc
