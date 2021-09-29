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
#include "ut_mod.h"
using namespace Hdc;

namespace HdcTest {
bool TestBaseCommand(void *runtimePtr)
{
    Runtime *rt = (Runtime *)runtimePtr;
    uint8_t *bufPtr = nullptr;
    int bytesIO = 0;
    bool ret = false;
    // test 'discover'
    rt->InnerCall(UT_DISCOVER);
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/base-discover.result").c_str(), (void **)&bufPtr, 0)) < 0) {
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
    rt->InnerCall(UT_LIST_TARGETS);
    constexpr int expert = 5;
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/base-list.result").c_str(), (void **)&bufPtr, 0)) < expert) {
        goto Finish;
    }
    if (strcmp(MESSAGE_SUCCESS.c_str(), (char *)bufPtr)) {
        goto Finish;
    }
    delete[] bufPtr;
    bufPtr = nullptr;
    // test 'any'
    rt->InnerCall(UT_CONNECT_ANY);
    if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/base-any.result").c_str(), (void **)&bufPtr, 0)) < 0) {
        goto Finish;
    }
    if (strcmp(MESSAGE_SUCCESS.c_str(), (char *)bufPtr)) {
        goto Finish;
    }
    // all pass
    ret = true;

Finish:
    if (bufPtr) {
        delete[] bufPtr;
        bufPtr = nullptr;
    }
    return ret;
}

bool TestShellExecute(void *runtimePtr)
{
    Runtime *rt = (Runtime *)runtimePtr;
    uint8_t *bufPtr = nullptr;
    int bytesIO = 0;
    bool ret = false;
    char bufString[BUF_SIZE_DEFAULT4] = "";
    string resultFile = "execute.result";
    while (true) {
        // test1
        rt->InnerCall(UT_SHELL_BASIC);
        constexpr int expert = 10;
        if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/" + resultFile).c_str(), (void **)&bufPtr, 0)) < expert) {
            break;
        }
        Base::RunPipeComand((const char *)"id", bufString, sizeof(bufString), false);
        if (strcmp(bufString, (char *)bufPtr)) {
            break;
        }
        delete[] bufPtr;
        bufPtr = nullptr;

        // test 2
        rt->ResetUtTmpFile(resultFile);
        rt->InnerCall(UT_SHELL_LIGHT);
        if ((bytesIO = Base::ReadBinFile((UT_TMP_PATH + "/" + resultFile).c_str(), (void **)&bufPtr, 0)) < expert) {
            break;
        }
        Base::RunPipeComand((const char *)"cat /etc/passwd", bufString, sizeof(bufString), false);
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
    return ret;
}

// file send like recv in our code, so just test send is enough
bool TestFileCommand(void *runtimePtr)
{
    Runtime *rt = (Runtime *)runtimePtr;
    bool ret = false;
    char bufString[BUF_SIZE_DEFAULT] = "";
    uint8_t *bufLocal = nullptr;
    uint8_t *bufRemote = nullptr;
    int sizeLocal = 0;
    int sizeRemote = 0;
    string localFile = Base::StringFormat("%s/file.local", UT_TMP_PATH.c_str());
    string remoteFile = Base::StringFormat("%s/file.remote", UT_TMP_PATH.c_str());
    do {
        // to be use random buf, not bash result
        string cmd = Base::StringFormat("find /usr > %s", localFile.c_str());
        Base::RunPipeComand(cmd.c_str(), bufString, sizeof(bufString), false);
        rt->InnerCall(UT_FILE_SEND);
        if ((sizeLocal = Base::ReadBinFile(localFile.c_str(), (void **)&bufLocal, 0)) < 0) {
            break;
        };
        if ((sizeRemote = Base::ReadBinFile(remoteFile.c_str(), (void **)&bufRemote, 0)) < 0) {
            break;
        };
        auto localHash = Base::Md5Sum(bufLocal, sizeLocal);
        auto remoteHash = Base::Md5Sum(bufRemote, sizeRemote);
        if (memcmp(localHash.data(), remoteHash.data(), localHash.size())) {
            break;
        }
        ret = true;
    } while (false);

    if (bufLocal) {
        delete[] bufLocal;
    }
    if (bufRemote) {
        delete[] bufRemote;
    }
    return ret;
}

