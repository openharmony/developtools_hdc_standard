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
#include "base.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <random>
#ifdef __MUSL__
extern "C" {
#include "parameter.h"
}
#endif
using namespace std::chrono;

namespace Hdc {
namespace Base {
    uint8_t g_logLevel = 0;
    void SetLogLevel(const uint8_t logLevel)
    {
        g_logLevel = logLevel;
    }

// Commenting the code will optimize and tune all log codes, and the compilation volume will be greatly reduced
#define ENABLE_DEBUGLOG
#ifdef ENABLE_DEBUGLOG
    void GetLogDebugFunctioname(string &debugInfo, int line, string &threadIdString)
    {
        uint32_t currentThreadId = 0;
        string tmpString = GetFileNameAny(debugInfo);
#ifdef _WIN32
        currentThreadId = GetCurrentThreadId();
#else
        currentThreadId = uv_thread_self();  // 64bit OS, just dispaly 32bit ptr
#endif
        debugInfo = StringFormat("%s:%d", tmpString.c_str(), line);
        if (g_logLevel < LOG_FULL) {
            debugInfo = "";
            threadIdString = "";
        } else {
            debugInfo = "[" + debugInfo + "]";
            threadIdString = StringFormat("[%x]", currentThreadId);
        }
    }

    bool IsWindowsSupportAnsiColor()
    {
#ifdef _WIN32
        // Set output mode to handle virtual terminal sequences
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode)) {
            return false;
        }
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hOut, dwMode)) {
            return false;
        }
#endif
        return true;
    }

    void GetLogLevelAndTime(uint8_t logLevel, string &logLevelString, string &timeString)
    {
        system_clock::time_point timeNow = system_clock::now();          // now time
        system_clock::duration sinceUnix0 = timeNow.time_since_epoch();  // since 1970
        time_t sSinceUnix0 = duration_cast<seconds>(sinceUnix0).count();
        std::tm tim = *std::localtime(&sSinceUnix0);
        bool enableAnsiColor = false;
#ifdef _WIN32
        enableAnsiColor = IsWindowsSupportAnsiColor();
#else
        enableAnsiColor = true;
#endif
        if (enableAnsiColor) {
            switch (logLevel) {
                case LOG_FATAL:
                    logLevelString = "\033[1;31mF\033[0m";
                    break;
                case LOG_INFO:
                    logLevelString = "\033[1;32mI\033[0m";
                    break;
                case LOG_WARN:
                    logLevelString = "\033[1;33mW\033[0m";
                    break;
                case LOG_DEBUG:
                    logLevelString = "\033[1;36mD\033[0m";
                    break;
                default:
                    logLevelString = "\033[1;36mD\033[0m";
                    break;
            }
        } else {
            logLevelString = std::to_string(logLevel);
        }
        string msTimeSurplus;
        if (g_logLevel > LOG_DEBUG) {
            const auto sSinceUnix0Rest = duration_cast<microseconds>(sinceUnix0).count() % (TIME_BASE * TIME_BASE);
            msTimeSurplus = StringFormat(".%06llu", sSinceUnix0Rest);
        }
        timeString = StringFormat("%d:%d:%d%s", tim.tm_hour, tim.tm_min, tim.tm_sec, msTimeSurplus.c_str());
    }

    void PrintLogEx(const char *functionName, int line, uint8_t logLevel, const char *msg, ...)
    {
        string debugInfo;
        string logBuf;
        string logLevelString;
        string threadIdString;
        string sep = "\n";
        string timeString;
        if (logLevel > g_logLevel) {
            return;
        }
        va_list vaArgs;
        va_start(vaArgs, msg);
        string logDetail = Base::StringFormat(msg, vaArgs);
        va_end(vaArgs);
        if (logDetail.back() == '\n') {
            sep = "\r\n";
        }
        debugInfo = functionName;
        GetLogDebugFunctioname(debugInfo, line, threadIdString);
        GetLogLevelAndTime(logLevel, logLevelString, timeString);
        logBuf = StringFormat("[%s][%s]%s%s %s%s", logLevelString.c_str(), timeString.c_str(), threadIdString.c_str(),
                              debugInfo.c_str(), logDetail.c_str(), sep.c_str());

        printf("%s", logBuf.c_str());
        fflush(stdout);
        // logfile, not thread-safe
        FILE *fp = fopen("/data/local/tmp/hdc.log", "a");
        if (fp == nullptr) {
            return;
        }
        fprintf(fp, "%s", logBuf.c_str());
        fflush(fp);
        fclose(fp);
        return;
    }
#else   // else ENABLE_DEBUGLOG.If disabled, the entire output code will be optimized by the compiler
    void PrintLogEx(uint8_t logLevel, char *msg, ...)
    {
    }
