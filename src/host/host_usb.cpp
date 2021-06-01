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

static constexpr uint32_t HDC_CLASS = 0xff;
static constexpr uint32_t HDC_SUBCLASS = 0x50;
static constexpr uint32_t HDC_PROTOCOL = 0x01;

namespace Hdc {
HdcHostUSB::HdcHostUSB(const bool serverOrDaemonIn, void *ptrMainBase, void *ctxUSBin)
    : HdcUSBBase(serverOrDaemonIn, ptrMainBase)
{
    modRunning = false;
    if (!ctxUSBin) {
        return;
    }
    HdcServer *pServer = (HdcServer *)ptrMainBase;
    uv_idle_init(&pServer->loopMain, &usbWork);
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
    Base::TryCloseHandle((uv_handle_t *)&usbWork);
    Base::TryCloseHandle((uv_handle_t *)&devListWatcher);
    if (hotplug) {
        libusb_hotplug_deregister_callback(ctxUSB, hotplug);
    }
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

bool HdcHostUSB::DetectMyNeed(libusb_device *device)
{
    bool ret = false;
    HUSB hUSB = new HdcUSB();
    hUSB->device = device;
    int childRet = OpenDeviceMyNeed(hUSB);
    if (!childRet) {
        WRITE_LOG(LOG_INFO, "Needed device found, busid:%d devid:%d connectkey:%s", hUSB->busId, hUSB->devId,
            hUSB->serialNumber.c_str());
        UpdateUSBDaemonInfo(hUSB, nullptr, STATUS_READY);
        libusb_release_interface(hUSB->devHandle, hUSB->interfaceNumber);
        libusb_close(hUSB->devHandle);
        hUSB->devHandle = nullptr;
        ret = true;

        // USB device is automatically connected after recognition, auto connect USB
        HdcServer *hdcServer = (HdcServer *)clsMainBase;
        HSession hSession = hdcServer->MallocSession(true, CONN_USB, this);
        hSession->connectKey = hUSB->serialNumber;
        uv_timer_t *waitTimeDoCmd = new uv_timer_t;
        uv_timer_init(&hdcServer->loopMain, waitTimeDoCmd);
        waitTimeDoCmd->data = hSession;
        uv_timer_start(waitTimeDoCmd, hdcServer->UsbPreConnect, 10, 100);
    }
    // return false; open failed Or not my need
    delete hUSB;
    return ret;
}

// The return value is negative, cancel the callback. PC-side USB device hot swappable callback processing function
int HdcHostUSB::HotplugHostUSBCallback(
    libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *userData)
{
    WRITE_LOG(LOG_DEBUG, "HotplugHostUSBCallback begin");
    HdcHostUSB *thisClass = (HdcHostUSB *)userData;
    HdcServer *ptrConnect = (HdcServer *)thisClass->clsMainBase;
    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
        // Hotplug has uniqueness, no need to detect repetition
        thisClass->DetectMyNeed(device);
    } else {  // LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT
        HSession hSession = ptrConnect->QueryUSBDeviceRegister(device, 0, 0);
        if (hSession) {
            ptrConnect->FreeSession(hSession->sessionId);
        }
        WRITE_LOG(LOG_DEBUG, "Hotplug device remove");
    }
    return 0;
}

// hotplug and sub-thread all called
void HdcHostUSB::PenddingUSBIO(uv_idle_t *handle)
{
    libusb_context *ctxUSB = (libusb_context *)handle->data;
    // every plug,handle，libusb_handle_events
    struct timeval zerotime;
    int nComplete = 0;
    zerotime.tv_sec = 0;
    zerotime.tv_usec = 1;  // if == 0,windows will be high CPU load
    libusb_handle_events_timeout_completed(ctxUSB, &zerotime, &nComplete);
}

// no hotplug support function2
void HdcHostUSB::KickoutZombie(HSession hSession)
{
    HdcServer *ptrConnect = (HdcServer *)hSession->classInstance;
    HUSB hUSB = hSession->hUSB;
    if (!hUSB->devHandle) {
        return;
    }
    if (LIBUSB_ERROR_NO_DEVICE != libusb_kernel_driver_active(hUSB->devHandle, hUSB->interfaceNumber)) {
        return;
    }
    ptrConnect->FreeSession(hSession->sessionId);
}

// no hotplug support function1
void HdcHostUSB::WatchDevPlugin(uv_timer_t *handle)
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
    int i = 0;
    // linux replug devid increment，windows will be not
    while ((dev = devs[i++]) != nullptr) {
        string szTmpKey = Base::StringFormat("%d-%d", libusb_get_bus_number(dev), libusb_get_device_address(dev));
        // check is in ignore list
        if (thisClass->mapIgnoreDevice[szTmpKey] == HOST_USB_IGNORE
            || thisClass->mapIgnoreDevice[szTmpKey] == HOST_USB_REGISTER) {
            continue;
        }
        if (!thisClass->DetectMyNeed(dev)) {
            // add to ignore device
            thisClass->mapIgnoreDevice[szTmpKey] = HOST_USB_IGNORE;
            WRITE_LOG(LOG_DEBUG, "Add %s to ignore list", szTmpKey.c_str());
        } else {
            thisClass->mapIgnoreDevice[szTmpKey] = HOST_USB_REGISTER;
            WRITE_LOG(LOG_DEBUG, "Add %s to register list", szTmpKey.c_str());
        }
    }
    libusb_free_device_list(devs, 1);
}

