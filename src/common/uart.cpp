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
#include "uart.h"

using namespace std::chrono;
namespace Hdc {
ExternInterface HdcUARTBase::defaultInterface;

void ExternInterface::SetTcpOptions(uv_tcp_t *tcpHandle)
{
    return Base::SetTcpOptions(tcpHandle);
}

int ExternInterface::SendToStream(uv_stream_t *handleStream, const uint8_t *buf, const int len)
{
    return Base::SendToStream(handleStream, buf, len);
}

int ExternInterface::UvTcpInit(uv_loop_t *loop, uv_tcp_t *tcp, int socketFd)
{
    if (uv_tcp_init(loop, tcp) == 0) {
        return uv_tcp_open(tcp, socketFd);
    } else {
        return -1;
    }
}

int ExternInterface::UvRead(uv_stream_t *stream, uv_alloc_cb allocCallBack, uv_read_cb readCallBack)
{
    return uv_read_start(stream, allocCallBack, readCallBack);
}

int ExternInterface::StartWorkThread(uv_loop_t *loop, uv_work_cb pFuncWorkThread,
                                     uv_after_work_cb pFuncAfterThread, void *pThreadData)
{
    return Base::StartWorkThread(loop, pFuncWorkThread, pFuncAfterThread, pThreadData);
}

void ExternInterface::TryCloseHandle(const uv_handle_t *handle, uv_close_cb closeCallBack)
{
    return Base::TryCloseHandle(handle, closeCallBack);
}

bool ExternInterface::TimerUvTask(uv_loop_t *loop, void *data, uv_timer_cb cb)
{
    return Base::TimerUvTask(loop, data, cb);
}
bool ExternInterface::UvTimerStart(uv_timer_t *handle, uv_timer_cb cb, uint64_t timeout,
                                   uint64_t repeat)
{
    return uv_timer_start(handle, cb, timeout, repeat);
}

bool ExternInterface::DelayDo(uv_loop_t *loop, const int delayMs, const uint8_t flag, string msg,
                              void *data, DelayCB cb)
{
    return Base::DelayDo(loop, delayMs, flag, msg, data, cb);
}

HdcUARTBase::HdcUARTBase(HdcSessionBase &sessionBaseIn, ExternInterface &interfaceIn)
    : externInterface(interfaceIn), sessionBase(sessionBaseIn)
{
    uartOpened = false;
}

HdcUARTBase::~HdcUARTBase(void) {}

#ifndef _WIN32
int HdcUARTBase::GetUartSpeed(int speed)
{
    switch (speed) {
        case UART_SPEED2400:
            return (B2400);
            break;
        case UART_SPEED4800:
            return (B4800);
            break;
        case UART_SPEED9600:
            return (B9600);
            break;
        case UART_SPEED115200:
            return (B115200);
            break;
        case UART_SPEED921600:
            return (B921600);
            break;
        default:
            return (B921600);
            break;
    }
}
int HdcUARTBase::GetUartBits(int bits)
{
    switch (bits) {
        case UART_BIT1:
            return (CS7);
            break;
        case UART_BIT2:
            return (CS8);
            break;
        default:
            return (CS8);
            break;
    }
}

int HdcUARTBase::SetSerial(int fd, int nSpeed, int nBits, char nEvent, int nStop)
{
    struct termios newttys1, oldttys1;
    if (tcgetattr(fd, &oldttys1) != 0) {
        constexpr int bufSize = 1024;
        char buf[bufSize] = { 0 };
        strerror_r(errno, buf, bufSize);
        WRITE_LOG(LOG_DEBUG, "tcgetattr failed with %s\n", buf);
        return ERR_GENERIC;
    }
    bzero(&newttys1, sizeof(newttys1));
    newttys1.c_cflag = GetUartSpeed(nSpeed);
    newttys1.c_cflag |= (CLOCAL | CREAD);
    newttys1.c_cflag &= ~CSIZE;
    newttys1.c_lflag &= ~ICANON;
    newttys1.c_cflag |= GetUartBits(nBits);
    switch (nEvent) {
        case '0':
            newttys1.c_cflag |= PARENB;
            newttys1.c_iflag |= (INPCK | ISTRIP);
            newttys1.c_cflag |= PARODD;
            break;
        case 'E':
            newttys1.c_cflag |= PARENB;
            newttys1.c_iflag |= (INPCK | ISTRIP);
            newttys1.c_cflag &= ~PARODD;
            break;
        case 'N':
            newttys1.c_cflag &= ~PARENB;
            break;
        default:
            break;
    }
    if (nStop == UART_STOP1) {
        newttys1.c_cflag &= ~CSTOPB;
    } else if (nStop == UART_STOP2) {
        newttys1.c_cflag |= CSTOPB;
    }
    newttys1.c_cc[VTIME] = 0;
    newttys1.c_cc[VMIN] = 0;
    if (tcflush(fd, TCIOFLUSH)) {
        WRITE_LOG(LOG_DEBUG, " tcflush error.");
        return ERR_GENERIC;
    }
    if ((tcsetattr(fd, TCSANOW, &newttys1)) != 0) {
        WRITE_LOG(LOG_DEBUG, " com set error");
        return ERR_GENERIC;
    }
    WRITE_LOG(LOG_DEBUG, " SetSerial OK");
    return RET_SUCCESS;
}
#endif // _WIN32

ssize_t HdcUARTBase::ReadUartDev(std::vector<uint8_t> &readBuf, size_t expectedSize, HdcUART &uart)
{
    ssize_t totalBytesRead = 0;
    uint8_t uartReadBuffer[MAX_UART_SIZE_IOBUF];
#ifdef _WIN32
    DWORD bytesRead = 0;
#else
    ssize_t bytesRead = 0;
#endif
    do {
        bytesRead = 0;
#ifdef _WIN32
        BOOL bReadStatus = ReadFile(uart.devUartHandle, uartReadBuffer, sizeof(uartReadBuffer),
                                    &bytesRead, &uart.ovRead);
        if (!bReadStatus) {
            if (GetLastError() == ERROR_IO_PENDING) {
                bytesRead = 0;
                DWORD dwMilliseconds = ReadGiveUpTimeOutTimeMs;
                if (expectedSize == 0) {
                    dwMilliseconds = INFINITE;
                }
                if (!GetOverlappedResultEx(uart.devUartHandle, &uart.ovRead, &bytesRead,
                                           dwMilliseconds, FALSE)) {
                    // wait io failed
                    DWORD error = GetLastError();
                    if (error == ERROR_OPERATION_ABORTED) {
                        totalBytesRead += bytesRead;
                        WRITE_LOG(LOG_DEBUG, "%s error cancel read. %u %zd", __FUNCTION__,
                                  bytesRead, totalBytesRead);
                        // Generally speaking, this is the cacnel caused by freesession
                        // Returning allows the outer read loop to run again. This checks the exit
                        // condition.
                        return totalBytesRead;
                    } else if (error == WAIT_TIMEOUT) {
                        totalBytesRead += bytesRead;
                        WRITE_LOG(LOG_DEBUG, "%s error timeout. %u %zd", __FUNCTION__, bytesRead,
                                  totalBytesRead);
                        return totalBytesRead;
                    } else {
                        WRITE_LOG(LOG_DEBUG, "%s error wait io:%d.", __FUNCTION__, GetLastError());
                    }
                    return -1;
                }
            } else {
                // not ERROR_IO_PENDING
                WRITE_LOG(LOG_DEBUG, "%s  err:%d. ", __FUNCTION__, GetLastError());
                return -1;
            }
        }
#else
        int ret = 0;
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(uart.devUartHandle, &readFds);
        const constexpr int msTous = 1000;
        struct timeval tv;
        tv.tv_sec = 0;

        if (expectedSize == 0) {
            tv.tv_usec = WaitResponseTimeOutMs * msTous;
#ifdef HDC_HOST
            // only host side need this
            // in this caes
            // We need a way to exit from the select for the destruction and recovery of the
            // serial port read thread.
            ret = select(uart.devUartHandle + 1, &readFds, nullptr, nullptr, &tv);
#else
            ret = select(uart.devUartHandle + 1, &readFds, nullptr, nullptr, nullptr);
#endif
        } else {
            // when we have expect size , we need timeout for link data drop issue
            tv.tv_usec = ReadGiveUpTimeOutTimeMs * msTous;
            ret = select(uart.devUartHandle + 1, &readFds, nullptr, nullptr, &tv);
        }
        if (ret == 0 and expectedSize == 0) {
            // no expect but timeout
            if (uart.ioCancel) {
                WRITE_LOG(LOG_DEBUG, "%s:uart select time out and io cancel", __FUNCTION__);
                uart.ioCancel = true;
                return totalBytesRead;
            } else {
                continue;
            }
        } else if (ret == 0) {
            WRITE_LOG(LOG_DEBUG, "%s:uart select time out!", __FUNCTION__);
            // we expected some byte , but not arrive before timeout
            return totalBytesRead;
        } else if (ret < 0) {
            WRITE_LOG(LOG_DEBUG, "%s:uart select error! %d", __FUNCTION__, errno);
            return -1; // wait failed.
        } else {
            // select > 0
            bytesRead = read(uart.devUartHandle, uartReadBuffer, sizeof(uartReadBuffer));
            if (bytesRead <= 0) {
                // read failed !
                WRITE_LOG(LOG_WARN, "%s:read failed! %zd:%d", __FUNCTION__, bytesRead, errno);
                return -1;
            }
        }
#endif
        if (bytesRead > 0) {
            readBuf.insert(readBuf.end(), uartReadBuffer, uartReadBuffer + bytesRead);
            totalBytesRead += bytesRead;
        }
    } while (readBuf.size() < expectedSize or
             bytesRead == 0); // if caller know how many bytes it want
    return totalBytesRead;
}

ssize_t HdcUARTBase::WriteUartDev(uint8_t *data, const size_t length, HdcUART &uart)
{
    ssize_t totalBytesWrite = 0;
    WRITE_LOG(LOG_ALL, "%s %d data %x %x", __FUNCTION__, length, *(data + sizeof(UartHead)),
              *(data + sizeof(UartHead) + 1));
    do {
#ifdef _WIN32
        DWORD bytesWrite = 0;
        BOOL bWriteStat = WriteFile(uart.devUartHandle, data + totalBytesWrite,
                                    length - totalBytesWrite, &bytesWrite, &uart.ovWrite);
        if (!bWriteStat) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(uart.devUartHandle, &uart.ovWrite, &bytesWrite, TRUE)) {
                    WRITE_LOG(LOG_DEBUG, "%s error wait io:%d. bytesWrite %zu", __FUNCTION__,
                              GetLastError(), bytesWrite);
                    return -1;
                }
            } else {
                WRITE_LOG(LOG_DEBUG, "%s err:%d. bytesWrite %zu", __FUNCTION__, GetLastError(),
                          bytesWrite);
                return -1;
            }
        }
