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
#ifndef HDC_BASE_H
#define HDC_BASE_H
#include "common.h"

namespace Hdc {
namespace Base {
    uint8_t GetLogLevel();
    extern uint8_t g_logLevel;
    void SetLogLevel(const uint8_t logLevel);
    void PrintLogEx(const char *functionName, int line, uint8_t logLevel, const char *msg, ...);
    void PrintMessage(const char *fmt, ...);
    // tcpHandle can't be const as it's passed into uv_tcp_keepalive
    void SetTcpOptions(uv_tcp_t *tcpHandle);
    // Realloc need to update origBuf&origSize which can't be const
    void ReallocBuf(uint8_t **origBuf, int *nOrigSize, size_t sizeWanted);
    // handle&sendHandle must keep sync with uv_write
    int SendToStreamEx(uv_stream_t *handleStream, const uint8_t *buf, const int bufLen, uv_stream_t *handleSend,
                       const void *finishCallback, const void *pWriteReqData);
    int SendToStream(uv_stream_t *handleStream, const uint8_t *buf, const int bufLen);
    // As an uv_write_cb it must keep the same as prototype
    void SendCallback(uv_write_t *req, int status);
    // As an uv_alloc_cb it must keep the same as prototype
    void AllocBufferCallback(uv_handle_t *handle, size_t sizeSuggested, uv_buf_t *buf);
    uint64_t GetRuntimeMSec();
    string GetRandomString(const uint16_t expectedLen);
    int GetRandomNum(const int min, const int max);
    uint64_t GetRandom(const uint64_t min = 0, const uint64_t max = UINT64_MAX);
    int ConnectKey2IPPort(const char *connectKey, char *outIP, uint16_t *outPort);
    // As an uv_work_cb it must keep the same as prototype
    int StartWorkThread(uv_loop_t *loop, uv_work_cb pFuncWorkThread, uv_after_work_cb pFuncAfterThread,
                        void *pThreadData);
    // As an uv_work_cb it must keep the same as prototype
    void FinishWorkThread(uv_work_t *req, int status);
    int GetMaxBufSize();
    bool TryCloseLoop(uv_loop_t *ptrLoop, const char *callerName);
    void TryCloseHandle(const uv_handle_t *handle);
    void TryCloseHandle(const uv_handle_t *handle, uv_close_cb closeCallBack);
    void TryCloseHandle(const uv_handle_t *handle, bool alwaysCallback, uv_close_cb closeCallBack);
    char **SplitCommandToArgs(const char *cmdStringLine, int *slotIndex);
    bool RunPipeComand(const char *cmdString, char *outBuf, uint16_t sizeOutBuf, bool ignoreTailLF);
    // results need to save in buf which can't be const
    int ReadBinFile(const char *pathName, void **buf, const size_t bufLen);
    int WriteBinFile(const char *pathName, const uint8_t *buf, const size_t bufLen, bool newFile = false);
    void CloseIdleCallback(uv_handle_t *handle);
    void CloseTimerCallback(uv_handle_t *handle);
    int ProgramMutex(const char *procname, bool checkOrNew);
    // result needs to save results which can't be const
    void SplitString(const string &origString, const string &seq, vector<string> &resultStrings);
    string GetShellPath();
    uint64_t HostToNet(uint64_t val);
    uint64_t NetToHost(uint64_t val);
    string GetFullFilePath(const string &s);
    string GetPathWithoutFilename(const string &s);
    int CreateSocketPair(int *fds);
    void CloseSocketPair(const int *fds);
    int StringEndsWith(string s, string sub);
    void BuildErrorString(const char *localPath, const char *op, const char *err, string &str);
    const char *GetFileType(mode_t mode);
    bool CheckDirectoryOrPath(const char *localPath, bool pathOrDir, bool readWrite, string &errStr);
    bool CheckDirectoryOrPath(const char *localPath, bool pathOrDir, bool readWrite);
    int Base64EncodeBuf(const uint8_t *input, const int length, uint8_t *bufOut);
    vector<uint8_t> Base64Encode(const uint8_t *input, const int length);
    int Base64DecodeBuf(const uint8_t *input, const int length, uint8_t *bufOut);
    string Base64Decode(const uint8_t *input, const int length);
    void ReverseBytes(void *start, int size);
    string CanonicalizeSpecPath(string &src);
    // Just zero a POD type, such as a structure or union
    // If it contains c++ struct such as stl-string or others, please use 'T = {}' to initialize struct
    template<class T> int ZeroStruct(T &structBuf)
    {
        return memset_s(&structBuf, sizeof(T), 0, sizeof(T));
    }
    // just zero a statically allocated array of POD or built-in types
    template<class T, size_t N> int ZeroArray(T (&arrayBuf)[N])
    {
        return memset_s(arrayBuf, sizeof(T) * N, 0, sizeof(T) * N);
    }
    // just zero memory buf, such as pointer
    template<class T> int ZeroBuf(T &arrayBuf, int size)
    {
        return memset_s(arrayBuf, size, 0, size);
    }
    // clang-format off
    const string StringFormat(const char * const formater, ...);
    const string StringFormat(const char * const formater, va_list &vaArgs);
    // clang-format on
    string GetVersion();
    bool IdleUvTask(uv_loop_t *loop, void *data, uv_idle_cb cb);
    bool TimerUvTask(uv_loop_t *loop, void *data, uv_timer_cb cb, int repeatTimeout = UV_DEFAULT_INTERVAL);
    bool DelayDo(uv_loop_t *loop, const int delayMs, const uint8_t flag, string msg, void *data,
                 std::function<void(const uint8_t, string &, const void *)> cb);
    inline bool DelayDoSimple(uv_loop_t *loop, const int delayMs,
                              std::function<void(const uint8_t, string &, const void *)> cb)
    {
        return DelayDo(loop, delayMs, 0, "", nullptr, cb);
    }
    inline bool DoNextLoop(uv_loop_t *loop, void *data, std::function<void(const uint8_t, string &, const void *)> cb)
    {
        return DelayDo(loop, 0, 0, "", data, cb);
    }

