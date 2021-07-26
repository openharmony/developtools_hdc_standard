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
#include "server.h"
#include "server_for_client.h"

#ifndef HARMONY_PROJECT
#include "../test/ut_command.h"
using namespace HdcTest;
#endif

#include "server.h"
#include "server_for_client.h"
using namespace Hdc;

static bool g_isServerMode = false;
static bool g_isPullServer = true;
static bool g_isPcDebugRun = false;
static bool g_isTCPorUSB = false;
static int g_isTestMethod = 0;
static string g_connectKey = "";
static string g_serverListenString = DEFAULT_SERVER_ADDR;

namespace Hdc {
// return value: 0 == not command, 1 == one command, 2 == double command
int IsRegisterCommand(string &outCommand, const char *cmd, const char *cmdnext)
{
    string sCmdNext = cmdnext == nullptr ? string("") : string(cmdnext);
    string doubleCommand = cmd + string(" ") + sCmdNext;
    vector<string> registerCommand;
    registerCommand.push_back(CMDSTR_SOFTWARE_VERSION);
    registerCommand.push_back(CMDSTR_SOFTWARE_HELP);
    registerCommand.push_back(CMDSTR_TARGET_DISCOVER);
    registerCommand.push_back(CMDSTR_LIST_TARGETS);
    registerCommand.push_back(CMDSTR_CONNECT_ANY);
    registerCommand.push_back(CMDSTR_CONNECT_TARGET);
    registerCommand.push_back(CMDSTR_SHELL);
    registerCommand.push_back(CMDSTR_FILE_SEND);
    registerCommand.push_back(CMDSTR_FILE_RECV);
    registerCommand.push_back(CMDSTR_FORWARD_FPORT);
    registerCommand.push_back(CMDSTR_FORWARD_RPORT);
    registerCommand.push_back(CMDSTR_SERVICE_KILL);
    registerCommand.push_back(CMDSTR_SERVICE_START);
    registerCommand.push_back(CMDSTR_GENERATE_KEY);
    registerCommand.push_back(CMDSTR_KILL_SERVER);
    registerCommand.push_back(CMDSTR_KILL_DAEMON);
    registerCommand.push_back(CMDSTR_APP_INSTALL);
    registerCommand.push_back(CMDSTR_APP_UNINSTALL);
    registerCommand.push_back(CMDSTR_TARGET_MOUNT);
    registerCommand.push_back(CMDSTR_HILOG);
    registerCommand.push_back(CMDSTR_STARTUP_MODE);
    registerCommand.push_back(CMDSTR_BUGREPORT);
    registerCommand.push_back(CMDSTR_TARGET_MODE);
    registerCommand.push_back(CMDSTR_APP_SIDELOAD);
    registerCommand.push_back(CMDSTR_TARGET_REBOOT);
    registerCommand.push_back(CMDSTR_LIST_JDWP);

    for (string v : registerCommand) {
        if (doubleCommand == v) {
            outCommand = doubleCommand;
            return 2;
        }
        if (cmd == v) {
            outCommand = cmd;
            return 1;
        }
    }
    return 0;
}

int SplitOptionAndCommand(int argc, const char **argv, string &outOption, string &outCommand)
{
    bool foundCommand = false;
    int resultChild = 0;
    for (int i = 0; i < argc; ++i) {
        if (!foundCommand) {
            resultChild = IsRegisterCommand(outCommand, argv[i], (i == argc - 1) ? nullptr : argv[i + 1]);
            if (resultChild > 0) {
                foundCommand = true;
                if (resultChild == 2) {
                    ++i;
                }
                continue;
            }
        }
        if (foundCommand) {
            outCommand += outCommand.size() ? " " : "";
            outCommand += argv[i];
        } else {
            outOption += outOption.size() ? " " : "";
            outOption += argv[i];
        }
    }
    return 0;
}

int RunServerMode(string &serverListenString)
{
    HdcServer server(true);
    if (!server.Initial(serverListenString.c_str())) {
        Base::PrintMessage("Initial failed");
        return -1;
    }
    server.WorkerPendding();
    return 0;
}

int RunPcDebugMode(bool isPullServer, bool isTCPorUSB, int isTestMethod)
{
#ifdef HARMONY_PROJECT
    Base::PrintMessage("Not support command...");
#else
    pthread_t pt;
    if (isPullServer) {
        pthread_create(&pt, nullptr, TestBackgroundServerForClient, nullptr);
        uv_sleep(200);  // give time to start serverForClient,at least 200ms
    }
    TestRuntimeCommandSimple(isTCPorUSB, isTestMethod, true);
    if (isPullServer) {
        pthread_join(pt, nullptr);
        WRITE_LOG(LOG_DEBUG, "!!!!!!!!!Server finish");
    }
#endif
    return 0;
}

int RunClientMode(string &commands, string &serverListenString, string &connectKey, bool isPullServer)
{
    uv_loop_t loopMain;
    uv_loop_init(&loopMain);
    HdcClient client(false, DEFAULT_SERVER_ADDR, &loopMain);
    if (!commands.size()) {
        Base::PrintMessage("Nothing to do...");
        TranslateCommand::Usage();
        return 0;
    }
    if (!strncmp(commands.c_str(), CMDSTR_SERVICE_START.c_str(), CMDSTR_SERVICE_START.size())
        || !strncmp(commands.c_str(), CMDSTR_SERVICE_KILL.c_str(), CMDSTR_SERVICE_KILL.size())
        || !strncmp(commands.c_str(), CMDSTR_GENERATE_KEY.c_str(), CMDSTR_GENERATE_KEY.size())) {
        client.CtrlServiceWork(commands.c_str());
        return 0;
    }
    if (isPullServer && Base::ProgramMutex(SERVER_NAME.c_str(), true) == 0) {
        HdcServer::CheckToPullUptrServer(serverListenString.c_str());
        uv_sleep(300);  // give time to start serverForClient,at least 200ms
    }
    client.Initial(connectKey);
    client.ExecuteCommand(commands.c_str());
    return 0;
}

bool ParseServerListenString(string &serverListenString, char *optarg)
{
    if (strlen(optarg) > 24) {
        Base::PrintMessage("Unknow content of parament '-s'");
        return false;
    }
    char buf[BUF_SIZE_TINY] = "";
    if (strcpy_s(buf, sizeof(buf), optarg) < 0) {
        return false;
    }
    char *p = strchr(buf, ':');
    if (!p) {  // Only port
        int port = atoi(buf);
        if (port <= 0 || port > MAX_IP_PORT) {
            Base::PrintMessage("Port range incorrect");
            return false;
        }
        snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "127.0.0.1:%d", port);
        serverListenString = buf;
    } else {
        *p = '\0';
        int port = atoi(p + 1);
        sockaddr_in addr;
        if ((port <= 0 || port > MAX_IP_PORT) || uv_ip4_addr(buf, port, &addr) < 0) {
            Base::PrintMessage("-s content incorrect.");
            return false;
        }
        serverListenString = optarg;
    }
    return true;
}