#else // not win32
        ssize_t bytesWrite = 0;
        bytesWrite = write(uart.devUartHandle, data + totalBytesWrite, length - totalBytesWrite);
        if (bytesWrite < 0) {
            if (errno == EINTR or errno == EAGAIN) {
                WRITE_LOG(LOG_WARN, "EINTR/EAGAIN, try again");
                continue;
            } else {
                // we don't know how to recory in this function
                // need reopen device ?
                constexpr int bufSize = 1024;
                char buf[bufSize] = { 0 };
                strerror_r(errno, buf, bufSize);
                WRITE_LOG(LOG_FATAL, "write fatal errno %d:%s", errno, buf);
                return -1;
            }
        } else {
            // waits until all output written to the object referred to by fd has been transmitted.
            tcdrain(uart.devUartHandle);
        }
#endif
        totalBytesWrite += bytesWrite;
    } while (totalBytesWrite < signed(length));

    return totalBytesWrite;
}

int HdcUARTBase::UartToHdcProtocol(uv_stream_t *stream, uint8_t *data, int dataSize)
{
    HSession hSession = (HSession)stream->data;
    unsigned int fd = hSession->dataFd[STREAM_MAIN];
    fd_set fdSet;
    struct timeval timeout = {3, 0};
    FD_ZERO(&fdSet);
    FD_SET(fd, &fdSet);
    int index = 0;
    int childRet = 0;

    while (index < dataSize) {
        childRet = select(fd + 1, NULL, &fdSet, NULL, &timeout);
        if (childRet <= 0) {
            constexpr int bufSize = 1024;
            char buf[bufSize] = { 0 };
#ifdef _WIN32
            strerror_s(buf, bufSize, errno);
#else
            strerror_r(errno, buf, bufSize);
#endif
            WRITE_LOG(LOG_FATAL, "%s select error:%d [%s][%d]", __FUNCTION__, errno,
                      buf, childRet);
            break;
        }
        childRet = send(fd, (const char *)data + index, dataSize - index, 0);
        if (childRet < 0) {
            constexpr int bufSize = 1024;
            char buf[bufSize] = { 0 };
#ifdef _WIN32
            strerror_s(buf, bufSize, errno);
#else
            strerror_r(errno, buf, bufSize);
#endif
            WRITE_LOG(LOG_FATAL, "%s senddata err:%d [%s]", __FUNCTION__, errno, buf);
            break;
        }
        index += childRet;
    }
    if (index != dataSize) {
        WRITE_LOG(LOG_FATAL, "%s partialsenddata err:%d [%d]", __FUNCTION__, index, dataSize);
        return ERR_IO_FAIL;
    }
    return index;
}

