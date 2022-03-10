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
#include "host_unity.h"

namespace Hdc {
HdcHostUnity::HdcHostUnity(HTaskInfo hTaskInfo)
    : HdcTaskBase(hTaskInfo)
{
    opContext.thisClass = this;
}

HdcHostUnity::~HdcHostUnity()
{
    WRITE_LOG(LOG_DEBUG, "HdcHostUnity::~HdcHostUnity finish");
}

bool HdcHostUnity::ReadyForRelease()
{
    if (!HdcTaskBase::ReadyForRelease()) {
        return false;
    }
    if (opContext.enableLog && !opContext.hasFilelogClosed) {
        return false;
    }
    return true;
}

void HdcHostUnity::StopTask()
{
    // Do not detect RunningProtect, force to close
    if (opContext.hasFilelogClosed) {
        return;
    }
    if (opContext.enableLog) {
        ++refCount;
        opContext.fsClose.data = &opContext;
        uv_fs_close(loopTask, &opContext.fsClose, opContext.fileLog, OnFileClose);
    }
};

void HdcHostUnity::OnFileClose(uv_fs_t *req)
{
    uv_fs_req_cleanup(req);
    ContextUnity *context = (ContextUnity *)req->data;
    HdcHostUnity *thisClass = (HdcHostUnity *)context->thisClass;
    context->hasFilelogClosed = true;
    --thisClass->refCount;
    return;
}

bool HdcHostUnity::InitLocalLog(const char *path)
{
    uv_fs_t reqFs;
    // block open
    if (uv_fs_open(nullptr, &reqFs, path, UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY, S_IWUSR | S_IRUSR, nullptr)
        < 0)
        return false;
    uv_fs_req_cleanup(&reqFs);
    opContext.fileLog = reqFs.result;
    return true;
}

void HdcHostUnity::OnFileIO(uv_fs_t *req)
{
    CtxUnityIO *contextIO = (CtxUnityIO *)req->data;
    ContextUnity *context = (ContextUnity *)contextIO->context;
    HdcHostUnity *thisClass = (HdcHostUnity *)context->thisClass;
    uint8_t *bufIO = contextIO->bufIO;
    uv_fs_req_cleanup(req);
    --thisClass->refCount;
    while (true) {
        if (req->result <= 0) {
            if (req->result < 0) {
                constexpr int bufSize = 1024;
                char buf[bufSize] = { 0 };
                uv_strerror_r((int)req->result, buf, bufSize);
                WRITE_LOG(LOG_DEBUG, "Error OnFileIO: %s", buf);
            }
            break;
        }
        context->fileIOIndex += req->result;
        break;
    }
    delete[] bufIO;
    delete contextIO;  // req is part of contextIO, no need to release
}

bool HdcHostUnity::AppendLocalLog(const char *bufLog, const int sizeLog)
{
    auto buf = new uint8_t[sizeLog];
    auto contextIO = new CtxUnityIO();
    if (!buf || !contextIO) {
        if (buf) {
            delete[] buf;
        }
        if (contextIO) {
            delete contextIO;
        }
        return false;
    }
    uv_fs_t *req = &contextIO->fs;
    contextIO->bufIO = buf;
    contextIO->context = &opContext;
    req->data = contextIO;
    ++refCount;

    if (memcpy_s(buf, sizeLog, bufLog, sizeLog)) {
    }
    uv_buf_t iov = uv_buf_init((char *)buf, sizeLog);
    uv_fs_write(loopTask, req, opContext.fileLog, &iov, 1, opContext.fileBufIndex, OnFileIO);
    opContext.fileBufIndex += sizeLog;
    return true;
}

bool HdcHostUnity::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    bool ret = true;
    // Both are executed, do not need to detect ChildReady
    switch (command) {
        case CMD_UNITY_BUGREPORT_INIT: {
            if (strlen((char *)payload)) {  // enable local log
                if (!InitLocalLog((const char *)payload)) {
                    LogMsg(MSG_FAIL, "Cannot set locallog");
                    ret = false;
                    break;
                };
                opContext.enableLog = true;
            }
            SendToAnother(CMD_UNITY_BUGREPORT_INIT, nullptr, 0);
            break;
        }
        case CMD_UNITY_BUGREPORT_DATA: {
            if (opContext.enableLog) {
                AppendLocalLog((const char *)payload, payloadSize);
            } else {
                ServerCommand(CMD_KERNEL_ECHO_RAW, payload, payloadSize);
            }
            break;
        }
        default:
            break;
    }
    return ret;
};
}  // namespace Hdc
