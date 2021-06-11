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
// MAX_PAYLOAD 4k, not change it, USB driver specific
// USB EP block max size about 10k, error if too big
constexpr uint16_t MAX_SIZE_IOBUF = 5120;
constexpr uint16_t VER_PROTOCOL = 0x01;
constexpr uint8_t SIZE_THREAD_POOL = 8;
constexpr uint8_t GLOBAL_TIMEOUT = 60;
constexpr uint16_t DEFAULT_PORT = 8710;
constexpr uint16_t EXTRA_ALLOC_SIZE = 2048;

const string UT_TMP_PATH = "/tmp/hdc-ut";
const string SERVER_NAME = "HDCServer";
const string STRING_EMPTY = "";
const string DEFAULT_SERVER_ADDR = "127.0.0.1:8710";

// ################################ macro define ###################################
constexpr uint16_t BUF_SIZE_MICRO = 16;
constexpr uint16_t BUF_SIZE_TINY = 64;
constexpr uint16_t BUF_SIZE_SMALL = 256;
constexpr uint16_t BUF_SIZE_MEDIUM = 512;
constexpr uint16_t BUF_SIZE_DEFAULT = 1024;
constexpr uint16_t BUF_SIZE_DEFAULT2 = BUF_SIZE_DEFAULT * 2;
constexpr uint8_t DWORD_SERIALIZE_SIZE = 4;
constexpr uint32_t HDC_BUF_MAX_BYTES = 1024000000;
constexpr uint16_t MAX_IP_PORT = 65535;
constexpr uint8_t STREAM_MAIN = 0;            // work at main thread
constexpr uint8_t STREAM_WORK = 1;            // work at work thread
constexpr uint16_t MAX_CONNECTKEY_SIZE = 32;  // usb sn/tcp ipport
constexpr uint8_t MAX_IO_OVERLAP = 128;
constexpr auto TIME_BASE = 1000;  // time unit conversion base value

// general one argument command argc
constexpr int CMD_ARG1_COUNT = 2;
// The first child versions must match, otherwise server and daemon must be upgraded
const string VERSION_NUMBER = "1.1.0b";       // same with openssl version, 1.1.2==VERNUMBER 0x10102000
const string HANDSHAKE_MESSAGE = "OHOS HDC";  // sep not char '-', not more than 11 bytes
const string PACKET_FLAG = "HW";              // must 2bytes
const string EMPTY_ECHO = "[Empty]";
const string MESSAGE_INFO = "[Info]";
const string MESSAGE_FAIL = "[Fail]";
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
const string CMDSTR_LIST_JDWP = "list jpid";
// ############################# enum define ###################################
enum LogLevel {
    LOG_OFF,
    LOG_FATAL,
    LOG_INFO,  // default
    LOG_WARN,
    LOG_DEBUG,
    LOG_FULL,
    LOG_LAST = LOG_FULL,  // tail, not use
};
#define WRITE_LOG(x, y...) Base::PrintLogEx(__FILE__, __LINE__, x, y)

enum MessageLevel {
    MSG_FAIL,
    MSG_INFO,
    MSG_OK,
};

enum ConnType { CONN_USB = 0, CONN_TCP, CONN_SERIAL, CONN_BT };
enum ConnStatus { STATUS_UNKNOW = 0, STATUS_READY, STATUS_CONNECTED, STATUS_OFFLINE };

enum OperateID {
    OP_ADD,
    OP_REMOVE,
    OP_QUERY,
    OP_GET_STRLIST,
    OP_GET_STRLIST_FULL,
    OP_GET_ANY,
    OP_UPDATE,
    OP_CLEAR,
    OP_INIT,
    OP_GET_ONLY
};

enum RetErrCode {
    ERR_SUCCESS = 0,
    ERR_GENERIC = -1,
    ERR_BUF_SIZE = -10000,
    ERR_BUF_ALLOC,
    ERR_BUF_OVERFLOW,
    ERR_BUF_CHECK,
    ERR_BUF_RESET,
    ERR_BUF_COPY,
    ERR_FILE_OPEN = -11000,
    ERR_FILE_READ,
    ERR_FILE_WRITE,
    ERR_FILE_STAT,
    ERR_PARM_FORMAT = -12000,
    ERR_PARM_SIZE,
    ERR_PARM_FAIL,
    ERR_API_FAIL = -13000,
    ERR_IO_FAIL = -14000,
    ERR_SESSION_NOFOUND = -15000,
    ERR_HANDSHAKE_NOTMATCH = -16000,
    ERR_HANDSHAKE_CONNECTKEY_FAILED,
};

