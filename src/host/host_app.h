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
#ifndef HDC_HOST_APP_H
#define HDC_HOST_APP_H
#include "host_common.h"

namespace Hdc {
class HdcHostApp : public HdcTransferBase {
public:
    HdcHostApp(HTaskInfo hTaskInfo);
    virtual ~HdcHostApp();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);

private:
    bool BeginInstall(CtxFile *context, const char *command);
    void CheckMaster(CtxFile *context);
    bool CheckInstallContinue(AppModType mode, bool lastResult, const char *msg);
    void RunQueue(CtxFile *context);
    bool BeginSideload(CtxFile *context, const char *localPath);
};
}
#endif