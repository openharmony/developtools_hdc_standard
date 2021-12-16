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
 *
 */
// The process is used to test jdwp.
// jpid List pids of processes hosting a JDWP transport
#include "define.h"
#include "HdcJdwpSimulator.h"
using namespace OHOS::HiviewDFX;
static constexpr HiLogLabel LABEL = {LOG_CORE, 0, "JDWP_TEST"};
static uv_loop_t loopMain;
static HdcJdwpSimulator *clsHdcJdwpSimulator = nullptr;

static void PrintMessage(const char *fmt, ...)
{
    int ret = 0;
    va_list ap;
    va_start(ap, fmt);
    ret = vfprintf(stdout, fmt, ap);
    ret = fprintf(stdout, "\n");
    va_end(ap);
}

static void TryCloseHandle(const uv_handle_t *handle, bool alwaysCallback,
                           uv_close_cb closeCallBack)
{
    bool hasCallClose = false;
    if (handle->loop && !uv_is_closing(handle)) {
        uv_close((uv_handle_t *)handle, closeCallBack);
        hasCallClose = true;
    }
    if (!hasCallClose && alwaysCallback) {
        closeCallBack((uv_handle_t *)handle);
    }
}

static void TryCloseHandle(const uv_handle_t *handle, uv_close_cb closeCallBack)
{
    TryCloseHandle(handle, false, closeCallBack);
}

static void TryCloseHandle(const uv_handle_t *handle)
{
    TryCloseHandle(handle, nullptr);
}

static bool TryCloseLoop(uv_loop_t *ptrLoop, const char *callerName)
{
    uint8_t closeRetry = 0;
    bool ret = false;
    constexpr int maxRetry = 3;
    constexpr int maxHandle = 2;
    for (closeRetry = 0; closeRetry < maxRetry; ++closeRetry) {
        if (uv_loop_close(ptrLoop) == UV_EBUSY) {
            if (closeRetry > maxRetry) {
                PrintMessage("%s close busy,try:%d", callerName, closeRetry);
            }

            if (ptrLoop->active_handles >= maxHandle) {
                PrintMessage("TryCloseLoop issue");
            }
            auto clearLoopTask = [](uv_handle_t *handle, void *arg) -> void {
                TryCloseHandle(handle);
            };
            uv_walk(ptrLoop, clearLoopTask, nullptr);
            // If all processing ends, Then return0,this call will block
            if (!ptrLoop->active_handles) {
                ret = true;
                break;
            }
            if (!uv_run(ptrLoop, UV_RUN_ONCE)) {
                ret = true;
                break;
            }
        } else {
            ret = true;
            break;
        }
    }
    return ret;
}

static void FreeInstance()
{
    if (clsHdcJdwpSimulator) {
        clsHdcJdwpSimulator->stop();
        delete clsHdcJdwpSimulator;
        clsHdcJdwpSimulator = nullptr;
    }
    uv_stop(&loopMain);
    TryCloseLoop(&loopMain, "Hdcjdwp test exit");
    HiLog::Info(LABEL, "jdwp_test_process exit.");
    PrintMessage("jdwp_test_process exit.");
}

static void Stop(int signo)
{
    FreeInstance();
    _exit(0);
}

int main(int argc, const char *argv[])
{
    uv_loop_init(&loopMain);

    HiLog::Info(LABEL, "jdwp_test_process start.");
    PrintMessage("jdwp_test_process start.");
    if (signal(SIGINT, Stop) == SIG_ERR) {
        PrintMessage("jdwp_test_process signal fail.");
    }
    clsHdcJdwpSimulator = new HdcJdwpSimulator(&loopMain, "com.example.myapplication");
    if (!clsHdcJdwpSimulator->Connect()) {
        PrintMessage("Connect fail.");
        return -1;
    }
    uv_run(&loopMain, UV_RUN_DEFAULT);

#ifdef JS_JDWP_CONNECT
    PrintMessage("Enter 'exit' will stop the test.");
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!strcmp(line.c_str(), "exit")) {
            PrintMessage("Exit current process.");
            break;
        }
    }
    FreeInstance();
#endif // JS_JDWP_CONNECT
    return 0;
}