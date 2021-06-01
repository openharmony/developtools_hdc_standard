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
#ifndef HDC_DAEMON_APP_H
#define HDC_DAEMON_APP_H
#include "daemon_common.h"

namespace Hdc {
class HdcDaemonApp : public HdcTransferBase {
public:
    HdcDaemonApp(HTaskInfo hTaskInfo);
    virtual ~HdcDaemonApp();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);
    bool ReadyForRelease();

private:
    void WhenTransferFinish(CtxFile *context);
    void PackageShell(bool installOrUninstall, const char *options, const char *package);
    void AsyncInstallFinish(bool runOK, const string result);
    void Sideload(const char *pathOTA);

    AsyncCmd asyncCommand;
    AsyncCmd::CmdResultCallback funcAppModFinish;
    AppModType mode = APPMOD_NONE;
};
}  // namespace Hdc
#endif