#endif  // ENABLE_DEBUGLOG

    void PrintMessage(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        fprintf(stdout, "\n");
        va_end(ap);
    }

    // if can linkwith -lstdc++fs, use std::filesystem::path(path).filename();
    string GetFileNameAny(string &path)
    {
        string tmpString = path;
        size_t tmpNum = tmpString.rfind('/');
        if (tmpNum == std::string::npos) {
            tmpNum = tmpString.rfind('\\');
            if (tmpNum == std::string::npos) {
                return tmpString;
            }
        }
        tmpString = tmpString.substr(tmpNum + 1, tmpString.size() - tmpNum);
        return tmpString;
    }

    int GetMaxBufSize()
    {
        return MAX_SIZE_IOBUF;
    }

    void SetTcpOptions(uv_tcp_t *tcpHandle)
    {
        constexpr int maxBufFactor = 10;
        if (!tcpHandle) {
            WRITE_LOG(LOG_WARN, "SetTcpOptions nullptr Ptr");
            return;
        }
        int timeout = GLOBAL_TIMEOUT;
        uv_tcp_keepalive(tcpHandle, 1, timeout / 2);
        // if MAX_SIZE_IOBUF==5k,bufMaxSize at least 40k. It must be set to io 8 times is more appropriate,
        // otherwise asynchronous IO is too fast, a lot of IO is wasted on IOloop, transmission speed will decrease
        int bufMaxSize = GetMaxBufSize() * maxBufFactor;
        uv_recv_buffer_size((uv_handle_t *)tcpHandle, &bufMaxSize);
        uv_send_buffer_size((uv_handle_t *)tcpHandle, &bufMaxSize);
    }

    void ReallocBuf(uint8_t **origBuf, int *nOrigSize, const int indexUsedBuf, int sizeWanted)
    {
        sizeWanted = GetMaxBufSize();
        int remainLen = *nOrigSize - indexUsedBuf;
        // init:0, left less than expected
        if (!*nOrigSize || (remainLen < sizeWanted && (*nOrigSize + sizeWanted < sizeWanted * 2))) {
            // Memory allocation size is slightly larger than the maximum
            int nNewLen = *nOrigSize + sizeWanted + EXTRA_ALLOC_SIZE;
            uint8_t *bufPtrOrig = *origBuf;
            *origBuf = new uint8_t[nNewLen]();
            if (!*origBuf) {
                *origBuf = bufPtrOrig;
            } else {
                *nOrigSize = nNewLen;
                if (bufPtrOrig) {
                    if (memcpy_s(*origBuf, nNewLen, bufPtrOrig, *nOrigSize)) {
                        WRITE_LOG(LOG_FATAL, "ReallocBuf failed");
                    }
                    delete[] bufPtrOrig;
                }
            }
            uint8_t *buf = static_cast<uint8_t *>(*origBuf + indexUsedBuf);
            Base::ZeroBuf(buf, nNewLen - indexUsedBuf);
        }
    }

    // As an uv_alloc_cb it must keep the same as prototype
    void AllocBufferCallback(uv_handle_t *handle, size_t sizeSuggested, uv_buf_t *buf)
    {
        const int size = GetMaxBufSize();
        buf->base = (char *)new uint8_t[size]();
        if (buf->base) {
            buf->len = size - 1;
        }
    }

    // As an uv_write_cb it must keep the same as prototype
    void SendCallback(uv_write_t *req, int status)
    {
        delete[]((uint8_t *)req->data);
        delete req;
    }

    // xxx must keep sync with uv_loop_close/uv_walk etc.
    bool TryCloseLoop(uv_loop_t *ptrLoop, const char *callerName)
    {
        // UV_RUN_DEFAULT: Runs the event loop until the reference count drops to zero. Always returns zero.
        // UV_RUN_ONCE:    Poll for new events once. Note that this function blocks if there are no pending events.
        //                 Returns zero when done (no active handles or requests left), or non-zero if more events are
        //                 expected meaning you should run the event loop again sometime in the future).
        // UV_RUN_NOWAIT:  Poll for new events once but don't block if there are no pending events.
        uint8_t closeRetry = 0;
        bool ret = false;
        constexpr int maxRetry = 3;
        for (closeRetry = 0; closeRetry < maxRetry; ++closeRetry) {
            if (uv_loop_close(ptrLoop) == UV_EBUSY) {
                if (closeRetry > 2) {
                    WRITE_LOG(LOG_WARN, "%s close busy,try:%d", callerName, closeRetry);
                }

                if (ptrLoop->active_handles >= 2) {
                    WRITE_LOG(LOG_DEBUG, "TryCloseLoop issue");
                }
                auto clearLoopTask = [](uv_handle_t *handle, void *arg) -> void { TryCloseHandle(handle); };
                uv_walk(ptrLoop, clearLoopTask, nullptr);
                // If all processing ends, Then return0,this call will block
                if (!ptrLoop->active_handles) {
                    ret = true;
                    break;
                }
                if (!uv_run(ptrLoop, UV_RUN_ONCE)) {
                    ret = true;
                    break;
                }
            } else {
                ret = true;
                break;
            }
        }
        return ret;
    }

    // Some handles may not be initialized or activated yet or have been closed, skip the closing process
    void TryCloseHandle(const uv_handle_t *handle)
    {
        TryCloseHandle(handle, nullptr);
    }

    void TryCloseHandle(const uv_handle_t *handle, uv_close_cb closeCallBack)
    {
        TryCloseHandle(handle, false, closeCallBack);
    }

    void TryCloseHandle(const uv_handle_t *handle, bool alwaysCallback, uv_close_cb closeCallBack)
    {
        bool hasCallClose = false;
        if (handle->loop && !uv_is_closing(handle)) {
            uv_close((uv_handle_t *)handle, closeCallBack);
            hasCallClose = true;
        }
        if (!hasCallClose && alwaysCallback) {
            closeCallBack((uv_handle_t *)handle);
        }
    }

    int SendToStream(uv_stream_t *handleStream, const uint8_t *buf, const int bufLen)
    {
        if (bufLen > static_cast<int>(HDC_BUF_MAX_BYTES)) {
            return ERR_BUF_ALLOC;
        }
        uint8_t *pDynBuf = new uint8_t[bufLen];
        if (!pDynBuf) {
            return ERR_BUF_ALLOC;
        }
        if (memcpy_s(pDynBuf, bufLen, buf, bufLen)) {
            delete[] pDynBuf;
            return ERR_BUF_COPY;
        }
        return SendToStreamEx(handleStream, pDynBuf, bufLen, nullptr, (void *)SendCallback, (void *)pDynBuf);
    }

    // handleSend is used for pipe thread sending, set nullptr for tcp, and dynamically allocated by malloc when buf
    // is required
    int SendToStreamEx(uv_stream_t *handleStream, const uint8_t *buf, const int bufLen, uv_stream_t *handleSend,
                       const void *finishCallback, const void *pWriteReqData)
    {
        int ret = -1;
        uv_write_t *reqWrite = new uv_write_t();
        if (!reqWrite) {
            return 0;
        }
        uv_buf_t bfr;
        while (true) {
            reqWrite->data = (void *)pWriteReqData;
            bfr.base = (char *)buf;
            bfr.len = bufLen;
            if (!uv_is_writable(handleStream)) {
                delete reqWrite;
                break;
            }
            // handleSend must be a TCP socket or pipe, which is a server or a connection (listening or
            // connected state). Bound sockets or pipes will be assumed to be servers.
            if (handleSend) {
                uv_write2(reqWrite, handleStream, &bfr, 1, handleSend, (uv_write_cb)finishCallback);
            } else {
                uv_write(reqWrite, handleStream, &bfr, 1, (uv_write_cb)finishCallback);
            }
            ret = bufLen;
            break;
        }
        return ret;
    }

    uint64_t GetRuntimeMSec()
    {
        struct timespec times = { 0, 0 };
        long time;
        clock_gettime(CLOCK_MONOTONIC, &times);
        time = times.tv_sec * TIME_BASE + times.tv_nsec / (TIME_BASE * TIME_BASE);
        return time;
    }

    uint64_t GetRandom(const uint64_t min, const uint64_t max)
    {
        uint64_t ret;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint64_t> dis(min, max);
        ret = dis(gen);
        return ret;
    }

    string GetRandomString(const uint16_t expectedLen)
    {
        std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        std::random_device rd;
        std::mt19937 generator(rd());
        std::shuffle(str.begin(), str.end(), generator);
        return str.substr(0, expectedLen);
    }

    int GetRandomNum(const int min, const int max)
    {
        return static_cast<int>(GetRandom(min, max));
    }

    int ConnectKey2IPPort(const char *connectKey, char *outIP, uint16_t *outPort)
    {
        char bufString[BUF_SIZE_TINY] = "";
        if (memcpy_s(bufString, sizeof(bufString), connectKey, sizeof(bufString))) {
            return ERR_BUF_COPY;
        }
        char *p = strchr(bufString, ':');
        if (!p) {
            return ERR_PARM_FORMAT;
        }
        *p = '\0';
        if (!strlen(bufString) || strlen(bufString) > 16) {
            return ERR_PARM_SIZE;
        }
        uint16_t wPort = static_cast<uint16_t>(atoi(p + 1));
        if (EOK != strcpy_s(outIP, BUF_SIZE_TINY, bufString)) {
            return ERR_BUF_COPY;
        }
        *outPort = wPort;
        return ERR_SUCCESS;
    }

    // After creating the session worker thread, execute it on the main thread
    void FinishWorkThread(uv_work_t *req, int status)
    {
        // This is operated in the main thread
        delete req;
    }

    // at the finsh of pFuncAfterThread must free uv_work_t*
    // clang-format off
    int StartWorkThread(uv_loop_t *loop, uv_work_cb pFuncWorkThread,
                        uv_after_work_cb pFuncAfterThread, void *pThreadData)
    {
        uv_work_t *workThread = new uv_work_t();
        if (!workThread) {
            return -1;
        }
        workThread->data = pThreadData;
        uv_queue_work(loop, workThread, pFuncWorkThread, pFuncAfterThread);
        return 0;
    }
    // clang-format on

    char **SplitCommandToArgs(const char *cmdStringLine, int *slotIndex)
    {
        constexpr int extraBufSize = 2;
        char **argv;
        char *temp = nullptr;
        int argc = 0;
        char a = 0;
        size_t i = 0;
        size_t j = 0;
        size_t len = 0;
        bool isQuoted = false;
        bool isText = false;
        bool isSpace = false;

        len = strlen(cmdStringLine);
        if (len < 1) {
            return nullptr;
        }
        i = ((len + extraBufSize) / extraBufSize) * sizeof(void *) + sizeof(void *);
        argv = reinterpret_cast<char **>(new char[i + (len + extraBufSize) * sizeof(char)]);
        temp = reinterpret_cast<char *>((reinterpret_cast<uint8_t *>(argv)) + i);
        argc = 0;
        argv[argc] = temp;
        isQuoted = false;
        isText = false;
        isSpace = true;
        i = 0;
        j = 0;

        while ((a = cmdStringLine[i]) != 0) {
            if (isQuoted) {
                if (a == '\"') {
                    isQuoted = false;
                } else {
                    temp[j] = a;
                    ++j;
                }
            } else {
                switch (a) {
                    case '\"':
                        isQuoted = true;
                        isText = true;
                        if (isSpace) {
                            argv[argc] = temp + j;
                            ++argc;
                        }
                        isSpace = false;
                        break;
                    case ' ':
                    case '\t':
                    case '\n':
                    case '\r':
                        if (isText) {
                            temp[j] = '\0';
                            ++j;
                        }
                        isText = false;
                        isSpace = true;
                        break;
                    default:
                        isText = true;
                        if (isSpace) {
                            argv[argc] = temp + j;
                            ++argc;
                        }
                        temp[j] = a;
                        ++j;
                        isSpace = false;
                        break;
                }
            }
            ++i;
        }
        temp[j] = '\0';
        argv[argc] = nullptr;

        (*slotIndex) = argc;
        return argv;
    }

    bool RunPipeComand(const char *cmdString, char *outBuf, uint16_t sizeOutBuf, bool ignoreTailLf)
    {
        FILE *pipeHandle = popen(cmdString, "r");
        if (pipeHandle == nullptr) {
            return false;
        }
        int bytesRead = 0;
        int bytesOnce = 0;
        while (!feof(pipeHandle)) {
            bytesOnce = fread(outBuf, 1, sizeOutBuf - bytesRead, pipeHandle);
            if (bytesOnce <= 0) {
                break;
            }
            bytesRead += bytesOnce;
        }
        if (bytesRead && ignoreTailLf) {
            if (outBuf[bytesRead - 1] == '\n') {
                outBuf[bytesRead - 1] = '\0';
            }
        }
        pclose(pipeHandle);
        return bytesRead;
    }

    bool SetHdcProperty(const char *key, const char *value)
    {
#ifndef __MUSL__
#ifdef HDC_PCDEBUG
        WRITE_LOG(LOG_DEBUG, "Setproperty, key:%s value:%s", key, value);
#else
        string sKey = key;
        string sValue = value;
        string sBuf = "setprop " + sKey + " " + value;
        system(sBuf.c_str());
#endif
#else
        SetParameter(key, value);
#endif
        return true;
    }

    bool GetHdcProperty(const char *key, char *value, uint16_t sizeOutBuf)
    {
#ifndef __MUSL__
#ifdef HDC_PCDEBUG
        WRITE_LOG(LOG_DEBUG, "Getproperty, key:%s value:%s", key, value);
#else
        string sKey = key;
        string sBuf = "getprop " + sKey;
        RunPipeComand(sBuf.c_str(), value, sizeOutBuf, true);
#endif
#else
        string sKey = key;
        string sBuf = "getparam " + sKey;
        RunPipeComand(sBuf.c_str(), value, sizeOutBuf, true);
#endif
        value[sizeOutBuf - 1] = '\0';
        return true;
    }

    // bufLen == 0: alloc buffer in heap, need free it later
    // >0: read max nBuffLen bytes to *buff
    // ret value: <0 or bytes read
    int ReadBinFile(const char *pathName, void **buf, const int bufLen)
    {
        uint8_t *pDst = nullptr;
        int byteIO = 0;
        struct stat statbuf;
        int ret = stat(pathName, &statbuf);
        if (ret < 0) {
            return -1;
        }
        int nFileSize = statbuf.st_size;
        int readMax = 0;
        uint8_t dynamicBuf = 0;
        ret = -3;
        if (bufLen == 0) {
            dynamicBuf = 1;
            pDst = new uint8_t[nFileSize + 1]();  // tail \0
            if (!pDst) {
                return -1;
            }
            readMax = nFileSize;
        } else {
            if (nFileSize > bufLen) {
                return -2;
            }
            readMax = nFileSize;
            pDst = reinterpret_cast<uint8_t *>(buf);  // The first address of the static array is the array address
        }

        string srcPath(pathName);
        string resolvedPath = CanonicalizeSpecPath(srcPath);
        FILE *fp = fopen(resolvedPath.c_str(), "r");
        if (fp == nullptr) {
            goto ReadFileFromPath_Finish;
        }
        byteIO = fread(pDst, 1, readMax, fp);
        fclose(fp);
        if (byteIO != readMax) {
            goto ReadFileFromPath_Finish;
        }
        ret = 0;
    ReadFileFromPath_Finish:
        if (ret) {
            if (dynamicBuf) {
                delete[] pDst;
            }
        } else {
            if (dynamicBuf) {
                *buf = pDst;
            }
            ret = byteIO;
        }
        return ret;
    }

    int WriteBinFile(const char *pathName, const uint8_t *buf, const int bufLen, bool newFile)
    {
        string mode;
        string resolvedPath;
        string srcPath(pathName);
        if (newFile) {
            mode = "wb+";
            // no std::fs supoort, else std::filesystem::canonical,-lstdc++fs
            if (srcPath.find("..") != string::npos) {
                return ERR_FILE_PATH_CHECK;
            }
            resolvedPath = srcPath.c_str();
        } else {
            mode = "a+";
            resolvedPath = CanonicalizeSpecPath(srcPath);
        }
        FILE *fp = fopen(resolvedPath.c_str(), mode.c_str());
        if (fp == nullptr) {
            WRITE_LOG(LOG_DEBUG, "Write to %s failed!", pathName);
            return ERR_FILE_OPEN;
        }
        int bytesDone = fwrite(buf, 1, bufLen, fp);
        fflush(fp);
        fclose(fp);
        if (bytesDone != bufLen) {
            return ERR_BUF_SIZE;
        }
        return ERR_SUCCESS;
    }

    void CloseIdleCallback(uv_handle_t *handle)
    {
        delete (uv_idle_t *)handle;
    };

    void CloseTimerCallback(uv_handle_t *handle)
    {
        delete (uv_timer_t *)handle;
    };

    // return value: <0 error; 0 can start new server instance; >0 server already exists
    int ProgramMutex(const char *procname, bool checkOrNew)
    {
        char bufPath[BUF_SIZE_DEFAULT] = "";
        char buf[BUF_SIZE_DEFAULT] = "";
        char pidBuf[BUF_SIZE_TINY] = "";
        size_t size = sizeof(buf);
        if (uv_os_tmpdir(buf, &size) < 0) {
            WRITE_LOG(LOG_FATAL, "Tmppath failed");
            return ERR_API_FAIL;
        }
        if (snprintf_s(bufPath, sizeof(bufPath), sizeof(bufPath) - 1, "%s/%s.pid", buf, procname) < 0) {
            return ERR_BUF_OVERFLOW;
        }
        int pid = static_cast<int>(getpid());
        if (snprintf_s(pidBuf, sizeof(pidBuf), sizeof(pidBuf) - 1, "%d", pid) < 0) {
            return ERR_BUF_OVERFLOW;
        }
        // no need to CanonicalizeSpecPath, else not work
        int fd = open(bufPath, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
        if (fd < 0) {
            WRITE_LOG(LOG_FATAL, "Open mutex file \"%s\" failed!!!Errno:%d\n", buf, errno);
            return ERR_FILE_OPEN;
        }
#ifdef _WIN32
        if (snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "Global\\%s", procname) < 0) {
            return ERR_BUF_OVERFLOW;
        }
        HANDLE hMutex = CreateMutex(nullptr, FALSE, buf);
        DWORD dwError = GetLastError();
        if (ERROR_ALREADY_EXISTS == dwError || ERROR_ACCESS_DENIED == dwError) {
            WRITE_LOG(LOG_DEBUG, "File \"%s\" locked. proc already exit!!!\n", procname);
            return 1;
        }
        if (checkOrNew) {
            CloseHandle(hMutex);
        }
#else
        struct flock fl;
        fl.l_type = F_WRLCK;
        fl.l_start = 0;
        fl.l_whence = SEEK_SET;
        fl.l_len = 0;
        int retChild = fcntl(fd, F_SETLK, &fl);
        if (-1 == retChild) {
            WRITE_LOG(LOG_DEBUG, "File \"%s\" locked. proc already exit!!!\n", bufPath);
            close(fd);
            return 1;
        }
#endif
        ftruncate(fd, 0);
        write(fd, pidBuf, strlen(pidBuf) + 1);
        WRITE_LOG(LOG_DEBUG, "Write mutext to %s, pid:%s", bufPath, pidBuf);
        if (checkOrNew) {
            // close it for check only
            close(fd);
        }
        // Do not close the file descriptor, the process will be mutext effect under no-Win32 OS
        return ERR_SUCCESS;
    }

    void SplitString(const string &origString, const string &seq, vector<string> &resultStrings)
    {
        string::size_type p1 = 0;
        string::size_type p2 = origString.find(seq);

        while (p2 != string::npos) {
            if (p2 == p1) {
                ++p1;
                p2 = origString.find(seq, p1);
                continue;
            }
            resultStrings.push_back(origString.substr(p1, p2 - p1));
            p1 = p2 + seq.size();
            p2 = origString.find(seq, p1);
        }

        if (p1 != origString.size()) {
            resultStrings.push_back(origString.substr(p1));
        }
    }

    string GetShellPath()
    {
        struct stat filecheck;
        string shellPath = "/bin/sh";
        if (stat(shellPath.c_str(), &filecheck) < 0) {
            shellPath = "/system/bin/sh";
            if (stat(shellPath.c_str(), &filecheck) < 0) {
                shellPath = "sh";
            }
        }
        return shellPath;
    }

    // Not supported on some platforms, Can only be achieved manually
    uint64_t HostToNet(uint64_t val)
    {
        if (htonl(1) == 1)
            return val;
        return (((uint64_t)htonl(val)) << 32) + htonl(val >> 32);
    }

    uint64_t NetToHost(uint64_t val)
    {
        if (htonl(1) == 1)
            return val;
        return (((uint64_t)ntohl(val)) << 32) + ntohl(val >> 32);
    }

    string GetFullFilePath(const string &s)
    {  // cannot use s.rfind(std::filesystem::path::preferred_separator
#ifdef _WIN32
        const char sep = '\\';
#else
        const char sep = '/';
#endif
        size_t i = s.rfind(sep, s.length());
        if (i != string::npos) {
            return (s.substr(i + 1, s.length() - i));
        }
        return s;
    }

    int CreateSocketPair(int *fds)
    {
#ifndef _WIN32
        return socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
#else
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int reuse = 1;
        if (fds == 0) {
            return -1;
        }
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == -1) {
            return -2;
        }
        Base::ZeroStruct(addr);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        fds[0] = fds[1] = (int)-1;
        do {
            if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, (socklen_t)sizeof(reuse))) {
                break;
            }
            if (::bind(listener, (struct sockaddr *)&addr, sizeof(addr))) {
                break;
            }
            if (getsockname(listener, (struct sockaddr *)&addr, &addrlen)) {
                break;
            }
            if (listen(listener, 1)) {
                break;
            }
            fds[0] = socket(AF_INET, SOCK_STREAM, 0);
            if (fds[0] == -1) {
                break;
            }
            if (connect(fds[0], (struct sockaddr *)&addr, sizeof(addr)) == -1) {
                break;
            }
            fds[1] = accept(listener, nullptr, nullptr);
            if (fds[1] == -1) {
                break;
            }
            closesocket(listener);
            return 0;
        } while (0);

        closesocket(listener);
        closesocket(fds[0]);
        closesocket(fds[1]);
        return -1;
