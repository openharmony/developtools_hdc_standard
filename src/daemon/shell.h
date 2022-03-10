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
#ifndef HDC_SHELL_H
#define HDC_SHELL_H
#include "daemon_common.h"
#include <mutex>

namespace Hdc {
class HdcShell : public HdcTaskBase {
public:
    HdcShell(HTaskInfoPtr hTaskInfo);
    virtual ~HdcShell();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);
    void StopTask();
    bool ReadyForRelease();

private:
    static bool FinishShellProc(const void *context, const bool result, const string exitMsg);
    static bool ChildReadCallback(const void *context, uint8_t *buf, const int size);
    int StartShell();
    int CreateSubProcessPTY(const char *cmd, const char *arg0, const char *arg1, pid_t *pid);
    int ChildForkDo(int pts, const char *cmd, const char *arg0, const char *arg1);
    bool SpecialSignal(uint8_t ch);
    int ShellFork(const char *cmd, const char *arg0, const char *arg1);

    HdcFileDescriptor *childShell;
    pid_t pidShell = 0;
    int fdPTY;
    int ptm = 0;
    const string devPTMX = "/dev/ptmx";
    static std::mutex mutexPty;
    char devname[BUF_SIZE_SMALL] = "";
};
}  // namespace Hdc
#endif