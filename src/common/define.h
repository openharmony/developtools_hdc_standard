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
#ifndef HDC_DEFINE_H
#define HDC_DEFINE_H
#include "define_plus.h"

namespace Hdc {
// ############################## config #######################################
constexpr uint8_t MINOR_TIMEOUT = 5;
constexpr uint8_t SIZE_THREAD_POOL = 16;
constexpr uint8_t GLOBAL_TIMEOUT = 30;
constexpr uint16_t DEFAULT_PORT = 8710;
constexpr uint16_t MAX_SIZE_IOBUF = 5120;  // USB EP block max size about 10k
constexpr bool ENABLE_IO_CHECKSUM = false;
const string DEFAULT_SERVER_ADDR = "127.0.0.1:8710";

// ################################ macro define ###################################
constexpr uint8_t DWORD_SERIALIZE_SIZE = 4;
constexpr uint8_t CMD_ARG1_COUNT = 2;
constexpr uint8_t STREAM_MAIN = 0;  // work at main thread
constexpr uint8_t STREAM_WORK = 1;  // work at work thread
constexpr uint16_t BUF_SIZE_MICRO = 16;
constexpr uint16_t BUF_SIZE_TINY = 64;
constexpr uint16_t BUF_SIZE_SMALL = 256;
constexpr uint16_t BUF_SIZE_MEDIUM = 512;
constexpr uint16_t BUF_SIZE_DEFAULT = 1024;
constexpr uint16_t BUF_SIZE_DEFAULT2 = BUF_SIZE_DEFAULT * 2;
constexpr uint16_t BUF_SIZE_DEFAULT4 = BUF_SIZE_DEFAULT * 4;
constexpr uint16_t MAX_IP_PORT = 65535;
constexpr uint16_t MAX_CONNECTKEY_SIZE = 32;  // usb sn/tcp ipport
constexpr uint16_t TIME_BASE = 1000;          // time unit conversion base value
constexpr uint16_t AID_SHELL = 2000;
constexpr uint16_t UV_DEFAULT_INTERVAL = 250;  // ms
constexpr uint16_t VER_PROTOCOL = 0x01;
constexpr uint16_t EXTRA_ALLOC_SIZE = 2048;
// double-word(hex)=[0]major[1][2]minor[3][4]version[5]fix(a-p)[6][7]reserve
constexpr uint32_t HDC_VERSION_NUMBER = 0x10101700;  // 1.1.1b=0x10101100
constexpr uint32_t HDC_BUF_MAX_BYTES = 1024000000;

const string WHITE_SPACES = " \t\n\r";
const string UT_TMP_PATH = "/tmp/hdc-ut";
const string SERVER_NAME = "HDCServer";
const string STRING_EMPTY = "";
const string HANDSHAKE_MESSAGE = "OHOS HDC";  // sep not char '-', not more than 11 bytes
const string PACKET_FLAG = "HW";              // must 2bytes
const string EMPTY_ECHO = "[Empty]";
const string MESSAGE_INFO = "[Info]";
const string MESSAGE_FAIL = "[Fail]";
const string MESSAGE_SUCCESS = "[Success]";
// input command
const string CMDSTR_SOFTWARE_VERSION = "version";
const string CMDSTR_SOFTWARE_HELP = "help";
const string CMDSTR_TARGET_DISCOVER = "discover";
const string CMDSTR_SERVICE_START = "start";
const string CMDSTR_SERVICE_KILL = "kill";
const string CMDSTR_GENERATE_KEY = "keygen";
const string CMDSTR_KILL_SERVER = "kserver";
const string CMDSTR_KILL_DAEMON = "kdaemon";
const string CMDSTR_LIST_TARGETS = "list targets";
const string CMDSTR_CONNECT_TARGET = "tconn";
const string CMDSTR_CONNECT_ANY = "any";
const string CMDSTR_SHELL = "shell";
const string CMDSTR_TARGET_REBOOT = "target boot";
const string CMDSTR_TARGET_MOUNT = "target mount";
const string CMDSTR_STARTUP_MODE = "smode";
const string CMDSTR_TARGET_MODE = "tmode";
const string CMDSTR_BUGREPORT = "bugreport";
const string CMDSTR_HILOG = "hilog";
const string CMDSTR_TMODE_USB = "usb";
const string CMDSTR_TMODE_TCP = "tcp";
const string CMDSTR_FILE_SEND = "file send";
const string CMDSTR_FILE_RECV = "file recv";
const string CMDSTR_FORWARD_FPORT = "fport";
const string CMDSTR_FORWARD_RPORT = "rport";
const string CMDSTR_APP_INSTALL = "install";
const string CMDSTR_APP_UNINSTALL = "uninstall";
const string CMDSTR_APP_SIDELOAD = "sideload";
const string CMDSTR_LIST_JDWP = "jpid";
const string CMDSTR_INNER_ENABLE_KEEPALIVE = "alive";
}  // namespace Hdc
#endif  // HDC_DEFINE_H
