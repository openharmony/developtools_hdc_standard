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
#include "file_descriptor.h"

namespace Hdc {
HdcFileDescriptor::HdcFileDescriptor(uv_loop_t *loopIn, int fdToRead, void *callerContextIn,
                                     CallBackWhenRead callbackReadIn, CmdResultCallback callbackFinishIn)
{
    loop = loopIn;
    workContinue = true;
    callbackFinish = callbackFinishIn;
    callbackRead = callbackReadIn;
    fdIO = fdToRead;
    refIO = 0;
    callerContext = callerContextIn;
}

HdcFileDescriptor::~HdcFileDescriptor()
{
    if (refIO > 0) {
        WRITE_LOG(LOG_FATAL, "~HdcFileDescriptor refIO > 0");
    }
}

bool HdcFileDescriptor::ReadyForRelease()
{
    return refIO == 0;
}

// just tryCloseFdIo = true, callback will be effect
void HdcFileDescriptor::StopWork(bool tryCloseFdIo, std::function<void()> closeFdCallback)
{
    workContinue = false;
    callbackCloseFd = closeFdCallback;
    if (tryCloseFdIo && refIO > 0) {
        ++refIO;
        reqClose.data = this;
        uv_fs_close(loop, &reqClose, fdIO, [](uv_fs_t *req) {
            if (req == nullptr || req->data == nullptr) {
                return;
            }
            auto thisClass = (HdcFileDescriptor *)req->data;
            uv_fs_req_cleanup(req);
            if (thisClass->callbackCloseFd != nullptr) {
                thisClass->callbackCloseFd();
            }
            --thisClass->refIO;
        });
    }
};

void HdcFileDescriptor::OnFileIO(uv_fs_t *req)
{
    if (req == nullptr || req->data == nullptr) {
        return;
    }
    CtxFileIO *ctxIO = static_cast<CtxFileIO *>(req->data);
    HdcFileDescriptor *thisClass = ctxIO->thisClass;
    uint8_t *buf = ctxIO->bufIO;
    if (thisClass == nullptr || buf == nullptr) {
        return;
    }
    bool bFinish = false;
    bool fetalFinish = false;

    do {
        if (req->result > 0) {
            if (req->fs_type == UV_FS_READ) {
                if (!thisClass->callbackRead(thisClass->callerContext, buf, req->result)) {
                    bFinish = true;
                    break;
                }
                thisClass->LoopRead();
            } else {
                // fs_write
            }
        } else {
            if (req->result != 0) {
                constexpr int bufSize = 1024;
                char buf[bufSize] = { 0 };
                uv_strerror_r((int)req->result, buf, bufSize);
                WRITE_LOG(LOG_DEBUG, "OnFileIO fd:%d failed:%s", thisClass->fdIO, buf);
            }
            bFinish = true;
            fetalFinish = true;
            break;
        }
    } while (false);
    uv_fs_req_cleanup(req);
    if (buff != nullptr) {
        delete[] buf;
    }

    if (ctxIO == nullptr) {
        delete ctxIO;
    }

    --thisClass->refIO;
    if (bFinish) {
        thisClass->callbackFinish(thisClass->callerContext, fetalFinish, STRING_EMPTY);
        thisClass->workContinue = false;
    }
}

int HdcFileDescriptor::LoopRead()
{
    uv_buf_t iov;
    int readMax = Base::GetMaxBufSize() * 1.2;
    auto contextIO = new(std::nothrow) CtxFileIO();
    auto buf = new(std::nothrow) uint8_t[readMax]();
    if (!contextIO || !buf) {
        if (contextIO) {
            delete contextIO;
        }
        if (buf) {
            delete[] buf;
        }
        WRITE_LOG(LOG_FATAL, "Memory alloc failed");
        callbackFinish(callerContext, true, "Memory alloc failed");
        return -1;
    }
    uv_fs_t *req = &contextIO->fs;
    contextIO->bufIO = buf;
    contextIO->thisClass = this;
    req->data = contextIO;
    ++refIO;
    iov = uv_buf_init((char *)buf, readMax);
    uv_fs_read(loop, req, fdIO, &iov, 1, -1, OnFileIO);
    return 0;
}

bool HdcFileDescriptor::StartWork()
{
    if (LoopRead() < 0) {
        return false;
    }
    return true;
}

int HdcFileDescriptor::Write(uint8_t *data, int size)
{
    if (size > static_cast<int>(HDC_BUF_MAX_BYTES - 1)) {
        size = static_cast<int>(HDC_BUF_MAX_BYTES - 1);
    }
    if (size <= 0) {
        WRITE_LOG(LOG_WARN, "Write failed, size:%d", size);
        return -1;
    }
    auto buf = new uint8_t[size];
    if (!buf) {
        return -1;
    }
    (void)memcpy_s(buf, size, data, size);
    return WriteWithMem(buf, size);
}

// Data's memory must be Malloc, and the callback FREE after this function is completed
int HdcFileDescriptor::WriteWithMem(uint8_t *data, int size)
{
    auto contextIO = new(std::nothrow) CtxFileIO();
    if (!contextIO) {
        delete[] data;
        WRITE_LOG(LOG_FATAL, "Memory alloc failed");
        callbackFinish(callerContext, true, "Memory alloc failed");
        return -1;
    }
    uv_fs_t *req = &contextIO->fs;
    contextIO->bufIO = data;
    contextIO->thisClass = this;
    req->data = contextIO;
    ++refIO;

    uv_buf_t iov = uv_buf_init((char *)data, size);
    uv_fs_write(loop, req, fdIO, &iov, 1, -1, OnFileIO);
    return size;
}
}  // namespace Hdc
