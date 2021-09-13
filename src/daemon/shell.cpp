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
#include <sys/wait.h>

namespace Hdc {
std::mutex HdcShell::mutexPty;

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
    if (fdPTY > 0) {
        close(fdPTY);
    }
    return true;
}

void HdcShell::StopTask()
{
    singalStop = true;
    WRITE_LOG(LOG_DEBUG, "HdcShell::StopTask");
    if (!childReady) {
        return;
    }
    if (childShell) {
        childShell->StopWork(false, nullptr);
    }
    kill(pidShell, SIGKILL);
    int status;
    waitpid(pidShell, &status, 0);
    WRITE_LOG(LOG_DEBUG, "StopTask, kill pidshell:%d", pidShell);
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
                LogMsg(MSG_FAIL, "Shell initialize failed");
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

int HdcShell::ChildForkDo(int pts, const char *cmd, const char *arg0, const char *arg1)
{
    dup2(pts, STDIN_FILENO);
    dup2(pts, STDOUT_FILENO);
    dup2(pts, STDERR_FILENO);
    close(pts);
    string text = "/proc/self/oom_score_adj";
    int fd = 0;
    if ((fd = open(text.c_str(), O_WRONLY)) >= 0) {
        write(fd, "0", 1);
        close(fd);
    }
    char *env = nullptr;
    if ((env = getenv("HOME")) && chdir(env) < 0) {
    }
    execl(cmd, cmd, arg0, arg1, nullptr);
    _Exit(1);
    return 0;
}

int HdcShell::ShellFork(const char *cmd, const char *arg0, const char *arg1)
{
    pid_t pid;
    pid = fork();
    if (pid < 0) {
        WRITE_LOG(LOG_DEBUG, "Fork shell failed:%s", strerror(errno));
        return -4;
    }
    if (pid == 0) {
        HdcShell::mutexPty.unlock();
        setsid();
        close(ptm);
        int pts = 0;
        if ((pts = open(devname, O_RDWR | O_CLOEXEC)) < 0) {
            return -1;
        }
        ChildForkDo(pts, cmd, arg0, arg1);
        // proc finish
    } else {
        return pid;
    }
    return 0;
}

int HdcShell::CreateSubProcessPTY(const char *cmd, const char *arg0, const char *arg1, pid_t *pid)
{
    ptm = open(devPTMX.c_str(), O_RDWR | O_CLOEXEC);
    if (ptm < 0) {
        WRITE_LOG(LOG_DEBUG, "Cannot open ptmx, error:%s", strerror(errno));
        return ERR_FILE_OPEN;
    }
    if (grantpt(ptm) || unlockpt(ptm)) {
        WRITE_LOG(LOG_DEBUG, "Cannot open2 ptmx, error:%s", strerror(errno));
        close(ptm);
        return ERR_API_FAIL;
    }
    if (ptsname_r(ptm, devname, sizeof(devname)) != 0) {
        WRITE_LOG(LOG_DEBUG, "Trouble with  ptmx, error:%s", strerror(errno));
        close(ptm);
        return ERR_API_FAIL;
    }
    *pid = ShellFork(cmd, arg0, arg1);
    return ptm;
}

bool HdcShell::FinishShellProc(const void *context, const bool result, const string exitMsg)
{
    WRITE_LOG(LOG_DEBUG, "FinishShellProc finish");
    HdcShell *thisClass = (HdcShell *)context;
    thisClass->TaskFinish();
    --thisClass->refCount;
    return true;
};

bool HdcShell::ChildReadCallback(const void *context, uint8_t *buf, const int size)
{
    HdcShell *thisClass = (HdcShell *)context;
    return thisClass->SendToAnother(CMD_KERNEL_ECHO_RAW, (uint8_t *)buf, size);
};

int HdcShell::StartShell()
{
    WRITE_LOG(LOG_DEBUG, "StartShell...");
    int ret = 0;
    HdcShell::mutexPty.lock();
    do {
        if ((fdPTY = CreateSubProcessPTY(Base::GetShellPath().c_str(), "-", 0, &pidShell)) < 0) {
            ret = ERR_PROCESS_SUB_FAIL;
            break;
        }
        childShell = new HdcFileDescriptor(loopTask, fdPTY, this, ChildReadCallback, FinishShellProc);
        if (!childShell->StartWork()) {
            ret = ERR_API_FAIL;
            break;
        }
        childReady = true;
        ++refCount;
    } while (false);
    if (ret != RET_SUCCESS) {
        if (pidShell > 0) {
            kill(pidShell, SIGKILL);
        }
        // fdPTY close by ~clase
    }
    HdcShell::mutexPty.unlock();
    return ret;
}
}  // namespace Hdc