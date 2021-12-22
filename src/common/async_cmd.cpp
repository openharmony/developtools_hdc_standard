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
#include "async_cmd.h"

namespace Hdc {
// Do not add thread-specific init op in the following methods as it's running in child thread.
AsyncCmd::AsyncCmd()
{
}

AsyncCmd::~AsyncCmd()
{
    WRITE_LOG(LOG_DEBUG, "~AsyncCmd");
};

bool AsyncCmd::ReadyForRelease() const
{
    if (childShell != nullptr && !childShell->ReadyForRelease()) {
        return false;
    }
    if (refCount != 0) {
        return false;
    }
    if (childShell != nullptr) {
        delete childShell;
    }
    if (fd > 0) {
        close(fd);
    }
    return true;
}

void AsyncCmd::DoRelease()
{
    WRITE_LOG(LOG_DEBUG, "AsyncCmd::DoRelease finish");
    if (childShell != nullptr) {
        childShell->StopWork(false, nullptr);
    }
    if (pid > 0) {
        uv_kill(pid, SIGTERM);
    }
}

bool AsyncCmd::Initial(uv_loop_t *loopIn, const CmdResultCallback callback, uint32_t optionsIn)
{
#if defined _WIN32 || defined HDC_HOST
    WRITE_LOG(LOG_FATAL, "Not support for win32-host");
    return false;
#endif
    loop = loopIn;
    resultCallback = callback;
    options = optionsIn;
    return true;
}

bool AsyncCmd::FinishShellProc(const void *context, const bool result, const string exitMsg)
{
    WRITE_LOG(LOG_DEBUG, "FinishShellProc finish");
    AsyncCmd *thisClass = (AsyncCmd *)context;
    thisClass->resultCallback(true, result, thisClass->cmdResult + exitMsg);
    --thisClass->refCount;
    return true;
};

bool AsyncCmd::ChildReadCallback(const void *context, uint8_t *buf, const int size)
{
    AsyncCmd *thisClass = (AsyncCmd *)context;
    if (thisClass->options & OPTION_COMMAND_ONETIME) {
        string s((char *)buf, size);
        thisClass->cmdResult += s;
        return true;
    }
    string s((char *)buf, size);
    return thisClass->resultCallback(false, 0, s);
};

int AsyncCmd::Popen(string command, bool readWrite, int &pid)
{
#ifdef _WIN32
    return ERR_NO_SUPPORT;
#else
    constexpr uint8_t PIPE_READ = 0;
    constexpr uint8_t PIPE_WRITE = 1;
    pid_t childPid;
    int fd[2];
    pipe(fd);

    if ((childPid = fork()) == -1) {
        return ERR_GENERIC;
    }
    if (childPid == 0) {
        // // ignre signal alarm to avoid usb async-read EINTR?
        struct sigaction action;
        action.sa_handler = SIG_IGN;
        sigemptyset(&action.sa_mask);
        sigaction(SIGALRM, &action, NULL);

        if (readWrite) {
            close(fd[PIPE_READ]);
            dup2(fd[PIPE_WRITE], STDOUT_FILENO);
            dup2(fd[PIPE_WRITE], STDERR_FILENO);
        } else {
            close(fd[PIPE_WRITE]);
            dup2(fd[PIPE_READ], STDIN_FILENO);
        }
        setsid();
        setpgid(childPid, childPid);
        string shellPath = Base::GetShellPath();
        execl(shellPath.c_str(), shellPath.c_str(), "-c", command.c_str(), NULL);
        exit(0);
    } else {
        if (readWrite) {
            close(fd[PIPE_WRITE]);
        } else {
            close(fd[PIPE_READ]);
        }
    }
    pid = childPid;
    if (readWrite) {
        return fd[PIPE_READ];
    } else {
        return fd[PIPE_WRITE];
    }
#endif
}

bool AsyncCmd::ExecuteCommand(const string &command)
{
    string cmd = command;
    Base::Trim(cmd, "\"");
    if ((fd = Popen(cmd, true, pid)) < 0) {
        return false;
    }
    childShell = new HdcFileDescriptor(loop, fd, this, ChildReadCallback, FinishShellProc);
    if (!childShell->StartWork()) {
        return false;
    }
    ++refCount;
    return true;
}
}  // namespace Hdc
