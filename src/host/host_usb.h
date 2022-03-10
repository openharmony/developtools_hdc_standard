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
#ifndef HDC_HOST_USB_H
#define HDC_HOST_USB_H
#include "host_common.h"

namespace Hdc {
class HdcHostUSB : public HdcUSBBase {
public:
    HdcHostUSB(const bool serverOrDaemonIn, void *ptrMainBase, void *ctxUSBin);
    virtual ~HdcHostUSB();
    int Initial();
    int SendUSBRaw(HSessionPtr hSessionPtr, uint8_t *data, const int length);
    HSessionPtr ConnectDetectDaemon(const HSessionPtr hSessionPtr, const HDaemonInfoPtr pdi);
    void Stop();
    void RemoveIgnoreDevice(string &mountInfo);

private:
    enum UsbCheckStatus {
        HOST_USB_IGNORE = 1,
        HOST_USB_READY,
        HOST_USB_REGISTER,
    };
    static int LIBUSB_CALL HotplugHostUSBCallback(libusb_context *ctx, libusb_device *device,
                                                  libusb_hotplug_event event, void *userData);
    static void UsbWorkThread(void *arg);  // 3rd thread
    static void WatchUsbNodeChange(uv_timer_t *handle);
    static void KickoutZombie(HSessionPtr hSessionPtr);
    static void LIBUSB_CALL USBBulkCallback(struct libusb_transfer *transfer);
    int StartupUSBWork();
    int CheckActiveConfig(libusb_device *device, HUSBPtr hUSB);
    int OpenDeviceMyNeed(HUSBPtr hUSB);
    int CheckDescriptor(HUSBPtr hUSB);
    bool IsDebuggableDev(const struct libusb_interface_descriptor *ifDescriptor);
    bool ReadyForWorkThread(HSessionPtr hSessionPtr);
    bool FindDeviceByID(HUSBPtr hUSB, const char *usbMountPoint, libusb_context *ctxUSB);
    bool DetectMyNeed(libusb_device *device, string &sn);
    void RestoreHdcProtocol(HUSBPtr hUsb, const uint8_t *buf, int bufSize);
    void UpdateUSBDaemonInfo(HUSBPtr hUSB, HSessionPtr hSessionPtr, uint8_t connStatus);
    void BeginUsbRead(HSessionPtr hSessionPtr);
    void ReviewUsbNodeLater(string &nodeKey);
    void CancelUsbIo(HSessionPtr hSessionPtr);
    int UsbToHdcProtocol(uv_stream_t *stream, uint8_t *appendData, int dataSize);
    int SubmitUsbBio(HSessionPtr hSessionPtr, bool sendOrRecv, uint8_t *buf, int bufSize);

    libusb_context *ctxUSB;
    uv_timer_t devListWatcher;
    map<string, UsbCheckStatus> mapIgnoreDevice;

private:
    uv_thread_t threadUsbWork;
};
}  // namespace Hdc
#endif