RetErrCode HdcUARTBase::DispatchToWorkThread(HSession hSession, uint8_t *readBuf, int readBytes)
{
    if (hSession == nullptr) {
        return ERR_SESSION_NOFOUND;
    }
    if (!UartSendToHdcStream(hSession, readBuf, readBytes)) {
        return ERR_IO_FAIL;
    }
    return RET_SUCCESS;
}

size_t HdcUARTBase::PackageProcess(vector<uint8_t> &data, HSession hSession)
{
    while (data.size() >= sizeof(UartHead)) {
        // is size more than one head
        size_t packetSize = 0;
        uint32_t sessionId = 0;
        uint32_t packageIndex = 0;
        // we erase all buffer. wait next read.
        if (ValidateUartPacket(data, sessionId, packageIndex, packetSize) != RET_SUCCESS) {
            WRITE_LOG(LOG_WARN, "%s package error. clean the read buffer.", __FUNCTION__);
            data.clear();
        } else if (packetSize == sizeof(UartHead)) {
            // nothing need to send, this is a head only package
            // only used in link layer
            WRITE_LOG(LOG_ALL, "%s headonly Package(%zu). dont send to session, erase it",
                      __FUNCTION__, packetSize);
        } else {
            // at least we got one package
            // if the size of packge have all received ?
            if (data.size() >= packetSize) {
                // send the data to logic level (link to logic)
                if (hSession == nullptr) {
#ifdef HDC_HOST
                    hSession = GetSession(sessionId);
#else
                    // for daemon side we can make a new session for it
                    hSession = GetSession(sessionId, true);
#endif
                }
                if (hSession == nullptr) {
                    WRITE_LOG(LOG_WARN, "%s have not found seesion (%u). skip it", __FUNCTION__, sessionId);
                } else {
                    if (hSession->hUART->dispatchedPackageIndex == packageIndex) {
                        // we need check if the duplication pacakge we have already send
                        WRITE_LOG(LOG_WARN, "%s dup package %u, skip send to session logic",
                                  __FUNCTION__, packageIndex);
                    } else {
                        // update the last package we will send to hdc
                        hSession->hUART->dispatchedPackageIndex = packageIndex;
                        RetErrCode ret = DispatchToWorkThread(hSession, data.data(), packetSize);
                        if (ret == RET_SUCCESS) {
                            WRITE_LOG(LOG_DEBUG, "%s DispatchToWorkThread successful",
                                      __FUNCTION__);
                        } else {
                            // send to logic failed.
                            // this kind of issue unable handle in link layer
                            WRITE_LOG(LOG_FATAL,
                                      "%s DispatchToWorkThread fail %d. requeset free session in "
                                      "other side",
                                      __FUNCTION__, ret);
                            ResponseUartTrans(hSession->sessionId, ++hSession->hUART->packageIndex,
                                              PKG_OPTION_FREE);
                        }
                    }
                }
            } else {
                WRITE_LOG(LOG_DEBUG, "%s valid package, however size not enough. expect %zu",
                          __FUNCTION__, packetSize);
                return packetSize;
            }
        }

        if (data.size() >= packetSize) {
            data.erase(data.begin(), data.begin() + packetSize);
        } else {
            // dont clean , should merge with next package
        }
        WRITE_LOG(LOG_DEBUG, "PackageProcess data.size():%d left", data.size());
    }
    // if we have at least one byte, we think there should be a head
    return data.size() > 1 ? sizeof(UartHead) : 0;
}

