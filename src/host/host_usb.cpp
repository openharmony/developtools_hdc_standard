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
#include "host_usb.h"
#include "server.h"
#include <thread>

namespace Hdc {
HdcHostUSB::HdcHostUSB(const bool serverOrDaemonIn, void *ptrMainBase, void *ctxUSBin)
    : HdcUSBBase(serverOrDaemonIn, ptrMainBase)
{
    modRunning = false;
    if (!ctxUSBin) {
        return;
    }
    HdcServer *pServer = (HdcServer *)ptrMainBase;
    ctxUSB = (libusb_context *)ctxUSBin;
    uv_timer_init(&pServer->loopMain, &devListWatcher);
}

HdcHostUSB::~HdcHostUSB()
{
    if (modRunning) {
        Stop();
    }
    WRITE_LOG(LOG_DEBUG, "~HdcHostUSB");
}

void HdcHostUSB::Stop()
{
    if (!ctxUSB) {
        return;
    }
    Base::TryCloseHandle((uv_handle_t *)&devListWatcher);
    modRunning = false;
}

int HdcHostUSB::Initial()
{
    if (!ctxUSB) {
        WRITE_LOG(LOG_FATAL, "USB mod ctxUSB is nullptr, recompile please");
        return -1;
    }
    WRITE_LOG(LOG_DEBUG, "HdcHostUSB init");
    modRunning = true;
    StartupUSBWork();  // Main thread registration, IO in sub-thread
    return 0;
}

bool HdcHostUSB::DetectMyNeed(libusb_device *device, string &sn)
{
    bool ret = false;
    HUSB hUSB = new HdcUSB();
    hUSB->device = device;
    // just get usb SN, close handle immediately
    int childRet = OpenDeviceMyNeed(hUSB);
    if (childRet < 0) {
        WRITE_LOG(LOG_WARN, "DetectMyNeed childRet:%d sn:%s", childRet, sn.c_str());
        delete hUSB;
        return false;
    }
    libusb_release_interface(hUSB->devHandle, hUSB->interfaceNumber);
    libusb_close(hUSB->devHandle);
    hUSB->devHandle = nullptr;

    WRITE_LOG(LOG_INFO, "Needed device found, busid:%d devid:%d connectkey:%s", hUSB->busId, hUSB->devId,
              hUSB->serialNumber.c_str());
    // USB device is automatically connected after recognition, auto connect USB
    UpdateUSBDaemonInfo(hUSB, nullptr, STATUS_READY);
    HdcServer *hdcServer = (HdcServer *)clsMainBase;
    HSession hSession = hdcServer->MallocSession(true, CONN_USB, this);
    hSession->connectKey = hUSB->serialNumber;
    uv_timer_t *waitTimeDoCmd = new uv_timer_t;
    uv_timer_init(&hdcServer->loopMain, waitTimeDoCmd);
    waitTimeDoCmd->data = hSession;
    uv_timer_start(waitTimeDoCmd, hdcServer->UsbPreConnect, 0, DEVICE_CHECK_INTERVAL);
    mapIgnoreDevice[sn] = HOST_USB_REGISTER;
    delete hUSB;
    return true;
}

void HdcHostUSB::KickoutZombie(HSession hSession)
{
    HdcServer *ptrConnect = (HdcServer *)hSession->classInstance;
    HUSB hUSB = hSession->hUSB;
    if (!hUSB->devHandle) {
        WRITE_LOG(LOG_WARN, "KickoutZombie devHandle:%p isDead:%d", hUSB->devHandle, hSession->isDead);
        return;
    }
    if (LIBUSB_ERROR_NO_DEVICE != libusb_kernel_driver_active(hUSB->devHandle, hUSB->interfaceNumber)) {
        return;
    }
    WRITE_LOG(LOG_WARN, "KickoutZombie LIBUSB_ERROR_NO_DEVICE serialNumber:%s", hUSB->serialNumber.c_str());
    ptrConnect->FreeSession(hSession->sessionId);
}

void HdcHostUSB::RemoveIgnoreDevice(string &mountInfo)
{
    if (mapIgnoreDevice.count(mountInfo)) {
        mapIgnoreDevice.erase(mountInfo);
    }
}

void HdcHostUSB::ReviewUsbNodeLater(string &nodeKey)
{
    HdcServer *hdcServer = (HdcServer *)clsMainBase;
    // add to ignore list
    mapIgnoreDevice[nodeKey] = HOST_USB_IGNORE;
    int delayRemoveFromList = DEVICE_CHECK_INTERVAL * MINOR_TIMEOUT;  // wait little time for daemon reinit
    Base::DelayDo(&hdcServer->loopMain, delayRemoveFromList, 0, nodeKey, nullptr,
                  [this](const uint8_t flag, string &msg, const void *) -> void { RemoveIgnoreDevice(msg); });
}

void HdcHostUSB::WatchUsbNodeChange(uv_timer_t *handle)
{
    HdcHostUSB *thisClass = (HdcHostUSB *)handle->data;
    HdcServer *ptrConnect = (HdcServer *)thisClass->clsMainBase;
    libusb_device **devs = nullptr;
    libusb_device *dev = nullptr;
    // kick zombie
    ptrConnect->EnumUSBDeviceRegister(KickoutZombie);
    // find new
    ssize_t cnt = libusb_get_device_list(thisClass->ctxUSB, &devs);
    if (cnt < 0) {
        WRITE_LOG(LOG_FATAL, "Failed to get device list");
        return;
    }
    WRITE_LOG(LOG_DEBUG, "WatchUsbNodeChange cnt: %lu", cnt);
    int i = 0;
    // linux replug devid increment，windows will be not
    while ((dev = devs[i++]) != nullptr) {  // must postfix++
        string szTmpKey = Base::StringFormat("%d-%d", libusb_get_bus_number(dev), libusb_get_device_address(dev));
        // check is in ignore list
        UsbCheckStatus statusCheck = thisClass->mapIgnoreDevice[szTmpKey];
        if (statusCheck == HOST_USB_IGNORE || statusCheck == HOST_USB_REGISTER) {
            continue;
        }
        WRITE_LOG(LOG_DEBUG, "WatchUsbNodeChange szTmpKey:%s", szTmpKey.c_str());
        string sn = szTmpKey;
        if (!thisClass->DetectMyNeed(dev, sn)) {
            thisClass->ReviewUsbNodeLater(szTmpKey);
        }
    }
    libusb_free_device_list(devs, 1);
}

// Main thread USB operates in this thread
void HdcHostUSB::UsbWorkThread(void *arg)
{
    HdcHostUSB *thisClass = (HdcHostUSB *)arg;
    constexpr uint8_t USB_HANDLE_TIMEOUT = 30;  // second
    while (thisClass->modRunning) {
        struct timeval zerotime;
        zerotime.tv_sec = USB_HANDLE_TIMEOUT;
        zerotime.tv_usec = 0;  // if == 0,windows will be high CPU load
        libusb_handle_events_timeout(thisClass->ctxUSB, &zerotime);
    }
    WRITE_LOG(LOG_DEBUG, "Host Sessionbase usb workthread finish");
}

int HdcHostUSB::StartupUSBWork()
{
    // Because libusb(winusb backend) does not support hotplug under win32, we use list mode for all platforms
    WRITE_LOG(LOG_DEBUG, "USBHost loopfind mode");
    devListWatcher.data = this;
    uv_timer_start(&devListWatcher, WatchUsbNodeChange, 0, DEVICE_CHECK_INTERVAL);
    // Running pendding in independent threads does not significantly improve the efficiency
    uv_thread_create(&threadUsbWork, UsbWorkThread, this);
    return 0;
}

int HdcHostUSB::CheckDescriptor(HUSB hUSB)
{
    char serialNum[BUF_SIZE_MEDIUM] = "";
    int childRet = 0;
    struct libusb_device_descriptor desc;
    int curBus = libusb_get_bus_number(hUSB->device);
    int curDev = libusb_get_device_address(hUSB->device);
    hUSB->busId = curBus;
    hUSB->devId = curDev;
    if (libusb_get_device_descriptor(hUSB->device, &desc)) {
        WRITE_LOG(LOG_WARN, "CheckDescriptor libusb_get_device_descriptor failed %d-%d", curBus, curDev);
        return -1;
    }
    // Get the serial number of the device, if there is no serial number, use the ID number to replace
    // If the device is not in time, occasionally can't get it, this is determined by the external factor, cannot be
    // changed. LIBUSB_SUCCESS
    childRet = libusb_get_string_descriptor_ascii(hUSB->devHandle, desc.iSerialNumber, (uint8_t *)serialNum,
                                                  sizeof(serialNum));
    if (childRet < 0) {
        hUSB->serialNumber = Base::StringFormat("%d-%d", curBus, curDev);
    } else {
        hUSB->serialNumber = serialNum;
    }
    WRITE_LOG(LOG_DEBUG, "CheckDescriptor busId-devId:%d-%d serialNum:%s", curBus, curDev, serialNum);
    return 0;
}

// hSession can be null
void HdcHostUSB::UpdateUSBDaemonInfo(HUSB hUSB, HSession hSession, uint8_t connStatus)
{
    // add to list
    HdcServer *pServer = (HdcServer *)clsMainBase;
    HdcDaemonInformation di;
    di.connectKey = hUSB->serialNumber;
    di.connType = CONN_USB;
    di.connStatus = connStatus;
    di.hSession = hSession;
    di.usbMountPoint = "";
    di.usbMountPoint = Base::StringFormat("%d-%d", hUSB->busId, hUSB->devId);

    HDaemonInfo pDi = nullptr;
    HDaemonInfo hdiNew = &di;
    pServer->AdminDaemonMap(OP_QUERY, hUSB->serialNumber, pDi);
    if (!pDi) {
        pServer->AdminDaemonMap(OP_ADD, hUSB->serialNumber, hdiNew);
    } else {
        pServer->AdminDaemonMap(OP_UPDATE, hUSB->serialNumber, hdiNew);
    }
}

bool HdcHostUSB::IsDebuggableDev(const struct libusb_interface_descriptor *ifDescriptor)
{
    constexpr uint8_t harmonyEpNum = 2;
    constexpr uint8_t harmonyClass = 0xff;
    constexpr uint8_t harmonySubClass = 0x50;
    constexpr uint8_t harmonyProtocol = 0x01;

    if (ifDescriptor->bInterfaceClass != harmonyClass || ifDescriptor->bInterfaceSubClass != harmonySubClass
        || ifDescriptor->bInterfaceProtocol != harmonyProtocol) {
        WRITE_LOG(LOG_DEBUG,
            "IsDebuggableDev false bInterfaceClass:%d bInterfaceSubClass:%d bInterfaceProtocol:%d",
            ifDescriptor->bInterfaceClass, ifDescriptor->bInterfaceSubClass, ifDescriptor->bInterfaceProtocol);
        return false;
    }
    if (ifDescriptor->bNumEndpoints != harmonyEpNum) {
        WRITE_LOG(LOG_DEBUG, "IsDebuggableDev false bNumEndpoints:%d", ifDescriptor->bNumEndpoints);
        return false;
    }
    return true;
}

int HdcHostUSB::CheckActiveConfig(libusb_device *device, HUSB hUSB)
{
    unsigned int j = 0;
    int ret = -1;
    struct libusb_config_descriptor *descConfig = nullptr;
    if (libusb_get_active_config_descriptor(device, &descConfig)) {
        WRITE_LOG(LOG_WARN, "CheckActiveConfig failed descConfig:%p", descConfig);
        return -1;
    }
    for (j = 0; j < descConfig->bNumInterfaces; ++j) {
        const struct libusb_interface *interface = &descConfig->interface[j];
        if (interface->num_altsetting >= 1) {
            const struct libusb_interface_descriptor *ifDescriptor = &interface->altsetting[0];
            if (!IsDebuggableDev(ifDescriptor)) {
                continue;
            }
            WRITE_LOG(LOG_DEBUG, "CheckActiveConfig IsDebuggableDev passed and then check endpoint attr");
            hUSB->interfaceNumber = ifDescriptor->bInterfaceNumber;
            unsigned int k = 0;
            for (k = 0; k < ifDescriptor->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor *ep_desc = &ifDescriptor->endpoint[k];
                if ((ep_desc->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep_desc->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        hUSB->hostBulkIn.endpoint = ep_desc->bEndpointAddress;
                        hUSB->hostBulkIn.bulkInOut = true;
                    } else {
                        hUSB->hostBulkOut.endpoint = ep_desc->bEndpointAddress;
                        hUSB->wMaxPacketSizeSend = ep_desc->wMaxPacketSize;
                        hUSB->hostBulkOut.bulkInOut = false;
                    }
                }
            }
            if (hUSB->hostBulkIn.endpoint == 0 || hUSB->hostBulkOut.endpoint == 0) {
                WRITE_LOG(LOG_WARN, "CheckActiveConfig endpoint is 0");
                break;
            }
            ret = 0;
        }
    }
    libusb_free_config_descriptor(descConfig);
    WRITE_LOG(LOG_DEBUG, "CheckActiveConfig ret:%d", ret);
    return ret;
}

// multi-thread calll
void HdcHostUSB::CancelUsbIo(HSession hSession)
{
    WRITE_LOG(LOG_DEBUG, "HostUSB CancelUsbIo, ref:%u", uint32_t(hSession->ref));
    HUSB hUSB = hSession->hUSB;
    std::unique_lock<std::mutex> lock(hUSB->lockDeviceHandle);
    if (!hUSB->hostBulkIn.isShutdown) {
        if (!hUSB->hostBulkIn.isComplete) {
            libusb_cancel_transfer(hUSB->hostBulkIn.transfer);
            hUSB->hostBulkIn.cv.notify_one();
        } else {
            hUSB->hostBulkIn.isShutdown = true;
        }
    }
    if (!hUSB->hostBulkOut.isShutdown) {
        if (!hUSB->hostBulkOut.isComplete) {
            libusb_cancel_transfer(hUSB->hostBulkOut.transfer);
            hUSB->hostBulkOut.cv.notify_one();
        } else {
            hUSB->hostBulkOut.isShutdown = true;
        }
    }
}

// 3rd write child-hdc-workthread
// no use uvwrite, raw write to socketpair's fd
int HdcHostUSB::UsbToHdcProtocol(uv_stream_t *stream, uint8_t *appendData, int dataSize)
{
    HSession hSession = (HSession)stream->data;
    int fd = hSession->dataFd[STREAM_MAIN];
    fd_set fdSet;
    struct timeval timeout = { 3, 0 };
    FD_ZERO(&fdSet);
    FD_SET(fd, &fdSet);
    int index = 0;
    int childRet = 0;

    while (index < dataSize) {
        if ((childRet = select(fd + 1, NULL, &fdSet, NULL, &timeout)) <= 0) {
            WRITE_LOG(LOG_FATAL, "select error:%d [%s][%d]", errno, strerror(errno), childRet);
            break;
        }
        childRet = send(fd, (const char *)appendData + index, dataSize - index, 0);
        if (childRet < 0) {
            WRITE_LOG(LOG_FATAL, "UsbToHdcProtocol senddata err:%d [%s]", errno, strerror(errno));
            break;
        }
        index += childRet;
    }
    if (index != dataSize) {
        WRITE_LOG(LOG_FATAL, "UsbToHdcProtocol partialsenddata err:%d [%d]", index, dataSize);
        return ERR_IO_FAIL;
    }
    return index;
}

void LIBUSB_CALL HdcHostUSB::USBBulkCallback(struct libusb_transfer *transfer)
{
    auto *ep = static_cast<HostUSBEndpoint *>(transfer->user_data);
    std::unique_lock<std::mutex> lock(ep->mutexIo);
    bool retrySumit = false;
    int childRet = 0;
    do {
        if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
            WRITE_LOG(LOG_FATAL, "USBBulkCallback1 failed, ret:%d", transfer->status);
            break;
        }
        if (!ep->bulkInOut && transfer->actual_length != transfer->length) {
            transfer->length -= transfer->actual_length;
            transfer->buffer += transfer->actual_length;
            retrySumit = true;
            break;
        }
    } while (false);
    while (retrySumit) {
        childRet = libusb_submit_transfer(transfer);
        if (childRet != 0) {
            WRITE_LOG(LOG_FATAL, "USBBulkCallback2 failed, ret:%d", childRet);
            transfer->status = LIBUSB_TRANSFER_ERROR;
            break;
        }
        return;
    }
    ep->isComplete = true;
    ep->cv.notify_one();
}

