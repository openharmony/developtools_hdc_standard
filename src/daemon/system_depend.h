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
#ifndef HDC_SYSTEM_DEPEND_H
#define HDC_SYSTEM_DEPEND_H
#include "daemon_common.h"

namespace Hdc {
namespace SystemDepend {
#ifdef HDC_SUPPORT_FLASHD
    // deprecated, remove later
    inline bool GetDevItem(const char *key, string value)
    {
        return false;
    };
    inline bool SetDevItem(const char *key, const char *value)
    {
        return false;
    };
#else
    bool GetDevItem(const char *key, string &out, const char *preDefine = nullptr);
    bool SetDevItem(const char *key, const char *value);
#endif
    bool RebootDevice(const string &cmd);
}  // namespace SystemDepend
}  // namespace Hdc

#endif  // HDC_BASE_H