bool HdcUARTBase::SendUARTRaw(HSession hSession, uint8_t *data, const size_t length)
{
    struct UartHead *uartHeader = (struct UartHead *)data;
#ifndef HDC_HOST
    // review nobody can plug out the daemon uart , if we still need split write in daemon side?
    HdcUART deamonUart;
    deamonUart.devUartHandle = uartHandle;
    if (uartHeader->IsResponsePackage()) {
        // for the response package and in daemon side,
        // we dont need seesion info
        ssize_t sendBytes = WriteUartDev(data, length, deamonUart);
        return sendBytes > 0;
    }
#endif

    // for normal package
    if (hSession == nullptr) {
        hSession = GetSession(uartHeader->sessionId);
        if (hSession == nullptr) {
            // session is not found
            WRITE_LOG(LOG_WARN, "%s hSession not found:%zu", __FUNCTION__, uartHeader->sessionId);
            return false;
        }
    }
    hSession->ref++;
    WRITE_LOG(LOG_DEBUG, "%s length:%d", __FUNCTION__, length);
#ifdef HDC_HOST
    ssize_t sendBytes = WriteUartDev(data, length, *hSession->hUART);
#else
    ssize_t sendBytes = WriteUartDev(data, length, deamonUart);
#endif
    WRITE_LOG(LOG_DEBUG, "%s sendBytes %zu", __FUNCTION__, sendBytes);
    if (sendBytes < 0) {
        WRITE_LOG(LOG_DEBUG, "%s send fail. try to freesession", __FUNCTION__);
        OnTransferError(hSession);
    }
    hSession->ref--;
    return sendBytes > 0;
}

// this function will not check the data correct again
// just send the data to hdc session side
bool HdcUARTBase::UartSendToHdcStream(HSession hSession, uint8_t *data, size_t size)
{
    WRITE_LOG(LOG_DEBUG, "%s send to session %s package size %zu", __FUNCTION__,
              hSession->ToDebugString().c_str(), size);

    int ret = RET_SUCCESS;

    if (size < sizeof(UartHead)) {
        WRITE_LOG(LOG_FATAL, "%s buf size too small %zu", __FUNCTION__, size);
        return ERR_BUF_SIZE;
    }

    UartHead *head = reinterpret_cast<UartHead *>(data);
    WRITE_LOG(LOG_DEBUG, "%s uartHeader:%s data: %x %x", __FUNCTION__,
              head->ToDebugString().c_str(), *(data + sizeof(UartHead)),
              *(data + sizeof(UartHead) + 1));

    // review need check logic again here or err process
    if (head->sessionId != hSession->sessionId) {
        if (hSession->serverOrDaemon && !hSession->hUART->resetIO) {
            WRITE_LOG(LOG_FATAL, "%s sessionId not matched, reset sessionId:%d.", __FUNCTION__,
                      head->sessionId);
            SendUartSoftReset(hSession, head->sessionId);
            hSession->hUART->resetIO = true;
            ret = ERR_IO_SOFT_RESET;
            // dont break ,we need rease these data in recv buffer
        }
    } else {
        //  data to session
        hSession->hUART->streamSize += head->dataSize; // this is only for debug,
        WRITE_LOG(LOG_ALL, "%s stream wait session read size: %zu", __FUNCTION__,
                  hSession->hUART->streamSize.load());
        if (UartToHdcProtocol(reinterpret_cast<uv_stream_t *>(&hSession->dataPipe[STREAM_MAIN]),
                              data + sizeof(UartHead), head->dataSize) < 0) {
            ret = ERR_IO_FAIL;
            WRITE_LOG(LOG_FATAL, "%s Error uart send to stream", __FUNCTION__);
        }
    }

    return ret == RET_SUCCESS;
}

