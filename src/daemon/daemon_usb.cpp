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
    usbMain = nullptr;
    Base::ZeroStruct(sendEP);
    uv_mutex_init(&sendEP);
}

HdcDaemonUSB::~HdcDaemonUSB()
{
    // Closed in the IO loop, no longer closing CLOSEENDPOINT
}

void HdcDaemonUSB::Stop()
{
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB Stop");
    // Here only clean up the IO-related resources, session related resources clear reason to clean up the session
    // module
    modRunning = false;
    if (!usbMain) {
        return;
    }
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB Stop free main session");
    HdcDaemon *pServ = (HdcDaemon *)clsMainBase;
    Base::TryCloseHandle((uv_handle_t *)&checkEP);
    pServ->FreeSession(usbMain->sessionId);
    if (usbMain->hUSB != nullptr) {
        CloseEndpoint(usbMain->hUSB);
    }
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB Stop free main session finish");
    usbMain = nullptr;
    // workaround for sendEP mutex only
}

int HdcDaemonUSB::Initial()
{
    // 4.4   Kit Kat      |19, 20     |3.10
    // after Linux-3.8，kernel switch to the USB Function FS
    // Implement USB hdc function in user space
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB init");
    if (access(USB_FFS_HDC_EP0, F_OK) != 0) {
        WRITE_LOG(LOG_DEBUG, "Just support usb-ffs, must kernel3.8+ and enable usb-ffs, usbmod disable");
        return -1;
    }
    const uint16_t usbFfsScanInterval = 3000;
    HdcDaemon *pServ = (HdcDaemon *)clsMainBase;
    usbMain = pServ->MallocSession(false, CONN_USB, this);
    if (!usbMain) {
        WRITE_LOG(LOG_DEBUG, "CheckNewUSBDeviceThread malloc failed");
        return -1;
    }
    WRITE_LOG(LOG_DEBUG, "HdcDaemonUSB::Initiall");
    uv_timer_init(&pServ->loopMain, &checkEP);
    checkEP.data = this;
    uv_timer_start(&checkEP, WatchEPTimer, 0, usbFfsScanInterval);
    return 0;
}

// DAEMON end USB module USB-FFS EP port connection
int HdcDaemonUSB::ConnectEPPoint(HUSB hUSB)
{
    int ret = -1;
    while (true) {
        if (!hUSB->control) {
            // After the control port sends the instruction, the device is initialized by the device to the HOST host,
            // which can be found for USB devices. Do not send initialization to the EP0 control port, the USB
            // device will not be initialized by Host
            WRITE_LOG(LOG_DEBUG, "enter ConnectEPPoint");
            WRITE_LOG(LOG_DEBUG, "Begin send to control(EP0) for usb descriptor init");
            if ((hUSB->control = open(USB_FFS_HDC_EP0, O_RDWR)) < 0) {
                WRITE_LOG(LOG_WARN, "%s: cannot open control endpoint: errno=%d", USB_FFS_HDC_EP0, errno);
                break;
            }
            if (write(hUSB->control, &USB_FFS_DESC, sizeof(USB_FFS_DESC)) < 0) {
                WRITE_LOG(LOG_WARN, "%s: write ffs_descriptors failed: errno=%d", USB_FFS_HDC_EP0, errno);
                break;
            }
            if (write(hUSB->control, &USB_FFS_VALUE, sizeof(USB_FFS_VALUE)) < 0) {
                WRITE_LOG(LOG_WARN, "%s: write USB_FFS_VALUE failed: errno=%d", USB_FFS_HDC_EP0, errno);
                break;
            }
            // active usbrc，Send USB initialization singal
            Base::SetHdcProperty("sys.usb.ffs.ready", "1");
            WRITE_LOG(LOG_DEBUG, "ConnectEPPoint ctrl init finish, set usb-ffs ready");
        }
        if ((hUSB->bulkOut = open(USB_FFS_HDC_OUT, O_RDWR)) < 0) {
            WRITE_LOG(LOG_WARN, "%s: cannot open bulk-out ep: errno=%d", USB_FFS_HDC_OUT, errno);
            break;
        }
        if ((hUSB->bulkIn = open(USB_FFS_HDC_IN, O_RDWR)) < 0) {
            WRITE_LOG(LOG_WARN, "%s: cannot open bulk-in ep: errno=%d", USB_FFS_HDC_IN, errno);
            break;
        }
        ret = 0;
        break;
    }
    if (ret < 0) {
        CloseEndpoint(hUSB);
    }
    return ret;
}

void HdcDaemonUSB::CloseEndpoint(HUSB hUSB)
{
    if (!hUSB->isEPAlive) {
        return;
    }
    if (hUSB->bulkIn > 0) {
        close(hUSB->bulkIn);
        hUSB->bulkIn = 0;
    }
    if (hUSB->bulkOut > 0) {
        close(hUSB->bulkOut);
        hUSB->bulkOut = 0;
    }
    if (hUSB->control > 0) {
        close(hUSB->control);
        hUSB->control = 0;
    }
    hUSB->isEPAlive = false;
    currentSessionId = 0;
}

