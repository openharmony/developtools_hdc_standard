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
#include "ut_command.h"
using namespace Hdc;

namespace HdcTest {
void *TestBackgroundServerForClient(void *param)
{
    HdcServer server(true);
    server.Initial("0.0.0.0:8710");
    server.WorkerPendding();
    WRITE_LOG(LOG_DEBUG, "Test ServerForClient free");
    return nullptr;
}

void TestRunClient(const string &debugServerPort, const string &debugConnectKey, const string &cmd)
{
    uv_loop_t loopMain;
    uv_loop_init(&loopMain);
    HdcClient client(false, debugServerPort, &loopMain);
    client.Initial(debugConnectKey);
    client.ExecuteCommand(cmd);
    uv_loop_close(&loopMain);
}

void PreConnectDaemon(const string &debugServerPort, const string &debugConnectKey)
{
    string bufString = "tconn ";
    bufString += debugConnectKey;
    WRITE_LOG(LOG_DEBUG, "------------Connect command------------");
    TestRunClient(debugServerPort, "", bufString.c_str());
}

int TestRuntimeCommandSimple(bool bTCPorUSB, int method, bool bNeedConnectDaemon)
{
    // These two parameters are tested, not much change, manually modify by myself
    string debugServerPort;
    string debugConnectKey;
    debugServerPort = DEBUG_ADDRESS;
    if (bTCPorUSB) {
        debugConnectKey = DEBUG_TCP_CONNECT_KEY;
    } else {
        debugConnectKey = DEBUG_USB_CONNECT_KEY;
    }
    if (bNeedConnectDaemon) {  // just tcp
        PreConnectDaemon(debugServerPort, debugConnectKey);
    }
    WRITE_LOG(LOG_DEBUG, "Test Jump TestRuntimeCommand");
    TestRuntimeCommand(method, debugServerPort, debugConnectKey);
    return 0;
}

int TestTaskCommand(int method, const string &debugServerPort, const string &debugConnectKey)
{
    WRITE_LOG(LOG_DEBUG, "------------Operate command------------");
    string bufString;
    switch (method) {
        case UT_SHELL_BASIC:  // Basic order test
            TestRunClient(debugServerPort, debugConnectKey, "shell id");
            break;
        case UT_SHELL_LIGHT:  // Small pressure test
            TestRunClient(debugServerPort, debugConnectKey, "shell cat /etc/passwd");
            break;
        case UT_SHELL_HEAVY:  // High pressure test (Long Time)
            TestRunClient(debugServerPort, debugConnectKey, "shell cat /data/local/tmp/root.txt");
            break;
        case UT_SHELL_INTERACTIVE:  // Interactive shell test
            TestRunClient(debugServerPort, debugConnectKey, CMDSTR_SHELL.c_str());
            break;
        case UT_FILE_SEND: {  // send files
            bufString = Base::StringFormat("file send %s/file.local %s/file.remote", UT_TMP_PATH.c_str(),
                                           UT_TMP_PATH.c_str());
            TestRunClient(debugServerPort, debugConnectKey, bufString);
            break;
        }
        case UT_FILE_RECV:  // recv files
            TestRunClient(debugServerPort, debugConnectKey,
                          "file recv /mnt/hgfs/vtmp/f.txt /mnt/hgfs/vtmp/f2.txt -z 1");
            break;
        case UT_FORWARD_TCP2TCP:  // TCP forward
            TestRunClient(debugServerPort, debugConnectKey, "fport tcp:8081 tcp:8082");
            break;
        case UT_FORWARD_TCP2FILE:  // localfilesystem forward
            TestRunClient(debugServerPort, debugConnectKey, "fport tcp:8081 localfilesystem:mysocket");
            break;
        case UT_FORWARD_TCP2DEV:
            TestRunClient(debugServerPort, debugConnectKey, "fport tcp:8081 dev:/dev/urandom");
            break;
        case UT_FORWARD_TCP2JDWP:  // jdwp forward
            TestRunClient(debugServerPort, debugConnectKey, "fport tcp:8081 jdwp:1234");
            break;
        case UT_APP_INSTALL:  // Single and multiple and multiple paths support
            bufString = Base::StringFormat("install %s/app.hap", UT_TMP_PATH.c_str());
            TestRunClient(debugServerPort, debugConnectKey, bufString);
            break;
        case UT_TEST_TMP:
#ifdef DEF_NULL
            while (true) {
                uv_sleep(GLOBAL_TIMEOUT);
                TestRunClient(debugServerPort, debugConnectKey, "list targets");
                TestRunClient(debugServerPort, debugConnectKey, "shell id");
                TestRunClient(debugServerPort, debugConnectKey, "shell bm dump -a");
            }
            TestRunClient(debugServerPort, debugConnectKey, "install /d/helloworld.hap");
            TestRunClient(debugServerPort, debugConnectKey, "target mount");
            TestRunClient(debugServerPort, debugConnectKey, "shell pwd");
            TestRunClient(debugServerPort, debugConnectKey, "target mount");
            TestRunClient(debugServerPort, debugConnectKey, "shell pwd");
            TestRunClient(debugServerPort, debugConnectKey, "install /d -rt");
            TestRunClient(debugServerPort, debugConnectKey, "fport tcp:8081 tcp:8082");
            TestRunClient(debugServerPort, debugConnectKey, "fport tcp:8081 dev:/dev/urandom");
            TestRunClient(debugServerPort, debugConnectKey, "shell hilog");
            TestRunClient(debugServerPort, debugConnectKey, "file send /mnt/hgfs/vtmp/f.txt /tmp/f2.txt");
            TestRunClient(debugServerPort, debugConnectKey, "file recv /tmp/f2.txt /mnt/hgfs/vtmp/f2.txt");
            TestRunClient(debugServerPort, debugConnectKey, "shell find /proc");
            TestRunClient(debugServerPort, debugConnectKey, "file send \"/d/a b/1.txt\" \"/d/a b/2.txt\"");
            TestRunClient(debugServerPort, debugConnectKey, "file recv \"/d/a b/1.txt\" \"/d/a b/2.txt\"");
#endif
            break;
        default:
            break;
    }
    WRITE_LOG(LOG_DEBUG, "!!!Client finish");
    return 0;
}

int TestRuntimeCommand(const int method, const string &debugServerPort, const string &debugConnectKey)
{
    switch (method) {
        case UT_HELP:
            TestRunClient(debugServerPort, "", CMDSTR_SOFTWARE_HELP.c_str());
            TestRunClient(debugServerPort, "", CMDSTR_SOFTWARE_VERSION.c_str());
            break;
        case UT_DISCOVER:
            TestRunClient(debugServerPort, "", CMDSTR_TARGET_DISCOVER.c_str());
            break;
        case UT_LIST_TARGETS:
            TestRunClient(debugServerPort, "", CMDSTR_LIST_TARGETS.c_str());
            break;
        case UT_CONNECT_ANY:
            TestRunClient(debugServerPort, "", CMDSTR_CONNECT_ANY.c_str());
            break;
        case UT_KILL_SERVER:
            TestRunClient(debugServerPort, "", CMDSTR_SERVICE_KILL.c_str());
            break;
        case UT_KILL_DAEMON:
            TestRunClient(debugServerPort, debugConnectKey, "kill daemon");
            break;
        default:
            TestTaskCommand(method, debugServerPort, debugConnectKey);
            break;
    }
    return 0;
}
}  // namespace HdcTest