void UtForwardWaiter(uv_loop_t *loop, uv_tcp_t *server)
{
    auto funcOnNewConn = [](uv_stream_t *server, int status) -> void {
        auto funcOnRead = [](uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) -> void {
            if (nread > 0 && !strcmp(buf->base, MESSAGE_SUCCESS.c_str())) {
                Base::WriteBinFile((UT_TMP_PATH + "/forward.result").c_str(), (uint8_t *)MESSAGE_SUCCESS.c_str(),
                                   MESSAGE_SUCCESS.size(), true);
            }
            uv_close((uv_handle_t *)client, [](uv_handle_t *handle) { free(handle); });
            free(buf->base);
        };
        if (status < 0) {
            return;
        }
        uv_tcp_t *client = new uv_tcp_t();
        uv_tcp_init(server->loop, client);
        if (uv_accept(server, (uv_stream_t *)client) == 0) {
            uv_read_start((uv_stream_t *)client,
                          [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
                              buf->base = new char[suggested_size]();
                              buf->len = suggested_size;
                          },
                          funcOnRead);
        } else {
            uv_close((uv_handle_t *)client, [](uv_handle_t *handle) { free(handle); });
        }
    };
    const int utForwardTargetPort = 8082;
    struct sockaddr_in addr;
    if (uv_tcp_init(loop, server) || uv_ip4_addr("127.0.0.1", utForwardTargetPort, &addr)) {
        return;
    }
    if (uv_tcp_bind(server, (const struct sockaddr *)&addr, 0) || uv_listen((uv_stream_t *)server, 5, funcOnNewConn)) {
        return;
    }
    WRITE_LOG(LOG_DEBUG, "UtForwardWaiter listen on port:%d", utForwardTargetPort);
}

bool UtForwardConnect(uv_loop_t *loop, uv_tcp_t *client, uv_tcp_t *server)
{
    auto funcConn = [](uv_connect_t *req, int status) -> void {
        uv_tcp_t *server = (uv_tcp_t *)req->data;
        delete req;
        if (status < 0) {
            return;
        }
        Base::SendToStream((uv_stream_t *)req->handle, (uint8_t *)MESSAGE_SUCCESS.c_str(), MESSAGE_SUCCESS.size());
        Base::DelayDoSimple(req->handle->loop, 3000, [=](const uint8_t flag, string &msg, const void *p) {
            uv_close((uv_handle_t *)server, nullptr);  // notify UtForwardWaiter stop
        });
    };

    const int utForwardListenPort = 8081;
    struct sockaddr_in addr;
    bool ret = false;
    uv_connect_t *connReq = new uv_connect_t();
    connReq->data = server;
    do {
        if (uv_tcp_init(loop, client)) {
            break;
        }
        uv_ip4_addr("127.0.0.1", utForwardListenPort, &addr);
        if (uv_tcp_connect(connReq, client, (const struct sockaddr *)&addr, funcConn)) {
            break;
        }

        ret = true;
    } while (false);
    return ret;
}

void TestForwardExternThread(void *arg)
{
    uv_loop_t loop;
    uv_tcp_t server;
    uv_tcp_t client;
    const int clientForwardTimeout = 1000;
    bool *clientOK = (bool *)arg;
    auto funcDelayCallUtForwardConnect = [&](const uint8_t flag, string &msg, const void *p) -> void {
        if (!*clientOK) {
            // client create forward timeout
            WRITE_LOG(LOG_WARN, "Client forward timeout");
            uv_stop(&loop);
        }
        UtForwardConnect(&loop, &client, &server);
    };

    uv_loop_init(&loop);
    UtForwardWaiter(&loop, &server);
    Base::DelayDoSimple(&loop, clientForwardTimeout, funcDelayCallUtForwardConnect);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);
};

bool TestForwardCommand(void *runtimePtr)
{
    Runtime *rt = (Runtime *)runtimePtr;
    uv_thread_t td;
    char buf[BUF_SIZE_TINY] = "";
    bool clientOK = false;
    int sizeResult = 0;
    uv_thread_create(&td, TestForwardExternThread, &clientOK);
    rt->InnerCall(UT_FORWARD_TCP2TCP);
    clientOK = true;
    uv_thread_join(&td);
    // all done, we will check result ok
    string localFile = Base::StringFormat("%s/forward.result", UT_TMP_PATH.c_str());
    if ((sizeResult = Base::ReadBinFile(localFile.c_str(), (void **)buf, sizeof(buf))) < 0) {
        return false;
    };
    if (strcmp(buf, MESSAGE_SUCCESS.c_str())) {
        return false;
    }
    return true;
}

bool TestAppCommand(void *runtimePtr)
{
    Runtime *rt = (Runtime *)runtimePtr;
    char bufString[BUF_SIZE_DEFAULT] = "";
    string localFile = Base::StringFormat("%s/app.hap", UT_TMP_PATH.c_str());
    string cmd = Base::StringFormat("id --help > %s", localFile.c_str());  // I know it is a invalid hap file
    Base::RunPipeComand(cmd.c_str(), bufString, sizeof(bufString), false);
    rt->InnerCall(UT_APP_INSTALL);

    constexpr int expert = 5;
    if (Base::ReadBinFile((UT_TMP_PATH + "/appinstall.result").c_str(), (void **)&bufString, sizeof(bufString))
        < expert) {
        return false;
    }
    if (strcmp(MESSAGE_SUCCESS.c_str(), (char *)bufString)) {
        return false;
    }
    return true;
}
}  // namespace HdcTest