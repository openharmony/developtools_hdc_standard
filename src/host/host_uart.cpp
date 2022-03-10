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
#include "host_uart.h"

#include <mutex>
#include <thread>

#include "server.h"

using namespace std::chrono_literals;
namespace Hdc {
HdcHostUART::HdcHostUART(HdcServer &serverIn, ExternInterface &externInterface)
    : HdcUARTBase(serverIn, externInterface), server(serverIn)
{
    uv_timer_init(&server.loopMain, &devUartWatcher);
}

HdcHostUART::~HdcHostUART()
{
    Stop();
}

int HdcHostUART::Initial()
{
    uartOpened = false; // modRunning
    return StartupUARTWork();
}

bool HdcHostUART::NeedStop(const HSessionPtr hSessionPtr)
{
    return (!uartOpened or (hSessionPtr->isDead and hSessionPtr->ref == 0));
}

bool HdcHostUART::IsDeviceOpened(const HdcUART &uart)
{
    // review why not use uartOpened?
#ifdef HOST_MINGW
    return uart.devUartHandle != INVALID_HANDLE_VALUE;
#else
    return uart.devUartHandle >= 0;
#endif
}

void HdcHostUART::UartWriteThread()
{
    // this thread don't care session.
    while (true) {
        WRITE_LOG(LOG_DEBUG, "%s wait sendLock.", __FUNCTION__);
        transfer.Wait();
        // it almost in wait , so we check stop after wait.
        if (stopped) {
            break;
        }
        SendPkgInUARTOutMap();
    }
    WRITE_LOG(LOG_INFO, "Leave %s", __FUNCTION__);
    return;
}

void HdcHostUART::UartReadThread(HSessionPtr hSessionPtr)
{
    HUARTPtr hUART = hSessionPtr->hUART;
    vector<uint8_t> dataReadBuf; // each thread/session have it own data buff
    // If something unexpected happens , max buffer size we allow
    WRITE_LOG(LOG_DEBUG, "%s devUartHandle:%d", __FUNCTION__, hUART->devUartHandle);
    size_t expectedSize = 0;
    while (dataReadBuf.size() < MAX_READ_BUFFER) {
        if (NeedStop(hSessionPtr)) {
            WRITE_LOG(LOG_FATAL, "%s stop ", __FUNCTION__);
            break;
        }
        ssize_t bytesRead = ReadUartDev(dataReadBuf, expectedSize, *hUART);
        if (bytesRead < 0) {
            WRITE_LOG(LOG_INFO, "%s read got fail , free the session", __FUNCTION__);
            OnTransferError(hSessionPtr);
        } else if (bytesRead == 0) {
            WRITE_LOG(LOG_DEBUG, "%s read %zd, clean the data try read again.", __FUNCTION__,
                      bytesRead);
            // drop current cache
            expectedSize = 0;
            dataReadBuf.clear();
            continue;
        }

        WRITE_LOG(LOG_DEBUG, "%s bytesRead:%d, dataReadBuf.size():%d.", __FUNCTION__, bytesRead,
                  dataReadBuf.size());

        if (dataReadBuf.size() < sizeof(UartHead)) {
            continue; // no enough ,read again
        }
        WRITE_LOG(LOG_DEBUG, "%s PackageProcess dataReadBuf.size():%d.", __FUNCTION__,
                  dataReadBuf.size());
        expectedSize = PackageProcess(dataReadBuf, hSessionPtr);
    }
    WRITE_LOG(LOG_INFO, "Leave %s", __FUNCTION__);
    return;
}

// review why not use QueryDosDevice ?
bool HdcHostUART::EnumSerialPort(bool &portChange)
{
    std::vector<string> newPortInfo;
    serialPortRemoved.clear();
    bool bRet = true;

#ifdef HOST_MINGW
    constexpr int MAX_KEY_LENGTH = 255;
    constexpr int MAX_VALUE_NAME = 16383;
    HKEY hKey;
    TCHAR achValue[MAX_VALUE_NAME];    // buffer for subkey name
    DWORD cchValue = MAX_VALUE_NAME;   // size of name string
    TCHAR achClass[MAX_PATH] = _T(""); // buffer for class name
    DWORD cchClassName = MAX_PATH;     // size of class string
    DWORD cSubKeys = 0;                // number of subkeys
    DWORD cbMaxSubKey;                 // longest subkey size
    DWORD cchMaxClass;                 // longest class string
    DWORD cKeyNum;                     // number of values for key
    DWORD cchMaxValue;                 // longest value name
    DWORD cbMaxValueData;              // longest value data
    DWORD cbSecurityDescriptor;        // size of security descriptor
    FILETIME ftLastWriteTime;          // last write time
    LSTATUS iRet = -1;
    std::string port;
    TCHAR strDSName[MAX_VALUE_NAME];
    if (memset_s(strDSName, sizeof(TCHAR) * MAX_VALUE_NAME, 0, sizeof(TCHAR) * MAX_VALUE_NAME) !=
        EOK) {
        return false;
    }
    DWORD nValueType = 0;
    DWORD nBuffLen = 10;
    if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("HARDWARE\\DEVICEMAP\\SERIALCOMM"), 0,
                                      KEY_READ, &hKey)) {
        // Get the class name and the value count.
        iRet = RegQueryInfoKey(hKey, achClass, &cchClassName, NULL, &cSubKeys, &cbMaxSubKey,
                               &cchMaxClass, &cKeyNum, &cchMaxValue, &cbMaxValueData,
                               &cbSecurityDescriptor, &ftLastWriteTime);
        // Enumerate the key values.
        if (ERROR_SUCCESS == iRet) {
            for (DWORD i = 0; i < cKeyNum; i++) {
                cchValue = MAX_VALUE_NAME;
                achValue[0] = '\0';
                nBuffLen = MAX_KEY_LENGTH;
                if (ERROR_SUCCESS == RegEnumValue(hKey, i, achValue, &cchValue, NULL, NULL,
                                                  (LPBYTE)strDSName, &nBuffLen)) {
#ifdef UNICODE
                    strPortName = WstringToString(strDSName);
#else
                    port = std::string(strDSName);
#endif
                    newPortInfo.push_back(port);
                    auto it = std::find(serialPortInfo.begin(), serialPortInfo.end(), port);
                    if (it == serialPortInfo.end()) {
                        portChange = true;
                        WRITE_LOG(LOG_DEBUG, "%s:new port %s", __FUNCTION__, port.c_str());
                    }
                } else {
                    bRet = false;
                    WRITE_LOG(LOG_DEBUG, "%s RegEnumValue fail. %d", __FUNCTION__, GetLastError());
                }
            }
        } else {
            bRet = false;
            WRITE_LOG(LOG_DEBUG, "%s RegQueryInfoKey failed %d", __FUNCTION__, GetLastError());
        }
    } else {
        bRet = false;
        WRITE_LOG(LOG_DEBUG, "%s RegOpenKeyEx fail %d", __FUNCTION__, GetLastError());
    }
    RegCloseKey(hKey);
