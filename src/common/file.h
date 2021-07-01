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
#ifndef HDC_FILE_TRANSFER_H
#define HDC_FILE_TRANSFER_H
#include "common.h"
#include "transfer.h"

namespace Hdc {
class HdcFile : public HdcTransferBase {
public:
    HdcFile(HTaskInfo hTaskInfo);
    virtual ~HdcFile();
    void StopTask();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);

protected:
private:
    bool SlaveCheck(uint8_t *payload, const int payloadSize);
    void CheckMaster(CtxFile *context);
    void WhenTransferFinish(CtxFile *context);
    bool BeginTransfer(CtxFile *context, const char *command);
    void TransferSummary(CtxFile *context);
};
} // namespace Hdc

#endif