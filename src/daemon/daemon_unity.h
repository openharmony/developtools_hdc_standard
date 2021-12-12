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
#ifndef HDC_DAEMON_UNITY_H
#define HDC_DAEMON_UNITY_H
#include "daemon_common.h"

namespace Hdc {
class HdcDaemonUnity : public HdcTaskBase {
public:
    HdcDaemonUnity(HTaskInfo hTaskInfo);
    virtual ~HdcDaemonUnity();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);
    void StopTask();
    bool ReadyForRelease();

private:
    static void OnFdRead(uv_fs_t *req);
    int ExecuteShell(const char *shellCommand);
    bool FindMountDeviceByPath(const char *toQuery, char *dev);
    bool RemountPartition(const char *dir);
    bool RemountDevice();
    bool RebootDevice(const string &cmd);
    bool SetDeviceRunMode(void *daemonIn, const char *cmd);
    bool GetHiLog(const char *cmd);
    bool ListJdwpProcess(void *daemonIn);
    bool AsyncCmdOut(bool finish, int64_t exitStatus, const string result);
    bool TrackJdwpProcess(void *daemonIn);

    AsyncCmd asyncCommand;
    uint16_t currentDataCommand;
#ifdef UNIT_TEST
    int countUt = 0;
#endif
};
}  // namespace Hdc
#endif  // HDC_DAEMON_UNITY_H
