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
#ifndef HDC_DEBUG_H
#define HDC_DEBUG_H
#include "common.h"

namespace Hdc {
namespace Debug {
    int WriteHexToDebugFile(const char *fileName, const uint8_t *buf, const int bufLen);
    int ReadHexFromDebugFile(const char *fileName, uint8_t *buf, const int bufLen);
    void DetermineThread(HSession hSession);
    int PrintfHexBuf(const uint8_t *buf, int bufLen);
}
}  // namespace Hdc

#endif