// Flags shared by multiple modules
enum AsyncEvent {
    ASYNC_STOP_MAINLOOP = 0,
    ASYNC_FREE_SESSION,
};
enum InnerCtrlCommand {
    SP_START_SESSION = 0,
    SP_STOP_SESSION,
    SP_REGISTER_CHANNEL,
    SP_ATTACH_CHANNEL,
    SP_DEATCH_CHANNEL,
    SP_JDWP_NEWFD,
};

enum HdcCommand {
    // core commands types
    CMD_KERNEL_HELP = 0,
    CMD_KERNEL_HANDSHAKE,
    CMD_KERNEL_CHANNEL_CLOSE,
    CMD_KERNEL_CHANNEL_DETCH,
    CMD_KERNEL_SERVER_KILL,
    CMD_KERNEL_TARGET_DISCOVER,
    CMD_KERNEL_TARGET_LIST,
    CMD_KERNEL_TARGET_ANY,
    CMD_KERNEL_TARGET_CONNECT,
    CMD_KERNEL_TARGET_DISCONNECT,
    CMD_KERNEL_ECHO,
    CMD_KERNEL_ECHO_RAW,
    // One-pass simple commands
    CMD_UNITY_EXECUTE = 1000,
    CMD_UNITY_REMOUNT,
    CMD_UNITY_REBOOT,
    CMD_UNITY_RUNMODE,
    CMD_UNITY_HILOG,
    CMD_UNITY_TERMINATE,
    CMD_UNITY_ROOTRUN,
    CMD_UNITY_BUGREPORT_INIT,
    CMD_UNITY_BUGREPORT_DATA,
    CMD_UNITY_JPID,
    // Shell commands types
    CMD_SHELL_INIT = 2000,
    // Forward commands types
    CMD_FORWARD_INIT = 2500,
    CMD_FORWARD_CHECK,
    CMD_FORWARD_CHECK_RESULT,
    CMD_FORWARD_ACTIVE_SLAVE,
    CMD_FORWARD_ACTIVE_MASTER,
    CMD_FORWARD_DATA,
    CMD_FORWARD_FREE_CONTEXT,
    CMD_FORWARD_LIST,
    CMD_FORWARD_REMOVE,
    CMD_FORWARD_SUCCESS,
    // File commands
    CMD_FILE_INIT = 3000,
    CMD_FILE_CHECK,
    CMD_FILE_BEGIN,
    CMD_FILE_DATA,
    CMD_FILE_FINISH,
    CMD_APP_SIDELOAD,
    // App commands
    CMD_APP_INIT = 3500,
    CMD_APP_CHECK,
    CMD_APP_BEGIN,
    CMD_APP_DATA,
    CMD_APP_FINISH,
    CMD_APP_UNINSTALL,
};

enum UsbProtocolOption {
    USB_OPTION_TAIL = 1,
    USB_OPTION_RESET = 2,
    USB_OPTION_RESERVE4 = 4,
    USB_OPTION_RESERVE8 = 8,
    USB_OPTION_RESERVE16 = 16,
};
// ################################### struct define ###################################
#pragma pack(push)
#pragma pack(1)

struct USBHead {
    uint8_t flag[2];
    uint8_t option;
    uint32_t sessionId;
    uint16_t dataSize;
};

struct AsyncParam {
    void *context;    // context=hsession or hchannel
    uint32_t sid;     // sessionId/channelId
    void *thisClass;  // caller's class ptr
    uint16_t method;
    int dataSize;
    void *data;  // put it in the last
};

struct TaskInformation {
    uint8_t taskType;
    uint32_t sessionId;
    uint32_t channelId;
    bool hasInitial;
    bool taskStop;
    bool taskFree;
    bool serverOrDaemon;
    uv_loop_t *runLoop;
    void *taskClass;
    void *ownerSessionClass;
};
using HTaskInfo = TaskInformation *;

#pragma pack(pop)

