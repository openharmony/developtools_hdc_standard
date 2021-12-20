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
#ifndef HDC_DAEMON_USB_H
#define HDC_DAEMON_USB_H
#include "daemon_common.h"

namespace Hdc {
class HdcDaemonUSB : public HdcUSBBase {
public:
    HdcDaemonUSB(const bool serverOrDaemonIn, void *ptrMainBase);
    virtual ~HdcDaemonUSB();
    int Initial();
    void Stop();
    int SendUSBRaw(HSession hSession, uint8_t *data, const int length);
    void OnNewHandshakeOK(const uint32_t sessionId);
    void OnSessionFreeFinally(const HSession hSession);

private:
    struct CtxUvFileCommonIo {
        void *thisClass;
        void *data;
        uint8_t *buf;
        int bufSizeMax;
        int bufSize;
        bool atPollQueue;
        uv_fs_t req;
    };
    static void OnUSBRead(uv_fs_t *req);
    static void WatchEPTimer(uv_timer_t *handle);
    int ConnectEPPoint(HUSB hUSB);
    int DispatchToWorkThread(uint32_t sessionId, uint8_t *readBuf, int readBytes);
    int AvailablePacket(uint8_t *ioBuf, int ioBytes, uint32_t *sessionId);
    void CloseEndpoint(HUSB hUSB, bool closeCtrlEp = false);
    string GetDevPath(const std::string &path);
    bool ReadyForWorkThread(HSession hSession);
    int LoopUSBRead(HUSB hUSB, int readMaxWanted);
    HSession PrepareNewSession(uint32_t sessionId, uint8_t *pRecvBuf, int recvBytesIO);
    bool JumpAntiquePacket(const uint8_t &buf, ssize_t bytes) const;
    int SendUSBIOSync(HSession hSession, HUSB hMainUSB, const uint8_t *data, const int length);
    int CloseBulkEp(bool bulkInOut, int bulkFd, uv_loop_t *loop);
    void ResetOldSession(uint32_t sessionId);
    int GetMaxPacketSize(int fdFfs);
    int UsbToHdcProtocol(uv_stream_t *stream, uint8_t *appendData, int dataSize);
    void FillUsbV2Head(struct usb_functionfs_desc_v2 &descUsbFfs);

    HdcUSB usbHandle;
    string basePath;                // usb device's base path
    uint32_t currentSessionId = 0;  // USB mode,limit only one session
    uv_timer_t checkEP;             // server-use
    uv_mutex_t sendEP;
    bool isAlive = false;
    int controlEp = 0;  // EP0
    CtxUvFileCommonIo ctxRecv = {};
};
}  // namespace Hdc
#endif