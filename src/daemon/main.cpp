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
#include "daemon_common.h"
using namespace Hdc;

static bool g_enableUsb = false;
static bool g_enableTcp = false;
static bool g_backgroundRun = false;
static bool g_root = false;
namespace Hdc {
bool RestartDaemon(bool forkchild)
{
    char path[256] = "";
    size_t nPathSize = 256;
    uv_exepath(path, &nPathSize);
    execl(path, "hdcd", forkchild ? "-forkchild" : nullptr, nullptr);
    return true;
}

bool ForkChildCheck(int argc, const char *argv[])
{
    // set servicelog
    Base::SetLogLevel(4);  // debug log print
    // hdcd        #service start forground
    // hdcd -b     #service start backgroundRun
    // hdcd -fork  #fork
    char modeSet[BUF_SIZE_TINY] = "";
    char droprootSet[BUF_SIZE_TINY] = "";
    Base::GetHdcProperty("persist.hdc.mode", modeSet, BUF_SIZE_TINY);
    Base::GetHdcProperty("persist.hdc.root", droprootSet, BUF_SIZE_TINY);
    Base::PrintMessage("Background mode, persist.hdc.mode:%s", modeSet);
    if (!strcmp(modeSet, "tcp")) {
        WRITE_LOG(LOG_DEBUG, "Property enable TCP");
        g_enableTcp = true;
    } else if (!strcmp(modeSet, CMDSTR_TMODE_USB.c_str())) {
        WRITE_LOG(LOG_DEBUG, "Property enable USB");
        g_enableUsb = true;
    } else if (!strcmp(modeSet, "all")) {
        WRITE_LOG(LOG_DEBUG, "Property enable USB and TCP");
        g_enableUsb = true;
        g_enableTcp = true;
    } else {
        WRITE_LOG(LOG_DEBUG, "Default USB mode");
        g_enableUsb = true;
    }
    droprootSet[sizeof(droprootSet) - 1] = '\0';
    if (!strcmp(droprootSet, "1")) {
        g_root = true;
        WRITE_LOG(LOG_DEBUG, "Root run");
    } else if (!strcmp(droprootSet, "0")) {
        // Interface for pm am, harmony system has not been implemented, not to achieve
        // Running is reduced by ROOT permission to run
        // setgid(USER_GID) setuid(USER_UID)
    }
    if (argc == 2) {
        if (!strcmp(argv[1], "-forkchild")) {
            g_backgroundRun = false;  // forkchild,Forced foreground
        } else if (!strcmp(argv[1], "-b")) {
            g_backgroundRun = true;
        }
    }
    return true;
}

int BackgroundRun()
{
    pid_t pc = fork();  // create process as daemon process
    if (pc < 0) {
        return -1;
    } else if (!pc) {
        int i;
        const int MAX_NUM = 64;
        for (i = 0; i < MAX_NUM; i++) {
            close(i);
        }
        RestartDaemon(true);
    } else {  // >0 orig process
    }
    return 0;
}

string DaemonUsage()
{
    string ret;
    ret = "\n                         Harmony device connector(HDC) daemon side...\n\n"
          "\n"
          "service mode commands:\n"
          " hdcd                          - Daemon server mode\n"
          " -b                            - Daemon server backgroundRun/fork mode\n"
          "\n"
          "paramenter mode commands:\n"
          " -h                            - Print help\n"
          " -l 0-5                        - Print runtime log\n"
          " -u                            - Enable USB mod\n"
          " -t                            - Enable TCP mod\n";
    return ret;
}

bool GetDaemonCommandlineOptions(int argc, const char *argv[])
{
    int ch;
    // hdcd -l4 ...
    WRITE_LOG(LOG_DEBUG, "Paraments mode");
    // Both settings are running with parameters
    while ((ch = getopt(argc, (char *const *)argv, "utl:")) != -1) {
        switch (ch) {
            case 'l': {
                int logLevel = atoi(optarg);
                if (logLevel < 0 || logLevel > LOG_LAST) {
                    WRITE_LOG(LOG_DEBUG, "Loglevel error!\n");
                    return -1;
                }
                Base::SetLogLevel(logLevel);
                break;
            }
            case 'u': {  // enable usb
                Base::PrintMessage("Parament Enable USB");
                g_enableUsb = true;
                break;
            }
            case 't': {  // enable tcp
                Base::PrintMessage("Parament Enable TCP");
                g_enableTcp = true;
                break;
            }
            default:
                Base::PrintMessage("other option:%c\n", ch);
                exit(0);
                break;
        }
    }
    return true;
}
}  // namespace Hdc

#ifndef UNIT_TEST
// daemon running with default behavior. options also can be given to custom its behavior including b/t/u/l etc.
int main(int argc, const char *argv[])
{
    // check property
    if (argc == 2 && !strcmp(argv[1], "-h")) {
        string usage = DaemonUsage();
        fprintf(stderr, "%s", usage.c_str());
        return 0;
    }
    if (argc == CMD_ARG1_COUNT && !strcmp(argv[1], "-v")) {
        string ver = Hdc::Base::GetVersion();
        fprintf(stderr, "%s\n", ver.c_str());
        return 0;
    }
    if (argc == 1 || (argc == 2 && (!strcmp(argv[1], "-forkchild") || !strcmp(argv[1], "-b")))) {
        ForkChildCheck(argc, argv);
    } else {
        GetDaemonCommandlineOptions(argc, argv);
    }
    if (!g_enableTcp && !g_enableUsb) {
        Base::PrintMessage("Both TCP and USB are disable, cannot run continue\n");
        return -1;
    }
    if (g_backgroundRun) {
        return BackgroundRun();
    }
    WRITE_LOG(LOG_DEBUG, "HdcDaemon main run");
    HdcDaemon daemon(false);
    daemon.InitMod(g_enableTcp, g_enableUsb);
    daemon.WorkerPendding();
    bool wantRestart = daemon.WantRestart();
    WRITE_LOG(LOG_DEBUG, "Daemon finish");
    // There is no daemon, we can only restart myself.
    if (g_root || wantRestart) {
        daemon.StopInstance();
        RestartDaemon(false);
    }
    return 0;
}
#endif