int HdcHostUSB::SubmitUsbBio(HSession hSession, bool sendOrRecv, uint8_t *buf, int bufSize)
{
    HUSB hUSB = hSession->hUSB;
    int timeout = 0;
    int childRet = 0;
    int ret = ERR_IO_FAIL;
    HostUSBEndpoint *ep = nullptr;

    if (sendOrRecv) {
        timeout = GLOBAL_TIMEOUT * TIME_BASE;
        ep = &hUSB->hostBulkOut;
    } else {
        timeout = 0;  // infinity
        ep = &hUSB->hostBulkIn;
    }
    hUSB->lockDeviceHandle.lock();
    ep->isComplete = false;
    do {
        std::unique_lock<std::mutex> lock(ep->mutexIo);
        libusb_fill_bulk_transfer(ep->transfer, hUSB->devHandle, ep->endpoint, buf, bufSize, USBBulkCallback, ep,
                                  timeout);
        childRet = libusb_submit_transfer(ep->transfer);
        hUSB->lockDeviceHandle.unlock();
        if (childRet < 0) {
            WRITE_LOG(LOG_FATAL, "SubmitUsbBio libusb_submit_transfer failed, ret:%d", childRet);
            break;
        }
        ep->cv.wait(lock, [ep]() { return ep->isComplete; });
        if (ep->transfer->status != 0) {
            WRITE_LOG(LOG_FATAL, "SubmitUsbBio transfer failed, status:%d", ep->transfer->status);
            break;
        }
        ret = ep->transfer->actual_length;
    } while (false);
    return ret;
}

