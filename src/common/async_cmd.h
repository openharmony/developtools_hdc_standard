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
    enum AsyncCmdOption {
        OPTION_COMMAND_ONETIME = 1,
        USB_OPTION_RESERVE2 = 2,
        OPTION_READBACK_OUT = 4,  // deprecated, remove it later
        USB_OPTION_RESERVE8 = 8,
    };
    // 1)is finish 2)exitStatus 3)resultString(maybe empty)
    using CmdResultCallback = std::function<bool(bool, int64_t, const string)>;
    // deprecated, remove it later
    static uint32_t GetDefaultOption()
    {
        return OPTION_COMMAND_ONETIME;
    }
    // uv_loop_t loop is given to uv_spawn, which can't be const
    bool Initial(uv_loop_t *loopIn, const CmdResultCallback callback, uint32_t optionsIn = 0);
    void DoRelease();  // Release process resources
    bool ExecuteCommand(const string &command);
    bool ReadyForRelease();

private:
    static bool FinishShellProc(const void *context, const bool result, const string exitMsg);
    static bool ChildReadCallback(const void *context, uint8_t *buf, const int size);
    int Popen(string command, bool readWrite, int &pid);

    uint32_t options = 0;
    int fd = 0;
    int pid = 0;
    HdcFileDescriptor *childShell = nullptr;
    uint32_t refCount = 0;
    CmdResultCallback resultCallback;
    uv_loop_t *loop = nullptr;
    string cmdResult;
};
}  // namespace Hdc
#endif