bool GetCommandlineOptions(int optArgc, const char *optArgv[])
{
    int ch = 0;
    bool needExit = false;
    opterr = 0;
    // get option paraments first
    while ((ch = getopt(optArgc, (char *const *)optArgv, "hvpfms:d:t:l:")) != -1) {
        switch (ch) {
            case 'h': {
                string usage = Hdc::TranslateCommand::Usage();
                fprintf(stderr, "%s", usage.c_str());
                needExit = true;
                return needExit;
            }
            case 'v': {
                string ver = Base::GetVersion();
                fprintf(stdout, "%s\n", ver.c_str());
                needExit = true;
                return needExit;
            }
            case 'f': {  // [not-publish]
                break;
            }
            case 'l': {
                int logLevel = atoi(optarg);
                if (logLevel < 0 || logLevel > LOG_LAST) {
                    Base::PrintMessage("Loglevel error!");
                    needExit = true;
                    return needExit;
                }
                Base::SetLogLevel(logLevel);
                break;
            }
            case 'm': {  // [not-publish] is server mode，or client mode
                g_isServerMode = true;
                break;
            }
            case 'p': {  // [not-publish]  not pullup server
                g_isPullServer = false;
                break;
            }
            case 't': {  // key
                if (strlen(optarg) > MAX_CONNECTKEY_SIZE) {
                    Base::PrintMessage("Sizeo of of parament '-t' %d is too long", strlen(optarg));
                    needExit = true;
                    return needExit;
                }
                g_connectKey = optarg;
                break;
            }
            case 's': {
                if (!Hdc::ParseServerListenString(g_serverListenString, optarg)) {
                    needExit = true;
                    return needExit;
                }
                break;
            }
            case 'd':  // [Undisclosed parameters] debug mode
                g_isPcDebugRun = true;
                if (optarg[0] == 't') {
                    g_isTCPorUSB = true;
                } else if (optarg[0] == 'u') {
                    g_isTCPorUSB = false;
                } else {
                    Base::PrintMessage("Unknow debug paraments");
                    needExit = true;
                    return needExit;
                }
                g_isTestMethod = atoi(optarg + 1);
                break;
            case '?':
                break;
            default: {
                Base::PrintMessage("Unknow paraments");
                needExit = true;
                return needExit;
            }
        }
    }
    return needExit;
}
}

#ifndef UNIT_TEST
// hdc -l4 -m
// hdc -l4 discover / hdc -l4 connect 127.0.0.1:10178
// hdc -l4 -t 127.0.0.1:10178 shell id
int main(int argc, const char *argv[])
{
    string options;
    string commands;
    Hdc::SplitOptionAndCommand(argc, argv, options, commands);
    int optArgc = 0;
    char **optArgv = Base::SplitCommandToArgs(options.c_str(), &optArgc);
    bool cmdOptionResult = GetCommandlineOptions(optArgc, (const char **)optArgv);
    delete[]((char *)optArgv);
    if (cmdOptionResult) {
        return 0;
    }
    if (g_isServerMode) {
        // -m server.Run alone in the background
        Hdc::RunServerMode(g_serverListenString);
    } else if (g_isPcDebugRun) {
        Hdc::RunPcDebugMode(g_isPullServer, g_isTCPorUSB, g_isTestMethod);
    } else {
        Hdc::RunClientMode(commands, g_serverListenString, g_connectKey, g_isPullServer);
    }
    WRITE_LOG(LOG_DEBUG, "!!!!!!!!!Main finish main");
    return 0;
}
#endif  // no UNIT_TEST