#endif
#ifdef HOST_LINUX
    DIR *dir = opendir("/dev");
    dirent *p = NULL;
    while ((p = readdir(dir)) != NULL) {
        if (p->d_name[0] != '.' && string(p->d_name).find("tty") != std::string::npos) {
            string port = "/dev/" + string(p->d_name);
            if (port.find("/dev/ttyUSB") == 0 || port.find("/dev/ttySerial") == 0) {
                newPortInfo.push_back(port);
                auto it = std::find(serialPortInfo.begin(), serialPortInfo.end(), port);
                if (it == serialPortInfo.end()) {
                    portChange = true;
                    WRITE_LOG(LOG_DEBUG, "new port:%s", port.c_str());
                }
            }
        }
    }
    closedir(dir);
#endif
    for (auto &oldPort : serialPortInfo) {
        auto it = std::find(newPortInfo.begin(), newPortInfo.end(), oldPort);
        if (it == newPortInfo.end()) {
            // not found in new port list
            // we need remove the connect info
            serialPortRemoved.emplace_back(oldPort);
        }
    }

    if (!portChange) {
        // new scan empty , same as port changed
        if (serialPortInfo.size() != newPortInfo.size()) {
            portChange = true;
        }
    }
    if (portChange) {
        serialPortInfo.swap(newPortInfo);
    }
    return bRet;
}

