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
/*
############
This file is used to support compatibility between platforms, differences between old and new projects and
compilation platforms

defined HARMONY_PROJECT
With openharmony toolchains suport. If not defined, it should be [device]buildroot or [PC]msys64(...)/ubuntu-apt(...)
envirments
############
*/
#include "system_depend.h"
#include "../common/base.h"
#if defined(HARMONY_PROJECT)
extern "C" {
#include "init_reboot.h"
#include "parameter.h"
}
#endif

namespace Hdc {
namespace SystemDepend {
    bool SetDevItem(const char *key, const char *value)
    {
        bool ret = true;
#ifdef HARMONY_PROJECT
        ret = SetParameter(key, value) == 0;
#else
        char outBuf[256] = "";
        string stringBuf = Base::StringFormat("param set %s %s", key, value);
        Base::RunPipeComand(stringBuf.c_str(), outBuf, sizeof(outBuf), true);
#endif  // HARMONY_PROJECT
        return ret;
    }

    bool GetDevItem(const char *key, string &out, string preDefine)
    {
        bool ret = true;
        char tmpStringBuf[BUF_SIZE_MEDIUM] = "";
#ifdef HARMONY_PROJECT
        const string strKey(key);
        if (GetParameter(key, preDefine.c_str(), tmpStringBuf, BUF_SIZE_MEDIUM) < 0) {
            ret = false;
            Base::ZeroStruct(tmpStringBuf);
        }
#else
        string sFailString = Base::StringFormat("Get parameter \"%s\" fail", key);
        string stringBuf = "param get " + string(key);
        Base::RunPipeComand(stringBuf.c_str(), tmpStringBuf, BUF_SIZE_MEDIUM - 1, true);
        if (!strcmp(sFailString.c_str(), tmpStringBuf)) {
            // failed
            ret = false;
            Base::ZeroStruct(tmpStringBuf);
        }
#endif
        out = tmpStringBuf;
        return ret;
    }

    bool CallDoReboot(const char *reason)
    {
#ifdef HARMONY_PROJECT
        return DoReboot(reason);
#else
        // todo
        return false;
#endif
    }

    bool RebootDevice(const string &cmd)
    {
        string reason;
        if (cmd == "recovery") {
            reason = "updater";
        } else if (cmd == "bootloader") {
            reason = "updater";
        }
        WRITE_LOG(LOG_DEBUG, "DoReboot with args:[%s] for cmd:[%s]", reason.c_str(), cmd.c_str());
        return CallDoReboot(reason.c_str());
    }
}
}  // namespace Hdc