void HdcHostUSB::BeginUsbRead(HSession hSession)
{
    HUSB hUSB = hSession->hUSB;
    hUSB->hostBulkIn.isShutdown = false;
    hUSB->hostBulkOut.isShutdown = false;
    ++hSession->ref;
    // loop read
    std::thread([this, hSession, hUSB]() {
        int childRet = 0;
        int nextReadSize = 0;
        while (!hSession->isDead) {
            // if readIO < wMaxPacketSizeSend, libusb report overflow
            nextReadSize = (childRet < hUSB->wMaxPacketSizeSend ? hUSB->wMaxPacketSizeSend
                                                                : std::min(childRet, Base::GetUsbffsBulkSize()));
            childRet = SubmitUsbBio(hSession, false, hUSB->hostBulkIn.buf, nextReadSize);
            if (childRet < 0) {
                WRITE_LOG(LOG_FATAL, "Read usb failed, ret:%d", childRet);
                break;
            }
            childRet = SendToHdcStream(hSession, reinterpret_cast<uv_stream_t *>(&hSession->dataPipe[STREAM_MAIN]),
                                       hUSB->hostBulkIn.buf, childRet);
            if (childRet < 0) {
                WRITE_LOG(LOG_FATAL, "SendToHdcStream failed, ret:%d", childRet);
                break;
            }
        }
        --hSession->ref;
        auto server = reinterpret_cast<HdcServer *>(clsMainBase);
        hUSB->hostBulkIn.isShutdown = true;
        server->FreeSession(hSession->sessionId);
        WRITE_LOG(LOG_DEBUG, "Usb loop read finish");
    }).detach();
}

