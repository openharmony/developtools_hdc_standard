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
#ifndef HDC_RUNTIME_FRAME_H
#define HDC_RUNTIME_FRAME_H
#include "ut_common.h"

namespace HdcTest {
class FrameRuntime {
public:
    enum UT_MOD_TYPE {
        UT_MOD_BASE,
        UT_MOD_SHELL,
        UT_MOD_SHELL_INTERACTIVE,
        UT_MOD_FILE,
        UT_MOD_APP,
    };
    FrameRuntime();
    ~FrameRuntime();
    bool Initial(bool bConnectToDaemon);
    bool CheckEntry(UT_MOD_TYPE type);

private:
    static void DoCheck(uv_idle_t *handle);
    static void StartServer(void *arg);
    static void StartDaemon(void *arg);
    static void CheckStopServer(uv_idle_t *arg);
    static void CheckStopDaemon(uv_idle_t *arg);
    static void FinishRemoveIdle(uv_handle_t *handle)
    {
        delete (uv_idle_t *)handle;
    }

    bool ResetTmpFolder();
    void WorkerPendding();
    bool TestShellExecute();
    bool TestShellInterActive();
    bool TestBaseCommand();
    bool TestFileCommand();
    int InnerCall(int method);

    bool serverRunning;
    bool daemonRunning;
    bool bCheckResult;
    bool checkFinish;
    bool hashInitialize;
    UT_MOD_TYPE checkType;
    uv_loop_t loopMain;
    uv_idle_t checkServerStop;
    uv_idle_t checkDaemonStop;
    Hdc::HdcServer *server;
    Hdc::HdcDaemon *daemon;
    uv_thread_t tdServer;
    uv_thread_t tdDaemon;
};
}  // namespace HdcTest
#endif  // HDC_FUNC_TEST_H