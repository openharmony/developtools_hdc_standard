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
#include <filesystem>
#include <sys/stat.h>
namespace fs = std::filesystem;
#ifdef HARMONY_PROJECT
#include <lz4.h>
#endif

namespace Hdc {
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
    context->fsOpenReq.data = context;
    context->fsCloseReq.data = context;
    context->thisClass = this;
    context->closeNotify = false;
    context->indexIO = 0;
    context->loop = loopTask;
    context->cb = OnFileIO;
    if (full) {
        context->localPath = "";
        context->remotePath = "";
        context->transferBegin = 0;
        context->taskQueue.clear();
    }
    return true;
}

int HdcTransferBase::SimpleFileIO(CtxFile *context, uint64_t index, uint8_t *sendBuf, int bytes)
{
    // The first 8 bytes file offset
    uint8_t *buf = new uint8_t[bytes]();
    CtxFileIO *ioContext = new CtxFileIO();
    bool ret = false;
    while (true) {
        if (!buf || !ioContext || !bytes) {
            break;
        }
        uv_fs_t *req = &ioContext->fs;
        ioContext->bufIO = buf;
        ioContext->context = context;
        req->data = ioContext;
        refCount++;
        if (context->master) {  // master just read, and slave just write.when master/read, sendBuf can be nullptr
            uv_buf_t iov = uv_buf_init(reinterpret_cast<char *>(buf), bytes);
            uv_fs_read(context->loop, req, context->fsOpenReq.result, &iov, 1, index, context->cb);
        } else {
            // The US_FS_WRITE here must be brought into the actual file offset, which cannot be incorporated with local
            // accumulated index because UV_FS_WRITE will be executed multiple times and then trigger a callback.
            if (!sendBuf || !bytes) {
                break;
            }
            if (memcpy_s(buf, bytes, sendBuf, bytes) != EOK) {
                break;
            }
            uv_buf_t iov = uv_buf_init(reinterpret_cast<char *>(buf), bytes);
            uv_fs_write(context->loop, req, context->fsOpenReq.result, &iov, 1, index, context->cb);
        }
        ret = true;
        break;
    }
    if (!ret) {
        WRITE_LOG(LOG_WARN, "txf.SimpleFileIO reterr");
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
    WRITE_LOG(LOG_WARN, "txf.SimpleFileIO okret bytes: %d", bytes);
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
    thisClass->refCount--;
    return;
}

void HdcTransferBase::SetFileTime(CtxFile *context)
{
    if (context->transferConfig.holdTimestamp) {
        return;
    }
    if (!context->transferConfig.mtime) {
        return;
    }
    uv_fs_t fs;
    // clang-format off
    uv_fs_futime(nullptr, &fs, context->fsOpenReq.result, context->transferConfig.atime,
                 context->transferConfig.mtime, nullptr);
    // clang-format on
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
    switch (payloadHead.compressType) {
#ifdef HARMONY_PROJECT
        case COMPRESS_LZ4: {
            // clang-format off
            compressSize = LZ4_compress_default((const char *)data, (char *)sendBuf + payloadPrefixReserve,
                                                dataSize, dataSize);
            // clang-format on
            break;
        }
#endif
        default: {  // COMPRESS_NONE
            if (memcpy_s(sendBuf + payloadPrefixReserve, sendBufSize - payloadPrefixReserve, data, dataSize) != EOK) {
                delete[] sendBuf;
                return false;
            }
            compressSize = dataSize;
            break;
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
    SendToAnother(commandData, sendBuf, payloadPrefixReserve + compressSize);
    delete[] sendBuf;
    return true;
}

void HdcTransferBase::OnFileIO(uv_fs_t *req)
{
    bool tryFinishIO = false;
    CtxFileIO *contextIO = (CtxFileIO *)req->data;
    CtxFile *context = (CtxFile *)contextIO->context;
    HdcTransferBase *thisClass = (HdcTransferBase *)context->thisClass;
    uint8_t *bufIO = contextIO->bufIO;
    uv_fs_req_cleanup(req);
    thisClass->refCount--;
    while (true) {
        if (req->result <= 0) {  // Read error or master read completion
            tryFinishIO = true;
            if (req->result < 0) {
                WRITE_LOG(LOG_DEBUG, "txf.OnFileIO error: %s", uv_strerror((int)req->result));
                context->closeNotify = true;
            }
            break;
        }
        context->indexIO += req->result;
        if (req->fs_type == UV_FS_READ) {
            if (!thisClass->SendIOPayload(context, context->indexIO - req->result, bufIO, req->result)) {
                tryFinishIO = true;
                break;
            }
            // read continu,util result >0;
            thisClass->SimpleFileIO(context, context->indexIO, nullptr, Base::GetMaxBufSize());
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
        thisClass->refCount++;
        uv_fs_close(thisClass->loopTask, &context->fsCloseReq, context->fsOpenReq.result, OnFileClose);
    }
}

void HdcTransferBase::OnFileOpen(uv_fs_t *req)
{
    CtxFile *context = (CtxFile *)req->data;
    HdcTransferBase *thisClass = (HdcTransferBase *)context->thisClass;
    uv_fs_req_cleanup(req);
    thisClass->refCount--;
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
        uv_fs_req_cleanup(&fs);

        TransferConfig &st = context->transferConfig;
        st.fileSize = fs.statbuf.st_size;
        st.optionalName = context->localName;
        st.atime = fs.statbuf.st_atim.tv_sec;
        st.mtime = fs.statbuf.st_mtim.tv_sec;
        st.path = context->remotePath;

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
            retNum++;
        }
    }
    uv_fs_req_cleanup(&req);
    return retNum;
}

// https://en.cppreference.com/w/cpp/filesystem/is_directory
// return true if file exist， false if file not exist
bool HdcTransferBase::SmartSlavePath(string &localPath, const char *optName)
{
    if (Base::CheckDirectoryOrPath(localPath.c_str(), true, false)) {
        return true;
    }
    uv_fs_t req;
    int r = uv_fs_lstat(nullptr, &req, localPath.c_str(), nullptr);
    uv_fs_req_cleanup(&req);
    if (r == 0 && req.statbuf.st_mode & S_IFDIR) {  // is dir
        localPath = localPath.c_str() + fs::path::preferred_separator + string(optName);
        return false;
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
            SimpleFileIO(context, context->indexIO, nullptr, Base::GetMaxBufSize());
            context->transferBegin = Base::GetRuntimeMSec();
        } else if (command == commandData) {
            // The size of the actual HOST end may be larger than maxbuf
            if (payloadSize > MAX_SIZE_IOBUF * 2 || payloadSize < 0) {
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
}  // namespace Hdc
