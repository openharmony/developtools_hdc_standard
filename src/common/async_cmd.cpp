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
#define PIPE_READ 0
#define PIPE_WRITE 1

namespace Hdc {
// Do not add thread-specific init op in the following methods as it's running in child thread.
AsyncCmd::AsyncCmd()
{
    Base::ZeroStruct(stdinPipe);
    Base::ZeroStruct(stdoutPipe);
    Base::ZeroStruct(stderrPipe);
    Base::ZeroStruct(proc);
    Base::ZeroStruct(procOptions);
    running = false;
    loop = nullptr;
}

AsyncCmd::~AsyncCmd()
{
    WRITE_LOG(LOG_DEBUG, "~AsyncCmd");
};

bool AsyncCmd::ReadyForRelease() const
{
    return !running;
}

// manual stop will not trigger ExitCallback, we call it
void AsyncCmd::DoRelease()
{
    if (hasStop || !running) {
        return;
    }
    hasStop = true;  // must set here to deny repeate release
    uv_process_kill(&proc, SIGKILL);
    WRITE_LOG(LOG_DEBUG, "AsyncCmd::DoRelease finish");
}

void AsyncCmd::ChildReadCallback(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    AsyncCmd *thisClass = (AsyncCmd *)stream->data;
    if (nread <= 0) {  // stdout and stderr
        WRITE_LOG(LOG_DEBUG, "Read ShellChildProcess failed %s", uv_err_name(nread));
    } else {
        if (thisClass->options & OPTION_READBACK_OUT) {
            thisClass->cmdResult = buf->base;
            if (!thisClass->resultCallback(false, 0, thisClass->cmdResult)) {
                uv_process_kill(&thisClass->proc, SIGKILL);
                uv_read_stop(stream);
            }
            thisClass->cmdResult = STRING_EMPTY;
        } else {  // output all when finish
            thisClass->cmdResult += buf->base;
        }
    }
    delete[] buf->base;
}

void AsyncCmd::ExitCallback(uv_process_t *req, int64_t exitStatus, int tersignal)
{
    auto funcReqClose = [](uv_handle_t *handle) -> void {
        AsyncCmd *thisClass = (AsyncCmd *)handle->data;
        if (--thisClass->uvRef == 0) {
            thisClass->running = false;
        }
    };
    AsyncCmd *thisClass = (AsyncCmd *)req->data;
    thisClass->hasStop = true;  // callback maybe call dorelease, so deny repeate ExitCallback

    thisClass->resultCallback(true, exitStatus, thisClass->cmdResult);
    WRITE_LOG(LOG_DEBUG, "AsyncCmd::ExitCallback");
    thisClass->uvRef = 4;
    Base::TryCloseHandle((uv_handle_t *)&thisClass->stdinPipe, true, funcReqClose);
    Base::TryCloseHandle((uv_handle_t *)&thisClass->stdoutPipe, true, funcReqClose);
    Base::TryCloseHandle((uv_handle_t *)&thisClass->stderrPipe, true, funcReqClose);
    Base::TryCloseHandle((uv_handle_t *)req, true, funcReqClose);
    thisClass->cmdResult = STRING_EMPTY;
}

bool AsyncCmd::Initial(uv_loop_t *loopIn, const CmdResultCallback callback, uint32_t optionsIn)
{
    if (running) {
        return false;
    }
    loop = loopIn;
    resultCallback = callback;
    if (StartProcess() < 0) {
        return false;
    }
    options = optionsIn;
    return true;
}

bool AsyncCmd::ExecuteCommand(const string &command) const
{
    string cmd = command;
    if (options & OPTION_APPEND_NEWLINE) {
        cmd += "\n";
    }
    if (options & OPTION_COMMAND_ONETIME) {
        cmd += "exit\n";
    }
    Base::SendToStream((uv_stream_t *)&stdinPipe, (uint8_t *)cmd.c_str(), cmd.size() + 1);
    return true;
}

int AsyncCmd::StartProcess()
{
    constexpr auto countStdIOCount = 3;
    char *ppShellArgs[2];
    string shellPath = Base::GetShellPath();
    uv_stdio_container_t stdioShellProc[3];
    while (true) {
        uv_pipe_init(loop, &stdinPipe, 1);
        uv_pipe_init(loop, &stdoutPipe, 1);
        uv_pipe_init(loop, &stderrPipe, 1);
        stdinPipe.data = this;
        stdoutPipe.data = this;
        stderrPipe.data = this;
        procOptions.stdio = stdioShellProc;
        procOptions.stdio[STDIN_FILENO].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
        procOptions.stdio[STDIN_FILENO].data.stream = (uv_stream_t *)&stdinPipe;
        procOptions.stdio[STDOUT_FILENO].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        procOptions.stdio[STDOUT_FILENO].data.stream = (uv_stream_t *)&stdoutPipe;
        procOptions.stdio[STDERR_FILENO].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
        procOptions.stdio[STDERR_FILENO].data.stream = (uv_stream_t *)&stderrPipe;
        procOptions.stdio_count = countStdIOCount;
        procOptions.file = shellPath.c_str();
        procOptions.exit_cb = ExitCallback;
        ppShellArgs[0] = (char *)shellPath.c_str();
        ppShellArgs[1] = nullptr;
        procOptions.args = ppShellArgs;
        proc.data = this;

        if (uv_spawn(loop, &proc, &procOptions)) {
            WRITE_LOG(LOG_FATAL, "Spawn shell process failed");
            break;
        }
        if (uv_read_start((uv_stream_t *)&stdoutPipe, Base::AllocBufferCallback, ChildReadCallback)) {
            break;
        }
        if (uv_read_start((uv_stream_t *)&stderrPipe, Base::AllocBufferCallback, ChildReadCallback)) {
            break;
        }
        running = true;
        break;
    }
    if (!running) {
        // failed
        resultCallback(true, -1, "Start process failed");
        return -1;
    } else {
        return 0;
    }
}
}  // namespace Hdc