// ==0 Represents new equipment and is what we need,<0  my need
int HdcHostUSB::OpenDeviceMyNeed(HUSB hUSB)
{
    libusb_device *device = hUSB->device;
    int ret = -1;
    if (LIBUSB_SUCCESS != libusb_open(device, &hUSB->devHandle)) {
        return -100;
    }
    while (modRunning) {
        libusb_device_handle *handle = hUSB->devHandle;
        if (CheckDescriptor(hUSB)) {
            WRITE_LOG(LOG_WARN, "OpenDeviceMyNeed CheckDescriptor break");
            break;
        }
        if (CheckActiveConfig(device, hUSB)) {
            WRITE_LOG(LOG_WARN, "OpenDeviceMyNeed CheckActiveConfig break");
            break;
        }
        // USB filter rules are set according to specific device pedding device
        libusb_claim_interface(handle, hUSB->interfaceNumber);
        ret = 0;
        break;
    }
    if (ret) {
        // not my need device
        libusb_close(hUSB->devHandle);
        hUSB->devHandle = nullptr;
    }
    WRITE_LOG(LOG_DEBUG, "OpenDeviceMyNeed ret:%d", ret);
    return ret;
}

int HdcHostUSB::SendUSBRaw(HSession hSession, uint8_t *data, const int length)
{
    int ret = ERR_GENERIC;
    HdcSessionBase *server = reinterpret_cast<HdcSessionBase *>(hSession->classInstance);
    ++hSession->ref;
    ret = SubmitUsbBio(hSession, true, data, length);
    if (ret < 0) {
        WRITE_LOG(LOG_FATAL, "Send usb failed, ret:%d", ret);
        CancelUsbIo(hSession);
        hSession->hUSB->hostBulkOut.isShutdown = true;
        server->FreeSession(hSession->sessionId);
    }
    --hSession->ref;
    return ret;
}

