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
#ifndef HDC_UART_H
#define HDC_UART_H
#include "common.h"

#include <chrono>
#include <numeric>
#include <sstream>
#include <unordered_set>

#ifndef _WIN32
#include <termios.h> // struct termios
#endif               // _WIN32

namespace Hdc {
#define USE_UART_CHECKSUM // all the data and head will have a checksum
#undef HDC_UART_TIMER_LOG // will have a lot of log from timer

enum UartProtocolOption {
    PKG_OPTION_TAIL = 1,  // makr is the last packget, can be send to session.
    PKG_OPTION_RESET = 2, // host request reset session in daemon
    PKG_OPTION_ACK = 4,   // reponse the pkg is received
    PKG_OPTION_NAK = 8,   // requeset resend pkg again
    PKG_OPTION_FREE = 16, // request free this session, some unable recovery error happend
};

static_assert(MAX_UART_SIZE_IOBUF != 0);

#pragma pack(push)
#pragma pack(1)
struct UartHead {
    UartHead(const UartHead &) = delete;
    UartHead &operator=(const UartHead &) = delete;
    UartHead(UartHead &&) = default;

    uint8_t flag[2];           // magic word
    uint16_t option;           // UartProtocolOption
    uint32_t sessionId;        // the package owner (COM dev owner)
    uint32_t dataSize;         // data size not include head
    uint32_t packageIndex;     // package index in this session
    uint32_t dataCheckSum = 0; // data checksum
    uint32_t headCheckSum = 0; // head checksum
    std::string ToPkgIdentityString(bool responsePackage = false) const
    {
        std::ostringstream oss;
        if (responsePackage) {
            oss << "R-";
        }
        oss << "Id:" << sessionId;
        oss << "pkgIdx:" << packageIndex;
        return oss.str();
    };
    std::string ToDebugString() const
    {
        std::ostringstream oss;
        oss << "UartHead [";
        oss << " flag:" << std::hex << unsigned(flag[0]) << " " << unsigned(flag[1]) << std::dec;
        oss << " option:" << unsigned(option);
        oss << " sessionId:" << sessionId;
        oss << " dataSize:" << dataSize;
        oss << " packageIndex:" << packageIndex;
        if (dataSize != 0) {
            oss << " dataCheckSum:" << std::hex << dataCheckSum;
        }
        oss << " headCheckSum:" << std::hex << headCheckSum;
        oss << std::dec;
        oss << "]";
        return oss.str();
    };
    UartHead(uint32_t sessionIdIn = 0, uint8_t optionIn = 0, uint32_t dataSizeIn = 0,
             uint32_t packageIndexIn = 0)
        : flag {PACKET_FLAG[0], PACKET_FLAG[1]},
          option(optionIn),
          sessionId(sessionIdIn),
          dataSize(dataSizeIn),
          packageIndex(packageIndexIn)
    {
    }
    bool operator==(const UartHead &r) const
    {
        return flag[0] == r.flag[0] and flag[1] == r.flag[1] and option == r.option and
               dataSize == r.dataSize and packageIndex == r.packageIndex;
    }
    bool IsResponsePackage() const
    {
        return (option & PKG_OPTION_ACK) or (option & PKG_OPTION_NAK);
    }
    void UpdateCheckSum()
    {
#ifdef USE_UART_CHECKSUM
        if (dataSize != 0) {
            const uint8_t *data = reinterpret_cast<const uint8_t *>(this) + sizeof(UartHead);
            dataCheckSum = std::accumulate(data, data + dataSize, 0u);
        }
        const uint8_t *head = reinterpret_cast<const uint8_t *>(this);
        size_t headCheckSumLen = sizeof(UartHead) - sizeof(headCheckSum);
        headCheckSum = std::accumulate(head, head + headCheckSumLen, 0u);
#endif
    }
    bool ValidateHead() const
    {
#ifdef USE_UART_CHECKSUM
        const uint8_t *head = reinterpret_cast<const uint8_t *>(this);
        size_t headCheckSumLen = sizeof(UartHead) - sizeof(headCheckSum);
        return (headCheckSum == std::accumulate(head, head + headCheckSumLen, 0u));
#else
        return true;
#endif
    }
    bool ValidateData() const
    {
#ifdef USE_UART_CHECKSUM
        const uint8_t *data = reinterpret_cast<const uint8_t *>(this) + sizeof(UartHead);
        if (dataSize == 0) {
            return true;
        } else {
            return (dataCheckSum == std::accumulate(data, data + dataSize, 0u));
        }
#else
        return true;
#endif
    }
};
#pragma pack(pop)

// we need virtual interface for UT the free function
class ExternInterface {
public:
    virtual void SetTcpOptions(uv_tcp_t *tcpHandle);
    virtual int SendToStream(uv_stream_t *handleStream, const uint8_t *buf, const int bufLen);
    virtual int UvTcpInit(uv_loop_t *, uv_tcp_t *, int);
    virtual int UvRead(uv_stream_t *, uv_alloc_cb, uv_read_cb);
    virtual int StartWorkThread(uv_loop_t *loop, uv_work_cb pFuncWorkThread,
                                uv_after_work_cb pFuncAfterThread, void *pThreadData);
    virtual void TryCloseHandle(const uv_handle_t *handle, uv_close_cb closeCallBack = nullptr);
    virtual bool TimerUvTask(uv_loop_t *loop, void *data, uv_timer_cb cb);
    virtual bool UvTimerStart(uv_timer_t *handle, uv_timer_cb cb, uint64_t timeout,
                              uint64_t repeat);
    using DelayCB = std::function<void(const uint8_t, string &, const void *)>;
    virtual bool DelayDo(uv_loop_t *loop, const int delayMs, const uint8_t flag, string msg,
                         void *data, DelayCB cb);
    virtual ~ExternInterface() = default;
};
class HdcSessionBase;
class HdcUARTBase {
public:
    static ExternInterface defaultInterface;
    HdcUARTBase(HdcSessionBase &, ExternInterface & = defaultInterface);
    virtual ~HdcUARTBase();
    bool ReadyForWorkThread(HSession hSession);
    int SendUARTData(HSession hSession, uint8_t *data, const size_t length);
    // call from session side
    // we need know when we need clear the pending send data
    virtual void StopSession(HSession hSession);

protected:
    static constexpr uint32_t DEFAULT_BAUD_RATE_VALUE = 921600;

