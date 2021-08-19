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
#ifndef HDC_HOST_UNITY_H
#define HDC_HOST_UNITY_H
#include "host_common.h"

namespace Hdc {
class HdcHostUnity : public HdcTaskBase {
public:
    HdcHostUnity(HTaskInfo hTaskInfo);
    virtual ~HdcHostUnity();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);
    void StopTask();
    bool ReadyForRelease();

private:
    struct ContextUnity {
        bool enableLog;
        uv_file fileLog;
        uint64_t fileIOIndex;
        uint64_t fileBufIndex;
        bool hasFilelogClosed;
        uv_fs_t fsClose;
        HdcHostUnity *thisClass;
    };
    struct CtxUnityIO {
        uv_fs_t fs;
        uint8_t *bufIO;
        ContextUnity *context;
    };
    static void OnFileIO(uv_fs_t *req);
    static void OnFileClose(uv_fs_t *req);
    bool InitLocalLog(const char *path);
    bool AppendLocalLog(const char *bufLog, const int sizeLog);

    ContextUnity opContext;
};
}  // namespace Hdc
#endif  // HDC_HOST_UNITY_H