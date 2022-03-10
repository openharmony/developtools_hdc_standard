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
#include "daemon_forward.h"

namespace Hdc {
HdcDaemonForward::HdcDaemonForward(HTaskInfo hTaskInfo)
    : HdcForwardBase(hTaskInfo)
{
}

HdcDaemonForward::~HdcDaemonForward()
{
}

void HdcDaemonForward::SetupJdwpPointCallBack(uv_idle_t *handle)
{
    HCtxForwardPtr ctxPoint = (HCtxForwardPtr)handle->data;
    HdcDaemonForward *thisClass = reinterpret_cast<HdcDaemonForward *>(ctxPoint->thisClass);
    thisClass->SetupPointContinue(ctxPoint, 1);  // It usually works
    Base::TryCloseHandle((const uv_handle_t *)handle, Base::CloseIdleCallback);
    WRITE_LOG(LOG_DEBUG, "Setup JdwpPointCallBack finish");
    --thisClass->refCount;
    return;
}

bool HdcDaemonForward::SetupJdwpPoint(HCtxForwardPtr ctxPoint)
{
    HdcDaemon *daemon = (HdcDaemon *)taskInfo->ownerSessionClass;
    HdcJdwp *clsJdwp = (HdcJdwp *)daemon->clsJdwp;
    uint32_t pid = std::stol(ctxPoint->localArgs[1]);
    if (ctxPoint->checkPoint) {  // checke
        bool ret = clsJdwp->CheckPIDExist(pid);
        SetupPointContinue(ctxPoint, (int)ret);
        WRITE_LOG(LOG_DEBUG, "Jdwp jump checkpoint");
        return true;
    }
    // do slave connect
    // fd[0] for forward, fd[1] for jdwp
    // forward to close fd[0], fd[1] for jdwp close
    int fds[2] = { 0 };
    bool ret = false;
    Base::CreateSocketPair(fds);
    if (uv_tcp_init(loopTask, &ctxPoint->tcp)) {
        return ret;
    }
    ctxPoint->tcp.data = ctxPoint;
    if (uv_tcp_open(&ctxPoint->tcp, fds[0])) {
        return ret;
    }
    constexpr auto len = sizeof(uint32_t);
    uint8_t flag[1 + len + len];
    flag[0] = SP_JDWP_NEWFD;
    if (memcpy_s(flag + 1, sizeof(flag) - 1, &pid, len) ||
        memcpy_s(flag + 1 + len, sizeof(flag) - len - 1, &fds[1], len)) {
        return ret;
    }
    if (ThreadCtrlCommunicate(flag, sizeof(flag)) > 0) {
        ret = true;
    }
    WRITE_LOG(LOG_DEBUG, "SendJdwpNewFD Finish,ret:%d fd0:%d fd1:%d", ret, fds[0], fds[1]);
    if (!ret) {
        Base::CloseSocketPair(fds);
        return ret;
    }

    ++refCount;
    Base::IdleUvTask(loopTask, ctxPoint, SetupJdwpPointCallBack);
    return ret;
}
}