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
#include "shell.h"

namespace Hdc {
HdcShell::HdcShell(HTaskInfo hTaskInfo)
    : HdcTaskBase(hTaskInfo)
{
    childShell = nullptr;
    fdPTY = 0;
}

HdcShell::~HdcShell()
{
    WRITE_LOG(LOG_DEBUG, "HdcShell deinit");
};

bool HdcShell::ReadyForRelease()
{
    if (!HdcTaskBase::ReadyForRelease()) {
        return false;
    }
    if (!childReady) {
        return true;
    }
    if (!childShell->ReadyForRelease()) {
        return false;
    }
    delete childShell;
    childShell = nullptr;
    return true;
}

void HdcShell::StopTask()
{
    WRITE_LOG(LOG_DEBUG, "HdcShell::StopTask");
    if (!childReady) {
        return;
    }
    if (childShell) {
        childShell->StopWork();
    }
    close(fdPTY);
    kill(pidShell, SIGKILL);
    runningProtect = false;
};

bool HdcShell::SpecialSignal(uint8_t ch)
{
    const uint8_t TXT_SIGNAL_ETX = 0x3;
    bool ret = true;
    switch (ch) {
        case TXT_SIGNAL_ETX: {  // Ctrl+C
            pid_t tpgid = tcgetpgrp(fdPTY);
            kill(tpgid, SIGINT);
            break;
        }
        default:
            ret = false;
            break;
    }
    return ret;
}

bool HdcShell::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    switch (command) {
        case CMD_SHELL_INIT: {  // initial
            if (StartShell()) {
                const string echo = "Shell not running";
                SendToAnother(CMD_KERNEL_ECHO_RAW, (uint8_t *)echo.c_str(), echo.size());
            }
            break;
        }
        case CMD_SHELL_DATA:
            if (!childReady) {
                WRITE_LOG(LOG_DEBUG, "Shell not running");
                return false;
            }
            if (payloadSize == 1 && SpecialSignal(payload[0])) {
            } else {
                childShell->Write(payload, payloadSize);
            }
            break;
        default:
            break;
    }
    return true;
}

int HdcShell::ChildForkDo(const char *devname, int ptm, const char *cmd, const char *arg0, const char *arg1)
{
    setsid();
    int pts = open(devname, O_RDWR | O_CLOEXEC);
    if (pts < 0) {
        return -1;
    }
    dup2(pts, STDIN_FILENO);
    dup2(pts, STDOUT_FILENO);
    dup2(pts, STDERR_FILENO);
    close(pts);
    close(ptm);

    string text = Base::StringFormat("/proc/%d/oom_score_adj", getpid());
    int fd = 0;
    if ((fd = open(text.c_str(), O_WRONLY)) >= 0) {
        write(fd, "0", 1);
        close(fd);
    }
    char *env = nullptr;
    if ((env = getenv("HOME")) && chdir(env) < 0) {
    }
    execl(cmd, cmd, arg0, arg1, nullptr);
    return 0;
}

int HdcShell::CreateSubProcessPTY(const char *cmd, const char *arg0, const char *arg1, pid_t *pid)
{
    char devname[BUF_SIZE_TINY];
    int ptm = open(devPTMX.c_str(), O_RDWR | O_CLOEXEC);
    if (ptm < 0) {
        WRITE_LOG(LOG_DEBUG, "Cannot open ptmx, error:%s", strerror(errno));
        return -1;
    }
    if (grantpt(ptm) || unlockpt(ptm)) {
        WRITE_LOG(LOG_DEBUG, "Cannot open2 ptmx, error:%s", strerror(errno));
        close(ptm);
        return -2;
    }
    fcntl(ptm, F_SETFD, FD_CLOEXEC);
    if (ptsname_r(ptm, devname, sizeof(devname)) != 0) {
        WRITE_LOG(LOG_DEBUG, "Trouble with  ptmx, error:%s", strerror(errno));
        close(ptm);
        return -3;
    }
    *pid = fork();
    if (*pid < 0) {
        WRITE_LOG(LOG_DEBUG, "Fork shell failed:%s", strerror(errno));
        close(ptm);
        return -4;
    }
    if (*pid == 0) {
        int childRet = ChildForkDo(devname, ptm, cmd, arg0, arg1);
        exit(childRet);
    } else {
        return ptm;
    }
}

bool HdcShell::FinishShellProc(const void *context, const bool result, const string exitMsg)
{
    HdcShell *thisClass = (HdcShell *)context;
    thisClass->TaskFinish();
    WRITE_LOG(LOG_DEBUG, "FinishShellProc finish");
    return true;
};

bool HdcShell::ChildReadCallback(const void *context, uint8_t *buf, const int size)
{
    HdcShell *thisClass = (HdcShell *)context;
    if (!thisClass->SendToAnother(CMD_KERNEL_ECHO_RAW, (uint8_t *)buf, size)) {
        thisClass->TaskFinish();
    }
    return true;
};

int HdcShell::StartShell()
{
    WRITE_LOG(LOG_DEBUG, "StartShell...");
    fdPTY = CreateSubProcessPTY(Base::GetShellPath().c_str(), "-", 0, &pidShell);
    childShell = new HdcFileDescriptor(loopTask, fdPTY, this, ChildReadCallback, FinishShellProc);
    childShell->StartWork();
    childReady = true;
    runningProtect = true;
    return 0;
}
}  // namespace Hdc