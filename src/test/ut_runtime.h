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
#ifndef HDC_UT_RUNTIME_H
#define HDC_UT_RUNTIME_H
#include "ut_common.h"

namespace HdcTest {
class Runtime {
public:
    enum UtModType {
        UT_MOD_BASE,
        UT_MOD_SHELL,
        UT_MOD_SHELL_INTERACTIVE,
        UT_MOD_FILE,
        UT_MOD_FORWARD,
        UT_MOD_APP,
    };
    Runtime();
    ~Runtime();
    bool Initial(bool bConnectToDaemonIn);
    bool CheckEntry(UtModType type);

    bool ResetUtTmpFolder();
    bool ResetUtTmpFile(string file);
    int InnerCall(int method);
    uv_loop_t *GetRuntimeLoop()
    {
        return &loopMain;
    };

private:
    static void DoCheck(uv_timer_t *handle);
    static void StartServer(uv_work_t *arg);
    static void StartDaemon(uv_work_t *arg);
    static void CheckStopServer(uv_idle_t *arg);
    static void CheckStopDaemon(uv_idle_t *arg);
    void WorkerPendding();
    int CheckServerDaemonReady();

    bool serverRunning;
    bool daemonRunning;
    bool bCheckResult;
    bool checkFinish;
    bool hashInitialize;
    UtModType checkType;
    uv_loop_t loopMain;
    uv_idle_t checkServerStop;
    uv_idle_t checkDaemonStop;
    Hdc::HdcServer *server;
    void *daemon;  // Hdc::HdcDaemon *
    uint8_t waitServerDaemonReadyCount = 0;
    bool bConnectToDaemon = false;
};
}  // namespace HdcTest
#endif  // HDC_FUNC_TEST_H