    // Trim from right side
    inline string &RightTrim(string &s, const string &w = WHITE_SPACES)
    {
        s.erase(s.find_last_not_of(w) + 1);
        return s;
    }

    // Trim from left side
    inline string &LeftTrim(string &s, const string &w = WHITE_SPACES)
    {
        s.erase(0, s.find_first_not_of(w));
        return s;
    }

    // Trim from both sides
    inline string &Trim(string &s, const string &w = WHITE_SPACES)
    {
        return LeftTrim(RightTrim(s, w), w);
    }
    string ReplaceAll(string str, const string from, const string to);
    uint8_t CalcCheckSum(const uint8_t *data, int len);
    string GetFileNameAny(string &path);
    string GetCwd();
    string GetTmpDir();
    void SetLogCache(bool enable);
    void RemoveLogFile();
    void RemoveLogCache();
    void RollLogFile(const char *path);
    uv_os_sock_t DuplicateUvSocket(uv_tcp_t *tcp);
    bool IsRoot();
    char GetPathSep();
    bool IsAbsolutePath(string &path);
    inline int GetMaxBufSize()
    {
        return MAX_SIZE_IOBUF;
    }
    inline int GetUsbffsBulkSize()
    {
        return MAX_USBFFS_BULK;
    }
#ifdef HDC_SUPPORT_FLASHD
    // deprecated, remove later
    inline bool SetHdcProperty(const char *key, const char *value)
    {
        return false;
    }
    // deprecated, remove later
    inline bool GetHdcProperty(const char *key, char *value, uint16_t sizeOutBuf)
    {
        return false;
    }
#endif
}  // namespace base
}  // namespace Hdc

#endif  // HDC_BASE_H
