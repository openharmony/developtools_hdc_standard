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
#ifndef HDC_ASYNC_CMD_H
#define HDC_ASYNC_CMD_H
#include "common.h"

namespace Hdc {
class AsyncCmd {
public:
    AsyncCmd();
    virtual ~AsyncCmd();

    using CmdResultCallback = std::function<void(bool, const string)>;
    // uv_loop_t loop is given to uv_spawn, which can't be const
    bool Initial(uv_loop_t *loopIn, const CmdResultCallback callback);
    void DoRelease();  // Release process resources
    bool ExecuteCommand(const string &command, bool once = true) const;
    bool ReadyForRelease() const;

private:
    uv_loop_t *loop;
    int StartProcess();
    // uv_read_cb callback 1st parameter can't be changed, const can't be added
    static void ChildReadCallback(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    // uv_exit_cb callback 1st parameter can't be changed, const can't be added
    static void ExitCallback(uv_process_t *req, int64_t exitStatus, int tersignal);

    // loop is given to uv_spawn, which can't be const
    uv_pipe_t stdinPipe;
    uv_pipe_t stdoutPipe;
    uv_pipe_t stderrPipe;
    uv_process_t proc;
    uv_process_options_t procOptions;
    CmdResultCallback resultCallback;
    string cmdResult;
    bool running;
};
}  // namespace Hdc
#endif