void HdcUARTBase::NotifyTransfer()
{
    WRITE_LOG(LOG_DEBUG, "%s", __FUNCTION__);
    transfer.Request();
}

/*
here we have a HandleOutputPkg vector
It is used to maintain the data reliability of the link layer
It consists of the following part
Log data to send (caller thread)                        --> RequestSendPackage
Send recorded data (loop sending thread)                --> SendPkgInUARTOutMap
Process the returned reply data (loop reading thread)   --> ProcessResponsePackage
Send reply packet (loop reading thread)                 --> ResponseUartTrans

The key scenarios are as follows:
Package is sent from side A to side B
Here we call the complete data package
package is divided into head and data
The response information is in the header.
data contains binary data.

case 1: Normal Process
    package
A   -->   B
    ACK
A   <--   B

case 2: packet is incorrect
At least one header must be received
For this the B side needs to have an accept timeout.
There is no new data within a certain period of time as the end of the packet.
(This mechanism is not handled in HandleOutputPkg retransmission)

    incorrect
A   -->   B
B sends NAK and A resends the packet.
    NAK
A   <--   B
    package resend
A   -->   B

case 3: packet is complete lost()
    package(complete lost)
A   -x->   B
The A side needs to resend the Package after a certain timeout
A   -->   B
Until the B side has a data report (ACK or NAK), or the number of retransmissions reaches the upper
limit.
*/
void HdcUARTBase::RequestSendPackage(uint8_t *data, const size_t length, bool queue)
{
    UartHead *head = reinterpret_cast<UartHead *>(data);
    bool response = head->IsResponsePackage();

    if (queue) {
        slots.Wait(head->sessionId);
    }

    std::lock_guard<std::recursive_mutex> lock(mapOutPkgsMutex);

    std::string pkgId = head->ToPkgIdentityString(response);
    auto it = std::find_if(outPkgs.begin(), outPkgs.end(), HandleOutputPkgKeyFinder(pkgId));
    if (it == outPkgs.end()) {
        // update che checksum , both head and data
        head->UpdateCheckSum();
        outPkgs.emplace_back(pkgId, head->sessionId, data, length, response,
                             head->option & PKG_OPTION_ACK);
        WRITE_LOG(LOG_DEBUG, "UartPackageManager: add pkg %s (pkgs size %zu)",
                  head->ToDebugString().c_str(), outPkgs.size());
    } else {
        WRITE_LOG(LOG_FATAL, "UartPackageManager: add pkg %s fail, %s has already been exist.",
                  head->ToDebugString().c_str(), pkgId.c_str());
    }
    NotifyTransfer();
}

void HdcUARTBase::ProcessResponsePackage(const UartHead &head)
{
    std::lock_guard<std::recursive_mutex> lock(mapOutPkgsMutex);
    bool ack = head.option & PKG_OPTION_ACK;
    // response package
    std::string pkgId = head.ToPkgIdentityString();
    WRITE_LOG(LOG_ALL, "UartPackageManager: got response pkgId:%s ack:%d.", pkgId.c_str(), ack);

    auto it = std::find_if(outPkgs.begin(), outPkgs.end(), HandleOutputPkgKeyFinder(pkgId));
    if (it != outPkgs.end()) {
        if (ack) { // response ACK.
            slots.Free(it->sessionId);
            outPkgs.erase(it);
            WRITE_LOG(LOG_DEBUG, "UartPackageManager: erase pkgId:%s.", pkgId.c_str());
        } else {                           // response NAK
            it->pkgStatus = PKG_WAIT_SEND; // Re send the pkg
            WRITE_LOG(LOG_WARN, "UartPackageManager: resend pkgId:%s.", pkgId.c_str());
        }
    } else {
        WRITE_LOG(LOG_FATAL, "UartPackageManager: hasn't found pkg for pkgId:%s.", pkgId.c_str());
        for (auto pkg : outPkgs) {
            WRITE_LOG(LOG_ALL, "UartPackageManager:  pkgId:%s.", pkg.key.c_str());
        }
    }
    NotifyTransfer();
    return;
}

