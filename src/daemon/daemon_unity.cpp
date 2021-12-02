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
#include "daemon_unity.h"
#include <sys/mount.h>
#ifdef __MUSL__
extern "C" {
#include "init_reboot.h"
}
#endif

namespace Hdc {
HdcDaemonUnity::HdcDaemonUnity(HTaskInfo hTaskInfo)
    : HdcTaskBase(hTaskInfo)
{
    currentDataCommand = CMD_KERNEL_ECHO_RAW;  // Default output to shelldata
}

HdcDaemonUnity::~HdcDaemonUnity()
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUnity::~HdcDaemonUnity finish");
}

void HdcDaemonUnity::StopTask()
{
    asyncCommand.DoRelease();
};

bool HdcDaemonUnity::ReadyForRelease()
{
    if (!HdcTaskBase::ReadyForRelease() || !asyncCommand.ReadyForRelease()) {
        return false;
    }
    return true;
}

bool HdcDaemonUnity::AsyncCmdOut(bool finish, int64_t exitStatus, const string result)
{
#ifdef UNIT_TEST
    Base::WriteBinFile((UT_TMP_PATH + "/execute.result").c_str(), (uint8_t *)result.c_str(), result.size(),
                       countUt++ == 0);
#endif
    bool ret = false;
    bool wantFinish = false;
    do {
        if (finish) {
            wantFinish = true;
            ret = true;
            --refCount;
            break;
        }
        if (!SendToAnother(currentDataCommand, (uint8_t *)result.c_str(), result.size())) {
            break;
        }
        ret = true;
    } while (false);
    if (wantFinish) {
        TaskFinish();
    }
    return ret;
}

int HdcDaemonUnity::ExecuteShell(const char *shellCommand)
{
    do {
        AsyncCmd::CmdResultCallback funcResultOutput;
        funcResultOutput = std::bind(&HdcDaemonUnity::AsyncCmdOut, this, std::placeholders::_1, std::placeholders::_2,
                                     std::placeholders::_3);
        if (!asyncCommand.Initial(loopTask, funcResultOutput,
                                  asyncCommand.GetDefaultOption() | asyncCommand.OPTION_READBACK_OUT)) {
            break;
        }
        asyncCommand.ExecuteCommand(shellCommand);
        ++refCount;
        return RET_SUCCESS;
    } while (false);

    TaskFinish();
    WRITE_LOG(LOG_DEBUG, "Shell failed finish");
    return -1;
}

