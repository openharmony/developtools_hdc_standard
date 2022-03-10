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
#include "task.h"

namespace Hdc {
// -----------------------------------------------------------
// notice!!! The constructor is called at the Child thread, so in addition to initialization, do not excess actions, if
// destructor is required, please clean up in the subclasses.
HdcTaskBase::HdcTaskBase(HTaskInfo hTaskInfo)
{
    taskInfo = hTaskInfo;
    if (hTaskInfo != nullptr) {
        loopTask = hTaskInfo->runLoop;
        clsSession = hTaskInfo->ownerSessionClass;
    } else {
        loopTask = nullptr;
        clsSession = nullptr;
    }

    childReady = false;
    singalStop = false;
    refCount = 0;
    if (taskInfo != nullptr && taskInfo->masterSlave)
        SendToAnother(CMD_KERNEL_WAKEUP_SLAVETASK, nullptr, 0);
}

HdcTaskBase::~HdcTaskBase()
{
    WRITE_LOG(LOG_DEBUG, "~HdcTaskBase");
}

bool HdcTaskBase::ReadyForRelease()
{
    return refCount == 0;
}

// Only the Task work thread call is allowed to use only when Workfortask returns FALSE.
void HdcTaskBase::TaskFinish()
{
    uint8_t count = 1;
    SendToAnother(CMD_KERNEL_CHANNEL_CLOSE, &count, 1);
    WRITE_LOG(LOG_DEBUG, "HdcTaskBase::TaskFinish notify");
}

bool HdcTaskBase::SendToAnother(const uint16_t command, uint8_t *bufPtr, const int size)
{
    if (singalStop || taskInfo == nullptr) {
        return false;
    }
    HdcSessionBase *sessionBase = reinterpret_cast<HdcSessionBase *>(taskInfo->ownerSessionClass);
    if (sessionBase != nullptr) {
        return sessionBase->Send(taskInfo->sessionId, taskInfo->channelId, command, bufPtr, size) > 0;
    }
    return false;
}

void HdcTaskBase::LogMsg(MessageLevel level, const char *msg, ...)
{
    va_list vaArgs;
    va_start(vaArgs, msg);
    string log = Base::StringFormat(msg, vaArgs);
    va_end(vaArgs);
    HdcSessionBase *sessionBase = reinterpret_cast<HdcSessionBase *>(clsSession);
    if (sessionBase != nullptr) {
        sessionBase->LogMsg(taskInfo->sessionId, taskInfo->channelId, level, log.c_str());
    }
}

bool HdcTaskBase::ServerCommand(const uint16_t command, uint8_t *bufPtr, const int size)
{
    if (taskInfo == nullptr || taskInfo->ownerSessionClass == nullptr) {
        return false;
    }
    HdcSessionBase *hSession = (HdcSessionBase *)taskInfo->ownerSessionClass;
    return hSession->ServerCommand(taskInfo->sessionId, taskInfo->channelId, command, bufPtr, size);
}

// cross thread
int HdcTaskBase::ThreadCtrlCommunicate(const uint8_t *bufPtr, const int size)
{
    if (taskInfo == nullptr || taskInfo->ownerSessionClass == nullptr) {
        return false;
    }
    HdcSessionBase *sessionBase = (HdcSessionBase *)taskInfo->ownerSessionClass;
    HSession hSession = sessionBase->AdminSession(OP_QUERY, taskInfo->sessionId, nullptr);
    if (!hSession) {
        return -1;
    }
    uv_stream_t *handleStream = nullptr;
    if (uv_thread_self() == hSession->hWorkThread) {
        handleStream = (uv_stream_t *)&hSession->ctrlPipe[STREAM_MAIN];
    } else if (uv_thread_self() == hSession->hWorkChildThread) {
        handleStream = (uv_stream_t *)&hSession->ctrlPipe[STREAM_WORK];
    } else {
        return ERR_GENERIC;
    }
    return Base::SendToStream(handleStream, bufPtr, size);
}
}