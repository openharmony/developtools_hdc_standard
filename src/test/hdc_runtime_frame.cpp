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
#include "hdc_runtime_frame.h"
#include "hdc_runtime_command.h"
using namespace Hdc;

namespace HdcTest {
FrameRuntime::FrameRuntime()
{
    uv_loop_init(&loopMain);
    uv_thread_create(&tdServer, StartServer, this);
    uv_thread_create(&tdDaemon, StartDaemon, this);

    bCheckResult = false;
    checkFinish = false;
    hashInitialize = false;
    // UintTest Running log level LOG_INFO/LOG_DEBUG
    Base::SetLogLevel(Hdc::LOG_INFO);
    ResetTmpFolder();
    serverRunning = false;
    daemonRunning = false;
};

FrameRuntime::~FrameRuntime()
{
    constexpr int sleepTime = 500;
    if (hashInitialize) {
        Base::TryCloseLoop(&loopMain, "FrameRuntime childUV");
        uv_loop_close(&loopMain);
    }
    while (serverRunning || daemonRunning) {
        uv_sleep(sleepTime);
    }
};

bool FrameRuntime::Initial(bool bConnectToDaemon)
{
    // server daemon runing check port listen
    // ++todo
    constexpr int loopTime = 20;
    constexpr int sleepTime = 300;
    bool bRunCheckOK = false;
    for (size_t i = 0; i < loopTime; i++) {
        if (serverRunning && daemonRunning) {
            bRunCheckOK = true;
            break;
        }
        uv_sleep(sleepTime);
    }
    if (!bRunCheckOK) {
        WRITE_LOG(LOG_DEBUG, "Unit server daemon not ready");
        return false;  // wait server and daemon ready
    }

    if (bConnectToDaemon) {
        PreConnectDaemon(DEBUG_ADDRESS.c_str(), DEBUG_TCP_CONNECT_KEY.c_str());
    }
    hashInitialize = true;
    return true;
}

inline int FrameRuntime::InnerCall(int method)
{
    return TestRuntimeCommand(method, DEBUG_ADDRESS.c_str(), DEBUG_TCP_CONNECT_KEY.c_str());
}

void FrameRuntime::CheckStopServer(uv_idle_t *arg)
{
    FrameRuntime *thisClass = (FrameRuntime *)arg->data;
    thisClass->serverRunning = true;
    if (!thisClass->checkFinish) {
        return;
    }
    thisClass->server->StopInstance();
    Base::TryCloseHandle((uv_handle_t *)&thisClass->checkServerStop);
}

void FrameRuntime::StartServer(void *arg)
{
    constexpr int sleepTime = 1000;
    FrameRuntime *thisClass = static_cast<FrameRuntime *>(arg);
    uv_sleep(sleepTime);
    HdcServer server(true);
    server.Initial(DEFAULT_SERVER_ADDR.c_str());
    thisClass->server = &server;

    uv_idle_t *idt = &thisClass->checkServerStop;
    idt->data = thisClass;
    uv_idle_init(&server.loopMain, idt);
    uv_idle_start(idt, CheckStopServer);

    server.WorkerPendding();
    WRITE_LOG(LOG_DEBUG, "TestServerForClient free");
    thisClass->serverRunning = false;
}

void FrameRuntime::CheckStopDaemon(uv_idle_t *arg)
{
    FrameRuntime *thisClass = (FrameRuntime *)arg->data;
    thisClass->daemonRunning = true;
    if (!thisClass->checkFinish) {
        return;
    }
    thisClass->daemon->StopInstance();
    Base::TryCloseHandle((uv_handle_t *)&thisClass->checkDaemonStop);
}

void FrameRuntime::StartDaemon(void *arg)
{
    FrameRuntime *thisClass = static_cast<FrameRuntime *>(arg);
    HdcDaemon daemon(false);
    daemon.InitMod(true, false);

    thisClass->daemon = &daemon;

    uv_idle_t *idt = &thisClass->checkDaemonStop;
    idt->data = thisClass;
    uv_idle_init(&daemon.loopMain, idt);
    uv_idle_start(idt, CheckStopDaemon);

    daemon.WorkerPendding();
    WRITE_LOG(LOG_DEBUG, "TestDaemon free");
    thisClass->daemonRunning = false;
}

void FrameRuntime::DoCheck(uv_idle_t *handle)
{
    FrameRuntime *thisClass = (FrameRuntime *)handle->data;
    if (!thisClass->hashInitialize) {
        WRITE_LOG(LOG_FATAL, "Need initialize first");
        return;  // wait server and daemon ready
    }
    switch (thisClass->checkType) {
        case UT_MOD_SHELL:
            thisClass->TestShellExecute();
            break;
        case UT_MOD_SHELL_INTERACTIVE:
            thisClass->TestShellInterActive();
            break;
        case UT_MOD_BASE:
            thisClass->TestBaseCommand();
            break;
        case UT_MOD_FILE:
            thisClass->TestFileCommand();
            break;
        default:
            break;
    }
    uv_close((uv_handle_t *)handle, FinishRemoveIdle);
    thisClass->checkFinish = true;
    uv_stop(&thisClass->loopMain);
}

bool FrameRuntime::CheckEntry(UT_MOD_TYPE type)
{
    checkFinish = false;
    checkType = type;

    Hdc::Base::IdleUvTask(&loopMain, this, DoCheck);
    WorkerPendding();
    return bCheckResult;
}

bool FrameRuntime::ResetTmpFolder()
{
#ifdef DEF_NULL
    struct stat statbuf;
    if (!stat(UT_TMP_PATH.c_str(), &statbuf))
        unlink(UT_TMP_PATH.c_str());  // exist
#endif
    string sCmd = "rm -rf " + UT_TMP_PATH;
    struct stat statbuf;
    if (!stat(UT_TMP_PATH.c_str(), &statbuf)) {
        system(sCmd.c_str());
    }
    constexpr uint32_t perm = 0666;
    mkdir(UT_TMP_PATH.c_str(), perm);
    return true;
}

void FrameRuntime::WorkerPendding()
{
    uv_run(&loopMain, UV_RUN_DEFAULT);
    WRITE_LOG(LOG_DEBUG, "TesPendding free");
}

// ----------------------------如上都是内部辅助测试函数--------------------------------------------------------
bool FrameRuntime::TestBaseCommand()
{
    uint8_t *bufPtr = nullptr;
    int bytesIO = 0;
    bool ret = false;
    char bufString[256] = "";
    // test 'discover'
    ResetTmpFolder();
    InnerCall(UT_DISCOVER);
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/base.result").c_str(), (void **)&bufPtr, 0)) < 0) {
        return false;
    }
    if (!strcmp("0", (char *)bufPtr)) {
        delete[] bufPtr;
        bufPtr = nullptr;
        return false;
    }
    delete[] bufPtr;
    bufPtr = nullptr;
    // test 'targets'
    ResetTmpFolder();
    InnerCall(UT_LIST_TARGETS);
    constexpr int expert = 10;
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/base.result").c_str(), (void **)&bufPtr, 0)) < expert) {
        goto Finish;
    }
    if (!strcmp(EMPTY_ECHO.c_str(), (char *)bufPtr)) {
        goto Finish;
    }
    delete[] bufPtr;
    bufPtr = nullptr;
    // test 'any'
    ResetTmpFolder();
    InnerCall(UT_CONNECT_ANY);
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/base.result").c_str(), (void **)&bufPtr, 0)) < 0) {
        goto Finish;
    }
    if (strcmp("OK", (char *)bufPtr)) {
        goto Finish;
    }
    // all pass
    ret = true;