#endif
    }

    void CloseSocketPair(const int *fds)
    {
#ifndef _WIN32
        close(fds[0]);
        close(fds[1]);
#else
        closesocket(fds[0]);
        closesocket(fds[1]);
#endif
    }

    int StringEndsWith(string s, string sub)
    {
        return s.rfind(sub) == (s.length() - sub.length()) ? 1 : 0;
    }

    // Both absolute and relative paths support
    bool CheckDirectoryOrPath(const char *localPath, bool pathOrDir, bool readWrite)
    {
        if (pathOrDir) {  // filepath
            uv_fs_t req;
            int r = uv_fs_lstat(nullptr, &req, localPath, nullptr);
            uv_fs_req_cleanup(&req);
            if (r == 0 && req.statbuf.st_mode & S_IFREG) {  // is file
                uv_fs_access(nullptr, &req, localPath, readWrite ? R_OK : W_OK, nullptr);
                uv_fs_req_cleanup(&req);
                if (req.result == 0)
                    return true;
            }
        } else {  // dir
        }
        return false;
    }

    // Using openssl encryption and decryption method, high efficiency; when encrypting more than 64 bytes,
    // the carriage return will not be added, and the tail padding 00 is removed when decrypting
    // The return value is the length of the string after Base64
    int Base64EncodeBuf(const uint8_t *input, const int length, uint8_t *bufOut)
    {
        return EVP_EncodeBlock(bufOut, input, length);
    }

    vector<uint8_t> Base64Encode(const uint8_t *input, const int length)
    {
        vector<uint8_t> retVec;
        uint8_t *pBuf = nullptr;
        while (true) {
            if (static_cast<uint32_t>(length) > HDC_BUF_MAX_BYTES) {
                break;
            }
            int base64Size = length * 1.4 + 256;
            if (!(pBuf = new uint8_t[base64Size]())) {
                break;
            }
            int childRet = Base64EncodeBuf(input, length, pBuf);
            if (childRet <= 0) {
                break;
            }
            retVec.insert(retVec.begin(), pBuf, pBuf + childRet);
            break;
        }
        if (pBuf) {
            delete[] pBuf;
        }

        return retVec;
    }

    inline int CalcDecodeLength(const uint8_t *b64input)
    {
        int len = strlen(reinterpret_cast<char *>(const_cast<uint8_t *>(b64input)));
        if (!len) {
            return 0;
        }
        int padding = 0;
        if (b64input[len - 1] == '=' && b64input[len - 2] == '=') {
            // last two chars are =
            padding = 2;
        } else if (b64input[len - 1] == '=') {
            // last char is =
            padding = 1;
        }
        return static_cast<int>(len * 0.75 - padding);
    }

    // return -1 error; >0 decode size
    int Base64DecodeBuf(const uint8_t *input, const int length, uint8_t *bufOut)
    {
        int nRetLen = CalcDecodeLength(input);
        if (!nRetLen) {
            return 0;
        }

        if (EVP_DecodeBlock(bufOut, input, length) > 0) {
            return nRetLen;
        }
        return 0;
    }

    string Base64Decode(const uint8_t *input, const int length)
    {
        string retString;
        uint8_t *pBuf = nullptr;
        while (true) {
            if ((uint32_t)length > HDC_BUF_MAX_BYTES) {
                break;
            }
            // must less than length
            if (!(pBuf = new uint8_t[length]())) {
                break;
            }
            int childRet = Base64DecodeBuf(input, length, pBuf);
            if (childRet <= 0) {
                break;
            }
            retString = (reinterpret_cast<char *>(pBuf));
            break;
        }
        if (pBuf) {
            delete[] pBuf;
        }
        return retString;
    }

    void ReverseBytes(void *start, int size)
    {
        uint8_t *istart = (uint8_t *)start;
        uint8_t *iend = istart + size;
        std::reverse(istart, iend);
    }

    // clang-format off
    const string StringFormat(const char * const formater, ...)
    {
        va_list vaArgs;
        va_start(vaArgs, formater);
        string ret = StringFormat(formater, vaArgs);
        va_end(vaArgs);
        return ret;
    }

    const string StringFormat(const char * const formater, va_list &vaArgs)
    {
        std::vector<char> zc(MAX_SIZE_IOBUF);
        const int retSize = vsnprintf_s(zc.data(), MAX_SIZE_IOBUF, zc.size() - 1, formater, vaArgs);
        if (retSize < 0) {
            return std::string("");
        } else {
            return std::string(zc.data(), retSize);
        }
    }
    // clang-format on

    string GetVersion()
    {
        const uint8_t a = 'a';
        uint8_t major = (HDC_VERSION_NUMBER >> 28) & 0xff;
        uint8_t minor = (HDC_VERSION_NUMBER << 4 >> 24) & 0xff;
        uint8_t version = (HDC_VERSION_NUMBER << 12 >> 24) & 0xff;
        uint8_t fix = (HDC_VERSION_NUMBER << 20 >> 28) & 0xff;  // max 16, tail is p
        string ver = StringFormat("%x.%x.%x%c", major, minor, version, a + fix);
        return "Ver: " + ver;
    }

    bool IdleUvTask(uv_loop_t *loop, void *data, uv_idle_cb cb)
    {
        uv_idle_t *idle = new uv_idle_t();
        if (idle == nullptr) {
            return false;
        }
        idle->data = data;
        uv_idle_init(loop, idle);
        uv_idle_start(idle, cb);
        // delete by callback
        return true;
    }

    bool TimerUvTask(uv_loop_t *loop, void *data, uv_timer_cb cb, int repeatTimeout)
    {
        uv_timer_t *timer = new uv_timer_t();
        if (timer == nullptr) {
            return false;
        }
        timer->data = data;
        uv_timer_init(loop, timer);
        // default 250ms
        uv_timer_start(timer, cb, 0, repeatTimeout);
        // delete by callback
        return true;
    }

    // callback, uint8_t flag, string msg, const void * data
    bool DelayDo(uv_loop_t *loop, const int delayMs, const uint8_t flag, string msg, void *data,
                 std::function<void(const uint8_t, string &, const void *)> cb)
    {
        struct DelayDoParam {
            uv_timer_t handle;
            uint8_t flag;
            string msg;
            void *data;
            std::function<void(const uint8_t, string &, const void *)> cb;
        };
        auto funcDelayDo = [](uv_timer_t *handle) -> void {
            DelayDoParam *st = (DelayDoParam *)handle->data;
            st->cb(st->flag, st->msg, st->data);
            uv_close((uv_handle_t *)handle, [](uv_handle_t *handle) {
                DelayDoParam *st = (DelayDoParam *)handle->data;
                delete st;
            });
        };
        DelayDoParam *st = new DelayDoParam();
        if (st == nullptr) {
            return false;
        }
        st->cb = cb;
        st->flag = flag;
        st->msg = msg;
        st->data = data;
        st->handle.data = st;
        uv_timer_init(loop, &st->handle);
        uv_timer_start(&st->handle, funcDelayDo, delayMs, 0);
        return true;
    }

    string ReplaceAll(string str, const string from, const string to)
    {
        string::size_type startPos = 0;
        while ((startPos = str.find(from, startPos)) != string::npos) {
            str.replace(startPos, from.length(), to);
            startPos += to.length();  // Handles case where 'to' is a substring of 'from'
        }
        return str;
    }

    string CanonicalizeSpecPath(string &src)
    {
        char resolvedPath[PATH_MAX] = { 0 };
#if defined(_WIN32)
        if (!_fullpath(resolvedPath, src.c_str(), PATH_MAX)) {
            WRITE_LOG(LOG_FATAL, "_fullpath %s failed", src.c_str());
            return "";
        }
#else
        if (realpath(src.c_str(), resolvedPath) == nullptr) {
            WRITE_LOG(LOG_FATAL, "realpath %s failed", src.c_str());
            return "";
        }
#endif
        string res(resolvedPath);
        return res;
    }

    uint8_t CalcCheckSum(const uint8_t *data, int len)
    {
        uint8_t ret = 0;
        for (int i = 0; i < len; ++i) {
            ret += data[i];
        }
        return ret;
    }

    int open_osfhandle(uv_os_fd_t os_fd)
    {
        // equal libuv's uv_open_osfhandle, libuv 1.23 added. old libuv not impl...
#ifdef _WIN32
        return _open_osfhandle((intptr_t)os_fd, 0);
#else
        return os_fd;
#endif
    }

    uv_os_sock_t DuplicateUvSocket(uv_tcp_t *tcp)
    {
        uv_os_sock_t dupFd = -1;
#ifdef _WIN32
        WSAPROTOCOL_INFO info;
        ZeroStruct(info);
        if (WSADuplicateSocketA(tcp->socket, GetCurrentProcessId(), &info) < 0) {
            return dupFd;
        }
        dupFd = WSASocketA(0, 0, 0, &info, 0, 0);
#else
        uv_os_fd_t fdOs;
        if (uv_fileno((const uv_handle_t *)tcp, &fdOs) < 0) {
            return ERR_API_FAIL;
        }
        dupFd = dup(open_osfhandle(fdOs));
#endif
        return dupFd;
    }

    vector<uint8_t> Md5Sum(uint8_t *buf, int size)
    {
        vector<uint8_t> ret;
        uint8_t md5Hash[MD5_DIGEST_LENGTH] = { 0 };
        if (EVP_Digest(buf, size, md5Hash, NULL, EVP_md5(), NULL)) {
            ret.insert(ret.begin(), md5Hash, md5Hash + sizeof(md5Hash));
        }
        return ret;
    }

    bool IsRoot()
    {
#ifdef _WIN32
        // reserve
        return true;
#else
        if (getuid() == 0) {
            return true;
        }
#endif
        return false;
    }
}
}  // namespace Hdc