void HdcUARTBase::SendPkgInUARTOutMap()
{
    std::lock_guard<std::recursive_mutex> lock(mapOutPkgsMutex);
    if (outPkgs.empty()) {
        WRITE_LOG(LOG_ALL, "UartPackageManager: No pkgs needs to be sent.");
        return;
    }
    WRITE_LOG(LOG_DEBUG, "UartPackageManager: send pkgs, have:%zu pkgs", outPkgs.size());
    // we have maybe more than one session
    // each session has it owner serial port
    std::unordered_set<uint32_t> hasWaitPkg;
    auto it = outPkgs.begin();
    while (it != outPkgs.end()) {
        if (it->pkgStatus == PKG_WAIT_SEND) {
            // we found a pkg wait for send
            // if a response package
            // response package always send nowait noorder
            if (!it->response and hasWaitPkg.find(it->sessionId) != hasWaitPkg.end()) {
                // this is not a response package
                // and this session is wait response
                // so we can send nothing
                // process next
                it++;
                continue;
            }
            // we will ready to send the package
            WRITE_LOG(LOG_DEBUG, "UartPackageManager: send pkg %s", it->ToDebugString().c_str());
            SendUARTRaw(nullptr, it->msgSendBuf.data(), it->msgSendBuf.size());
            if (it->response) {
                // response pkg dont need wait response again.
                WRITE_LOG(LOG_DEBUG, "UartPackageManager: erase pkg %s",
                          it->ToDebugString().c_str());
                it = outPkgs.erase(it);
                continue;
            } else {
                // normal send package
                it->pkgStatus = PKG_WAIT_RESPONSE;
                it->sendTimePoint = steady_clock::now();
                hasWaitPkg.emplace(it->sessionId);
                transfer.Sent(); // something is sendout, transfer will timeout for next wait.
            }
        } else if (it->pkgStatus == PKG_WAIT_RESPONSE) {
            // we found a pkg wiat for response
            auto elapsedTime = duration_cast<milliseconds>(steady_clock::now() - it->sendTimePoint);
            WRITE_LOG(LOG_DEBUG, "UartPackageManager: pkg:%s is wait ACK. elapsedTime %lld",
                      it->ToDebugString().c_str(), (long long)elapsedTime.count());
            if (elapsedTime.count() >= WaitResponseTimeOutMs) {
                // check the response timeout
                if (it->retryChance > 0) {
                    // if it send timeout, resend it again.
                    WRITE_LOG(LOG_WARN, "UartPackageManager: pkg:%s try resend it.",
                              it->ToDebugString().c_str());
                    it->pkgStatus = PKG_WAIT_SEND;
                    it->retryChance--;
                    NotifyTransfer(); // make transfer reschedule
                    break;            // dont process anything now.
                } else {
                    // the response it timeout and retry counx is 0
                    // the link maybe not stable
                    // let's free this session
                    WRITE_LOG(LOG_WARN, "UartPackageManager: reach max retry ,free the seesion %u",
                              it->sessionId);
                    OnTransferError(GetSession(it->sessionId));
                    // dont reschedule here
                    // wait next schedule from this path
                    // OnTransferError -> FreeSession -> ClearUARTOutMap -> NotifyTransfer
                    break;
                }
            }
            hasWaitPkg.emplace(it->sessionId);
        }
        it++; // next package
    }
    WRITE_LOG(LOG_DEBUG, "UartPackageManager: send finish, have %zu pkgs", outPkgs.size());
}

void HdcUARTBase::ClearUARTOutMap(uint32_t sessionId)
{
    WRITE_LOG(LOG_DEBUG, "%s UartPackageManager clean for sessionId %u", __FUNCTION__, sessionId);
    size_t erased = 0;
    std::lock_guard<std::recursive_mutex> lock(mapOutPkgsMutex);
    auto it = outPkgs.begin();
    while (it != outPkgs.end()) {
        if (it->sessionId == sessionId) {
            if (!it->response) {
                slots.Free(it->sessionId);
            }
            it = outPkgs.erase(it);
            erased++;
        } else {
            it++;
        }
    }
    WRITE_LOG(LOG_DEBUG, "%s erased %zu", __FUNCTION__, erased);

    NotifyTransfer(); // tell transfer we maybe have some change
}

void HdcUARTBase::EnsureAllPkgsSent()
{
    WRITE_LOG(LOG_DEBUG, "%s", __FUNCTION__);
    slots.WaitFree();
    if (!outPkgs.empty()) {
        std::this_thread::sleep_for(1000ms);
    }
    WRITE_LOG(LOG_DEBUG, "%s done.", __FUNCTION__);
}

