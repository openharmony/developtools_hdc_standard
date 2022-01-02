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
#include "daemon_usb.h"
#include "usb_ffs.h"

namespace Hdc {
HdcDaemonUSB::HdcDaemonUSB(const bool serverOrDaemonIn, void *ptrMainBase)
    : HdcUSBBase(serverOrDaemonIn, ptrMainBase)
{
    Base::ZeroStruct(usbHandle);
}

HdcDaemonUSB::~HdcDaemonUSB()
{
    // Closed in the IO loop, no longer closing CLOSE ENDPOINT
    if (controlEp > 0) {
        close(controlEp);
    }
    if (ctxRecv.buf) {
        delete[] ctxRecv.buf;
    }
    uv_fs_req_cleanup(&ctxRecv.req);
}

void HdcDaemonUSB::Stop()
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB Stop");
    // Here only clean up the IO-related resources, session related resources clear reason to clean up the session
    // module
    modRunning = false;
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB Stop free main session");
    Base::TryCloseHandle((uv_handle_t *)&checkEP);
    CloseEndpoint(&usbHandle);
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB Stop free main session finish");
}

string HdcDaemonUSB::GetDevPath(const std::string &path)
{
    DIR *dir = ::opendir(path.c_str());
    if (dir == nullptr) {
        WRITE_LOG(LOG_WARN, "%s: cannot open devpath: errno: %d", path.c_str(), errno);
        return "";
    }

    string res = USB_FFS_BASE;
    string node;
    int count = 0;
    struct dirent *entry = nullptr;
    while ((entry = ::readdir(dir))) {
        if (*entry->d_name == '.') {
            continue;
        }
        node = entry->d_name;
        ++count;
    }
    if (count > 1) {
        res += "hdc";
    } else {
        res += node;
    }
    ::closedir(dir);
    return res;
}

int HdcDaemonUSB::GetMaxPacketSize(int fdFfs)
{
    // no ioctl support, todo dynamic get
    return MAX_PACKET_SIZE_HISPEED;
}

int HdcDaemonUSB::Initial()
{
    // after Linux-3.8，kernel switch to the USB Function FS
    // Implement USB hdc function in user space
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB init");
    basePath = GetDevPath(USB_FFS_BASE);
    if (access((basePath + "/ep0").c_str(), F_OK) != 0) {
        WRITE_LOG(LOG_DEBUG, "Only support usb-ffs, make sure kernel3.8+ and usb-ffs enabled, usbmode disabled");
        return ERR_API_FAIL;
    }
    ctxRecv.thisClass = this;
    ctxRecv.bufSizeMax = Base::GetUsbffsBulkSize();
    ctxRecv.buf = new uint8_t[ctxRecv.bufSizeMax]();
    if (!ctxRecv.buf) {
        WRITE_LOG(LOG_FATAL, "Init alloc memory failed");
        return ERR_BUF_ALLOC;
    }

    HdcDaemon *daemon = (HdcDaemon *)clsMainBase;
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB::Initiall");
    uv_timer_init(&daemon->loopMain, &checkEP);
    checkEP.data = this;
    uv_timer_start(&checkEP, WatchEPTimer, 0, TIME_BASE);
    return 0;
}

