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
    int SendUSBRaw(HSession hSession, uint8_t *data, const int length);
    HSession ConnectDetectDaemon(const HSession hSession, const HDaemonInfo pdi);
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
    static void LIBUSB_CALL ReadUSBBulkCallback(struct libusb_transfer *transfer);
    static void PenddingUSBIO(uv_idle_t *handle);
    static void LIBUSB_CALL WriteUSBBulkCallback(struct libusb_transfer *transfer);
    static void WatchDevPlugin(uv_timer_t *handle);
    static void KickoutZombie(HSession hSession);
    int StartupUSBWork();
    int CheckActiveConfig(libusb_device *device, HUSB hUSB);
    void RegisterReadCallback(HSession hSession);
    int OpenDeviceMyNeed(HUSB hUSB);
    int CheckDescriptor(HUSB hUSB);
    bool IsDebuggableDev(const struct libusb_interface_descriptor *ifDescriptor);
    bool ReadyForWorkThread(HSession hSession);
    bool FindDeviceByID(HUSB hUSB, const char *usbMountPoint, libusb_context *ctxUSB);
    void UpdateUSBDaemonInfo(HUSB hUSB, HSession hSession, uint8_t connStatus);
    bool DetectMyNeed(libusb_device *device, string &sn);
    void SendUsbReset(HUSB hUSB, uint32_t sessionId);
    void RestoreHdcProtocol(HUSB hUsb, const uint8_t *buf, int bufSize);

    uv_idle_t usbWork;
    libusb_context *ctxUSB;
    uv_timer_t devListWatcher;
    map<string, UsbCheckStatus> mapIgnoreDevice;
    uv_sem_t semUsbSend;

private:
};
}  // namespace Hdc
#endif