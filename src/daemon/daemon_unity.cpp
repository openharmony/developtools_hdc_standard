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

namespace Hdc {
HdcDaemonUnity::HdcDaemonUnity(HTaskInfo hTaskInfo)
    : HdcTaskBase(hTaskInfo)
{
    Base::ZeroStruct(opContext);
    opContext.thisClass = this;
    opContext.dataCommand = CMD_KERNEL_ECHO_RAW;  // Default output to shelldata
}

HdcDaemonUnity::~HdcDaemonUnity()
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUnity::~HdcDaemonUnity finish");
}

void HdcDaemonUnity::StopTask()
{
    singalStop = true;
    ClearContext(&opContext);
};

void HdcDaemonUnity::ClearContext(ContextUnity *ctx)
{
    if (ctx->hasCleared) {
        return;
    }
    switch (ctx->typeUnity) {
        case UNITY_SHELL_EXECUTE: {
            if (ctx->fpOpen) {
                pclose(ctx->fpOpen);
                ctx->fpOpen = nullptr;
            }
            break;
        }
        default:
            break;
    }
    ctx->hasCleared = true;
}

void HdcDaemonUnity::OnFdRead(uv_fs_t *req)
{
    CtxUnityIO *ctxIO = static_cast<CtxUnityIO *>(req->data);
    ContextUnity *ctx = static_cast<ContextUnity *>(ctxIO->context);
    HdcDaemonUnity *thisClass = ctx->thisClass;
    thisClass->refCount--;
    uint8_t *buf = ctxIO->bufIO;
    bool readContinue = false;
    while (true) {
        if (thisClass->singalStop || req->result <= 0) {
            break;
        }
        if (!thisClass->SendToAnother(thisClass->opContext.dataCommand, (uint8_t *)buf, req->result)) {
            break;
        }
        if (thisClass->LoopFdRead(&thisClass->opContext) < 0) {
            break;
        }
        readContinue = true;
        break;
    }
    delete[] buf;
    uv_fs_req_cleanup(req);
    delete req;
    delete ctxIO;
    if (!readContinue) {
        thisClass->ClearContext(ctx);
        thisClass->TaskFinish();
    }
}

// Consider merage file_descriptor class
int HdcDaemonUnity::LoopFdRead(ContextUnity *ctx)
{
    uv_buf_t iov;
    int readMax = Base::GetMaxBufSize();
    CtxUnityIO *contextIO = new CtxUnityIO();
    uint8_t *buf = new uint8_t[readMax]();
    uv_fs_t *req = new uv_fs_t();
    if (!contextIO || !buf || !req) {
        if (contextIO) {
            delete contextIO;
        }
        if (buf) {
            delete[] buf;
        }
        if (req) {
            delete req;
        }
        WRITE_LOG(LOG_WARN, "Memory alloc failed");
        return -1;
    }
    contextIO->bufIO = buf;
    contextIO->context = ctx;
    req->data = contextIO;
    refCount++;

    iov = uv_buf_init((char *)buf, readMax);
    uv_fs_read(loopTask, req, ctx->fd, &iov, 1, -1, OnFdRead);
    return 0;
}

