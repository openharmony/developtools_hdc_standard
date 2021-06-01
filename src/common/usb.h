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
    virtual int SendUSBRaw(HSession hSession, uint8_t *data, const int length)
    {
        return 0;
    }
    virtual bool ReadyForWorkThread(HSession hSession);
    int SendUSBBlock(HSession hSession, uint8_t *data, const int length);
    static void ReadUSB(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);

protected:
    void *clsMainBase;
    bool serverOrDaemon;
    bool modRunning;
};
}  // namespace Hdc

#endif