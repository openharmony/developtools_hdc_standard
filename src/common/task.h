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
#ifndef HDC_TASK_H
#define HDC_TASK_H
#include "common.h"

namespace Hdc {
// Only allow inheritance
class HdcTaskBase {
public:
    HdcTaskBase(HTaskInfo hTaskInfo);
    virtual ~HdcTaskBase();
    virtual bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
    {
        return true;
    }
    // The following two functions are used to clean up. To ensure that subclasses are safely cleaned, each subclass is
    // directly instantified of these two virtual functions.
    virtual void StopTask()
    {
        singalStop = false;
    }
    bool ReadyForRelease();
    void TaskFinish();

protected:                                                                        // D/S==daemon/server
    bool SendToAnother(const uint16_t command, uint8_t *bufPtr, const int size);  // D / S corresponds to the Task class
    void LogMsg(MessageLevel level, const char *msg, ...);                        // D / S log Send to Client
    bool ServerCommand(const uint16_t command, uint8_t *bufPtr, const int size);  // D / s command is sent to Server
    int ThreadCtrlCommunicate(const uint8_t *bufPtr, const int size);             // main thread and session thread

    uv_loop_t *loopTask;  // childuv pointer
    void *clsSession;
    // Task has stopped running. When Task is officially running, set True as soon as possible, set FALSE after the last
    // step, when the value is false, the Task class will be destructured as soon as possible
    bool runningProtect;  // [deprecated]will be remove, please use refCount
    bool childReady;      // Subcompulents have been prepared
    bool singalStop;      // Request stop signal
    HTaskInfo taskInfo;
    uint32_t refCount;

private:
};
}  // namespace Hdc

#endif