Finish:
    if (bufPtr) {
        delete[] bufPtr;
        bufPtr = nullptr;
    }
    bCheckResult = ret;
    return ret;
}

bool FrameRuntime::TestShellExecute()
{
    uint8_t *bufPtr = nullptr;
    int bytesIO = 0;
    bool ret = false;
    char bufString[8192] = "";
    while (true) {
        // test1
        ResetTmpFolder();
        InnerCall(UT_SHELL_BASIC);
        if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/execute.result").c_str(), (void **)&bufPtr, 0)) < 10) {
            break;
        }
        Base::RunPipeComand((const char *)"id", bufString, 8192, false);
        if (strcmp(bufString, (char *)bufPtr)) {
            break;
        }
        delete[] bufPtr;
        bufPtr = nullptr;
        // test 2
        ResetTmpFolder();
        InnerCall(UT_SHELL_LIGHT);
        constexpr int expert = 10;
        if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/execute.result").c_str(), (void **)&bufPtr, 0)) < expert) {
            break;
        }
        Base::RunPipeComand((const char *)"cat /etc/passwd", bufString, 8192, false);
        if (strcmp(bufString, (char *)bufPtr)) {
            break;
        }
        delete[] bufPtr;
        bufPtr = nullptr;
        // all pass
        ret = true;
        break;
    }
    if (bufPtr) {
        delete[] bufPtr;
    }
    bCheckResult = ret;
    return ret;
}

bool FrameRuntime::TestShellInterActive()
{
    uint8_t *bufPtr = nullptr;
    int bytesIO = 0;
    bool ret = false;

    constexpr int buffSize = 256;
    char bufString[buffSize] = "";
    // test
    ResetTmpFolder();
    InnerCall(UT_SHELL_INTERACTIVE);
    constexpr int expert = 10;
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/shell.result").c_str(), (void **)&bufPtr, 0)) < expert) {
        return false;
    }
    Base::RunPipeComand("id", bufString, 256, false);
    if (strcmp(bufString, (char *)bufPtr)) {
        goto Finish;
    }
    // all pass
    ret = true;

Finish:
    if (bufPtr) {
        delete[] bufPtr;
        bufPtr = nullptr;
    }
    bCheckResult = ret;
    return ret;
}

bool FrameRuntime::TestFileCommand()
{
    bCheckResult = true;
    return true;
}
}  // namespace HdcTest