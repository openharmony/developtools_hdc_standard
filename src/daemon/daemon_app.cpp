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
#include "daemon_app.h"

namespace Hdc {
HdcDaemonApp::HdcDaemonApp(HTaskInfo hTaskInfo)
    : HdcTransferBase(hTaskInfo)
{
    commandBegin = CMD_APP_BEGIN;
    commandData = CMD_APP_DATA;
    funcAppModFinish = nullptr;
}

HdcDaemonApp::~HdcDaemonApp()
{
    WRITE_LOG(LOG_DEBUG, "~HdcDaemonApp");
}

bool HdcDaemonApp::ReadyForRelease()
{
    if (!HdcTaskBase::ReadyForRelease()) {
        return false;
    }
    if (!asyncCommand.ReadyForRelease()) {
        return false;
    }
    return true;
}

bool HdcDaemonApp::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    if (!HdcTransferBase::CommandDispatch(command, payload, payloadSize)) {
        return false;
    }
    bool ret = true;
    switch (command) {
        case CMD_APP_CHECK: {
            string tmpData = "/data/local/tmp/";
            string tmpSD = "/sdcard/tmp/";
            string dstPath = tmpData;
            string bufString((char *)payload, payloadSize);
            SerialStruct::ParseFromString(ctxNow.transferConfig, bufString);
            // update transferconfig to main context
            ctxNow.master = false;
            ctxNow.fsOpenReq.data = &ctxNow;
            // -lrtsdpg, -l -r -t -s..,
            if (ctxNow.transferConfig.functionName == CMDSTR_APP_INSTALL
                && ctxNow.transferConfig.options.find("s") != std::string::npos) {
                dstPath = tmpSD;
            }
#ifdef HDC_PCDEBUG
            char tmpPath[256] = "";
            size_t size = 256;
            uv_os_tmpdir(tmpPath, &size);
            dstPath = tmpPath;
            dstPath += PREF_SEPARATOR;
#endif
            dstPath += ctxNow.transferConfig.optionalName;
            ctxNow.localPath = dstPath;
            ctxNow.transferBegin = Base::GetRuntimeMSec();
            ctxNow.fileSize = ctxNow.transferConfig.fileSize;
            ++refCount;
            uv_fs_open(loopTask, &ctxNow.fsOpenReq, ctxNow.localPath.c_str(),
                       UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY, S_IRUSR, OnFileOpen);
            break;
        }
        case CMD_APP_UNINSTALL: {  // This command has a command to implant the risk, but it is a controllable device,
                                   // it can be ignored
            PackageShell(false, "", (const char *)payload);  // Connection parameters to pass over the interface
            break;
        }
        default:
            break;
    }
    return ret;
};

bool HdcDaemonApp::AsyncInstallFinish(bool finish, int64_t exitStatus, const string result)
{
    if (mode == APPMOD_INSTALL) {
        unlink(ctxNow.localPath.c_str());
    }
    asyncCommand.DoRelease();
    string echo = result;
    echo = Base::ReplaceAll(echo, "\n", " ");
    vector<uint8_t> vecBuf;
    vecBuf.push_back(mode);
    vecBuf.push_back(exitStatus == 0);
    vecBuf.insert(vecBuf.end(), (uint8_t *)echo.c_str(), (uint8_t *)echo.c_str() + echo.size());
    SendToAnother(CMD_APP_FINISH, vecBuf.data(), vecBuf.size());
    --refCount;
#ifdef UNIT_TEST
    Base::WriteBinFile((UT_TMP_PATH + "/appinstall.result").c_str(), (uint8_t *)MESSAGE_SUCCESS.c_str(),
                       MESSAGE_SUCCESS.size(), true);
#endif
    return true;
}

void HdcDaemonApp::PackageShell(bool installOrUninstall, const char *options, const char *package)
{
    ++refCount;
    // asynccmd Other processes, no RunningProtect protection
    chmod(package, 0644); // 0644 : permission
    string doBuf;
    if (installOrUninstall) {
        doBuf = Base::StringFormat("bm install %s -p %s", options, package);
    } else {
        doBuf = Base::StringFormat("bm uninstall %s -n %s", options, package);
    }
    funcAppModFinish = std::bind(&HdcDaemonApp::AsyncInstallFinish, this, std::placeholders::_1, std::placeholders::_2,
                                 std::placeholders::_3);
    if (installOrUninstall) {
        mode = APPMOD_INSTALL;
    } else {
        mode = APPMOD_UNINSTALL;
    }
    asyncCommand.Initial(loopTask, funcAppModFinish);
    asyncCommand.ExecuteCommand(doBuf);
}

void HdcDaemonApp::Sideload(const char *pathOTA)
{
    mode = APPMOD_SIDELOAD;
    LogMsg(MSG_OK, "[placeholders] sideload %s", pathOTA);
    TaskFinish();
    unlink(pathOTA);
}

void HdcDaemonApp::WhenTransferFinish(CtxFile *context)
{
    if (ctxNow.transferConfig.functionName == CMDSTR_APP_SIDELOAD) {
        Sideload(context->localPath.c_str());
    } else if (ctxNow.transferConfig.functionName == CMDSTR_APP_INSTALL) {
        PackageShell(true, context->transferConfig.options.c_str(), context->localPath.c_str());
    } else {
    }
};
}