bool HdcHostUSB::FindDeviceByID(HUSB hUSB, const char *usbMountPoint, libusb_context *ctxUSB)
{
    libusb_device **listDevices = nullptr;
    bool ret = false;
    char tmpStr[BUF_SIZE_TINY] = "";
    int busNum = 0;
    int devNum = 0;
    int curBus = 0;
    int curDev = 0;

    int device_num = libusb_get_device_list(ctxUSB, &listDevices);
    if (device_num <= 0) {
        libusb_free_device_list(listDevices, 1);
        return false;
    }
    if (strchr(usbMountPoint, '-') && EOK == strcpy_s(tmpStr, sizeof(tmpStr), usbMountPoint)) {
        *strchr(tmpStr, '-') = '\0';
        busNum = atoi(tmpStr);
        devNum = atoi(tmpStr + strlen(tmpStr) + 1);
    } else
        return false;

    int i = 0;
    for (i = 0; i < device_num; ++i) {
        struct libusb_device_descriptor desc;
        if (LIBUSB_SUCCESS != libusb_get_device_descriptor(listDevices[i], &desc)) {
            break;
        }
        curBus = libusb_get_bus_number(listDevices[i]);
        curDev = libusb_get_device_address(listDevices[i]);
        if ((curBus == busNum && curDev == devNum)) {
            hUSB->device = listDevices[i];
            int childRet = OpenDeviceMyNeed(hUSB);
            if (!childRet) {
                ret = true;
            }
            break;
        }
    }
    libusb_free_device_list(listDevices, 1);
    return ret;
}