int HdcDaemonUnity::ExecuteShell(const char *shellCommand)
{
    string sUTPath;
    if ((opContext.fpOpen = popen(shellCommand, "r")) == nullptr) {
        goto FAILED;
    }
    opContext.fd = fileno(opContext.fpOpen);
    opContext.typeUnity = UNITY_SHELL_EXECUTE;
    while (true) {
#ifdef UNIT_TEST  // UV_FS_READ can not respond to read file content in time when the unit test
        uint8_t readBuf[BUF_SIZE_DEFAULT] = "";
        int bytesIO = 0;
        bytesIO = fread(readBuf, 1, BUF_SIZE_DEFAULT, opContext.fpOpen);
        if (bytesIO <= 0 || !SendToAnother(opContext.dataCommand, readBuf, bytesIO)) {
            break;
        }
        Base::WriteBinFile((UT_TMP_PATH + "/execute.result").c_str(), readBuf, bytesIO, false);
#else
        if (LoopFdRead(&opContext) < 0) {
            break;
        }
        return 0;
#endif
    }
FAILED:
    TaskFinish();
    WRITE_LOG(LOG_DEBUG, "Shell finish");
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
        if (res == 4 && (strcmp(toQuery, dir) == 0)) {
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

    if (!FindMountDeviceByPath(dir, dev) || strlen(dev) < 4) {
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
    // Test first write root directory
    const char cmd[] = "mount -o remount,rw /";
    if (-1 == system(cmd)) {
        LogMsg(MSG_FAIL, "Mount failed");
        return false;
    }
    struct stat info;
    if (!lstat("/vendor", &info) && (info.st_mode & S_IFMT) == S_IFDIR) {
        // has vendor
        if (!RemountPartition("/vendor")) {
            return false;
        }
    }
    LogMsg(MSG_OK, "Mount finish");
    return true;
}

bool HdcDaemonUnity::RebootDevice(const uint8_t *cmd, const int cmdSize)
{
    sync();
    string propertyVal;
    int ret = -1;
    if (!cmdSize) {
        propertyVal = "reboot";
    } else {
        propertyVal = Base::StringFormat("reboot,%s", cmd);
    }
    if (ret > 0) {
        Base::SetHdcProperty(rebootProperty.c_str(), propertyVal.c_str());
        return true;
    } else {
        return false;
    }
}

bool HdcDaemonUnity::SetDeviceRunMode(void *daemonIn, const char *cmd)
{
    HdcDaemon *daemon = (HdcDaemon *)daemonIn;
    WRITE_LOG(LOG_DEBUG, "Set run mode:%s", cmd);
    if (!strcmp(CMDSTR_TMODE_USB.c_str(), cmd)) {
        Base::SetHdcProperty("persist.hdc.mode", CMDSTR_TMODE_USB.c_str());
    } else if (!strncmp("port", cmd, 4)) {
        Base::SetHdcProperty("persist.hdc.mode", CMDSTR_TMODE_TCP.c_str());
        if (!strncmp("port ", cmd, 5)) {
            const char *port = cmd + 5;
            Base::SetHdcProperty("persist.hdc.port", port);
        }
    } else {
        LogMsg(MSG_FAIL, "Unknow command");
        return false;
    }
    // shutdown
    daemon->StopDaemon(true);
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

bool HdcDaemonUnity::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    bool ret = true;
    HdcDaemon *daemon = (HdcDaemon *)taskInfo->ownerSessionClass;
    // Both are not executed, do not need to be detected 'childReady'
    switch (command) {
        case CMD_UNITY_EXECUTE: {
            ExecuteShell((char *)payload);
            break;
        }
        case CMD_UNITY_REMOUNT: {
            ret = false;
            RemountDevice();
            break;
        }
        case CMD_UNITY_REBOOT: {
            ret = false;
            RebootDevice(payload, payloadSize);
            break;
        }
        case CMD_UNITY_RUNMODE: {
            ret = false;
            SetDeviceRunMode(daemon, (const char *)payload);
            break;
        }
        case CMD_UNITY_HILOG: {
            GetHiLog((const char *)payload);
            break;
        }
        case CMD_UNITY_ROOTRUN: {
            ret = false;
            if (payload && !strcmp((char *)payload, "r")) {
                Base::SetHdcProperty("persist.hdc.root", "0");
            }
            Base::SetHdcProperty("persist.hdc.root", "1");
            daemon->StopDaemon(true);
            break;
        }
        case CMD_UNITY_TERMINATE: {
            daemon->StopDaemon(!strcmp((char *)payload, "1"));
            break;
        }
        case CMD_UNITY_BUGREPORT_INIT: {
            opContext.dataCommand = CMD_UNITY_BUGREPORT_DATA;
            ExecuteShell((char *)CMDSTR_BUGREPORT.c_str());
            break;
        }
        case CMD_UNITY_JPID: {
            ret = false;
            ListJdwpProcess(daemon);
            break;
        }
        default:
            break;
    }
    return ret;
};
}  // namespace Hdc
