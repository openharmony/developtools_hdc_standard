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
#ifndef HDC_TRANSFER_H
#define HDC_TRANSFER_H
#include "common.h"

namespace Hdc {
class HdcTransferBase : public HdcTaskBase {
public:
    enum CompressType { COMPRESS_NONE, COMPRESS_LZ4, COMPRESS_LZ77, COMPRESS_LZMA, COMPRESS_BROTLI };
    // used for child class
    struct TransferConfig {
        uint64_t fileSize;
        uint64_t atime;
        uint64_t mtime;
        string options;
        string path;
        string optionalName;
        bool updateIfNew;
        uint8_t compressType;
        bool holdTimestamp;
        string functionName;
    };
    // used for HdcTransferBase. just base class use, not public
    struct TransferPayload {
        uint64_t index;
        uint8_t compressType;
        uint32_t compressSize;
        uint32_t uncompressSize;
    };
    HdcTransferBase(HTaskInfo hTaskInfo);
    virtual ~HdcTransferBase();
    virtual void StopTask()
    {
    }
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);

protected:
    // Static file context
    struct CtxFile {  // The structure cannot be initialized by MEMSET
        bool master;  // Document transmission initiative
        bool closeNotify;
        void *thisClass;
        uint64_t fileSize;
        uint64_t indexIO;  // Id or written IO bytes
        uv_loop_t *loop;
        uv_fs_cb cb;
        string localName;
        string localPath;
        string remotePath;
        uv_fs_t fsOpenReq;
        uv_fs_t fsCloseReq;
        uint64_t transferBegin;
        vector<string> taskQueue;
        TransferConfig transferConfig;  // Used for network IO configuration initialization
    };
    // Just app-mode use
    enum AppModType {
        APPMOD_NONE,
        APPMOD_INSTALL,
        APPMOD_UNINSTALL,
        APPMOD_SIDELOAD,
    };

    static void OnFileOpen(uv_fs_t *req);
    static void OnFileClose(uv_fs_t *req);
    int GetSubFiles(const char *path, string filter, vector<string> *out);
    virtual void CheckMaster(CtxFile *context)
    {
    }
    virtual void WhenTransferFinish(CtxFile *context)
    {
    }
    bool MatchPackageExtendName(string fileName, string extName);
    bool ResetCtx(CtxFile *context, bool full = false);
    bool SmartSlavePath(string &localPath, const char *optName);
    void SetFileTime(CtxFile *context);

    CtxFile ctxNow;
    uint16_t commandBegin;
    uint16_t commandData;

private:
    // dynamic IO context
    struct CtxFileIO {
        uv_fs_t fs;
        uint8_t *bufIO;
        CtxFile *context;
    };
    const uint8_t payloadPrefixReserve = 64;
    static void OnFileIO(uv_fs_t *req);
    int SimpleFileIO(CtxFile *context, uint64_t index, uint8_t *sendBuf, int bytesIO);
    bool SendIOPayload(CtxFile *context, int index, uint8_t *data, int dataSize);
    bool RecvIOPayload(CtxFile *context, uint8_t *data, int dataSize);
};
}  // namespace Hdc

#endif