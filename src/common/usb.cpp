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
#include "usb.h"

namespace Hdc {
HdcUSBBase::HdcUSBBase(const bool serverOrDaemonIn, void *ptrMainBase)
{
    serverOrDaemon = serverOrDaemonIn;
    clsMainBase = ptrMainBase;
    modRunning = true;
}

HdcUSBBase::~HdcUSBBase()
{
}

void HdcUSBBase::ReadUSB(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    HSession hSession = (HSession)stream->data;
    HdcSessionBase *hSessionBase = (HdcSessionBase *)hSession->classInstance;
    if (hSessionBase->FetchIOBuf(hSession, hSession->ioBuf, nread) < 0) {
        hSessionBase->FreeSession(hSession->sessionId);
    }
}

bool HdcUSBBase::ReadyForWorkThread(HSession hSession)
{
    // Server-end USB IO is handed over to each sub-thread, only the daemon is still read by the main IO to distribute
    // to each sub-thread by DataPipe.
    if (uv_tcp_init(&hSession->childLoop, &hSession->dataPipe[STREAM_WORK])
        || uv_tcp_open(&hSession->dataPipe[STREAM_WORK], hSession->dataFd[STREAM_WORK])) {
        WRITE_LOG(LOG_FATAL, "USBBase ReadyForWorkThread init child TCP failed");
        return false;
    }
    hSession->dataPipe[STREAM_WORK].data = hSession;
    HdcSessionBase *pSession = (HdcSessionBase *)hSession->classInstance;
    Base::SetTcpOptions(&hSession->dataPipe[STREAM_WORK]);
    if (uv_read_start((uv_stream_t *)&hSession->dataPipe[STREAM_WORK], pSession->AllocCallback, ReadUSB)) {
        WRITE_LOG(LOG_FATAL, "USBBase ReadyForWorkThread child TCP read failed");
        return false;
    }
    WRITE_LOG(LOG_DEBUG, "USBBase ReadyForWorkThread finish");
    return true;
};

vector<uint8_t> HdcUSBBase::BuildPacketHeader(uint32_t sessionId, uint8_t option, uint32_t dataSize)
{
    vector<uint8_t> vecData;
    USBHead head;
    head.sessionId = htonl(sessionId);
    for (size_t i = 0; i < sizeof(head.flag); i++) {
        head.flag[i] = USB_PACKET_FLAG.data()[i];
    }
    head.option = option;
    head.dataSize = htonl(dataSize);
    vecData.insert(vecData.end(), (uint8_t *)&head, (uint8_t *)&head + sizeof(USBHead));
    return vecData;
}

// USB big data stream, block transmission, mainly to prevent accidental data packets from writing through EP port,
// inserting the send queue causes the program to crash
int HdcUSBBase::SendUSBBlock(HSession hSession, uint8_t *data, const int length)
{
    int childRet = 0;
    int ret = ERR_IO_FAIL;
    auto header = BuildPacketHeader(hSession->sessionId, USB_OPTION_HEADER, length);
    hSession->hUSB->lockDeviceHandle.lock();
    do {
        if ((childRet = SendUSBRaw(hSession, header.data(), header.size())) <= 0) {
            WRITE_LOG(LOG_FATAL, "SendUSBRaw index failed");
            break;
        }
        if ((childRet = SendUSBRaw(hSession, data, length)) <= 0) {
            WRITE_LOG(LOG_FATAL, "SendUSBRaw body failed");
            break;
        }
        if (childRet > 0 && (childRet % hSession->hUSB->wMaxPacketSizeSend == 0)) {
            // win32 send ZLP will block winusb driver and LIBUSB_TRANSFER_ADD_ZERO_PACKET not effect
            // so, we send dummy packet to prevent zero packet generate
            auto dummy = BuildPacketHeader(hSession->sessionId, 0, 0);
            if ((childRet = SendUSBRaw(hSession, dummy.data(), dummy.size())) <= 0) {
                WRITE_LOG(LOG_FATAL, "SendUSBRaw dummy failed");
                break;
            }
        }
        ret = length;
    } while (false);
    hSession->hUSB->lockDeviceHandle.unlock();
    return ret;
}

bool HdcUSBBase::IsUsbPacketHeader(uint8_t *ioBuf, int ioBytes)
{
    USBHead *usbPayloadHeader = (struct USBHead *)ioBuf;
    uint32_t maybeSize = ntohl(usbPayloadHeader->dataSize);
    bool isHeader = false;
    do {
        if (memcmp(usbPayloadHeader->flag, USB_PACKET_FLAG.c_str(), USB_PACKET_FLAG.size())) {
            break;
        }
        if (ioBytes != sizeof(USBHead)) {
            break;
        }
        if (maybeSize == 0) {
            isHeader = true;  // nop packet
            break;
        } else {  // maybeSize != 0
            if (usbPayloadHeader->option & USB_OPTION_HEADER) {
                isHeader = true;
                break;
            }
        }
    } while (false);
    return isHeader;
}

void HdcUSBBase::PreSendUsbSoftReset(HSession hSession, uint32_t sessionIdOld)
{
    HUSB hUSB = hSession->hUSB;
    if (hSession->serverOrDaemon && !hUSB->resetIO) {
        uint32_t sid = sessionIdOld;
        // or we can sendmsg to childthread send?
        hUSB->lockDeviceHandle.lock();
        ++hSession->ref;
        WRITE_LOG(LOG_WARN, "SendToHdcStream check, sessionId not matched");
        SendUsbSoftReset(hSession, sid);
        --hSession->ref;
        hUSB->lockDeviceHandle.unlock();
    }
}

int HdcUSBBase::CheckPacketOption(HSession hSession, uint8_t *appendData, int dataSize)
{
    HUSB hUSB = hSession->hUSB;
    // special short packet
    USBHead *header = (USBHead *)appendData;
    header->sessionId = ntohl(header->sessionId);
    header->dataSize = ntohl(header->dataSize);
    if (header->sessionId != hSession->sessionId) {
        // Only server do it here, daemon 'SendUsbSoftReset' no use
        // hilog + ctrl^C to reproduction scene
        //
        // Because the USB-reset API does not work on all platforms, the last session IO data may be
        // recveived, we need to ignore it.
        PreSendUsbSoftReset(hSession, header->sessionId);
        return 0;
    }
    if (header->option & USB_OPTION_HEADER) {
        // header packet
        hUSB->bulkinDataSize = header->dataSize;
    }
    // soft ZLP
    return hUSB->bulkinDataSize;
}

// return value: <0 error; = 0 all finish; >0 need size
int HdcUSBBase::SendToHdcStream(HSession hSession, uv_stream_t *stream, uint8_t *appendData, int dataSize)
{
    int childRet = 0;
    HUSB hUSB = hSession->hUSB;
    if (IsUsbPacketHeader(appendData, dataSize)) {
        return CheckPacketOption(hSession, appendData, dataSize);
    }
    if (hUSB->bulkinDataSize <= (uint32_t)childRet) {
        // last session data
        PreSendUsbSoftReset(hSession, 0);  // 0 == reset current
        return 0;
    }
    if ((childRet = UsbToHdcProtocol(stream, appendData, dataSize)) < 0) {
        WRITE_LOG(LOG_FATAL, "Error usb send to stream dataSize:%d", dataSize);
        return ERR_IO_FAIL;
    }
    hUSB->bulkinDataSize -= childRet;
    return hUSB->bulkinDataSize;
}

}