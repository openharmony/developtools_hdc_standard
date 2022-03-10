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
#include "debug.h"
#include "base.h"

namespace Hdc {
namespace Debug {
    using namespace std;

    FILE* OpenValidDebugFilePath(const char *fileName, const string& mode, string& filePath)
    {
        if (fileName == nullptr) {
            return nullptr;
        }
        char pathName[BUF_SIZE_DEFAULT];
        if (snprintf_s(pathName, sizeof(pathName), sizeof(pathName) - 1, "/mnt/hgfs/vtmp/%s", fileName) < 0) {
            return nullptr;
        }
        string srcPath = pathName;
        string resolvedPath = Base::CanonicalizeSpecPath(srcPath);
        FILE *fp = fopen(resolvedPath.c_str(), mode.c_str());
        if (fp == nullptr) {
            if (snprintf_s(pathName, sizeof(pathName), sizeof(pathName) - 1, "/tmp/%s", fileName) < 0) {
                WRITE_LOG(LOG_DEBUG, "Write hex to %s failed!", pathName);
                return nullptr;
            }

            srcPath = pathName;
            resolvedPath = Base::CanonicalizeSpecPath(srcPath);
            if ((fp = fopen(resolvedPath.c_str(), mode.c_str())) == nullptr) {
                WRITE_LOG(LOG_DEBUG, "Write hex to %s failed!", pathName);
                return nullptr;
            }
        }
        filePath = resolvedPath;
        return fp;
    }

    int WriteHexToDebugFile(const char *fileName, const uint8_t *buf, const int bufLen)
    {
        if (fileName == nullptr || buf == nullptr) {
            return ERR_GENERIC;
        }

        string filePath;
        FILE* fp = OpenValidDebugFilePath(fileName, "a+", filePath);
        if (fp == nullptr) {
            return ERR_FILE_OPEN;
        }

        fwrite(buf, 1, bufLen, fp);
        fflush(fp);
        fclose(fp);
        return RET_SUCCESS;
    }

    int ReadHexFromDebugFile(const char *fileName, uint8_t *buf, const int bufLen)
    {
        if (fileName == nullptr || buf == nullptr) {
            return ERR_GENERIC;
        }
        string filePath;
        FILE* fp = OpenValidDebugFilePath(fileName, "r", filePath);
        if (fp == nullptr) {
            return ERR_FILE_OPEN;
        }

        struct stat statbuf;
        stat(filePath.c_str(), &statbuf);
        int size = statbuf.st_size;
        if (size > bufLen) {
            fclose(fp);
            return ERR_BUF_SIZE;
        }
        int ret = fread(buf, 1, size, fp);
        fflush(fp);
        fclose(fp);
        if (ret != size) {
            return ERR_FILE_READ;
        }
        return size;
    }

    void DetermineThread(HSessionPtr hSessionPtr)
    {
        if (uv_thread_self() == hSessionPtr->hWorkThread) {
            WRITE_LOG(LOG_WARN, "At main workthread");
        } else if (uv_thread_self() == hSessionPtr->hWorkChildThread) {
            WRITE_LOG(LOG_WARN, "At child workthread");
        } else {
            WRITE_LOG(LOG_WARN, "At unknow workthread");
        }
    }

    int PrintfHexBuf(const uint8_t *buf, int bufLen)
    {
        if (buf == nullptr) {
            return -1;
        }
        int i = 0;
        for (i = 0; i < bufLen; ++i) {
            printf("0x%02x, ", buf[i]);
            fflush(stdout);
        }
        printf("\r\n");
        fflush(stdout);
        return 0;
    }
}
}  // namespace Hdc
