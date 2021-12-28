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
#ifndef HDC_USB_H
#define HDC_USB_H
#include "common.h"

namespace Hdc {
class HdcUSBBase {
public:
    HdcUSBBase(const bool serverOrDaemonIn, void *ptrMainBase);
    virtual ~HdcUSBBase();
    virtual bool ReadyForWorkThread(HSession hSession);
    virtual void CancelUsbIo(HSession hSession) {};
    int SendUSBBlock(HSession hSession, uint8_t *data, const int length);

protected:
    virtual int SendUSBRaw(HSession hSession, uint8_t *data, const int length)
    {
        return 0;
    }
    virtual int UsbToHdcProtocol(uv_stream_t *stream, uint8_t *appendData, int dataSize)
    {
        return 0;
    };
    int SendToHdcStream(HSession hSession, uv_stream_t *stream, uint8_t *appendData, int dataSize);
    int GetSafeUsbBlockSize(uint16_t wMaxPacketSizeSend);
    bool IsUsbPacketHeader(uint8_t *ioBuf, int ioBytes);

    void *clsMainBase;
    bool modRunning;
    bool serverOrDaemon;
    const string USB_PACKET_FLAG = "UB";  // must 2bytes

private:
    static void ReadUSB(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    vector<uint8_t> BuildPacketHeader(uint32_t sessionId, uint8_t option, uint32_t dataSize);
    int CheckPacketOption(HSession hSession, uint8_t *appendData, int dataSize);
    void PreSendUsbSoftReset(HSession hSession, uint32_t sessionIdOld);
};
}  // namespace Hdc

#endif