RetErrCode HdcUARTBase::ValidateUartPacket(vector<uint8_t> &data, uint32_t &sessionId,
                                           uint32_t &packageIndex, size_t &packetSize)
{
    constexpr auto maxBufFactor = 1;
    struct UartHead *head = (struct UartHead *)data.data();
    WRITE_LOG(LOG_DEBUG, "%s %s", __FUNCTION__, head->ToDebugString().c_str());

    if (memcmp(head->flag, PACKET_FLAG.c_str(), PACKET_FLAG.size()) != 0) {
        WRITE_LOG(LOG_FATAL, "%s,PACKET_FLAG not correct %x %x", __FUNCTION__, head->flag[0],
                  head->flag[1]);
        return ERR_BUF_CHECK;
    }

    if (!head->ValidateHead()) {
        WRITE_LOG(LOG_FATAL, "%s head checksum not correct", __FUNCTION__);
        return ERR_BUF_CHECK;
    }
    // after validate , id and fullPackageLength is correct
    sessionId = head->sessionId;
    packetSize = head->dataSize + sizeof(UartHead);
    packageIndex = head->packageIndex;

    if ((head->dataSize + sizeof(UartHead)) > MAX_UART_SIZE_IOBUF * maxBufFactor) {
        WRITE_LOG(LOG_FATAL, "%s dataSize too larger:%d", __FUNCTION__, head->dataSize);
        return ERR_BUF_OVERFLOW;
    }

    if ((head->option & PKG_OPTION_RESET)) {
        // The Host end program is restarted, but the UART cable is still connected
        WRITE_LOG(LOG_WARN, "%s host side want restart daemon, restart old sessionId:%u",
                  __FUNCTION__, head->sessionId);
        ResetOldSession(head->sessionId);
        return ERR_IO_SOFT_RESET;
    }

    if ((head->option & PKG_OPTION_FREE)) {
        // other side tell us the session need reset
        // we should free it
        WRITE_LOG(LOG_WARN, "%s other side tell us the session need free:%u", __FUNCTION__,
                  head->sessionId);
        Restartession(GetSession(head->sessionId));
    }

    // check data
    if (data.size() >= packetSize) {
        // if we have full package now ?
        if (!head->ValidateData()) {
            WRITE_LOG(LOG_FATAL, "%s data checksum not correct", __FUNCTION__);
            return ERR_BUF_CHECK;
        }
        if (head->IsResponsePackage()) {
            // response package
            ProcessResponsePackage(*head);
        } else {
            // link layer response for no response package
            ResponseUartTrans(head->sessionId, head->packageIndex, PKG_OPTION_ACK);
        }
    }

    return RET_SUCCESS;
}

void HdcUARTBase::ResponseUartTrans(uint32_t sessionId, uint32_t packageIndex,
                                    UartProtocolOption option)
{
    UartHead uartHeader(sessionId, option, 0, packageIndex);
    WRITE_LOG(LOG_DEBUG, "%s option:%u", __FUNCTION__, option);
    RequestSendPackage(reinterpret_cast<uint8_t *>(&uartHeader), sizeof(UartHead), false);
}

int HdcUARTBase::SendUARTData(HSession hSession, uint8_t *data, const size_t length)
{
    constexpr int maxIOSize = MAX_UART_SIZE_IOBUF;
    WRITE_LOG(LOG_DEBUG, "SendUARTData hSession:%u, total length:%d", hSession->sessionId, length);
    const int packageDataMaxSize = maxIOSize - sizeof(UartHead);
    size_t offset = 0;
    uint8_t sendDataBuf[MAX_UART_SIZE_IOBUF];

    WRITE_LOG(LOG_ALL, "SendUARTData data length :%d", length);

    do {
        UartHead *head = (UartHead *)sendDataBuf;
        if (memset_s(head, sizeof(UartHead), 0, sizeof(UartHead)) != EOK) {
            return ERR_BUF_RESET;
        }
        if (memcpy_s(head->flag, sizeof(head->flag), PACKET_FLAG.c_str(), PACKET_FLAG.size()) !=
            EOK) {
            return ERR_BUF_COPY;
        }
        head->sessionId = hSession->sessionId;
        head->packageIndex = ++hSession->hUART->packageIndex;

        int RemainingDataSize = length - offset;
        if (RemainingDataSize > packageDataMaxSize) {
            // more than one package max data size
            head->dataSize = static_cast<uint16_t>(packageDataMaxSize);
        } else {
            // less then the max size
            head->dataSize = static_cast<uint16_t>(RemainingDataSize);
            // this is the last package . all the data will send after this time
            head->option = head->option | PKG_OPTION_TAIL;
        }
#ifdef UART_FULL_LOG
        WRITE_LOG(LOG_FULL, "offset %d length %d", offset, length);
#endif
        uint8_t *payload = sendDataBuf + sizeof(UartHead);
        if (EOK !=
            memcpy_s(payload, packageDataMaxSize, (uint8_t *)data + offset, head->dataSize)) {
            WRITE_LOG(LOG_FATAL, "memcpy_s failed max %zu , need %zu",
                      packageDataMaxSize, head->dataSize);
            return ERR_BUF_COPY;
        }
        offset += head->dataSize;
        int packageFullSize = sizeof(UartHead) + head->dataSize;
        WRITE_LOG(LOG_ALL, "SendUARTData =============> %s", head->ToDebugString().c_str());
        RequestSendPackage(sendDataBuf, packageFullSize);
    } while (offset != length);

    return offset;
}