int HdcHostUSB::StartupUSBWork()
{
    //    LIBUSB_HOTPLUG_NO_FLAGS = 0,//Only the registered callback function will only be called when the plug is
    //    inserted. LIBUSB_HOTPLUG_ENUMERATE = 1<<0,//The program load initialization before the device has been
    //    inserted, and the registered callback function is called (execution, scanning)
    int childRet = 0;
    // if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
    if (1) {  // If do not support hotplug
        WRITE_LOG(LOG_DEBUG, "USBHost loopfind mode");
        devListWatcher.data = this;
        uv_timer_start(&devListWatcher, WatchDevPlugin, 0, 5000);  // 5s interval
    } else {                                                       // support hotplug
        WRITE_LOG(LOG_DEBUG, "USBHost hotplug mode");
        childRet = libusb_hotplug_register_callback(ctxUSB,
            static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
            LIBUSB_HOTPLUG_ENUMERATE, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_CLASS_PER_INTERFACE,
            HotplugHostUSBCallback, this, &hotplug);
    }
    if (childRet != 0) {
        return -2;
    }
    usbWork.data = ctxUSB;
    uv_idle_start(&usbWork, PenddingUSBIO);
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
        WRITE_LOG(LOG_DEBUG, "CheckDescriptor libusb_get_device_descriptor failed");
        return -1;
    }
    WRITE_LOG(LOG_DEBUG, "CheckDescriptor busid:%d devid:%d", curBus, curDev);
    // Get the serial number of the device, if there is no serial number, use the ID number to replace
    // If the device is not in time, occasionally can't get it, this is determined by the external factor, cannot be
    // changed. LIBUSB_SUCCESS
    childRet = libusb_get_string_descriptor_ascii(
        hUSB->devHandle, desc.iSerialNumber, (uint8_t *)serialNum, sizeof(serialNum));
    if (childRet < 0) {
        hUSB->serialNumber = Base::StringFormat("%d-%d", curBus, curDev);
    } else {
        hUSB->serialNumber = serialNum;
    }
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
    const int harmonyEpNum = 2;
    if (ifDescriptor->bInterfaceClass != HDC_CLASS || ifDescriptor->bInterfaceSubClass != HDC_SUBCLASS
        || ifDescriptor->bInterfaceProtocol != HDC_PROTOCOL) {
        return false;
    }
    if (ifDescriptor->bNumEndpoints != harmonyEpNum) {
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
        return -1;
    }
    for (j = 0; j < descConfig->bNumInterfaces; j++) {
        const struct libusb_interface *interface = &descConfig->interface[j];
        if (interface->num_altsetting >= 1) {
            const struct libusb_interface_descriptor *ifDescriptor = &interface->altsetting[0];
            if (!IsDebuggableDev(ifDescriptor)) {
                continue;
            }
            hUSB->interfaceNumber = ifDescriptor->bInterfaceNumber;
            unsigned int k = 0;
            for (k = 0; k < ifDescriptor->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep_desc = &ifDescriptor->endpoint[k];
                if ((ep_desc->bmAttributes & 0x03) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep_desc->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        hUSB->epDevice = ep_desc->bEndpointAddress;
                    } else {
                        hUSB->epHost = ep_desc->bEndpointAddress;
                    }
                }
            }
            if (hUSB->epDevice == 0 || hUSB->epHost == 0) {
                break;
            }
            ret = 0;
        }
    }
    libusb_free_config_descriptor(descConfig);
    return ret;
}