// make gnuc++ happy. Clang support direct assignment value to structure, buf g++ weakness
void HdcDaemonUSB::FillUsbV2Head(usb_functionfs_desc_v2 &descUsbFfs)
{
    descUsbFfs.head.magic = LONG_LE(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    descUsbFfs.head.length = LONG_LE(sizeof(descUsbFfs));
    descUsbFfs.head.flags
        = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC | FUNCTIONFS_HAS_SS_DESC | FUNCTIONFS_HAS_MS_OS_DESC;
    descUsbFfs.config1Count = 3;
    descUsbFfs.config2Count = 3;
    descUsbFfs.config3Count = 5;
    descUsbFfs.configWosCount = 1;
    descUsbFfs.config1Desc = config1;
    descUsbFfs.config2Desc = config2;
    descUsbFfs.config3Desc = config3;
    descUsbFfs.wosHead.interface = 1;
    descUsbFfs.wosHead.dwLength = LONG_LE(sizeof(descUsbFfs.wosHead) + sizeof(descUsbFfs.wosDesc));
    descUsbFfs.wosHead.bcdVersion = SHORT_LE(1);
    descUsbFfs.wosHead.wIndex = SHORT_LE(4);
    descUsbFfs.wosHead.bCount = 1;
    descUsbFfs.wosHead.Reserved = 0;
    descUsbFfs.wosDesc.bFirstInterfaceNumber = 0;
    descUsbFfs.wosDesc.Reserved1 = 1;
    descUsbFfs.wosDesc.CompatibleID[0] = 'W';
    descUsbFfs.wosDesc.CompatibleID[1] = 'I';
    descUsbFfs.wosDesc.CompatibleID[2] = 'N';
    descUsbFfs.wosDesc.CompatibleID[3] = 'U';
    descUsbFfs.wosDesc.CompatibleID[4] = 'S';
    descUsbFfs.wosDesc.CompatibleID[5] = 'B';
    descUsbFfs.wosDesc.CompatibleID[6] = '\0';
    Base::ZeroArray(descUsbFfs.wosDesc.SubCompatibleID);
    Base::ZeroArray(descUsbFfs.wosDesc.Reserved2);
}

// DAEMON end USB module USB-FFS EP port connection
int HdcDaemonUSB::ConnectEPPoint(HUSB hUSB)
{
    int ret = ERR_GENERIC;
    struct usb_functionfs_desc_v2 descUsbFfs = {};
    FillUsbV2Head(descUsbFfs);
    while (true) {
        if (controlEp <= 0) {
            // After the control port sends the instruction, the device is initialized by the device to the HOST host,
            // which can be found for USB devices. Do not send initialization to the EP0 control port, the USB
            // device will not be initialized by Host
            WRITE_LOG(LOG_DEBUG, "Begin send to control(EP0) for usb descriptor init");
            string ep0Path = basePath + "/ep0";
            if ((controlEp = open(ep0Path.c_str(), O_RDWR)) < 0) {
                WRITE_LOG(LOG_WARN, "%s: cannot open control endpoint: errno=%d", ep0Path.c_str(), errno);
                break;
            }
            if (write(controlEp, &descUsbFfs, sizeof(descUsbFfs)) < 0) {
                WRITE_LOG(LOG_WARN, "%s: write ffs configs failed: errno=%d", ep0Path.c_str(), errno);
                break;
            }
            if (write(controlEp, &USB_FFS_VALUE, sizeof(USB_FFS_VALUE)) < 0) {
                WRITE_LOG(LOG_WARN, "%s: write USB_FFS_VALUE failed: errno=%d", ep0Path.c_str(), errno);
                break;
            }
            // active usbrc，Send USB initialization singal
            SystemDepend::SetDevItem("sys.usb.ffs.ready", "1");
            WRITE_LOG(LOG_DEBUG, "ConnectEPPoint ctrl init finish, set usb-ffs ready");
        }
        string outPath = basePath + "/ep1";
        if ((hUSB->bulkOut = open(outPath.c_str(), O_RDWR)) < 0) {
            WRITE_LOG(LOG_WARN, "%s: cannot open bulk-out ep: errno=%d", outPath.c_str(), errno);
            break;
        }
        string inPath = basePath + "/ep2";
        if ((hUSB->bulkIn = open(inPath.c_str(), O_RDWR)) < 0) {
            WRITE_LOG(LOG_WARN, "%s: cannot open bulk-in ep: errno=%d", inPath.c_str(), errno);
            break;
        }
        // cannot open with O_CLOEXEC, must fcntl
        fcntl(controlEp, F_SETFD, FD_CLOEXEC);
        fcntl(hUSB->bulkOut, F_SETFD, FD_CLOEXEC);
        fcntl(hUSB->bulkIn, F_SETFD, FD_CLOEXEC);
        hUSB->wMaxPacketSizeSend = GetMaxPacketSize(hUSB->bulkIn);

        WRITE_LOG(LOG_DEBUG, "New bulk in\\out open bulkout:%d bulkin:%d", hUSB->bulkOut, hUSB->bulkIn);
        ret = RET_SUCCESS;
        break;
    }
    if (ret != RET_SUCCESS) {
        CloseEndpoint(hUSB, true);
    }
    return ret;
}

void HdcDaemonUSB::CloseEndpoint(HUSB hUSB, bool closeCtrlEp)
{
    if (hUSB->bulkIn > 0) {
        close(hUSB->bulkIn);
        hUSB->bulkIn = 0;
    }
    if (hUSB->bulkOut > 0) {
        close(hUSB->bulkOut);
        hUSB->bulkOut = 0;
    }
    if (controlEp > 0 && closeCtrlEp) {
        close(controlEp);
        controlEp = 0;
    }
    isAlive = false;
    WRITE_LOG(LOG_FATAL, "DaemonUSB close endpoint");
}

void HdcDaemonUSB::ResetOldSession(uint32_t sessionId)
{
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(clsMainBase);
    if (sessionId == 0) {
        sessionId = currentSessionId;
    }
    HSession hSession = daemon->AdminSession(OP_QUERY, sessionId, nullptr);
    if (hSession == nullptr) {
        return;
    }
    // The Host side is restarted, but the USB cable is still connected
    WRITE_LOG(LOG_WARN, "Hostside softreset to restart daemon, old sessionId:%u", sessionId);
    daemon->FreeSession(sessionId);
}

// Prevent other USB data misfortunes to send the program crash
int HdcDaemonUSB::AvailablePacket(uint8_t *ioBuf, int ioBytes, uint32_t *sessionId)
{
    int ret = RET_SUCCESS;
    while (true) {
        if (!IsUsbPacketHeader(ioBuf, ioBytes)) {
            break;
        }
        // usb header
        USBHead *usbPayloadHeader = (struct USBHead *)ioBuf;
        uint32_t inSessionId = ntohl(usbPayloadHeader->sessionId);
        if ((usbPayloadHeader->option & USB_OPTION_RESET)) {
            ResetOldSession(inSessionId);
            ret = ERR_IO_SOFT_RESET;
            break;
        }
        *sessionId = inSessionId;
        break;
    }
    return ret;
}

// Work in subcrete，Work thread is ready
bool HdcDaemonUSB::ReadyForWorkThread(HSession hSession)
{
    HdcUSBBase::ReadyForWorkThread(hSession);
    return true;
};

int HdcDaemonUSB::CloseBulkEp(bool bulkInOut, int bulkFd, uv_loop_t *loop)
{
    struct CtxCloseBulkEp {
        uv_fs_t req;
        HdcDaemonUSB *thisClass;
        bool bulkInOut;
    };
    CtxCloseBulkEp *ctx = new CtxCloseBulkEp();
    uv_fs_t *req = &ctx->req;
    req->data = ctx;
    ctx->bulkInOut = bulkInOut;
    ctx->thisClass = this;
    isAlive = false;
    uv_fs_close(loop, req, bulkFd, [](uv_fs_t *req) {
        auto ctx = (CtxCloseBulkEp *)req->data;
        WRITE_LOG(LOG_DEBUG, "Try to abort blukin write callback %s", ctx->bulkInOut ? "bulkin" : "bulkout");
        if (ctx->bulkInOut) {
            ctx->thisClass->usbHandle.bulkIn = 0;
        } else {
            ctx->thisClass->usbHandle.bulkOut = 0;
        }
        uv_fs_req_cleanup(req);
        delete ctx;
    });
    return 0;
}

int HdcDaemonUSB::SendUSBIOSync(HSession hSession, HUSB hMainUSB, const uint8_t *data, const int length)
{
    int bulkIn = hMainUSB->bulkIn;
    int childRet = 0;
    int ret = ERR_IO_FAIL;
    int offset = 0;
    while (modRunning && isAlive && !hSession->isDead) {
        childRet = write(bulkIn, (uint8_t *)data + offset, length - offset);
        if (childRet <= 0) {
            int err = errno;
            if (err == EINTR) {
                WRITE_LOG(LOG_DEBUG, "BulkinWrite write EINTR, try again, offset:%u", offset);
                continue;
            } else {
                WRITE_LOG(LOG_FATAL, "BulkinWrite write fatal errno %d", err);
                isAlive = false;
            }
            break;
        }
        offset += childRet;
        if (offset >= length) {
            break;
        }
    }
    if (offset == length) {
        ret = length;
    } else {
        WRITE_LOG(LOG_FATAL, "BulkinWrite write failed, nsize:%d really:%d modRunning:%d isAlive:%d SessionDead:%d",
                  length, offset, modRunning, isAlive, hSession->isDead);
    }
    return ret;
}

int HdcDaemonUSB::SendUSBRaw(HSession hSession, uint8_t *data, const int length)
{
    HdcDaemon *daemon = (HdcDaemon *)hSession->classInstance;
    std::unique_lock<std::mutex> lock(mutexUsbFfs);
    ++hSession->ref;
    int ret = SendUSBIOSync(hSession, &usbHandle, data, length);
    --hSession->ref;
    if (ret < 0) {
        daemon->FreeSession(hSession->sessionId);
        WRITE_LOG(LOG_DEBUG, "SendUSBRaw try to freesession");
    }
    return ret;
}

// cross thread call
void HdcDaemonUSB::OnNewHandshakeOK(const uint32_t sessionId)
{
    currentSessionId = sessionId;  // sync with server, and set server's real Id
}

// MainThreadCall, when seession was freeed
void HdcDaemonUSB::OnSessionFreeFinally(const HSession hSession)
{
    if (currentSessionId == hSession->sessionId) {
        isAlive = false;
        // uv_cancel ctxRecv.req == UV_EBUSY, not effect immediately. It must be close by logic
    }
}

HSession HdcDaemonUSB::PrepareNewSession(uint32_t sessionId, uint8_t *pRecvBuf, int recvBytesIO)
{
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(clsMainBase);
    HSession hChildSession = daemon->MallocSession(false, CONN_USB, this, sessionId);
    if (!hChildSession) {
        return nullptr;
    }
    currentSessionId = sessionId;
    Base::StartWorkThread(&daemon->loopMain, daemon->SessionWorkThread, Base::FinishWorkThread, hChildSession);
    auto funcNewSessionUp = [](uv_timer_t *handle) -> void {
        HSession hChildSession = reinterpret_cast<HSession>(handle->data);
        HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(hChildSession->classInstance);
        if (hChildSession->childLoop.active_handles == 0) {
            return;
        }
        if (!hChildSession->isDead) {
            auto ctrl = daemon->BuildCtrlString(SP_START_SESSION, 0, nullptr, 0);
            Base::SendToStream((uv_stream_t *)&hChildSession->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
            WRITE_LOG(LOG_DEBUG, "Main thread usbio mirgate finish");
        }
        Base::TryCloseHandle(reinterpret_cast<uv_handle_t *>(handle), Base::CloseTimerCallback);
    };
    Base::TimerUvTask(&daemon->loopMain, hChildSession, funcNewSessionUp);
    return hChildSession;
}

int HdcDaemonUSB::UsbToHdcProtocol(uv_stream_t *stream, uint8_t *appendData, int dataSize)
{
    return Base::SendToStream(stream, appendData, dataSize);
}

int HdcDaemonUSB::DispatchToWorkThread(uint32_t sessionId, uint8_t *readBuf, int readBytes)
{
    HSession hChildSession = nullptr;
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(clsMainBase);
    int childRet = RET_SUCCESS;
    if (sessionId == 0) {
        // hdc packet data
        sessionId = currentSessionId;
    }
    if (currentSessionId != 0 && sessionId != currentSessionId) {
        WRITE_LOG(LOG_WARN, "New session coming, restart old sessionId:%u", currentSessionId);
        ResetOldSession(currentSessionId);
        currentSessionId = 0;
    }
    hChildSession = daemon->AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hChildSession) {
        hChildSession = PrepareNewSession(sessionId, readBuf, readBytes);
        if (!hChildSession) {
            WRITE_LOG(LOG_WARN, "prep new session err for sessionId:%u", sessionId);
            return ERR_SESSION_NOFOUND;
        }
    }

    if (hChildSession->childCleared || hChildSession->isDead) {
        WRITE_LOG(LOG_WARN, "session dead clr:%d - %d", hChildSession->childCleared, hChildSession->isDead);
        return ERR_SESSION_DEAD;
    }
    uv_stream_t *stream = reinterpret_cast<uv_stream_t *>(&hChildSession->dataPipe[STREAM_MAIN]);
    if ((childRet = SendToHdcStream(hChildSession, stream, readBuf, readBytes)) < 0) {
        WRITE_LOG(LOG_WARN, "DispatchToWorkThread SendToHdcStream err ret:%d", childRet);
        return ERR_IO_FAIL;
    }
    return childRet;
}

bool HdcDaemonUSB::JumpAntiquePacket(const uint8_t &buf, ssize_t bytes) const
{
    constexpr size_t antiqueFlagSize = 4;
    constexpr size_t antiqueFullSize = 24;
    // anti CNXN 0x4e584e43
    uint8_t flag[] = { 0x43, 0x4e, 0x58, 0x4e };
    if (bytes == antiqueFullSize && !memcmp(&buf, flag, antiqueFlagSize)) {
        return true;
    }
    return false;
}

// Only physically swap EP ports will be reset
void HdcDaemonUSB::OnUSBRead(uv_fs_t *req)
{  // Only read at the main thread
    auto ctxIo = reinterpret_cast<CtxUvFileCommonIo *>(req->data);
    auto hUSB = reinterpret_cast<HUSB>(ctxIo->data);
    auto thisClass = reinterpret_cast<HdcDaemonUSB *>(ctxIo->thisClass);
    uint8_t *bufPtr = ctxIo->buf;
    ssize_t bytesIOBytes = req->result;
    uint32_t sessionId = 0;
    bool ret = false;
    int childRet = 0;
    if (bytesIOBytes > hUSB->wMaxPacketSizeSend && bytesIOBytes != thisClass->saveNextReadSize) {
        WRITE_LOG(LOG_WARN, "Not full packet, wanted:%d really:%d", thisClass->saveNextReadSize, bytesIOBytes);
    }
    while (thisClass->isAlive) {
        // Don't care is module running, first deal with this
        if (bytesIOBytes < 0) {
            // logic alive and EINTER is gdb attach
            //
            // [about gdb attach known issue]
            // When GDB debugging is loaded, the number of USB read interrupts of libuv will increase. Multiple
            // interrupts will increase the correctness of USB data reading. Setting GDB to asynchronous mode or using
            // log debugging can avoid this problem
            if (bytesIOBytes != -EINTR) {  // Epoll will be broken when gdb attach
                WRITE_LOG(LOG_WARN, "USBIO ret:%d failed:%s", bytesIOBytes, uv_strerror(bytesIOBytes));
                ret = false;
                break;
            } else {
                WRITE_LOG(LOG_ALL, "OnUSBRead signal EINTR");
            }
        } else if (bytesIOBytes == 0) {  // zero packet
            WRITE_LOG(LOG_ALL, "Zero packet received");
        } else {
            if (thisClass->JumpAntiquePacket(*bufPtr, bytesIOBytes)) {
                WRITE_LOG(LOG_DEBUG, "JumpAntiquePacket auto jump");
                ret = true;
                break;
            }
            // guess is head of packet
            if ((childRet = thisClass->AvailablePacket((uint8_t *)bufPtr, bytesIOBytes, &sessionId)) != RET_SUCCESS) {
                if (childRet != ERR_IO_SOFT_RESET) {
                    WRITE_LOG(LOG_WARN, "AvailablePacket check failed, ret:%d buf:%-50s", bytesIOBytes, bufPtr);
                    break;
                }
                // reset packet
                childRet = 0;  // need max size
            } else {
                // AvailablePacket case
                if ((childRet = thisClass->DispatchToWorkThread(sessionId, bufPtr, bytesIOBytes)) < 0) {
                    WRITE_LOG(LOG_FATAL, "DispatchToWorkThread failed");
                    break;
                }
            }
        }
        int nextReadSize = childRet == 0 ? hUSB->wMaxPacketSizeSend : std::min(childRet, Base::GetUsbffsBulkSize());
        thisClass->saveNextReadSize = nextReadSize;
        if (thisClass->LoopUSBRead(hUSB, nextReadSize) < 0) {
            WRITE_LOG(LOG_FATAL, "LoopUSBRead failed");
            break;
        }
        ret = true;
        break;
    }
    if (!ret) {
        thisClass->isAlive = false;
        thisClass->ctxRecv.atPollQueue = false;
    }
}

int HdcDaemonUSB::LoopUSBRead(HUSB hUSB, int readMaxWanted)
{
    int ret = ERR_GENERIC;
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(clsMainBase);
    uv_buf_t iov;
    ctxRecv.data = hUSB;
    ctxRecv.bufSize = readMaxWanted;
    ctxRecv.req = {};
    uv_fs_t *req = &ctxRecv.req;
    req->data = &ctxRecv;
    iov = uv_buf_init(reinterpret_cast<char *>(ctxRecv.buf), ctxRecv.bufSize);
    ret = uv_fs_read(&daemon->loopMain, req, hUSB->bulkOut, &iov, 1, -1, OnUSBRead);
    if (ret < 0) {
        WRITE_LOG(LOG_FATAL, "uv_fs_read < 0");
        return ERR_API_FAIL;
    }
    ctxRecv.atPollQueue = true;
    return RET_SUCCESS;
}

// Because USB can connect to only one host，daemonUSB is only one Session by default
void HdcDaemonUSB::WatchEPTimer(uv_timer_t *handle)
{
    HdcDaemonUSB *thisClass = (HdcDaemonUSB *)handle->data;
    HUSB hUSB = &thisClass->usbHandle;
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(thisClass->clsMainBase);
    if (thisClass->isAlive || thisClass->ctxRecv.atPollQueue) {
        return;
    }
    bool resetEp = false;
    do {
        if (hUSB->bulkIn > 0) {
            WRITE_LOG(LOG_DEBUG, "Watchdog close bulkin");
            thisClass->CloseBulkEp(true, thisClass->usbHandle.bulkIn, &daemon->loopMain);
            resetEp = true;
        }
        if (hUSB->bulkOut > 0) {
            WRITE_LOG(LOG_DEBUG, "Watchdog close bulkout");
            thisClass->CloseBulkEp(false, thisClass->usbHandle.bulkOut, &daemon->loopMain);
            resetEp = true;
        }
        if (thisClass->controlEp > 0) {
            close(thisClass->controlEp);
            thisClass->controlEp = 0;
            resetEp = true;
        }
    } while (false);
    if (resetEp || thisClass->usbHandle.bulkIn != 0 || thisClass->usbHandle.bulkOut != 0) {
        return;
    }
    // until all bulkport reset
    if (thisClass->ConnectEPPoint(hUSB) != RET_SUCCESS) {
        return;
    }
    // connect OK
    thisClass->isAlive = true;
    thisClass->LoopUSBRead(hUSB, hUSB->wMaxPacketSizeSend);
}
}  // namespace Hdc