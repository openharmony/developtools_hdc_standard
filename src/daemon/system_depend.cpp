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

defined __MUSL__ Has migrated to the latest version of harmony project

defined HARMONY_PROJECT
With openharmony toolchains suport. If not defined, it should be [device]buildroot or [PC]msys64(...)/ubuntu-apt(...)
envirments
############
*/
#include "system_depend.h"
#include "../common/base.h"
#if defined(__MUSL__) && defined(HARMONY_PROJECT)
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
#if defined __MUSL__
#ifdef HARMONY_PROJECT
        ret = SetParameter(key, value) == 0;
#else
        char outBuf[256] = "";
        string stringBuf = Base::StringFormat("param set %s %s", key, value);
        Base::RunPipeComand(stringBuf.c_str(), outBuf, sizeof(outBuf), true);
#endif  // HARMONY_PROJECT
#else   // not __MUSL__
#ifdef HDC_PCDEBUG
        WRITE_LOG(LOG_DEBUG, "SetDevItem, key:%s value:%s", key, value);
#else
        string keyValue = key;
        string stringBuf = "setprop " + keyValue + " " + value;
        system(stringBuf.c_str());
#endif  // HDC_PCDEBUG
#endif  // __MUSL__
        return ret;
    }

    bool GetDevItem(const char *key, string &out, string preDefine)
    {
        bool ret = true;
        char tmpStringBuf[BUF_SIZE_MEDIUM] = "";
#if defined __MUSL__
#ifdef HARMONY_PROJECT
        const string strKey(key);
        if (GetParameter(key, preDefine.c_str(), tmpStringBuf, BUF_SIZE_MEDIUM) < 0) {
            ret = false;
            Base::ZeroStruct(tmpStringBuf);
        }
#else
        string sFailString = Base::StringFormat("Get parameter \"%s\" fail", key);
        string stringBuf = "getparam " + string(key);
        Base::RunPipeComand(stringBuf.c_str(), tmpStringBuf, BUF_SIZE_MEDIUM - 1, true);
        if (!strcmp(sFailString.c_str(), tmpStringBuf)) {
            // failed
            ret = false;
            Base::ZeroStruct(tmpStringBuf);
        }
#endif
#else  // not __MUSL__
#ifdef HDC_PCDEBUG
        WRITE_LOG(LOG_DEBUG, "GetDevItem, key:%s", key);
#else
        string stringBuf = "getprop " + string(key);
        Base::RunPipeComand(stringBuf.c_str(), tmpStringBuf, BUF_SIZE_MEDIUM - 1, true);
#endif  // HDC_PCDEBUG
#endif  //__MUSL__
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
#if defined __MUSL__
        string reason;
        if (cmd == "recovery") {
            reason = "updater";
        } else if (cmd == "bootloader") {
            reason = "updater";
        }
        WRITE_LOG(LOG_DEBUG, "DoReboot with args:[%s] for cmd:[%s]", reason.c_str(), cmd.c_str());
        return CallDoReboot(reason.c_str());
#else
        const string rebootProperty = "sys.powerctl";
        string propertyVal;
        if (!cmd.size()) {
            propertyVal = "reboot";
        } else {
            propertyVal = Base::StringFormat("reboot,%s", cmd.c_str());
        }
        return SetDevItem(rebootProperty.c_str(), propertyVal.c_str());
#endif
    }
}
}  // namespace Hdc