void LIBUSB_CALL HdcHostUSB::ReadUSBBulkCallback(struct libusb_transfer *transfer)
{
    HSession hSession = (HSession)transfer->user_data;
    int sizeUSBPacketHead = sizeof(USBHead);
    HdcHostUSB *thisClass = (HdcHostUSB *)hSession->classModule;
    bool bOK = false;
    while (true) {
        if (!thisClass->modRunning || (hSession->isDead && 0 == hSession->sendRef))
            break;
        if (LIBUSB_TRANSFER_COMPLETED != transfer->status)
            break;
        // usb data to logic
        Base::SendToStream((uv_stream_t *)&hSession->dataPipe[STREAM_MAIN],
            hSession->hUSB->bufDevice + sizeUSBPacketHead, transfer->actual_length - sizeUSBPacketHead);
        // loop self
        HUSB hUSB = hSession->hUSB;
        libusb_fill_bulk_transfer(transfer, hUSB->devHandle, hUSB->epDevice, hUSB->bufDevice, hUSB->bufSizeDevice,
            ReadUSBBulkCallback, hSession, 0);  // no user data
        libusb_submit_transfer(transfer);
        bOK = true;
        break;
    }
    if (!bOK) {
        WRITE_LOG(LOG_DEBUG, "ReadUSBBulkCallback send failed");
        libusb_free_transfer(transfer);
    }
}

void HdcHostUSB::RegisterReadCallback(HSession hSession)
{
    HUSB hUSB = hSession->hUSB;
    if (hSession->isDead || !modRunning) {
        return;
    }
    libusb_transfer *transfer_in = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(transfer_in, hUSB->devHandle, hUSB->epDevice, hUSB->bufDevice,
        hUSB->bufSizeDevice,                // Note: in_buffer is where input data
        ReadUSBBulkCallback, hSession, 0);  // no user data
    transfer_in->user_data = hSession;
    libusb_submit_transfer(transfer_in);
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
            break;
        }
        if (CheckActiveConfig(device, hUSB)) {
            break;
        }

        // USB filter rules are set according to specific device
        // pedding device
        libusb_claim_interface(handle, hUSB->interfaceNumber);
        ret = 0;
        break;
    }
    if (ret) {
        // not my need device
        libusb_close(hUSB->devHandle);
        hUSB->devHandle = nullptr;
    }
    return ret;
}

// at main thread
void LIBUSB_CALL HdcHostUSB::WriteUSBBulkCallback(struct libusb_transfer *transfer)
{
    USBHead *pUSBHead = (USBHead *)transfer->buffer;
    HSession hSession = (HSession)transfer->user_data;
    if (pUSBHead->indexNum + 1 == pUSBHead->total) {
        hSession->sendRef--;  // send finish
    }
    if (LIBUSB_TRANSFER_COMPLETED != transfer->status || (hSession->isDead && 0 == hSession->sendRef)) {
        WRITE_LOG(LOG_WARN, "SendUSBRaw status:%d", transfer->status);
        HdcSessionBase *thisClass = (HdcSessionBase *)hSession->classInstance;
        thisClass->FreeSession(hSession->sessionId);
    }
    delete[] transfer->buffer;
    libusb_free_transfer(transfer);
}

// libusb can send directly across threads?!!!
int HdcHostUSB::SendUSBRaw(HSession hSession, uint8_t *data, const int length)
{
    HUSB hUSB = hSession->hUSB;
    libusb_transfer *transfer_in = libusb_alloc_transfer(0);
    uint8_t *pSendBuf = new uint8_t[length];
    if (!pSendBuf) {
        return -1;
    }
    memcpy_s(pSendBuf, length, data, length);
    libusb_fill_bulk_transfer(
        transfer_in, hUSB->devHandle, hUSB->epHost, pSendBuf, length, WriteUSBBulkCallback, hSession, 3 * 1000);
    libusb_submit_transfer(transfer_in);
    return 0;
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
    for (i = 0; i < device_num; i++) {
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
    RegisterReadCallback(hSession);

    hUSB->usbMountPoint = pdi->usbMountPoint;
    WRITE_LOG(LOG_DEBUG, "HSession HdcHostUSB::ConnectDaemon");

    Base::StartWorkThread(&pServer->loopMain, pServer->SessionWorkThread, Base::FinishWorkThread, hSession);
    // wait for thread up
    while (hSession->childLoop.active_handles == 0) {
        uv_sleep(1);
    }
    // junk data to pullup acceptchild
    uint8_t *byteFlag = new uint8_t[1];
    *byteFlag = SP_START_SESSION;
    Base::SendToStreamEx((uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN], (uint8_t *)byteFlag, 1, nullptr,
        (void *)Base::SendCallback, byteFlag);

    return hSession;
}
}  // namespace Hdc