bool HdcDaemonUnity::FindMountDeviceByPath(const char *toQuery, char *dev)
{
    int fd;
    int res;
    char *token = nullptr;
    const char delims[] = "\n";
    char buf[BUF_SIZE_DEFAULT2];

    fd = open("/proc/mounts", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    buf[sizeof(buf) - 1] = '\0';
    token = strtok(buf, delims);

    while (token) {
        char dir[BUF_SIZE_SMALL] = "";
        int freq;
        int passnno;
        // clang-format off
        res = sscanf_s(token, "%255s %255s %*s %*s %d %d\n", dev, BUF_SIZE_SMALL - 1,
                       dir, BUF_SIZE_SMALL - 1, &freq, &passnno);
        // clang-format on
        dev[BUF_SIZE_SMALL - 1] = '\0';
        dir[BUF_SIZE_SMALL - 1] = '\0';
        if (res == 4 && (strcmp(toQuery, dir) == 0)) { // 4 : The correct number of parameters
            return true;
        }
        token = strtok(nullptr, delims);
    }
    return false;
}

bool HdcDaemonUnity::RemountPartition(const char *dir)
{
    int fd;
    int off = 0;
    char dev[BUF_SIZE_SMALL] = "";

    if (!FindMountDeviceByPath(dir, dev) || strlen(dev) < 4) { // 4 : file count
        WRITE_LOG(LOG_DEBUG, "FindMountDeviceByPath failed");
        return false;
    }

    if ((fd = open(dev, O_RDONLY | O_CLOEXEC)) < 0) {
        WRITE_LOG(LOG_DEBUG, "Open device:%s failed， error:%d", dev, errno);
        return false;
    }
    ioctl(fd, BLKROSET, &off);
    close(fd);

    if (mount(dev, dir, "none", MS_REMOUNT, nullptr) < 0) {
        WRITE_LOG(LOG_DEBUG, "Mount device failed");
        return false;
    }
    return true;
}

bool HdcDaemonUnity::RemountDevice()
{
    if (getuid() != 0) {
        LogMsg(MSG_FAIL, "Opearte need running as root");
        return false;
    }
    struct stat info;
    if (!lstat("/vendor", &info) && (info.st_mode & S_IFMT) == S_IFDIR) {
        // has vendor
        if (!RemountPartition("/vendor")) {
            LogMsg(MSG_FAIL, "Mount failed");
            return false;
        }
    }
    if (!lstat("/data", &info) && (info.st_mode & S_IFMT) == S_IFDIR) {
        if (!RemountPartition("/data")) {
            return false;
        }
    }
    LogMsg(MSG_OK, "Mount finish");
    return true;
}

bool HdcDaemonUnity::RebootDevice(const string &cmd)
{
    sync();
#ifndef __MUSL__
    string propertyVal;
    if (!cmd.size()) {
        propertyVal = "reboot";
    } else {
        propertyVal = Base::StringFormat("reboot,%s", cmd.c_str());
    }
    return Base::SetHdcProperty(rebootProperty.c_str(), propertyVal.c_str());
#else
    if ((cmd == "recovery") || (cmd == "bootloader")) {
        return DoReboot("updater");
    }
    return DoReboot("");
#endif
}

bool HdcDaemonUnity::SetDeviceRunMode(void *daemonIn, const char *cmd)
{
    HdcDaemon *daemon = (HdcDaemon *)daemonIn;
    WRITE_LOG(LOG_DEBUG, "Set run mode:%s", cmd);
    if (!strcmp(CMDSTR_TMODE_USB.c_str(), cmd)) {
        Base::SetHdcProperty("persist.hdc.mode", CMDSTR_TMODE_USB.c_str());
    } else if (!strncmp("port", cmd, strlen("port"))) {
        Base::SetHdcProperty("persist.hdc.mode", CMDSTR_TMODE_TCP.c_str());
        if (!strncmp("port ", cmd, strlen("port "))) {
            const char *port = cmd + 5;
            Base::SetHdcProperty("persist.hdc.port", port);
        }
    } else {
        LogMsg(MSG_FAIL, "Unknow command");
        return false;
    }
    // shutdown
    daemon->PostStopInstanceMessage(true);
    LogMsg(MSG_OK, "Set device run mode successful.");
    return true;
}

inline bool HdcDaemonUnity::GetHiLog(const char *cmd)
{
    string cmdDo = "hilog";
    if (cmd && !strcmp((char *)cmd, "v")) {
        cmdDo += " -v long";
    }
    ExecuteShell(cmdDo.c_str());
    return true;
}

inline bool HdcDaemonUnity::ListJdwpProcess(void *daemonIn)
{
    HdcDaemon *daemon = (HdcDaemon *)daemonIn;
    string result = ((HdcJdwp *)daemon->clsJdwp)->GetProcessList();
    if (!result.size()) {
        result = EMPTY_ECHO;
    } else {
        result.erase(result.end() - 1);  // remove tail \n
    }
    LogMsg(MSG_OK, result.c_str());
    return true;
}

inline bool HdcDaemonUnity::TrackJdwpProcess(void *daemonIn)
{
    HdcDaemon *daemon = (HdcDaemon *)daemonIn;
    if (!(((HdcJdwp *)daemon->clsJdwp)->CreateJdwpTracker(taskInfo))) {
        string result = MESSAGE_FAIL;
        LogMsg(MSG_OK, result.c_str());
        return false;
    }
    return true;
}

bool HdcDaemonUnity::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    bool ret = true;
    HdcDaemon *daemon = (HdcDaemon *)taskInfo->ownerSessionClass;
    // Both are not executed, do not need to be detected 'childReady'
    string strPayload = string((char *)payload, payloadSize);
    switch (command) {
        case CMD_UNITY_EXECUTE: {
            ExecuteShell((char *)strPayload.c_str());
            break;
        }
        case CMD_UNITY_REMOUNT: {
            ret = false;
            RemountDevice();
            break;
        }
        case CMD_UNITY_REBOOT: {
            ret = false;
            RebootDevice(strPayload);
            break;
        }
        case CMD_UNITY_RUNMODE: {
            ret = false;
            SetDeviceRunMode(daemon, strPayload.c_str());
            break;
        }
        case CMD_UNITY_HILOG: {
            GetHiLog(strPayload.c_str());
            break;
        }
        case CMD_UNITY_ROOTRUN: {
            ret = false;
            if (payloadSize != 0 && !strcmp((char *)strPayload.c_str(), "r")) {
                Base::SetHdcProperty("persist.hdc.root", "0");
            } else {
                Base::SetHdcProperty("persist.hdc.root", "1");
            }
            daemon->PostStopInstanceMessage(true);
            break;
        }
        case CMD_UNITY_TERMINATE: {
            daemon->PostStopInstanceMessage(!strcmp((char *)strPayload.c_str(), "1"));
            break;
        }
        case CMD_UNITY_BUGREPORT_INIT: {
            currentDataCommand = CMD_UNITY_BUGREPORT_DATA;
            ExecuteShell((char *)CMDSTR_BUGREPORT.c_str());
            break;
        }
        case CMD_UNITY_JPID: {
            ret = false;
            ListJdwpProcess(daemon);
            break;
        }
        case CMD_TRACK_JPID: {
            if (!TrackJdwpProcess(daemon)) {
                ret = false;
            }
            break;
        }
        default:
            break;
    }
    return ret;
};
}  // namespace Hdc
