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
#include "ut_runtime.h"
using namespace Hdc;

namespace HdcTest {
Runtime::Runtime()
{
    uv_loop_init(&loopMain);
    bCheckResult = false;
    checkFinish = false;
    hashInitialize = false;
    // UintTest Running log level LOG_INFO/LOG_ALL
    Base::SetLogLevel(Hdc::LOG_INFO);
    // three nodes all run host, at least 5+(reserve:2)=7 threads for use
    // client 1 + (server+daemon)= SIZE_THREAD_POOL*2+1
    string threadNum = std::to_string(SIZE_THREAD_POOL * 2);
    uv_os_setenv("UV_THREADPOOL_SIZE", threadNum.c_str());
    ResetUtTmpFolder();

    serverRunning = false;
    daemonRunning = false;
};

Runtime::~Runtime()
{
    constexpr int sleepTime = 500;
    if (hashInitialize) {
        Base::TryCloseLoop(&loopMain, "Runtime childUV");
        uv_loop_close(&loopMain);
    }
    while (serverRunning || daemonRunning) {
        uv_sleep(sleepTime);
    }
};

int Runtime::InnerCall(int method)
{
    return TestRuntimeCommand(method, DEBUG_ADDRESS.c_str(), DEBUG_TCP_CONNECT_KEY.c_str());
}

void Runtime::CheckStopServer(uv_idle_t *arg)
{
    Runtime *thisClass = (Runtime *)arg->data;
    thisClass->serverRunning = true;
    if (!thisClass->checkFinish) {
        return;
    }
    WRITE_LOG(LOG_DEBUG, "Try stop test server");
    thisClass->server->PostStopInstanceMessage();
    Base::TryCloseHandle((uv_handle_t *)&thisClass->checkServerStop);
}

void Runtime::StartServer(uv_work_t *arg)
{
    constexpr int sleepTime = 1000;
    Runtime *thisClass = static_cast<Runtime *>(arg->data);
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
}

void Runtime::CheckStopDaemon(uv_idle_t *arg)
{
    Runtime *thisClass = (Runtime *)arg->data;
    thisClass->daemonRunning = true;
    if (!thisClass->checkFinish) {
        return;
    }
    WRITE_LOG(LOG_DEBUG, "Try stop test daemon");
    thisClass->daemon->PostStopInstanceMessage();
    Base::TryCloseHandle((uv_handle_t *)&thisClass->checkDaemonStop);
}

void Runtime::StartDaemon(uv_work_t *arg)
{
    Runtime *thisClass = static_cast<Runtime *>(arg->data);
    HdcDaemon daemon(false);
    daemon.InitMod(true, false);
    thisClass->daemon = &daemon;

    uv_idle_t *idt = &thisClass->checkDaemonStop;
    idt->data = thisClass;
    uv_idle_init(&daemon.loopMain, idt);
    uv_idle_start(idt, CheckStopDaemon);

    daemon.WorkerPendding();
    WRITE_LOG(LOG_DEBUG, "TestDaemon free");
}

int Runtime::CheckServerDaemonReady()
{
    constexpr auto waitCount = 10;
    if (++waitServerDaemonReadyCount > waitCount) {
        return ERR_UT_MODULE_WAITMAX;
    }
    if (!serverRunning || !daemonRunning) {
        return ERR_UT_MODULE_NOTREADY;
    }
    if (bConnectToDaemon) {
        PreConnectDaemon(DEBUG_ADDRESS.c_str(), DEBUG_TCP_CONNECT_KEY.c_str());
    }
    hashInitialize = true;
    return RET_SUCCESS;
}

void Runtime::DoCheck(uv_timer_t *handle)
{
    Runtime *thisClass = (Runtime *)handle->data;
    do {
        int checkRet = thisClass->CheckServerDaemonReady();
        if (checkRet == ERR_UT_MODULE_WAITMAX) {
            break;
        } else if (checkRet == ERR_UT_MODULE_NOTREADY) {
            return;
        }
        // every case can be add more test...
        switch (thisClass->checkType) {
            case UT_MOD_SHELL:
                thisClass->bCheckResult = TestShellExecute(thisClass);
                break;
            case UT_MOD_BASE:
                thisClass->bCheckResult = TestBaseCommand(thisClass);
                break;
            case UT_MOD_FILE:
                thisClass->bCheckResult = TestFileCommand(thisClass);
                break;
            case UT_MOD_FORWARD:
                thisClass->bCheckResult = TestForwardCommand(thisClass);
                break;
            case UT_MOD_APP:
                thisClass->bCheckResult = TestAppCommand(thisClass);
                break;
            default:
                break;
        }
    } while (false);
    uv_close((uv_handle_t *)handle, Base::CloseIdleCallback);
    thisClass->checkFinish = true;
}

bool Runtime::Initial(bool bConnectToDaemonIn)
{
    bConnectToDaemon = bConnectToDaemonIn;
    constexpr int sleepTime = 300;
    auto funcServerFinish = [](uv_work_t *req, int status) -> void {
        auto thisClass = (Runtime *)req->data;
        thisClass->serverRunning = false;
        WRITE_LOG(LOG_DEBUG, "Ut runtime frame server thread finish");
        delete req;
    };
    auto funcDaemonFinish = [](uv_work_t *req, int status) -> void {
        auto thisClass = (Runtime *)req->data;
        thisClass->daemonRunning = false;
        WRITE_LOG(LOG_DEBUG, "Ut runtime frame daemon thread finish");
        delete req;
    };

    Base::StartWorkThread(&loopMain, StartServer, funcServerFinish, this);
    Base::StartWorkThread(&loopMain, StartDaemon, funcDaemonFinish, this);
    Base::TimerUvTask(&loopMain, this, DoCheck, sleepTime);
    return true;
}

bool Runtime::CheckEntry(UtModType type)
{
    checkFinish = false;
    checkType = type;

    WorkerPendding();
    return bCheckResult;
}

bool Runtime::ResetUtTmpFolder()
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

bool Runtime::ResetUtTmpFile(string file)
{
    string utFile = Base::StringFormat("%s/%s", UT_TMP_PATH.c_str(), file.c_str());
    string sCmd = "rm -f " + utFile;
    struct stat statbuf;
    if (!stat(utFile.c_str(), &statbuf)) {
        system(sCmd.c_str());
    }
    return true;
}

void Runtime::WorkerPendding()
{
    uv_run(&loopMain, UV_RUN_DEFAULT);
    WRITE_LOG(LOG_DEBUG, "TesPendding free");
}
}  // namespace HdcTest