bool HdcHostUSB::ReadyForWorkThread(HSession hSession)
{
    HdcUSBBase::ReadyForWorkThread(hSession);
    return true;
};

// Determines that daemonInfo must have the device
HSession HdcHostUSB::ConnectDetectDaemon(const HSession hSession, const HDaemonInfo pdi)
{
    HdcServer *pServer = (HdcServer *)clsMainBase;
    HUSB hUSB = hSession->hUSB;
    hUSB->usbMountPoint = pdi->usbMountPoint;
    hUSB->ctxUSB = ctxUSB;
    if (!FindDeviceByID(hUSB, hUSB->usbMountPoint.c_str(), hUSB->ctxUSB)) {
        pServer->FreeSession(hSession->sessionId);
        return nullptr;
    }
    UpdateUSBDaemonInfo(hUSB, hSession, STATUS_CONNECTED);
    BeginUsbRead(hSession);
    hUSB->usbMountPoint = pdi->usbMountPoint;
    WRITE_LOG(LOG_DEBUG, "HSession HdcHostUSB::ConnectDaemon");

    Base::StartWorkThread(&pServer->loopMain, pServer->SessionWorkThread, Base::FinishWorkThread, hSession);
    // wait for thread up
    while (hSession->childLoop.active_handles == 0) {
        uv_sleep(1);
    }
    auto ctrl = pServer->BuildCtrlString(SP_START_SESSION, 0, nullptr, 0);
    Base::SendToStream((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], ctrl.data(), ctrl.size());
    return hSession;
}
}  // namespace Hdc
