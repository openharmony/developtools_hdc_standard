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
#include "file.h"
#include "serial_struct.h"

namespace Hdc {
HdcFile::HdcFile(HTaskInfo hTaskInfo)
    : HdcTransferBase(hTaskInfo)
{
    commandBegin = CMD_FILE_BEGIN;
    commandData = CMD_FILE_DATA;
}

HdcFile::~HdcFile()
{
    WRITE_LOG(LOG_DEBUG, "~HdcFile");
};

void HdcFile::StopTask()
{
    WRITE_LOG(LOG_DEBUG, "HdcFile StopTask");
    singalStop = true;
};

bool HdcFile::BeginTransfer(CtxFile *context, const string &command)
{
    int argc = 0;
    bool ret = false;
    char **argv = Base::SplitCommandToArgs(command.c_str(), &argc);
    if (argc < CMD_ARG1_COUNT || argv == nullptr) {
        LogMsg(MSG_FAIL, "Transfer path split failed");
        if (argv) {
            delete[]((char *)argv);
        }
        return false;
    }
    if (!SetMasterParameters(context, command.c_str(), argc, argv)) {
        delete[]((char *)argv);
        return false;
    }
    do {
        ++refCount;
        uv_fs_open(loopTask, &context->fsOpenReq, context->localPath.c_str(), O_RDONLY, S_IWUSR | S_IRUSR, OnFileOpen);
        context->master = true;
        ret = true;
    } while (false);
    if (!ret) {
        LogMsg(MSG_FAIL, "Transfer path failed, Master:%s Slave:%s", context->localPath.c_str(),
               context->remotePath.c_str());
    }
    delete[]((char *)argv);
    return ret;
}

bool HdcFile::SetMasterParameters(CtxFile *context, const char *command, int argc, char **argv)
{
    int srcArgvIndex = 0;
    string errStr;
    const string CMD_OPTION_TSTMP = "-a";
    const string CMD_OPTION_SYNC = "-sync";
    const string CMD_OPTION_ZIP = "-z";

    for (int i = 0; i < argc - CMD_ARG1_COUNT; i++) {
        if (argv[i] == CMD_OPTION_ZIP) {
            context->transferConfig.compressType = COMPRESS_LZ4;
            ++srcArgvIndex;
        } else if (argv[i] == CMD_OPTION_SYNC) {
            context->transferConfig.updateIfNew = true;
            ++srcArgvIndex;
        } else if (argv[i] == CMD_OPTION_TSTMP) {
            // The time zone difference may cause the display time on the PC and the
            // device to differ by several hours
            //
            // ls -al --full-time
            context->transferConfig.holdTimestamp = true;
            ++srcArgvIndex;
        } else if (argv[i] == CMD_OPTION_CLIENTCWD) {
            context->transferConfig.clientCwd = argv[i + 1];
            srcArgvIndex += CMD_ARG1_COUNT;  // skip 2args
        } else if (argv[i][0] == '-') {
            LogMsg(MSG_FAIL, "Unknow file option: %s", argv[i]);
            return false;
        }
    }
    context->remotePath = argv[argc - 1];
    context->localPath = argv[argc - 2];
    if (taskInfo->serverOrDaemon) {
        // master and server
        ExtractRelativePath(context->transferConfig.clientCwd, context->localPath);
    }

    context->localName = Base::GetFullFilePath(context->localPath);

    mode_t mode = mode_t(~S_IFMT);
    if (!Base::CheckDirectoryOrPath(context->localPath.c_str(), true, true, errStr, mode) && (mode & S_IFDIR)) {
        context->isDir = true;

        GetSubFilesRecursively(context->localPath, context->localName, &context->taskQueue);
        if (context->taskQueue.size() == 0) {
            LogMsg(MSG_FAIL, "Directory empty.");
            return false;
        }
        context->fileCnt = 0;
        context->dirSize = 0;
        context->localDirName = Base::GetPathWithoutFilename(context->localPath);

        WRITE_LOG(LOG_INFO, "send directory file list %u %s", context->taskQueue.size(), context->taskQueue[0].c_str());
        WRITE_LOG(LOG_INFO, "context->localDirName = %s", context->localDirName.c_str());

        context->localName = context->taskQueue.back();
        context->localPath = context->localDirName + context->localName;

        WRITE_LOG(LOG_WARN, "localName = %s context->localPath = %s", context->localName.c_str(), context->localPath.c_str());
        context->taskQueue.pop_back();
    }
    return true;
}

void HdcFile::CheckMaster(CtxFile *context)
{
    string s = SerialStruct::SerializeToString(context->transferConfig);
    SendToAnother(CMD_FILE_CHECK, (uint8_t *)s.c_str(), s.size());
}

void HdcFile::WhenTransferFinish(CtxFile *context)
{
    WRITE_LOG(LOG_DEBUG, "HdcTransferBase WhenTransferFinish");
    uint8_t flag = 1;
    context->fileCnt++;
    context->dirSize += context->indexIO;
    SendToAnother(CMD_FILE_FINISH, &flag, 1);
}

void HdcFile::TransferSummary(CtxFile *context)
{
    uint64_t nMSec = Base::GetRuntimeMSec() -
                     (context->fileCnt > 1 ? context->transferDirBegin : context->transferBegin);
    uint64_t fSize = context->fileCnt > 1 ? context->dirSize : context->indexIO;
    double fRate = static_cast<double>(fSize) / nMSec; // / /1000 * 1000 = 0
    if (context->indexIO >= context->fileSize) {
        WRITE_LOG(LOG_INFO, "HdcFile::TransferSummary success");
        LogMsg(MSG_OK, "FileTransfer finish, File count = %d, Size:%lld time:%lldms rate:%.2lfkB/s",
               context->fileCnt, fSize, nMSec, fRate);
    } else {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        uv_strerror_r((int)(-context->lastErrno), buf, bufSize);
        LogMsg(MSG_FAIL, "Transfer Stop at:%lld/%lld(Bytes), Reason: %s", context->indexIO, context->fileSize,
               buf);
    }
}

bool HdcFile::SlaveCheck(uint8_t *payload, const int payloadSize)
{
    bool ret = true;
    bool childRet = false;
    // parse option
    string serialString((char *)payload, payloadSize);
    TransferConfig &stat = ctxNow.transferConfig;
    SerialStruct::ParseFromString(stat, serialString);
    ctxNow.fileSize = stat.fileSize;
    ctxNow.localPath = stat.path;
    ctxNow.master = false;
    ctxNow.fsOpenReq.data = &ctxNow;
#ifdef HDC_DEBUG
    WRITE_LOG(LOG_DEBUG, "HdcFile fileSize got %" PRIu64 "", ctxNow.fileSize);
#endif
    // check path
    childRet = SmartSlavePath(stat.clientCwd, ctxNow.localPath, stat.optionalName.c_str());
    if (childRet && ctxNow.transferConfig.updateIfNew) {  // file exist and option need update
        // if is newer
        uv_fs_t fs = {};
        uv_fs_stat(nullptr, &fs, ctxNow.localPath.c_str(), nullptr);
        uv_fs_req_cleanup(&fs);
        if ((uint64_t)fs.statbuf.st_mtim.tv_sec >= ctxNow.transferConfig.mtime) {
            LogMsg(MSG_FAIL, "Target file is the same date or newer,path: %s", ctxNow.localPath.c_str());
            return false;
        }
    }
    // begin work
    ++refCount;
    uv_fs_open(loopTask, &ctxNow.fsOpenReq, ctxNow.localPath.c_str(), UV_FS_O_TRUNC | UV_FS_O_CREAT | UV_FS_O_WRONLY,
               S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH, OnFileOpen);
    if (ctxNow.transferDirBegin == 0) {
        ctxNow.transferDirBegin = Base::GetRuntimeMSec();
    }
    ctxNow.transferBegin = Base::GetRuntimeMSec();
    return ret;
}

void HdcFile::TransferNext(CtxFile *context)
{
    WRITE_LOG(LOG_WARN, "HdcFile::TransferNext");

    context->localName = context->taskQueue.back();
    context->localPath = context->localDirName + context->localName;
    context->taskQueue.pop_back();
    WRITE_LOG(LOG_WARN, "context->localName = %s context->localPath = %s queuesize:%d",
              context->localName.c_str(), context->localPath.c_str(), ctxNow.taskQueue.size());
    do {
        ++refCount;
        uv_fs_open(loopTask, &context->fsOpenReq, context->localPath.c_str(), O_RDONLY, S_IWUSR | S_IRUSR, OnFileOpen);
    } while (false);

    return;
}

bool HdcFile::CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize)
{
    HdcTransferBase::CommandDispatch(command, payload, payloadSize);
    bool ret = true;
    switch (command) {
        case CMD_FILE_INIT: {  // initial
            string s = string((char *)payload, payloadSize);
            ret = BeginTransfer(&ctxNow, s);
            ctxNow.transferBegin = Base::GetRuntimeMSec();
            break;
        }
        case CMD_FILE_CHECK: {
            ret = SlaveCheck(payload, payloadSize);
            break;
        }

        case CMD_FILE_FINISH: {
            if (*payload) {  // close-step3
                WRITE_LOG(LOG_DEBUG, "Dir = %d taskQueue size = %d", ctxNow.isDir, ctxNow.taskQueue.size());
                if (ctxNow.isDir && (ctxNow.taskQueue.size() > 0)) {
                    TransferNext(&ctxNow);
                } else {
                    ctxNow.ioFinish = true;
                    ctxNow.transferDirBegin = 0;
                    --(*payload);
                    SendToAnother(CMD_FILE_FINISH, payload, 1);
                }
            } else {  // close-step3
                TransferSummary(&ctxNow);
                TaskFinish();
            }
            break;
        }
        default:
            break;
    }
    return ret;
}
}  // namespace Hdc