    bool stopped = false; // stop only can be call one times

    // something is processing on working thread
    // Mainly used to reply a data back before stop.
    std::mutex workThreadProcessingData;

    // review how about make a HUART in daemon side and put the devhandle in it ?
    int uartHandle = -1;
    virtual bool SendUARTRaw(HSession hSession, uint8_t *data, const size_t length);
    virtual void SendUartSoftReset(HSession hUART, uint32_t sessionId) {};
    virtual RetErrCode ValidateUartPacket(vector<uint8_t> &data, uint32_t &sessionId,
                                          uint32_t &packageIndex, size_t &fullPackageLength);
    virtual void NotifyTransfer();
    virtual void ResetOldSession(uint32_t sessionId)
    {
        return;
    }
    virtual void Restartession(const HSession session);

#ifndef _WIN32
    int SetSerial(int fd, int nSpeed, int nBits, char nEvent, int nStop);
#endif // _WIN32
    virtual bool UartSendToHdcStream(HSession hSession, uint8_t *data, size_t size);
    static void ReadDataFromUARTStream(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    bool uartOpened;

    static constexpr size_t MAX_READ_BUFFER = MAX_UART_SIZE_IOBUF * 10;
    static constexpr int ReadGiveUpTimeOutTimeMs = 500; // 500ms
    virtual int UartToHdcProtocol(uv_stream_t *stream, uint8_t *appendData, int dataSize);
    int GetUartSpeed(int speed);
    int GetUartBits(int bits);
    virtual void ResponseUartTrans(uint32_t sessionId, uint32_t packageIndex,
                                   UartProtocolOption option);

    virtual size_t PackageProcess(vector<uint8_t> &data, HSession hSession = nullptr);
    virtual RetErrCode DispatchToWorkThread(HSession hSession, uint8_t *readBuf, int readBytes);

    virtual void OnTransferError(const HSession session) = 0;
    virtual HSession GetSession(const uint32_t sessionId, bool create = false) = 0;

    /*
        read data from uart devices
        Args:
        readBuf         data will append to readBuf
        expectedSize    function will not return until expected size read

        Return:
        ssize_t         >   0 how many bytes read after this function called
                        ==  0 nothing read , timeout happend(expectedSize > 0)
                        <   0 means devices error
    */

    // we have some oswait in huart(bind to each session/uart device)
    virtual ssize_t ReadUartDev(std::vector<uint8_t> &readBuf, size_t expectedSize, HdcUART &uart);

    virtual ssize_t WriteUartDev(uint8_t *data, const size_t length, HdcUART &uart);

    ExternInterface &externInterface;

    virtual void RequestSendPackage(uint8_t *data, const size_t length, bool queue = true);
    virtual void ProcessResponsePackage(const UartHead &head);
    virtual void SendPkgInUARTOutMap();
    virtual void ClearUARTOutMap(uint32_t sessionId);
    virtual void EnsureAllPkgsSent();
    static constexpr int WaitResponseTimeOutMs = 1000; // 1000ms
    static constexpr int OneMoreMs = 1;

    class TransferStateMachine {
    public:
        void Request()
        {
            std::unique_lock<std::mutex> lock(mutex);
            requested = true;
            cv.notify_one();
        }

        void Sent()
        {
            std::unique_lock<std::mutex> lock(mutex);
            timeout = true;
            // wait_for will timeout in 999ms in linux platform, so we add one more
            timeoutPoint = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(WaitResponseTimeOutMs + OneMoreMs);
            cv.notify_one();
        }

        void Wait();

    private:
        std::mutex mutex;
        std::condition_variable cv;
        bool requested = false; // some one request send something
        std::chrono::steady_clock::time_point timeoutPoint;
        bool timeout = false; // some data is sendout and next wait need wait response
    } transfer;

private:
    HdcSessionBase &sessionBase;

    enum PkgStatus {
        PKG_WAIT_SEND,
        PKG_WAIT_RESPONSE,
    };
    struct HandleOutputPkg {
        std::string key;
        uint32_t sessionId = 0; // like group , sometimes we will delete by this filter
        bool response;          // PKG for response
        bool ack;               // UartResponseCode for this packge
        uint8_t pkgStatus;
        vector<uint8_t> msgSendBuf;
        size_t retryChance = 4; // how many time need retry
        std::chrono::time_point<std::chrono::steady_clock> sendTimePoint;
        // reivew if we need direct process UartHead ?
        HandleOutputPkg(std::string keyIn, uint32_t sessionIdIn, uint8_t *data, size_t length,
                        bool responseIn = false, bool ackIn = false)
            : key(keyIn),
              sessionId(sessionIdIn),
              response(responseIn),
              ack(ackIn),
              pkgStatus(PKG_WAIT_SEND),
              msgSendBuf(data, data + length)
        {
        }
        std::string ToDebugString()
        {
            std::string debug;
            debug.append(key);
            debug.append(" pkgStatus:");
            debug.append(std::to_string(pkgStatus));
            if (pkgStatus == PKG_WAIT_RESPONSE) {
                debug.append(" sent:");
                auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - sendTimePoint);
                debug.append(std::to_string(elapsedTime.count()));
                debug.append(" ms");
                debug.append(" retry Chance:");
                debug.append(std::to_string(retryChance));
            }
            if (response) {
                debug.append(" response:");
                if (ack) {
                    debug.append(" ACK");
                } else {
                    debug.append(" NAK");
                }
            }
            return debug;
        }
    };

    class TransferSlot {
    public:
        void Wait(uint32_t sessionId)
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [=] { return hasWaitPkg.find(sessionId) == hasWaitPkg.end(); });
            hasWaitPkg.emplace(sessionId);
        }

        void Free(uint32_t sessionId)
        {
            std::unique_lock<std::mutex> lock(mutex);
            hasWaitPkg.erase(sessionId);
            cv.notify_one();
        }

        // call when exit
        void WaitFree()
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, std::chrono::milliseconds(WaitResponseTimeOutMs),
                        [=] { return hasWaitPkg.size() == 0; });
        }

    private:
        std::mutex mutex;
        std::condition_variable cv;
        std::unordered_set<uint32_t> hasWaitPkg;
    } slots;

    vector<HandleOutputPkg> outPkgs; // Pkg label, HOutPkg
    std::recursive_mutex mapOutPkgsMutex;
    struct HandleOutputPkgKeyFinder {
        const std::string &key;
        HandleOutputPkgKeyFinder(const std::string &keyIn) : key(keyIn) {}
        bool operator()(const HandleOutputPkg &other)
        {
            return key == other.key;
        }
    };
};
} // namespace Hdc
#endif