struct HdcUSB {
#ifdef HDC_HOST
    uint16_t retryCount;
    libusb_device *device;
    libusb_device_handle *devHandle;
    uint8_t interfaceNumber;
    // D2H device to host endpoint's address
    uint8_t epDevice;
    // H2D host to device endpoint's address
    uint8_t epHost;
    uint8_t devId;
    uint8_t busId;
    int32_t bufSizeDevice;  // packetSizeD2H
    uint8_t *bufDevice;
    uint8_t *bufHost;
    uint32_t bufSizeHost;
    string serialNumber;
    string usbMountPoint;
    libusb_context *ctxUSB = nullptr;  // child-use, main null
#endif
    // usb accessory FunctionFS
    // USB main thread use, sub-thread disable, sub-thread uses the main thread USB handle
    int control;  // EP0
    int bulkOut;  // EP1
    int bulkIn;   // EP2
    vector<uint8_t> bufRecv;
};
using HUSB = struct HdcUSB *;

struct HdcSession {
    bool serverOrDaemon;  // instance of daemon or server
    bool handshakeOK;     // Is an expected peer side
    bool isDead;
    string connectKey;
    uint8_t connType;  // ConnType
    uint32_t sessionId;
    uv_mutex_t sendMutex;
    std::atomic<uint16_t> sendRef;
    uint8_t uvRef;  // libuv handle ref -- just main thread now
    bool childCleared;
    bool mainCleared;
    map<uint32_t, HTaskInfo> *mapTask;
    // class ptr
    void *classInstance;  //  HdcSessionBase instance, HdcServer or HdcDaemon
    void *classModule;    //  Communicate module, TCP or USB instance,HdcDaemonUSB HdcDaemonTCP etc...
    // io cache
    int bufSize;         // total buffer size
    int availTailIndex;  // buffer available data size
    uint8_t *ioBuf;
    // auth
    list<void *> *listKey;  // rsa private or publickey list
    uint8_t authKeyIndex;
    string tokenRSA;  // SHA_DIGEST_LENGTH+1==21
    // child work
    uv_loop_t childLoop;  // run in work thread
    // pipe0 in main thread(hdc server mainloop), pipe1 in work thread
    uv_tcp_t ctrlPipe[2];  // control channel
    int ctrlFd[2];         // control channel socketpair
    // data channel(TCP with socket, USB with thread forward)
    uv_tcp_t dataPipe[2];
    int dataFd[2];           // data channel socketpair
    uv_tcp_t hChildWorkTCP;  // work channel，separate thread for server/daemon
    uv_os_fd_t fdChildWorkTCP;
    // usb handle
    HUSB hUSB;
    // tcp handle
    uv_tcp_t hWorkTCP;
    uv_thread_t hWorkThread;
    uv_thread_t hWorkChildThread;
};
using HSession = struct HdcSession *;

struct HdcChannel {
    void *clsChannel;  // ptr Class of serverForClient or client
    uint32_t channelId;
    uv_mutex_t sendMutex;  // lock of send
    string connectKey;
    uv_tcp_t hWorkTCP;  // work channel for client, forward channel for server
    uv_thread_t hWorkThread;
    bool handshakeOK;
    bool channelDead;
    bool serverOrClient;  // client's channel/ server's channel
    bool childCleared;
    bool mainCleared;
    bool interactiveShellMode;  // Is shell interactive mode
    std::atomic<uint16_t> sendRef;
    HSession targetSession;
    // child work
    uv_tcp_t hChildWorkTCP;  // work channel for server, no use in client
    uv_os_fd_t fdChildWorkTCP;
    // read io cache
    int bufSize;         // total buffer size
    int availTailIndex;  // buffer available data size
    uint8_t *ioBuf;
    // std
    uv_pipe_t stdinPipe;
    uv_pipe_t stdoutPipe;
    uv_tty_t stdinTty;
    uv_tty_t stdoutTty;
    char bufStd[128];
};
using HChannel = struct HdcChannel *;

struct HdcDaemonInformation {
    uint8_t connType;
    uint8_t connStatus;
    string connectKey;
    string usbMountPoint;
    string devName;
    HSession hSession;
};
using HDaemonInfo = struct HdcDaemonInformation *;

struct HdcForwardInformation {
    string taskString;
    bool forwardDirection;  // true for forward, false is reverse;
    uint32_t sessionId;
    uint32_t channelId;
};
using HForwardInfo = struct HdcForwardInformation *;
}  // namespace Hdc
#endif  // HDC_DEFINE_H