// Prevent other USB data misfortunes to send the program crash
bool HdcDaemonUSB::AvailablePacket(uint8_t *ioBuf, uint32_t *sessionId)
{
    bool ret = false;
    const uint8_t maxUSBPacketCount = 100;
    while (true) {
        struct USBHead *pHead = (struct USBHead *)ioBuf;
        if (memcmp(pHead->flag, PACKET_FLAG.c_str(), 2)) {
            break;
        }
        if (pHead->total > maxUSBPacketCount) {
            break;
        }
        if (pHead->total < pHead->indexNum) {
            break;
        }
        if (pHead->dataSize <= 0 || pHead->dataSize > MAX_SIZE_IOBUF * 2) {
            break;
        }
        *sessionId = pHead->sessionId;
        ret = true;
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

// daemon, usb-ffs data sends a critical function
// The speed of sending is too fast, IO will cause memory stacking, temporarily do not use asynchronous
int HdcDaemonUSB::SendUSBIOSync(HSession hSession, HUSB hMainUSB, uint8_t *data, const int length)
{
    int bulkIn = hMainUSB->bulkIn;
    int childRet = 0;
    int ret = -1;
    int offset = 0;
    if (!hMainUSB->isEPAlive) {
        goto Finish;
    }
    if (!modRunning) {
        goto Finish;
    }
    while (modRunning) {
        childRet = write(bulkIn, (uint8_t *)data + offset, length - offset);
        if (childRet <= 0) {
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
        WRITE_LOG(LOG_FATAL, "BulkinWrite write failed, nsize:%d really:%d, lasterror:%d", length, offset, errno);
    }
Finish:
    USBHead *pUSBHead = (USBHead *)data;
    if (pUSBHead->indexNum + 1 == pUSBHead->total) {
        hSession->sendRef--;  // send finish
    }
    if (ret < 0 && hMainUSB->isEPAlive) {
        WRITE_LOG(LOG_FATAL, "BulkinWrite CloseEndpoint");
        // It actually closed the subsession, the EP port is also closed
        CloseEndpoint(hMainUSB);
    }
    return ret;
}

int HdcDaemonUSB::SendUSBRaw(HSession hSession, uint8_t *data, const int length)
{
    HdcDaemon *daemon = (HdcDaemon *)hSession->classInstance;
    // Prevent memory stacking, send temporary way to use asynchronous
    // Generally sent in the same thread, but when new session is created, there is a possibility that the old session
    // is not retired.
    // At present, the radical transmission method is currently opened directly in various threads, and
    // it can be used exclusive File-DESC transmission mode in each thread. The late stage can be used as asynchronous +
    // SendPipe to the main thread transmission.
    uv_mutex_lock(&sendEP);
    if (SendUSBIOSync(hSession, usbMain->hUSB, data, length) < 0) {
        WRITE_LOG(LOG_DEBUG, "dusb.SendUSBRaw willfreesn: %p", hSession);
        daemon->FreeSession(hSession->sessionId);
    }
    uv_mutex_unlock(&sendEP);
    return length;
}

// cross thread call
void HdcDaemonUSB::OnNewHandshakeOK(const uint32_t sessionId)
{
    // Because the sessionId is synchronized with the host, the current session only can be recorded after the
    // session handshake ok
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(clsMainBase);
    // USB-ffs can't control the session through disconnect sigal like TCP,
    // so it can only replace the old session when the new session handshake ok.
    // limit onse session per host...
    if (currentSessionId != 0) {
        // The Host end program is restarted, but the USB cable is still connected
        daemon->PushAsyncMessage(currentSessionId, ASYNC_FREE_SESSION, nullptr, 0);
    }
    currentSessionId = sessionId;
}

HSession HdcDaemonUSB::PrepareNewSession(uint8_t *pRecvBuf, int recvBytesIO)
{
    HdcDaemon *daemon = (HdcDaemon *)clsMainBase;
    // new session
    HSession hChildSession = daemon->MallocSession(false, CONN_USB, this);
    if (!hChildSession) {
        return nullptr;
    }
    Base::StartWorkThread(&daemon->loopMain, daemon->SessionWorkThread, Base::FinishWorkThread, hChildSession);
    // wait for thread up
    constexpr int loopTime = 250;
    while (hChildSession->childLoop.active_handles == 0) {
        usleep(loopTime);
    }
    uint8_t flag = SP_START_SESSION;
    Base::SendToStream((uv_stream_t *)&hChildSession->ctrlPipe[STREAM_MAIN], &flag, 1);
    WRITE_LOG(LOG_DEBUG, "Main thread usbio mirgate finish");
    return hChildSession;
}

int HdcDaemonUSB::DispatchToWorkThread(HSession hSession, const uint32_t sessionId, uint8_t *readBuf, int readBytes)
{
    // Format:USBPacket1 payload1...USBPacketn
    // payloadn-[USBHead1(PayloadHead1+Payload1)]+[USBHead2(Payload2)]+...+[USBHeadN(PayloadN)]
    HSession hChildSession = nullptr;
    int sizeUSBPacketHead = sizeof(USBHead);
    HdcDaemon *daemon = reinterpret_cast<HdcDaemon *>(hSession->classInstance);
    hChildSession = daemon->AdminSession(OP_QUERY, sessionId, nullptr);
    if (!hChildSession) {
        hChildSession = PrepareNewSession(readBuf, readBytes);
        if (!hChildSession) {
            return -1;
        }
    }
    int ret = Base::SendToStream((uv_stream_t *)&hChildSession->dataPipe[STREAM_MAIN], readBuf + sizeUSBPacketHead,
                                 readBytes - sizeUSBPacketHead);
    return ret;
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
    HSession hSession = (HSession)req->data;
    if (hSession == nullptr) {
        return;
    }
    HdcDaemonUSB *thisClass = (HdcDaemonUSB *)hSession->classModule;
    HUSB hUSB = hSession->hUSB;
    if (hUSB == nullptr) {
        return;
    }
    uint8_t *bufPtr = hUSB->bufRecv;
    ssize_t bytesIOBytes = req->result;
    uint32_t sessionId = 0;
    bool ret = false;
    while (true) {
        // Don't care is module running, first deal with this
        if (bytesIOBytes < 0) {
            WRITE_LOG(LOG_WARN, "USBIO failed1 %s", uv_strerror(bytesIOBytes));
            break;
        }
        if (thisClass->JumpAntiquePacket(*bufPtr, bytesIOBytes)) {
            ret = true;
            break;
        }
        // guess is head of packet
        if (!thisClass->AvailablePacket((uint8_t *)bufPtr, &sessionId)) {
            WRITE_LOG(LOG_WARN, "AvailablePacket false, ret:%d buf:%-50s", bytesIOBytes, bufPtr);
            break;
        }
        if (thisClass->DispatchToWorkThread(hSession, sessionId, bufPtr, bytesIOBytes) < 0) {
            WRITE_LOG(LOG_FATAL, "DispatchToWorkThread failed");
            break;
        }
        if (thisClass->LoopUSBRead(hSession) < 0) {
            WRITE_LOG(LOG_FATAL, "LoopUSBRead failed");
            break;
        }
        ret = true;
        break;
    }
    delete[] bufPtr;
    uv_fs_req_cleanup(req);
    delete req;
    if (!ret || !thisClass->modRunning) {
        thisClass->CloseEndpoint(hSession->hUSB);
    }
}

int HdcDaemonUSB::LoopUSBRead(HSession hSession)
{
    int ret = -1;
    HUSB hUSB = hSession->hUSB;
    HdcDaemon *pServ = (HdcDaemon *)clsMainBase;
    uv_buf_t iov;
    // Reading size is greater than the biggest package
    int readMax = Base::GetMaxBufSize() * 1.2;
    uv_fs_t *req = new uv_fs_t();
    uint8_t *newBuf = new uint8_t[readMax]();
    if (!req || !newBuf) {
        WRITE_LOG(LOG_FATAL, "Memory alloc failed");
        goto FAILED;
    }
    req->data = hSession;
    hUSB->bufRecv = newBuf;
    iov = uv_buf_init((char *)newBuf, readMax);
    ret = uv_fs_read(&pServ->loopMain, req, hUSB->bulkOut, &iov, 1, -1, OnUSBRead);
    if (ret < 0) {
        WRITE_LOG(LOG_FATAL, "uv_fs_read < 0");
        goto FAILED;
    }
    return 0;

FAILED:
    if (newBuf) {
        delete[] newBuf;
    }
    if (req) {
        uv_fs_req_cleanup(req);
        delete req;
    }
    return -1;
}

bool HdcDaemonUSB::BeginEPRead(HSession hSession, uv_loop_t *loopDepend)
{
    HUSB hUSB = hSession->hUSB;
    hUSB->isEPAlive = true;
    LoopUSBRead(hSession);
    WRITE_LOG(LOG_DEBUG, "Begin loop USB read, %s",
              (uv_thread_self() == hSession->hWorkThread) ? "Main thread" : "Child thread");
    return true;
}

// Because USB can connect to only one host，daemonUSB is only one Session by default
void HdcDaemonUSB::WatchEPTimer(uv_timer_t *handle)
{
    HdcDaemonUSB *thisClass = (HdcDaemonUSB *)handle->data;
    HUSB hUSB = thisClass->usbMain->hUSB;
    HdcDaemon *pServ = (HdcDaemon *)thisClass->clsMainBase;
    if (hUSB->isEPAlive) {
        return;  // ok not todo...
    }
    if (thisClass->ConnectEPPoint(hUSB)) {
        return;
    }
    // connect OK
    thisClass->BeginEPRead(thisClass->usbMain, &pServ->loopMain);
}
}  // namespace Hdc