void HdcUARTBase::ReadDataFromUARTStream(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    HSession hSession = (HSession)stream->data;
    HdcUARTBase *hUARTBase = (HdcUARTBase *)hSession->classModule;
    std::lock_guard<std::mutex> lock(hUARTBase->workThreadProcessingData);

    constexpr int bufSize = 1024;
    char buffer[bufSize] = { 0 };
    if (nread < 0) {
        uv_err_name_r(nread, buffer, bufSize);
    }
    WRITE_LOG(LOG_DEBUG, "%s sessionId:%u, nread:%zd %s streamSize %zu", __FUNCTION__,
              hSession->sessionId, nread, buffer,
              hSession->hUART->streamSize.load());
    HdcSessionBase *hSessionBase = (HdcSessionBase *)hSession->classInstance;
    if (nread <= 0 or nread > signed(hSession->hUART->streamSize)) {
        WRITE_LOG(LOG_FATAL, "%s nothing need to do ! because no data here", __FUNCTION__);
        return;
    }
    if (hSessionBase->FetchIOBuf(hSession, hSession->ioBuf, nread) < 0) {
        WRITE_LOG(LOG_FATAL, "%s FetchIOBuf failed , free the other side session", __FUNCTION__);
        // seesion side said the dont understand this seesion data
        // so we also need tell other side to free it session.
        hUARTBase->ResponseUartTrans(hSession->sessionId, ++hSession->hUART->packageIndex,
                                     PKG_OPTION_FREE);

        WRITE_LOG(LOG_FATAL, "%s FetchIOBuf failed , free the session", __FUNCTION__);
        hSessionBase->FreeSession(hSession->sessionId);
    }
    hSession->hUART->streamSize -= nread;
    WRITE_LOG(LOG_DEBUG, "%s sessionId:%u, nread:%d", __FUNCTION__, hSession->sessionId, nread);
}

bool HdcUARTBase::ReadyForWorkThread(HSession hSession)
{
    if (externInterface.UvTcpInit(&hSession->childLoop, &hSession->dataPipe[STREAM_WORK],
                                  hSession->dataFd[STREAM_WORK])) {
        WRITE_LOG(LOG_FATAL, "%s init child TCP failed", __FUNCTION__);
        return false;
    }
    hSession->dataPipe[STREAM_WORK].data = hSession;
    HdcSessionBase *pSession = (HdcSessionBase *)hSession->classInstance;
    externInterface.SetTcpOptions(&hSession->dataPipe[STREAM_WORK]);
    if (externInterface.UvRead((uv_stream_t *)&hSession->dataPipe[STREAM_WORK],
                               pSession->AllocCallback, &HdcUARTBase::ReadDataFromUARTStream)) {
        WRITE_LOG(LOG_FATAL, "%s child TCP read failed", __FUNCTION__);
        return false;
    }
    WRITE_LOG(LOG_DEBUG, "%s finish", __FUNCTION__);
    return true;
}

void HdcUARTBase::Restartession(const HSession session)
{
    if (session != nullptr) {
        WRITE_LOG(LOG_FATAL, "%s:%s", __FUNCTION__, session->ToDebugString().c_str());
        ClearUARTOutMap(session->sessionId);
        sessionBase.FreeSession(session->sessionId);
    }
}

void HdcUARTBase::StopSession(HSession hSession)
{
    if (hSession != nullptr) {
        WRITE_LOG(LOG_WARN, "%s:%s", __FUNCTION__, hSession->ToDebugString().c_str());
        ClearUARTOutMap(hSession->sessionId);
    } else {
        WRITE_LOG(LOG_FATAL, "%s: clean null session", __FUNCTION__);
    }
}

void HdcUARTBase::TransferStateMachine::Wait()
{
    std::unique_lock<std::mutex> lock(mutex);
    WRITE_LOG(LOG_ALL, "%s", __FUNCTION__);
    if (timeout) {
        auto waitTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            timeoutPoint - std::chrono::steady_clock::now());
        WRITE_LOG(LOG_ALL, "wait timeout %lld", waitTimeout.count());
        if (cv.wait_for(lock, waitTimeout, [=] { return requested; }) == false) {
            // must wait one timeout
            // because sometime maybe not timeout but we got a request first.
            timeout = false;
            WRITE_LOG(LOG_ALL, "timeout");
        }
    } else {
        cv.wait(lock, [=] { return requested; });
    }
    requested = false;
}

HdcUART::HdcUART()
{
#ifdef _WIN32
    Base::ZeroStruct(ovWrite);
    ovWrite.hEvent = CreateEvent(NULL, false, false, NULL);
    Base::ZeroStruct(ovRead);
    ovRead.hEvent = CreateEvent(NULL, false, false, NULL);
#endif
}

HdcUART::~HdcUART()
{
#ifdef _WIN32
    CloseHandle(ovWrite.hEvent);
    ovWrite.hEvent = NULL;
    CloseHandle(ovRead.hEvent);
    ovRead.hEvent = NULL;
#endif
}
} // namespace Hdc
