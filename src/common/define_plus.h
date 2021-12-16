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
#ifndef DEFINE_PLUS_H
#define DEFINE_PLUS_H

namespace Hdc {
constexpr uint8_t LOG_LEVEL_FULL = 5;
// ############################# enum define ###################################
enum LogLevel {
    LOG_OFF,
    LOG_FATAL,
    LOG_INFO,  // default
    LOG_WARN,
    LOG_DEBUG,
    LOG_ALL,
    LOG_LAST = LOG_ALL,  // tail, not use
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
    OP_QUERY_REF,  // crossthread query, manually reduce ref
    OP_GET_STRLIST,
    OP_GET_STRLIST_FULL,
    OP_GET_ANY,
    OP_UPDATE,
    OP_CLEAR,
    OP_INIT,
    OP_GET_ONLY
};

enum RetErrCode {
    RET_SUCCESS = 0,
    ERR_GENERIC = -1,
    ERR_NO_SUPPORT = -2,
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
    ERR_FILE_PATH_CHECK,
    ERR_PARM_FORMAT = -12000,
    ERR_PARM_SIZE,
    ERR_PARM_FAIL,
    ERR_API_FAIL = -13000,
    ERR_IO_FAIL = -14000,
    ERR_IO_TIMEOUT,
    ERR_IO_SOFT_RESET,
    ERR_SESSION_NOFOUND = -15000,
    ERR_SESSION_OFFLINE,
    ERR_SESSION_DEAD,
    ERR_HANDSHAKE_NOTMATCH = -16000,
    ERR_HANDSHAKE_CONNECTKEY_FAILED,
    ERR_HANDSHAKE_HANGUP_CHILD,
    ERR_SOCKET_FAIL = -17000,
    ERR_SOCKET_DUPLICATE,
    ERR_MODULE_JDWP_FAILED = -18000,
    ERR_UT_MODULE_NOTREADY = -19000,
    ERR_UT_MODULE_WAITMAX,
    ERR_THREAD_MUTEX_FAIL = -20000,
    ERR_PROCESS_SUB_FAIL = -21000,
    ERR_PRIVELEGE_NEED = -22000,
};

// Flags shared by multiple modules
enum AsyncEvent {
    ASYNC_STOP_MAINLOOP = 0,
    ASYNC_FREE_SESSION,
    ASYNC_FREE_CHANNEL,
};
enum InnerCtrlCommand {
    SP_START_SESSION = 0,
    SP_STOP_SESSION,
    SP_ATTACH_CHANNEL,
    SP_DEATCH_CHANNEL,
    SP_JDWP_NEWFD,
};

enum HdcCommand {
    // core commands types
    CMD_KERNEL_HELP = 0,
    CMD_KERNEL_HANDSHAKE,
    CMD_KERNEL_CHANNEL_CLOSE,
    CMD_KERNEL_SERVER_KILL,
    CMD_KERNEL_TARGET_DISCOVER,
    CMD_KERNEL_TARGET_LIST,
    CMD_KERNEL_TARGET_ANY,
    CMD_KERNEL_TARGET_CONNECT,
    CMD_KERNEL_TARGET_DISCONNECT,
    CMD_KERNEL_ECHO,
    CMD_KERNEL_ECHO_RAW,
    CMD_KERNEL_ENABLE_KEEPALIVE,
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
    CMD_JDWP_LIST,
    CMD_JDWP_TRACK,
    // Shell commands types
    CMD_SHELL_INIT = 2000,
    CMD_SHELL_DATA,
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
    USB_OPTION_HEADER = 1,
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
    uint32_t dataSize;
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
    libusb_context *ctxUSB = nullptr;  // child-use, main null
    libusb_device *device;
    libusb_device_handle *devHandle;
    uint8_t interfaceNumber;
    uint16_t retryCount;
    // D2H device to host endpoint's address
    uint8_t epDevice;
    // H2D host to device endpoint's address
    uint8_t epHost;
    uint8_t devId;
    uint8_t busId;
    uint16_t sizeEpBuf;
    string serialNumber;
    string usbMountPoint;
    uint8_t *bufDevice;
    uint8_t *bufHost;
    libusb_transfer *transferRecv;
    mutex lockTransferRecv;
    bool recvIOComplete;
    bool sendIOComplete;
    uv_thread_t threadUsbChildWork;
#else
    // usb accessory FunctionFS
    // USB main thread use, sub-thread disable, sub-thread uses the main thread USB handle
    int bulkOut;  // EP1 device recv
    int bulkIn;   // EP2 device send
#endif
    mutex lockDeviceHandle;
    uint16_t wMaxPacketSizeSend;
    uint32_t bulkinDataSize;
    bool resetIO;  // if true, must break write and read,default false
};
using HUSB = struct HdcUSB *;

struct HdcSession {
    bool serverOrDaemon;  // instance of daemon or server
    bool handshakeOK;     // Is an expected peer side
    bool isDead;
    string connectKey;
    uint8_t connType;  // ConnType
    uint32_t sessionId;
    std::atomic<uint32_t> ref;
    uint8_t uvHandleRef;  // libuv handle ref -- just main thread now
    uint8_t uvChildRef;   // libuv handle ref -- just main thread now
    bool childCleared;
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
    uv_os_sock_t fdChildWorkTCP;
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
    string connectKey;
    uv_tcp_t hWorkTCP;  // work channel for client, forward channel for server
    uv_thread_t hWorkThread;
    uint8_t uvHandleRef = 0;  // libuv handle ref -- just main thread now
    bool handshakeOK;
    bool isDead;
    bool serverOrClient;  // client's channel/ server's channel
    bool childCleared;
    bool interactiveShellMode;  // Is shell interactive mode
    bool keepAlive;             // channel will not auto-close by server
    std::atomic<uint32_t> ref;
    uint32_t targetSessionId;
    // child work
    uv_tcp_t hChildWorkTCP;  // work channel for server, no use in client
    uv_os_sock_t fdChildWorkTCP;
    // read io cache
    int bufSize;         // total buffer size
    int availTailIndex;  // buffer available data size
    uint8_t *ioBuf;
    // std
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
}
#endif