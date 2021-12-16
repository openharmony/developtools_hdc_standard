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
#include "transfer.h"
#include "serial_struct.h"
#include <sys/stat.h>
#ifdef HARMONY_PROJECT
#include <lz4.h>
#endif

namespace Hdc {
constexpr uint64_t HDC_TIME_CONVERT_BASE = 1000000000;

HdcTransferBase::HdcTransferBase(HTaskInfo hTaskInfo)
    : HdcTaskBase(hTaskInfo)
{
    ResetCtx(&ctxNow, true);
    commandBegin = 0;
    commandData = 0;
}

HdcTransferBase::~HdcTransferBase()
{
    WRITE_LOG(LOG_DEBUG, "~HdcTransferBase");
};

bool HdcTransferBase::ResetCtx(CtxFile *context, bool full)
{
    if (full) {
        *context = {};
        context->fsOpenReq.data = context;
        context->fsCloseReq.data = context;
        context->thisClass = this;
        context->loop = loopTask;
        context->cb = OnFileIO;
    }
    context->closeNotify = false;
    context->indexIO = 0;
    return true;
}

int HdcTransferBase::SimpleFileIO(CtxFile *context, uint64_t index, uint8_t *sendBuf, int bytes)
{
    // The first 8 bytes file offset
    uint8_t *buf = new uint8_t[bytes]();
    CtxFileIO *ioContext = new CtxFileIO();
    bool ret = false;
    while (true) {
        if (!buf || !ioContext || bytes < 0) {
            break;
        }
        uv_fs_t *req = &ioContext->fs;
        ioContext->bufIO = buf;
        ioContext->context = context;
        req->data = ioContext;
        ++refCount;
        if (context->master) {  // master just read, and slave just write.when master/read, sendBuf can be nullptr
            uv_buf_t iov = uv_buf_init(reinterpret_cast<char *>(buf), bytes);
            uv_fs_read(context->loop, req, context->fsOpenReq.result, &iov, 1, index, context->cb);
        } else {
            // The US_FS_WRITE here must be brought into the actual file offset, which cannot be incorporated with local
            // accumulated index because UV_FS_WRITE will be executed multiple times and then trigger a callback.
            if (bytes > 0 && memcpy_s(ioContext->bufIO, bytes, sendBuf, bytes) != EOK) {
                break;
            }
            uv_buf_t iov = uv_buf_init(reinterpret_cast<char *>(ioContext->bufIO), bytes);
            uv_fs_write(context->loop, req, context->fsOpenReq.result, &iov, 1, index, context->cb);
        }
        ret = true;
        break;
    }
    if (!ret) {
        WRITE_LOG(LOG_WARN, "SimpleFileIO error");
        if (buf != nullptr) {
            delete[] buf;
            buf = nullptr;
        }
        if (ioContext != nullptr) {
            delete ioContext;
            ioContext = nullptr;
        }
        return -1;
    }
    return bytes;
}

void HdcTransferBase::OnFileClose(uv_fs_t *req)
{
    uv_fs_req_cleanup(req);
    CtxFile *context = (CtxFile *)req->data;
    HdcTransferBase *thisClass = (HdcTransferBase *)context->thisClass;
    if (context->closeNotify) {
        // close-step2
        thisClass->WhenTransferFinish(context);
    }
    --thisClass->refCount;
    return;
}

void HdcTransferBase::SetFileTime(CtxFile *context)
{
    if (!context->transferConfig.holdTimestamp) {
        return;
    }
    if (!context->transferConfig.mtime) {
        return;
    }
    uv_fs_t fs;
    double aTimeSec = static_cast<long double>(context->transferConfig.atime) / HDC_TIME_CONVERT_BASE;
    double mTimeSec = static_cast<long double>(context->transferConfig.mtime) / HDC_TIME_CONVERT_BASE;
    uv_fs_futime(nullptr, &fs, context->fsOpenReq.result, aTimeSec, mTimeSec, nullptr);
    uv_fs_req_cleanup(&fs);
}

bool HdcTransferBase::SendIOPayload(CtxFile *context, int index, uint8_t *data, int dataSize)
{
    TransferPayload payloadHead;
    string head;
    int compressSize = 0;
    int sendBufSize = payloadPrefixReserve + dataSize;
    uint8_t *sendBuf = new uint8_t[sendBufSize]();
    if (!sendBuf) {
        return false;
    }
    payloadHead.compressType = context->transferConfig.compressType;
    payloadHead.uncompressSize = dataSize;
    payloadHead.index = index;
    if (dataSize > 0) {
        switch (payloadHead.compressType) {
#ifdef HARMONY_PROJECT
            case COMPRESS_LZ4: {
                compressSize = LZ4_compress_default((const char *)data, (char *)sendBuf + payloadPrefixReserve,
                                                    dataSize, dataSize);
                break;
            }
#endif
            default: {  // COMPRESS_NONE
                if (memcpy_s(sendBuf + payloadPrefixReserve, sendBufSize - payloadPrefixReserve, data, dataSize)
                    != EOK) {
                    delete[] sendBuf;
                    return false;
                }
                compressSize = dataSize;
                break;
            }
        }
    }
    payloadHead.compressSize = compressSize;
    head = SerialStruct::SerializeToString(payloadHead);
    if (head.size() + 1 > payloadPrefixReserve) {
        delete[] sendBuf;
        return false;
    }
    int errCode = memcpy_s(sendBuf, sendBufSize, head.c_str(), head.size() + 1);
    if (errCode != EOK) {
        delete[] sendBuf;
        return false;
    }
    bool ret = SendToAnother(commandData, sendBuf, payloadPrefixReserve + compressSize) > 0;
    delete[] sendBuf;
    return ret;
}

void HdcTransferBase::OnFileIO(uv_fs_t *req)
{
    bool tryFinishIO = false;
    CtxFileIO *contextIO = (CtxFileIO *)req->data;
    CtxFile *context = (CtxFile *)contextIO->context;
    HdcTransferBase *thisClass = (HdcTransferBase *)context->thisClass;
    uint8_t *bufIO = contextIO->bufIO;
    uv_fs_req_cleanup(req);
    --thisClass->refCount;
    while (true) {
        if (req->result < 0) {
            WRITE_LOG(LOG_DEBUG, "OnFileIO error: %s", uv_strerror((int)req->result));
            context->closeNotify = true;
            tryFinishIO = true;
            break;
        }
        context->indexIO += req->result;
        if (req->fs_type == UV_FS_READ) {
            if (!thisClass->SendIOPayload(context, context->indexIO - req->result, bufIO, req->result)) {
                tryFinishIO = true;
                break;
            }
            if (context->indexIO < context->fileSize) {
                thisClass->SimpleFileIO(context, context->indexIO, nullptr,
                                        Base::GetMaxBufSize() * thisClass->maxTransferBufFactor);
            }
        } else if (req->fs_type == UV_FS_WRITE) {  // write
            if (context->indexIO >= context->fileSize) {
                // The active end must first read it first, but you can't make Finish first, because Slave may not
                // end.Only slave receives complete talents Finish
                context->closeNotify = true;
                tryFinishIO = true;
                thisClass->SetFileTime(context);
            }
        } else {
            tryFinishIO = true;
        }
        break;
    }
    delete[] bufIO;
    delete contextIO;  // Req is part of the Contextio structure, no free release
    if (tryFinishIO) {
        // close-step1
        ++thisClass->refCount;
        uv_fs_fsync(thisClass->loopTask, &context->fsCloseReq, context->fsOpenReq.result, nullptr);
        uv_fs_close(thisClass->loopTask, &context->fsCloseReq, context->fsOpenReq.result, OnFileClose);
    }
}

void HdcTransferBase::OnFileOpen(uv_fs_t *req)
{
    CtxFile *context = (CtxFile *)req->data;
    HdcTransferBase *thisClass = (HdcTransferBase *)context->thisClass;
    uv_fs_req_cleanup(req);
    WRITE_LOG(LOG_DEBUG, "Filemod openfile:%s", context->localPath.c_str());
    --thisClass->refCount;
    if (req->result < 0) {
        thisClass->LogMsg(MSG_FAIL, "Error opening file: %s, path:%s", uv_strerror((int)req->result),
                          context->localPath.c_str());
        thisClass->TaskFinish();
        return;
    }
    thisClass->ResetCtx(context);
    if (context->master) {
        // init master
        uv_fs_t fs;
        Base::ZeroStruct(fs.statbuf);
        uv_fs_fstat(nullptr, &fs, context->fsOpenReq.result, nullptr);
        TransferConfig &st = context->transferConfig;
        st.fileSize = fs.statbuf.st_size;
        st.optionalName = context->localName;
        if (st.holdTimestamp) {
            st.atime = fs.statbuf.st_atim.tv_sec * HDC_TIME_CONVERT_BASE + fs.statbuf.st_atim.tv_nsec;
            st.mtime = fs.statbuf.st_mtim.tv_sec * HDC_TIME_CONVERT_BASE + fs.statbuf.st_mtim.tv_nsec;
        }
        st.path = context->remotePath;
        // update ctxNow=context child value
        context->fileSize = st.fileSize;

        uv_fs_req_cleanup(&fs);
        thisClass->CheckMaster(context);
    } else {  // write
        thisClass->SendToAnother(thisClass->commandBegin, nullptr, 0);
    }
}

bool HdcTransferBase::MatchPackageExtendName(string fileName, string extName)
{
    bool match = false;
    int subfixIndex = fileName.rfind(extName);
    if ((fileName.size() - subfixIndex) != extName.size()) {
        return false;
    }
    match = true;
    return match;
}

// filter can be empty
int HdcTransferBase::GetSubFiles(const char *path, string filter, vector<string> *out)
{
    int retNum = 0;
    uv_fs_t req;
    Base::ZeroStruct(req);
    uv_dirent_t dent;
    vector<string> filterStrings;
    if (!strlen(path)) {
        return retNum;
    }
    if (filter.size()) {
        Base::SplitString(filter, ";", filterStrings);
    }

    if (uv_fs_scandir(nullptr, &req, path, 0, nullptr) < 0) {
        uv_fs_req_cleanup(&req);
        return retNum;
    }
    while (uv_fs_scandir_next(&req, &dent) != UV_EOF) {
        // Skip. File
        if (strcmp(dent.name, ".") == 0 || strcmp(dent.name, "..") == 0)
            continue;
        if (!(static_cast<uint32_t>(dent.type) & UV_DIRENT_FILE))
            continue;
        string fileName = dent.name;
        for (auto &&s : filterStrings) {
            int subfixIndex = fileName.rfind(s);
            if ((fileName.size() - subfixIndex) != s.size())
                continue;
            string fullPath = string(path) + "/";
            fullPath += fileName;
            out->push_back(fullPath);
            ++retNum;
        }
    }
    uv_fs_req_cleanup(&req);
    return retNum;
}

// https://en.cppreference.com/w/cpp/filesystem/is_directory
// return true if file exist， false if file not exist
bool HdcTransferBase::SmartSlavePath(string &cwd, string &localPath, const char *optName)
{
    string errStr;
    if (taskInfo->serverOrDaemon) {
        // slave and server
        ExtractRelativePath(cwd, localPath);
    }
    if (Base::CheckDirectoryOrPath(localPath.c_str(), true, false, errStr)) {
        WRITE_LOG(LOG_INFO, "%s", errStr.c_str());
        return true;
    }
    uv_fs_t req;
    int r = uv_fs_lstat(nullptr, &req, localPath.c_str(), nullptr);
    uv_fs_req_cleanup(&req);
    if (r == 0 && req.statbuf.st_mode & S_IFDIR) {  // is dir
        localPath = Base::StringFormat("%s%c%s", localPath.c_str(), Base::GetPathSep(), optName);
    }
    return false;
}

bool HdcTransferBase::RecvIOPayload(CtxFile *context, uint8_t *data, int dataSize)
{
    uint8_t *clearBuf = nullptr;
    string serialStrring((char *)data, payloadPrefixReserve);
    TransferPayload pld;
    bool ret = false;
    SerialStruct::ParseFromString(pld, serialStrring);
    clearBuf = new uint8_t[pld.uncompressSize]();
    if (!clearBuf) {
        return false;
    }
    int clearSize = 0;
    if (pld.compressSize > 0) {
        switch (pld.compressType) {
#ifdef HARMONY_PROJECT
            case COMPRESS_LZ4: {
                clearSize = LZ4_decompress_safe((const char *)data + payloadPrefixReserve, (char *)clearBuf,
                                                pld.compressSize, pld.uncompressSize);
                break;
            }
#endif
            default: {  // COMPRESS_NONE
                if (memcpy_s(clearBuf, pld.uncompressSize, data + payloadPrefixReserve, pld.compressSize) != EOK) {
                    delete[] clearBuf;
                    return false;
                }
                clearSize = pld.compressSize;
                break;
            }
        }
    }
    while (true) {
        if ((uint32_t)clearSize != pld.uncompressSize) {
            break;
        }
        if (SimpleFileIO(context, pld.index, clearBuf, clearSize) < 0) {
            break;
        }
        ret = true;
        break;
    }
    delete[] clearBuf;
    return ret;
}

bool HdcTransferBase::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    bool ret = true;
    while (true) {
        if (command == commandBegin) {
            CtxFile *context = &ctxNow;
            SimpleFileIO(context, context->indexIO, nullptr, Base::GetMaxBufSize() * maxTransferBufFactor);
            context->transferBegin = Base::GetRuntimeMSec();
        } else if (command == commandData) {
            if ((uint32_t)payloadSize > HDC_BUF_MAX_BYTES || payloadSize < 0) {
                ret = false;
                break;
            }
            // Note, I will trigger FileIO after multiple times.
            CtxFile *context = &ctxNow;
            if (!RecvIOPayload(context, payload, payloadSize)) {
                ret = false;
                break;
            }
        } else {
            // Other subclass commands
        }
        break;
    }
    return ret;
}

void HdcTransferBase::ExtractRelativePath(string &cwd, string &path)
{
    bool absPath = Base::IsAbsolutePath(path);
    if (!absPath) {
        path = cwd + path;
    }
}
}  // namespace Hdc