#ifdef HOST_MINGW
std::string WstringToString(const std::wstring &wstr)
{
    if (wstr.empty()) {
        return std::string();
    }
    int size = WideCharToMultiByte(CP_ACP, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string ret = std::string(size, 0);
    WideCharToMultiByte(CP_ACP, 0, &wstr[0], (int)wstr.size(), &ret[0], size, NULL,
                        NULL); // CP_UTF8
    return ret;
}

// review reanme for same func from linux
int HdcHostUART::WinSetSerial(HUARTPtr hUART, string serialPort, int byteSize, int eqBaudRate)
{
    int winRet = RET_SUCCESS;
    COMMTIMEOUTS timeouts;
    GetCommTimeouts(hUART->devUartHandle, &timeouts);
    int interTimeout = 5;
    timeouts.ReadIntervalTimeout = interTimeout;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(hUART->devUartHandle, &timeouts);
    constexpr int max = DEFAULT_BAUD_RATE_VALUE / 8 * 2; // 2 second buffer size
    do {
        if (!SetupComm(hUART->devUartHandle, max, max)) {
            WRITE_LOG(LOG_WARN, "SetupComm %s fail, err:%d.", serialPort.c_str(), GetLastError());
            winRet = ERR_GENERIC;
            break;
        }
        DCB dcb;
        if (!GetCommState(hUART->devUartHandle, &dcb)) {
            WRITE_LOG(LOG_WARN, "GetCommState %s fail, err:%d.", serialPort.c_str(),
                      GetLastError());
            winRet = ERR_GENERIC;
        }
        dcb.DCBlength = sizeof(DCB);
        dcb.BaudRate = eqBaudRate;
        dcb.Parity = 0;
        dcb.ByteSize = byteSize;
        dcb.StopBits = ONESTOPBIT;
        if (!SetCommState(hUART->devUartHandle, &dcb)) {
            WRITE_LOG(LOG_WARN, "SetCommState %s fail, err:%d.", serialPort.c_str(),
                      GetLastError());
            winRet = ERR_GENERIC;
            break;
        }
        if (!PurgeComm(hUART->devUartHandle,
                       PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT)) {
            WRITE_LOG(LOG_WARN, "PurgeComm  %s fail, err:%d.", serialPort.c_str(), GetLastError());
            winRet = ERR_GENERIC;
            break;
        }
        DWORD dwError;
        COMSTAT cs;
        if (!ClearCommError(hUART->devUartHandle, &dwError, &cs)) {
            WRITE_LOG(LOG_WARN, "ClearCommError %s fail, err:%d.", serialPort.c_str(),
                      GetLastError());
            winRet = ERR_GENERIC;
            break;
        }
    } while (false);
    if (winRet != RET_SUCCESS) {
        CloseSerialPort(hUART);
    }
    return winRet;
}
#endif // HOST_MINGW

bool HdcHostUART::WaitUartIdle(HdcUART &uart, bool retry)
{
    std::vector<uint8_t> readBuf;
    WRITE_LOG(LOG_DEBUG, "%s clear read", __FUNCTION__);
    ssize_t ret = ReadUartDev(readBuf, 1, uart);
    if (ret == 0) {
        WRITE_LOG(LOG_DEBUG, "%s port read timeout", __FUNCTION__);
        return true;
    } else {
        WRITE_LOG(LOG_WARN, "%s port read something %zd", __FUNCTION__, ret);
        if (retry) {
            // we will read again , but only retry one time
            return WaitUartIdle(uart, false);
        } else {
            return false;
        }
    }
    return false;
}

int HdcHostUART::OpenSerialPort(const std::string &connectKey)
{
    HdcUART uart;
    std::string portName;
    uint32_t baudRate;
    static int ret = 0;

    if (memset_s(&uart, sizeof(HdcUART), 0, sizeof(HdcUART)) != EOK) {
        return -1;
    }

    if (!GetPortFromKey(connectKey, portName, baudRate)) {
        WRITE_LOG(LOG_ALL, "%s unknow format %s", __FUNCTION__, connectKey.c_str());
        return -1;
    }
    do {
        ret = 0;
        WRITE_LOG(LOG_ALL, "%s try to open %s with rate %u", __FUNCTION__, portName.c_str(),
                  baudRate);

#ifdef HOST_MINGW
        // review change to wstring ?
        TCHAR apiBuf[PORT_NAME_LEN * numTmp];
#ifdef UNICODE
        _stprintf_s(apiBuf, MAX_PATH, _T("%S"), port.c_str());
#else
        _stprintf_s(apiBuf, MAX_PATH, _T("%s"), portName.c_str());
#endif
        DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
        uart.devUartHandle = CreateFile(apiBuf, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                        OPEN_EXISTING, dwFlagsAndAttributes, NULL);
        if (uart.devUartHandle == INVALID_HANDLE_VALUE) {
            ret = ERR_GENERIC;
            WRITE_LOG(LOG_DEBUG, "%s CreateFile %s err:%d.", __FUNCTION__, portName.c_str(),
                      GetLastError());
            break; // review for onethan one uart , here we need change to continue?
        } else {
            uart.serialPort = portName;
        }
        ret = WinSetSerial(&uart, uart.serialPort, UART_BIT2, baudRate);
        if (ret != RET_SUCCESS) {
            WRITE_LOG(LOG_WARN, "%s WinSetSerial:%s fail.", __FUNCTION__, uart.serialPort.c_str());
            break;
        }
#endif

#if defined HOST_LINUX
        string uartName = Base::CanonicalizeSpecPath(portName);
        uart.devUartHandle = open(uartName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (uart.devUartHandle < 0) {
            constexpr int bufSize = 1024;
            char buf[bufSize] = { 0 };
            strerror_r(errno, buf, bufSize);
            WRITE_LOG(LOG_WARN, "Linux open serial port faild,serialPort:%s, Message : %s",
                      uart.serialPort.c_str(), buf);
            ret = ERR_GENERIC;
            break;
        }
        {
            uart.serialPort = portName;
        }
        SetSerial(uart.devUartHandle, baudRate, UART_BIT2, 'N', 1);
#endif
        // if the dev is idle
        if (!WaitUartIdle(uart)) {
            ret = ERR_GENERIC;
            WRITE_LOG(LOG_INFO, "This is not a Idle UART port", uart.serialPort.c_str());
            break;
        }
        if (!ConnectMyNeed(&uart, connectKey)) {
            WRITE_LOG(LOG_WARN, "ConnectMyNeed failed");
            ret = ERR_GENERIC;
            break;
        } else {
            uartOpened = true;
            WRITE_LOG(LOG_INFO,
                      "Serial Open Successfully! uart.serialPort:%s "
                      "devUartHandle:%d",
                      uart.serialPort.c_str(), uart.devUartHandle);
        }
        break;
    } while (false);
    if (ret != RET_SUCCESS) {
        CloseSerialPort(&uart);
    }
    return ret;
}

void HdcHostUART::UpdateUARTDaemonInfo(const std::string &connectKey, HSessionPtr hSessionPtr,
                                       ConnStatus connStatus)
{
    // add to list
    HdcDaemonInformation diNew;
    HDaemonInfoPtr diNewPtr = &diNew;
    diNew.connectKey = connectKey;
    diNew.connType = CONN_SERIAL;
    diNew.connStatus = connStatus;
    diNew.hSessionPtr = hSessionPtr;
    WRITE_LOG(LOG_DEBUG, "%s uart connectKey :%s session %s change to %d", __FUNCTION__,
              connectKey.c_str(),
              hSessionPtr == nullptr ? "<null>" : hSessionPtr->ToDebugString().c_str(), connStatus);
    if (connStatus == STATUS_UNKNOW) {
        server.AdminDaemonMap(OP_REMOVE, connectKey, diNewPtr);
        if (hSessionPtr != nullptr and hSessionPtr->hUART != nullptr) {
            connectedPorts.erase(hSessionPtr->hUART->serialPort);
        }
    } else {
        if (connStatus == STATUS_CONNECTED) {
            if (hSessionPtr != nullptr and hSessionPtr->hUART != nullptr) {
                connectedPorts.emplace(hSessionPtr->hUART->serialPort);
            }
        }
        HDaemonInfoPtr diOldPtr = nullptr;
        server.AdminDaemonMap(OP_QUERY, connectKey, diOldPtr);
        if (diOldPtr == nullptr) {
            WRITE_LOG(LOG_DEBUG, "%s add new di", __FUNCTION__);
            server.AdminDaemonMap(OP_ADD, connectKey, diNewPtr);
        } else {
            server.AdminDaemonMap(OP_UPDATE, connectKey, diNewPtr);
        }
    }
}

bool HdcHostUART::StartUartReadThread(HSessionPtr hSessionPtr)
{
    try {
        HUARTPtr hUART = hSessionPtr->hUART;
        hUART->readThread = std::thread(&HdcHostUART::UartReadThread, this, hSessionPtr);
    } catch (...) {
        server.FreeSession(hSessionPtr->sessionId);
        UpdateUARTDaemonInfo(hSessionPtr->connectKey, hSessionPtr, STATUS_UNKNOW);
        WRITE_LOG(LOG_WARN, "%s failed err", __FUNCTION__);
        return false;
    }

    WRITE_LOG(LOG_INFO, "%s success.", __FUNCTION__);
    return true;
}

bool HdcHostUART::StartUartSendThread()
{
    WRITE_LOG(LOG_DEBUG, "%s.", __FUNCTION__);
    try {
        sendThread = std::thread(&HdcHostUART::UartWriteThread, this);
    } catch (...) {
        WRITE_LOG(LOG_WARN, "%s sendThread create failed", __FUNCTION__);
        return false;
    }

    WRITE_LOG(LOG_INFO, "%s success.", __FUNCTION__);
    return true;
}

// Determines that daemonInfo must have the device
HSessionPtr HdcHostUART::ConnectDaemonByUart(const HSessionPtr hSessionPtr, const HDaemonInfoPtr)
{
    if (!uartOpened) {
        WRITE_LOG(LOG_DEBUG, "%s non uart opened.", __FUNCTION__);
        return nullptr;
    }
    HUARTPtr hUART = hSessionPtr->hUART;
    UpdateUARTDaemonInfo(hSessionPtr->connectKey, hSessionPtr, STATUS_READY);
    WRITE_LOG(LOG_DEBUG, "%s :%s", __FUNCTION__, hUART->serialPort.c_str());
    if (!StartUartReadThread(hSessionPtr)) {
        WRITE_LOG(LOG_DEBUG, "%s StartUartReadThread fail.", __FUNCTION__);
        return nullptr;
    }

    externInterface.StartWorkThread(&server.loopMain, server.SessionWorkThread,
                                    Base::FinishWorkThread, hSessionPtr);
    // wait for thread up
    while (hSessionPtr->childLoop.active_handles == 0) {
        uv_sleep(1);
    }
    auto ctrl = server.BuildCtrlString(SP_START_SESSION, 0, nullptr, 0);
    externInterface.SendToStream((uv_stream_t *)&hSessionPtr->ctrlPipe[STREAM_MAIN], ctrl.data(),
                                 ctrl.size());
    return hSessionPtr;
}

RetErrCode HdcHostUART::StartupUARTWork()
{
    WRITE_LOG(LOG_DEBUG, "%s", __FUNCTION__);
    devUartWatcher.data = this;
    constexpr int interval = 3000;
    constexpr int delay = 1000;
    if (externInterface.UvTimerStart(&devUartWatcher, UvWatchUartDevPlugin, delay, interval) != 0) {
        WRITE_LOG(LOG_FATAL, "devUartWatcher start fail");
        return ERR_GENERIC;
    }
    if (!StartUartSendThread()) {
        WRITE_LOG(LOG_DEBUG, "%s StartUartSendThread fail.", __FUNCTION__);
        return ERR_GENERIC;
    }
    return RET_SUCCESS;
}

HSessionPtr HdcHostUART::ConnectDaemon(const std::string &connectKey)
{
    WRITE_LOG(LOG_DEBUG, "%s", __FUNCTION__);
    OpenSerialPort(connectKey);
    return nullptr;
}

/*
This function does the following:
1. Existing serial device, check whether a session is established, if not, go to establish
2. The connection is established but the serial device does not exist, delete the session
*/
void HdcHostUART::WatchUartDevPlugin()
{
    std::lock_guard<std::mutex> lock(semUartDevCheck);
    bool portChange = false;

    if (!EnumSerialPort(portChange)) {
        WRITE_LOG(LOG_WARN, "%s enumDetailsSerialPorts fail.", __FUNCTION__);
        portChange = false;
    } else if (portChange) {
        for (const auto &port : serialPortInfo) {
            WRITE_LOG(LOG_INFO, "%s found uart port :%s", __FUNCTION__, port.c_str());
            // check port have session
            HDaemonInfoPtr hdi = nullptr;
            server.AdminDaemonMap(OP_QUERY, port, hdi);
            if (hdi == nullptr and connectedPorts.find(port) == connectedPorts.end()) {
                UpdateUARTDaemonInfo(port, nullptr, STATUS_READY);
            }
        }
        for (const auto &port : serialPortRemoved) {
            WRITE_LOG(LOG_INFO, "%s remove uart port :%s", __FUNCTION__, port.c_str());
            // check port have session
            HDaemonInfoPtr hdi = nullptr;
            server.AdminDaemonMap(OP_QUERY, port, hdi);
            if (hdi != nullptr and hdi->hSessionPtr == nullptr) {
                // we only remove the empty port
                UpdateUARTDaemonInfo(port, nullptr, STATUS_UNKNOW);
            }
        }
    }
}

bool HdcHostUART::ConnectMyNeed(HUARTPtr hUART, std::string connectKey)
{
    // we never use port to connect, we use connect key
    if (connectKey.empty()) {
        connectKey = hUART->serialPort;
    }
    if (connectKey != hUART->serialPort) {
        UpdateUARTDaemonInfo(hUART->serialPort, nullptr, STATUS_UNKNOW);
    }
    UpdateUARTDaemonInfo(connectKey, nullptr, STATUS_READY);

    HSessionPtr hSessionPtr = server.MallocSession(true, CONN_SERIAL, this);
    hSessionPtr->connectKey = connectKey;
#if defined(HOST_LINUX)
    hSessionPtr->hUART->devUartHandle = hUART->devUartHandle;
#elif defined(HOST_MINGW)
    hSessionPtr->hUART->devUartHandle = hUART->devUartHandle;
#endif

    hSessionPtr->hUART->serialPort = hUART->serialPort;
    WRITE_LOG(LOG_DEBUG, "%s connectkey:%s,port:%s", __FUNCTION__, hSessionPtr->connectKey.c_str(),
              hUART->serialPort.c_str());
    uv_timer_t *waitTimeDoCmd = new(std::nothrow) uv_timer_t;
    if (waitTimeDoCmd == nullptr) {
        WRITE_LOG(LOG_FATAL, "ConnectMyNeed new waitTimeDoCmd failed");
        return false;
    }
    uv_timer_init(&server.loopMain, waitTimeDoCmd);
    waitTimeDoCmd->data = hSessionPtr;
    if (externInterface.UvTimerStart(waitTimeDoCmd, server.UartPreConnect, UV_TIMEOUT, UV_REPEAT) !=
        RET_SUCCESS) {
        WRITE_LOG(LOG_DEBUG, "%s for %s:%s fail.", __FUNCTION__, hSessionPtr->connectKey.c_str(),
                  hUART->serialPort.c_str());
        return false;
    }
    WRITE_LOG(LOG_DEBUG, "%s %s register a session", __FUNCTION__, hUART->serialPort.c_str());

    return true;
}

void HdcHostUART::KickoutZombie(HSessionPtr hSessionPtr)
{
    if (hSessionPtr == nullptr or hSessionPtr->hUART == nullptr or hSessionPtr->isDead) {
        return;
    }
#ifdef _WIN32
    if (hSessionPtr->hUART->devUartHandle == INVALID_HANDLE_VALUE) {
        return;
    }
#else
    if (hSessionPtr->hUART->devUartHandle < 0) {
        return;
    }
#endif
    WRITE_LOG(LOG_DEBUG, "%s FreeSession %s", __FUNCTION__, hSessionPtr->ToDebugString().c_str());
    server.FreeSession(hSessionPtr->sessionId);
}

HSessionPtr HdcHostUART::GetSession(const uint32_t sessionId, bool)
{
    return server.AdminSession(OP_QUERY, sessionId, nullptr);
}
void HdcHostUART::CloseSerialPort(const HUARTPtr hUART)
{
    WRITE_LOG(LOG_DEBUG, "try to close dev handle %d", __FUNCTION__, hUART->devUartHandle);

#ifdef _WIN32
    if (hUART->devUartHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(hUART->devUartHandle);
        hUART->devUartHandle = INVALID_HANDLE_VALUE;
    }
#else
    if (hUART->devUartHandle != -1) {
        close(hUART->devUartHandle);
        hUART->devUartHandle = -1;
    }
#endif
}

void HdcHostUART::OnTransferError(const HSessionPtr session)
{
    if (session != nullptr) {
        WRITE_LOG(LOG_FATAL, "%s:%s", __FUNCTION__, session->ToDebugString().c_str());
        if (session->hUART != nullptr) {
            if (IsDeviceOpened(*session->hUART)) {
                // same device dont echo twice to client
                string echoStr = "ERR: uart link layer transmission error.\n";
                server.EchoToClientsForSession(session->sessionId, echoStr);
            }
            // 1. dev opened by other application
            // 2. dev is plug out
            // 3. dev line is broken ?
            // we set the status to empty
            // watcher will reopen it if it can find this again
            CloseSerialPort(session->hUART);
            UpdateUARTDaemonInfo(session->connectKey, session, STATUS_OFFLINE);
        }

        server.FreeSession(session->sessionId);
        ClearUARTOutMap(session->sessionId);
    }
}

// review what about merge Restartession with OnTransferError ?
void HdcHostUART::Restartession(const HSessionPtr session)
{
    HdcUARTBase::Restartession(session);
    // allow timer watcher make a new session.
    if (session != nullptr and session->hUART != nullptr) {
        WRITE_LOG(LOG_FATAL, "%s reset serialPort:%s", __FUNCTION__,
                  session->hUART->serialPort.c_str());
        CloseSerialPort(session->hUART); // huart will free , so we must clost it here
        server.EchoToClientsForSession(session->sessionId,
                                       "uart link relased by daemon. need connect again.");
    }
}

void HdcHostUART::StopSession(HSessionPtr hSessionPtr)
{
    if (hSessionPtr == nullptr) {
        WRITE_LOG(LOG_FATAL, "%s hSessionPtr is null", __FUNCTION__);
        return;
    }
    WRITE_LOG(LOG_DEBUG, "%s hSessionPtr %s will be stop and free", __FUNCTION__,
              hSessionPtr->ToDebugString().c_str());
    HUARTPtr hUART = hSessionPtr->hUART;
    if (hUART == nullptr) {
        WRITE_LOG(LOG_FATAL, "%s hUART is null", __FUNCTION__);
    } else {
#ifdef _WIN32
        CancelIoEx(hUART->devUartHandle, NULL);
#endif
        // we make select always have a timeout in linux
        // also we make a mark here
        // ReadUartDev will return for this flag
        hUART->ioCancel = true;

        if (hUART->readThread.joinable()) {
            WRITE_LOG(LOG_DEBUG, "wait readThread Stop");
            hUART->readThread.join();
        } else {
            WRITE_LOG(LOG_FATAL, "readThread is not joinable");
        }
    }

    // call the base side
    HdcUARTBase::StopSession(hSessionPtr);
}

std::vector<std::string> HdcHostUART::StringSplit(std::string source, std::string split)
{
    std::vector<std::string> result;

    // find
    if (!split.empty()) {
        size_t pos = 0;
        while ((pos = source.find(split)) != std::string::npos) {
            // split
            std::string token = source.substr(0, pos);
            if (!token.empty()) {
                result.push_back(token);
            }
            source.erase(0, pos + split.length());
        }
    }
    // add last token
    if (!source.empty()) {
        result.push_back(source);
    }
    return result;
}

bool HdcHostUART::GetPortFromKey(const std::string &connectKey, std::string &portName,
                                 uint32_t &baudRate)
{
    // we support UART_NAME:UART_RATE format
    // like COM5:115200
    constexpr size_t TWO_ARGS = 2;
    std::vector<std::string> result = StringSplit(connectKey, ",");
    if (result.size() == TWO_ARGS) {
        portName = result[0];
        try {
            baudRate = static_cast<uint32_t>(std::stoul(result[1]));
        } catch (...) {
            return false;
        }
        return true;
    } else if (result.size() == 1) {
        portName = result[0];
        baudRate = DEFAULT_BAUD_RATE_VALUE;
        return true;
    } else {
        return false;
    }
}

void HdcHostUART::SendUartSoftReset(HSessionPtr hSessionPtr, uint32_t sessionId)
{
    UartHead resetPackage(sessionId, PKG_OPTION_RESET);
    resetPackage.dataSize = sizeof(UartHead);
    RequestSendPackage(reinterpret_cast<uint8_t *>(&resetPackage), sizeof(UartHead), false);
}

void HdcHostUART::Stop()
{
    WRITE_LOG(LOG_DEBUG, "%s Stop!", __FUNCTION__);
    if (!stopped) {
        externInterface.TryCloseHandle((uv_handle_t *)&devUartWatcher);
        uartOpened = false;
        stopped = true;
        // just click it for exit
        NotifyTransfer();
        if (sendThread.joinable()) {
            WRITE_LOG(LOG_DEBUG, "%s wait sendThread Stop!", __FUNCTION__);
            sendThread.join();
        } else {
            WRITE_LOG(LOG_FATAL, "%s sendThread is not joinable", __FUNCTION__);
        }
    }
}
} // namespace Hdc
