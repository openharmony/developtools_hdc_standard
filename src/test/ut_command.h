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
#ifndef HDC_UT_COMMAND_H
#define HDC_UT_COMMAND_H
#include "ut_common.h"

namespace HdcTest {
enum UtType {
    UT_HELP,
    UT_DISCOVER,
    UT_LIST_TARGETS,
    UT_CONNECT_ANY,
    UT_KILL_SERVER,
    UT_KILL_DAEMON,
    UT_SHELL_BASIC,
    UT_SHELL_LIGHT,
    UT_SHELL_HEAVY,
    UT_SHELL_INTERACTIVE,
    UT_FILE_SEND,
    UT_FILE_RECV,
    UT_FORWARD_TCP2TCP,
    UT_FORWARD_TCP2FILE,
    UT_FORWARD_TCP2DEV,
    UT_FORWARD_TCP2JDWP,
    UT_APP_INSTALL,
    UT_TEST_TMP,
};

const string DEBUG_ADDRESS = Hdc::DEFAULT_SERVER_ADDR;
const string DEBUG_TCP_CONNECT_KEY = "127.0.0.1:10178";
const string DEBUG_USB_CONNECT_KEY = "any";

int TestRuntimeCommand(const int method, const string &debugServerPort, const string &debugConnectKey);
int TestRuntimeCommandSimple(bool bTCPorUSB, int method, bool bNeedConnectDaemon);
void TestRunClient(const string &debugServerPort, const string &debugConnectKey, const string &cmd);
void PreConnectDaemon(const string &debugServerPort, const string &debugConnectKey);
void *TestBackgroundServerForClient(void *param);
void *DdmcallThreadEntry(void *param);
int DdmCallCommandEntry(int argc, const char *argv[]);
}  // namespace HdcTest
#endif