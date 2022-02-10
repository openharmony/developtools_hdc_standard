/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "host_uart_test.h"

#include <chrono>
#include <random>
#include <thread>

#include "server.h"

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace testing::ext;
using namespace testing;

namespace Hdc {
class HdcHostUARTTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

    constexpr static int PAIR = 2;
    constexpr static int SENDER = 0;
    constexpr static int RECEIVER = 1;

    std::default_random_engine rnd;
    const std::string nullDevPath = "/dev/null";
    class MockHdcServer : public HdcServer {
        MockHdcServer() : HdcServer(true) {};
        MOCK_METHOD3(AdminDaemonMap, string(uint8_t, const string &, HDaemonInfo &));
        MOCK_METHOD1(EnumUARTDeviceRegister, void(UartKickoutZombie));
        MOCK_METHOD4(MallocSession, HSession(bool, const ConnType, void *, uint32_t));
        MOCK_METHOD1(FreeSession, void(const uint32_t));
        MOCK_METHOD3(AdminSession, HSession(const uint8_t, const uint32_t, HSession));
        MOCK_METHOD2(EchoToClientsForSession, void(uint32_t, const string &));
    } mockServer;

    class MockBaseInterface : public ExternInterface {
    public:
        MOCK_METHOD4(StartWorkThread, int(uv_loop_t *, uv_work_cb, uv_after_work_cb, void *));
        MOCK_METHOD2(TryCloseHandle, void(const uv_handle_t *, uv_close_cb));
        MOCK_METHOD3(TimerUvTask, bool(uv_loop_t *, void *, uv_timer_cb));
        MOCK_METHOD3(SendToStream, int(uv_stream_t *, const uint8_t *, const int));
        MOCK_METHOD6(DelayDo, bool(uv_loop_t *, const int, const uint8_t, string, void *, DelayCB));
        MOCK_METHOD4(UvTimerStart, bool(uv_timer_t *, uv_timer_cb, uint64_t, uint64_t));
    } mockInterface;

    class MockHdcHostUART : public HdcHostUART {
    public:
        explicit MockHdcHostUART(HdcServer &daemonIn,
                        ExternInterface &externInterface = HdcUARTBase::defaultInterface)
            : HdcHostUART(daemonIn, externInterface)
        {
        }
        MOCK_METHOD0(StartupUARTWork, RetErrCode());
        MOCK_METHOD0(UartWriteThread, void());
        MOCK_METHOD1(CloseSerialPort, void(const HUART));
        MOCK_METHOD1(EnumSerialPort, bool(bool &));
        MOCK_METHOD0(Stop, void());
        MOCK_METHOD3(UpdateUARTDaemonInfo, void(const std::string &, HSession, ConnStatus));
        MOCK_METHOD1(StartUartReadThread, bool(HSession));
        MOCK_METHOD0(StartUartSendThread, bool());
        MOCK_METHOD0(WatchUartDevPlugin, void());
        MOCK_METHOD1(OpenSerialPort, int(const std::string &));
        MOCK_METHOD1(UartReadThread, void(HSession));
        MOCK_METHOD3(SendUARTRaw, bool(HSession, uint8_t *, const size_t));
        MOCK_METHOD3(RequestSendPackage, void(uint8_t *, const size_t, bool));
        MOCK_METHOD1(UartSendThread, void(HSession));
        MOCK_METHOD0(SendPkgInUARTOutMap, void());
        MOCK_METHOD1(IsDeviceOpened, bool(const HdcUART &));
        MOCK_METHOD3(ReadUartDev, ssize_t(std::vector<uint8_t> &, size_t, HdcUART &));
        MOCK_METHOD1(NeedStop, bool(const HSession));
        MOCK_METHOD2(PackageProcess, size_t(vector<uint8_t> &, HSession hSession));
        MOCK_METHOD1(OnTransferError, void(const HSession));
        MOCK_METHOD1(ClearUARTOutMap, void(uint32_t));
    } mockHostUART;

    HdcSession mySession;
    HdcUART myUART;
    const int testSerialHandle = 0x1234;
    const std::string testSerialPortName = "COMX";

    HdcHostUARTTest() : mockHostUART(mockServer, mockInterface)
    {
        myUART.serialPort = testSerialPortName;
        myUART.devUartHandle = testSerialHandle;
        mySession.hUART = &myUART;
        mySession.classModule = &mockHostUART;
        mySession.classInstance = &mockServer;
    };
};

void HdcHostUARTTest::SetUpTestCase()
{
#ifdef UT_DEBUG
    Hdc::Base::SetLogLevel(LOG_ALL);
#else
    Hdc::Base::SetLogLevel(LOG_OFF);
#endif
}

void HdcHostUARTTest::TearDownTestCase() {}

void HdcHostUARTTest::SetUp()
{
    // The destructor will call these
    EXPECT_CALL(mockHostUART, Stop).Times(AnyNumber());
    EXPECT_CALL(mockInterface, TryCloseHandle).Times(AnyNumber());
}

void HdcHostUARTTest::TearDown()
{
    // session will close mockDaemon.loopMain ,so we dont need close it in UT

    // ~HdcHostUART will call close
}

/*
 * @tc.name: HdcHostUART
 * @tc.desc: Check the behavior of the HdcHostUART function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, HdcHostUART, TestSize.Level1)
{
    HdcHostUART hostUart(mockServer);
    EXPECT_EQ(hostUart.uartOpened, false);
    EXPECT_EQ(hostUart.baudRate, 0u);
    EXPECT_EQ(hostUart.mapIgnoreDevice.empty(), true);
    EXPECT_EQ(hostUart.serialPortInfo.empty(), true);

    hostUart.devUartWatcher.data = this;
    uv_timer_start(
        &hostUart.devUartWatcher,
        [](uv_timer_s *timer) {
            EXPECT_NE(timer->data, nullptr);
            timer->data = nullptr;
        },
        100, 0);

    std::this_thread::sleep_for(200ms);
    uv_run(&mockServer.loopMain, UV_RUN_NOWAIT);

    EXPECT_EQ(hostUart.devUartWatcher.data, nullptr);
    EXPECT_EQ(hostUart.semUartDevCheck.try_lock(), true);

    EXPECT_NE(uv_has_ref(reinterpret_cast<uv_handle_t *>(&hostUart.devUartWatcher)), 0);
}

/*
 * @tc.name: Initial
 * @tc.desc: Check the behavior of the Initial function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, Initial, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, StartupUARTWork).Times(1);
    EXPECT_EQ(mockHostUART.Initial(), 0);
    EXPECT_EQ(mockHostUART.uartOpened, false);
    EXPECT_EQ(mockHostUART.stopped, false);
}

/*
 * @tc.name: Stop
 * @tc.desc: Check the behavior of the Stop function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, Stop, TestSize.Level1)
{
    ON_CALL(mockHostUART, Stop).WillByDefault(Invoke([&]() {
        return mockHostUART.HdcHostUART::Stop();
    }));
    EXPECT_CALL(mockHostUART, Stop).Times(1);
    EXPECT_CALL(
        mockInterface,
        TryCloseHandle(reinterpret_cast<uv_handle_t *>(&mockHostUART.devUartWatcher), nullptr))
        .Times(1);
    mockHostUART.Stop();

    // unable stop twice
    EXPECT_CALL(mockHostUART, Stop).Times(1);
    EXPECT_CALL(mockInterface, TryCloseHandle).Times(0);
    mockHostUART.Stop();
}

/*
 * @tc.name: ConnectDaemonByUart
 * @tc.desc: Check the behavior of the ConnectDaemonByUart function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, ConnectDaemonByUart, TestSize.Level1)
{
    mockHostUART.uartOpened = true;

    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo(mySession.connectKey, &mySession, STATUS_READY))
        .Times(1);
    EXPECT_CALL(mockHostUART, StartUartReadThread(&mySession)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(mockInterface, StartWorkThread(&mockServer.loopMain, mockServer.SessionWorkThread,
                                               Base::FinishWorkThread, &mySession))
        .Times(1);
    EXPECT_CALL(
        mockInterface,
        SendToStream(reinterpret_cast<uv_stream_t *>(&mySession.ctrlPipe[STREAM_MAIN]), _, _))
        .Times(1);
    mySession.childLoop.active_handles = 1;
    EXPECT_EQ(mockHostUART.ConnectDaemonByUart(&mySession), &mySession);

    // delay case
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo(mySession.connectKey, &mySession, STATUS_READY))
        .Times(1);
    EXPECT_CALL(mockHostUART, StartUartReadThread(&mySession)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(mockInterface, StartWorkThread(&mockServer.loopMain, mockServer.SessionWorkThread,
                                               Base::FinishWorkThread, &mySession))
        .Times(1);
    EXPECT_CALL(
        mockInterface,
        SendToStream(reinterpret_cast<uv_stream_t *>(&mySession.ctrlPipe[STREAM_MAIN]), _, _))
        .Times(1);

    mySession.childLoop.active_handles = 0;
    std::thread mockActiveHandles([&]() {
        std::this_thread::sleep_for(1000ms);
        mySession.childLoop.active_handles = 1;
    });
    EXPECT_EQ(mockHostUART.ConnectDaemonByUart(&mySession), &mySession);
    mockActiveHandles.join();
}

/*
 * @tc.name: ConnectDaemonByUart
 * @tc.desc: Check the behavior of the ConnectDaemonByUart function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, ConnectDaemonByUartFail, TestSize.Level1)
{
    mockHostUART.uartOpened = false;
    EXPECT_EQ(mockHostUART.ConnectDaemonByUart(&mySession), nullptr);

    mockHostUART.uartOpened = true;
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo(mySession.connectKey, &mySession, STATUS_READY))
        .Times(1);
    EXPECT_CALL(mockHostUART, StartUartReadThread).Times(1).WillOnce(Return(false));
    EXPECT_EQ(mockHostUART.ConnectDaemonByUart(&mySession), nullptr);
}

/*
 * @tc.name: UvWatchUartDevPlugin
 * @tc.desc: Check the behavior of the UvWatchUartDevPlugin function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, UvWatchUartDevPlugin, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, WatchUartDevPlugin).Times(0);
    HdcHostUART::UvWatchUartDevPlugin(nullptr);

    EXPECT_CALL(mockHostUART, WatchUartDevPlugin()).Times(1);
    uv_timer_t handle;
    handle.data = &mockHostUART;
    HdcHostUART::UvWatchUartDevPlugin(&handle);

    handle.data = nullptr;
    HdcHostUART::UvWatchUartDevPlugin(&handle);
}

/*
 * @tc.name: WatchUartDevPlugin
 * @tc.desc: Check the behavior of the WatchUartDevPlugin function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, WatchUartDevPluginHaveSerialDaemon, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, WatchUartDevPlugin).WillRepeatedly([&]() {
        mockHostUART.HdcHostUART::WatchUartDevPlugin();
    });
    // case 1 EnumSerialPort failed
    EXPECT_CALL(mockHostUART, EnumSerialPort).WillOnce(Return(false));
    EXPECT_CALL(mockServer, AdminDaemonMap).Times(0);
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo).Times(0);
    mockHostUART.WatchUartDevPlugin();

    // case 2 EnumSerialPort not fail but no port change
    EXPECT_CALL(mockHostUART, EnumSerialPort)
        .WillOnce(DoAll(SetArgReferee<0>(false), Return(true)));
    EXPECT_CALL(mockServer, AdminDaemonMap).Times(0);
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo).Times(0);
    mockHostUART.WatchUartDevPlugin();
}

/*
 * @tc.name: WatchUartDevPlugin
 * @tc.desc: Check the behavior of the WatchUartDevPlugin function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, WatchUartDevPluginHaveSomePort, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, WatchUartDevPlugin).WillRepeatedly([&]() {
        mockHostUART.HdcHostUART::WatchUartDevPlugin();
    });

    // case 3 EnumSerialPort return two port, not port removed
    // one port is not connected , the other is connected
    EXPECT_CALL(mockHostUART, EnumSerialPort).WillOnce(DoAll(SetArgReferee<0>(true), Return(true)));
    mockHostUART.serialPortInfo.clear();
    mockHostUART.serialPortInfo.emplace_back("COMX");
    mockHostUART.serialPortInfo.emplace_back("COMY");
    mockHostUART.connectedPorts.clear();
    mockHostUART.connectedPorts.emplace("COMY");
    EXPECT_CALL(mockServer, AdminDaemonMap(OP_QUERY, "COMX", _))
        .WillOnce(DoAll(SetArgReferee<2>(nullptr), Return("")));
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo("COMX", nullptr, STATUS_READY)).Times(1);
    EXPECT_CALL(mockServer, AdminDaemonMap(OP_QUERY, "COMY", _))
        .WillOnce(DoAll(SetArgReferee<2>(nullptr), Return("")));
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo("COMY", nullptr, STATUS_READY)).Times(0);
    mockHostUART.WatchUartDevPlugin();
}

/*
 * @tc.name: WatchUartDevPlugin
 * @tc.desc: Check the behavior of the WatchUartDevPlugin function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, WatchUartDevPluginHaveRemovedPort, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, WatchUartDevPlugin).WillRepeatedly([&]() {
        mockHostUART.HdcHostUART::WatchUartDevPlugin();
    });

    // case 4 EnumSerialPort return two port, and one port removed
    // one port is not connected , the other is connected
    EXPECT_CALL(mockHostUART, EnumSerialPort).WillOnce(DoAll(SetArgReferee<0>(true), Return(true)));
    mockHostUART.serialPortInfo.clear();
    mockHostUART.serialPortInfo.emplace_back("COMX");
    mockHostUART.serialPortInfo.emplace_back("COMY");
    mockHostUART.connectedPorts.clear();
    mockHostUART.connectedPorts.emplace("COMY");
    mockHostUART.serialPortRemoved.clear();
    mockHostUART.serialPortRemoved.emplace_back("COMZ");

    HdcDaemonInformation di;
    EXPECT_CALL(mockServer, AdminDaemonMap(OP_QUERY, "COMX", _))
        .WillOnce(DoAll(SetArgReferee<2>(nullptr), Return("")));
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo("COMX", nullptr, STATUS_READY)).Times(1);
    EXPECT_CALL(mockServer, AdminDaemonMap(OP_QUERY, "COMY", _))
        .WillOnce(DoAll(SetArgReferee<2>(nullptr), Return("")));
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo("COMY", nullptr, STATUS_READY)).Times(0);
    EXPECT_CALL(mockServer, AdminDaemonMap(OP_QUERY, "COMZ", _))
        .WillOnce(DoAll(SetArgReferee<2>(&di), Return("")));
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo("COMZ", nullptr, STATUS_READY)).Times(0);
    mockHostUART.WatchUartDevPlugin();
}

/*
 * @tc.name: UpdateUARTDaemonInfo
 * @tc.desc: Check the behavior of the UpdateUARTDaemonInfo function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, UpdateUARTDaemonInfo, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo)
        .WillRepeatedly(
            [&](const std::string &connectKey, HSession hSession, ConnStatus connStatus) {
                mockHostUART.HdcHostUART::UpdateUARTDaemonInfo(connectKey, hSession, connStatus);
            });

    // case 1 STATUS_UNKNOW
    HdcDaemonInformation diNew;
    EXPECT_CALL(mockServer,
                AdminDaemonMap(OP_REMOVE, testSerialPortName,
                               Field(&HdcDaemonInformation::connectKey, testSerialPortName)))
        .Times(1)
        .WillOnce(DoAll(SetArgReferee<2>(nullptr), Return("")));

    mockHostUART.UpdateUARTDaemonInfo(myUART.serialPort, &mySession, STATUS_UNKNOW);

    // case 2 STATUS_CONNECTED
    HdcDaemonInformation diDummy;
    EXPECT_CALL(mockServer, AdminDaemonMap(OP_QUERY, testSerialPortName, _))
        .Times(1)
        .WillOnce(DoAll(SetArgReferee<2>(&diDummy), Return("")));

    EXPECT_CALL(mockServer,
                AdminDaemonMap(OP_UPDATE, testSerialPortName,
                               Field(&HdcDaemonInformation::connectKey, testSerialPortName)))
        .Times(1)
        .WillOnce(DoAll(SetArgReferee<2>(nullptr), Return("")));

    mockHostUART.UpdateUARTDaemonInfo(myUART.serialPort, &mySession, STATUS_CONNECTED);
}

/*
 * @tc.name: StartUartReadThread
 * @tc.desc: Check the behavior of the StartUartReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, StartUartReadThread, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, StartUartReadThread).WillOnce(Invoke([&](HSession hSession) {
        return mockHostUART.HdcHostUART::StartUartReadThread(hSession);
    }));

    EXPECT_CALL(mockHostUART, UartReadThread(&mySession)).Times(1);
    mockHostUART.StartUartReadThread(&mySession);
    if (mySession.hUART->readThread.joinable()) {
        mySession.hUART->readThread.join();
    }
}

/*
 * @tc.name: ConnectMyNeed
 * @tc.desc: Check the behavior of the ConnectMyNeed function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, ConnectMyNeed, TestSize.Level1)
{
    HdcUART newUART;
    mySession.hUART = &newUART;
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo(myUART.serialPort, nullptr, STATUS_READY))
        .Times(1);
    EXPECT_CALL(mockServer, MallocSession(true, CONN_SERIAL, &mockHostUART, 0))
        .Times(1)
        .WillOnce(Return(&mySession));
    EXPECT_CALL(mockInterface, UvTimerStart(Field(&uv_timer_t::data, &mySession),
                                            &mockServer.UartPreConnect, UV_TIMEOUT, UV_REPEAT))
        .Times(1)
        .WillOnce(Return(RET_SUCCESS));

    EXPECT_EQ(mockHostUART.ConnectMyNeed(&myUART, myUART.serialPort), true);

    EXPECT_EQ(newUART.devUartHandle, testSerialHandle);
    EXPECT_EQ(newUART.serialPort, testSerialPortName);
}

/*
 * @tc.name: ConnectMyNeed
 * @tc.desc: Check the behavior of the ConnectMyNeed function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, ConnectMyNeedFail, TestSize.Level1)
{
    HdcUART newUART;
    mySession.hUART = &newUART;
    EXPECT_CALL(mockHostUART, UpdateUARTDaemonInfo(myUART.serialPort, nullptr, STATUS_READY))
        .Times(1);
    EXPECT_CALL(mockServer, MallocSession(true, CONN_SERIAL, &mockHostUART, 0))
        .Times(1)
        .WillOnce(Return(&mySession));
    EXPECT_CALL(mockInterface, UvTimerStart(Field(&uv_timer_t::data, &mySession),
                                            &mockServer.UartPreConnect, UV_TIMEOUT, UV_REPEAT))
        .Times(1)
        .WillOnce(Return(-1));

    EXPECT_EQ(mockHostUART.ConnectMyNeed(&myUART), false);

    EXPECT_EQ(newUART.devUartHandle, testSerialHandle);
    EXPECT_EQ(newUART.serialPort, testSerialPortName);
}

/*
 * @tc.name: StartupUARTWork
 * @tc.desc: Check the behavior of the StartupUARTWork function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, StartupUARTWork, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, StartupUARTWork).WillRepeatedly([&]() {
        return mockHostUART.HdcHostUART::StartupUARTWork();
    });
    EXPECT_CALL(mockInterface, UvTimerStart(Field(&uv_timer_t::data, &mockHostUART),
                                            mockHostUART.UvWatchUartDevPlugin, _, _))
        .Times(1)
        .WillOnce(Return(0));

    EXPECT_CALL(mockHostUART, StartUartSendThread()).Times(1).WillOnce(Return(false));

    EXPECT_EQ(mockHostUART.StartupUARTWork(), ERR_GENERIC);

    EXPECT_CALL(mockInterface, UvTimerStart(Field(&uv_timer_t::data, &mockHostUART),
                                            mockHostUART.UvWatchUartDevPlugin, _, _))
        .Times(1)
        .WillOnce(Return(-1));
    EXPECT_EQ(mockHostUART.StartupUARTWork(), ERR_GENERIC);
}

MATCHER_P(EqUartHead, otherUartHead, "Equality matcher for type UartHead")
{
    UartHead *argUartHead = reinterpret_cast<UartHead *>(arg);
    return *argUartHead == *otherUartHead;
}

/*
 * @tc.name: SendUartSoftReset
 * @tc.desc: Check the behavior of the SendUartSoftReset function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, SendUartSoftReset, TestSize.Level1)
{
    UartHead resetPackage;
    constexpr uint32_t sessionId = 1234;
    resetPackage.option = PKG_OPTION_RESET;
    resetPackage.dataSize = sizeof(UartHead);
    resetPackage.sessionId = sessionId;

    EXPECT_CALL(mockHostUART,
                RequestSendPackage(EqUartHead(&resetPackage), sizeof(UartHead), false))
        .Times(1);
    mockHostUART.SendUartSoftReset(&mySession, sessionId);
}

/*
 * @tc.name: KickoutZombie
 * @tc.desc: Check the behavior of the KickoutZombie function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, KickoutZombie, TestSize.Level1)
{
    mockHostUART.KickoutZombie(nullptr);

    mySession.isDead = true;
    mockHostUART.KickoutZombie(&mySession);
    mySession.isDead = false;

    myUART.devUartHandle = -1;
    mockHostUART.KickoutZombie(&mySession);
    myUART.devUartHandle = 0;

    EXPECT_CALL(mockServer, FreeSession(mySession.sessionId)).Times(1);
    mockHostUART.KickoutZombie(&mySession);
}

/*
 * @tc.name: StartUartSendThread
 * @tc.desc: Check the behavior of the StartUartSendThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, StartUartSendThread, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, StartUartSendThread).WillRepeatedly([&]() {
        return mockHostUART.HdcHostUART::StartUartSendThread();
    });
    EXPECT_CALL(mockHostUART, UartWriteThread).Times(1);
    EXPECT_TRUE(mockHostUART.StartUartSendThread());
    std::this_thread::sleep_for(500ms);
}

/*
 * @tc.name: NeedStop
 * @tc.desc: Check the behavior of the NeedStop function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, NeedStop, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, NeedStop).WillRepeatedly([&](const HSession hSession) {
        return mockHostUART.HdcHostUART::NeedStop(hSession);
    });
    mockHostUART.uartOpened = true;
    mySession.isDead = false;
    mySession.ref = 1;
    EXPECT_EQ(mockHostUART.NeedStop(&mySession), false);

    mockHostUART.uartOpened = true;
    mySession.isDead = false;
    mySession.ref = 0;
    EXPECT_EQ(mockHostUART.NeedStop(&mySession), false);

    mockHostUART.uartOpened = true;
    mySession.isDead = true;
    mySession.ref = 1;
    EXPECT_EQ(mockHostUART.NeedStop(&mySession), false);

    mockHostUART.uartOpened = true;
    mySession.isDead = true;
    mySession.ref = 0;
    EXPECT_EQ(mockHostUART.NeedStop(&mySession), true);

    mockHostUART.uartOpened = false;
    mySession.isDead = false;
    mySession.ref = 1;
    EXPECT_EQ(mockHostUART.NeedStop(&mySession), true);
}

/*
 * @tc.name: IsDeviceOpened
 * @tc.desc: Check the behavior of the IsDeviceOpened function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, IsDeviceOpened, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, IsDeviceOpened).WillRepeatedly([&](const HdcUART &uart) {
        return mockHostUART.HdcHostUART::IsDeviceOpened(uart);
    });
    myUART.devUartHandle = -1;
    EXPECT_EQ(mockHostUART.IsDeviceOpened(myUART), false);

    myUART.devUartHandle = -2;
    EXPECT_EQ(mockHostUART.IsDeviceOpened(myUART), false);

    myUART.devUartHandle = 0;
    EXPECT_EQ(mockHostUART.IsDeviceOpened(myUART), true);

    myUART.devUartHandle = 1;
    EXPECT_EQ(mockHostUART.IsDeviceOpened(myUART), true);
}

/*
 * @tc.name: UartWriteThread
 * @tc.desc: Check the behavior of the UartWriteThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, UartWriteThread, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, UartWriteThread).WillOnce(Invoke([&]() {
        return mockHostUART.HdcHostUART::UartWriteThread();
    }));

    EXPECT_CALL(mockHostUART, SendPkgInUARTOutMap).Times(0);
    auto sendThread = std::thread(&HdcHostUART::UartWriteThread, &mockHostUART);
    std::this_thread::sleep_for(1000ms);
    EXPECT_CALL(mockHostUART, SendPkgInUARTOutMap).Times(1);
    mockHostUART.transfer.Request();
    std::this_thread::sleep_for(200ms);
    mockHostUART.stopped = true;
    mockHostUART.transfer.Request();
    sendThread.join();
}

/*
 * @tc.name: UartReadThread
 * @tc.desc: Check the behavior of the UartReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, UartReadThread, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, UartReadThread).WillRepeatedly(Invoke([&](HSession hSession) {
        return mockHostUART.HdcHostUART::UartReadThread(hSession);
    }));

    // case 1 need stop
    EXPECT_CALL(mockHostUART, NeedStop).WillOnce(Return(true));
    auto readThread = std::thread(&HdcHostUART::UartReadThread, &mockHostUART, &mySession);
    readThread.join();

    // case 2 ReadUartDev return -1 will cause OnTransferError
    EXPECT_CALL(mockHostUART, NeedStop).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_CALL(mockHostUART, ReadUartDev).WillOnce(Return(-1));
    EXPECT_CALL(mockHostUART, OnTransferError).Times(1);
    EXPECT_CALL(mockHostUART, PackageProcess).Times(0);
    auto readThread2 = std::thread(&HdcHostUART::UartReadThread, &mockHostUART, &mySession);
    readThread2.join();

    // case 3 ReadUartDev retturn 0 will timeout, try again
    EXPECT_CALL(mockHostUART, NeedStop).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_CALL(mockHostUART, ReadUartDev).WillOnce(Return(0));
    EXPECT_CALL(mockHostUART, OnTransferError).Times(0);
    EXPECT_CALL(mockHostUART, PackageProcess).Times(0);
    auto readThread3 = std::thread(&HdcHostUART::UartReadThread, &mockHostUART, &mySession);
    readThread3.join();

    // case 4 ReadUartDev return sizeof(UartHead)
    EXPECT_CALL(mockHostUART, NeedStop).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_CALL(mockHostUART, ReadUartDev)
        .WillOnce(Invoke([&](std::vector<uint8_t> &readBuf, size_t expectedSize, HdcUART &uart) {
            readBuf.resize(sizeof(UartHead));
            return readBuf.size();
        }));
    EXPECT_CALL(mockHostUART, OnTransferError).Times(0);
    EXPECT_CALL(mockHostUART, PackageProcess).WillOnce(Return(0));
    auto readThread4 = std::thread(&HdcHostUART::UartReadThread, &mockHostUART, &mySession);
    readThread4.join();

    // case 5 ReadUartDev return more than sizeof(UartHead)
    EXPECT_CALL(mockHostUART, NeedStop).WillOnce(Return(false)).WillOnce(Return(true));
    const size_t testDataSize = sizeof(UartHead) * 2;

    EXPECT_CALL(mockHostUART, ReadUartDev)
        .WillOnce(Invoke([&](std::vector<uint8_t> &readBuf, size_t expectedSize, HdcUART &uart) {
            readBuf.resize(testDataSize);
            return readBuf.size();
        }));
    EXPECT_CALL(mockHostUART, OnTransferError).Times(0);
    EXPECT_CALL(mockHostUART, PackageProcess).WillOnce(Return(0));
    auto readThread5 = std::thread(&HdcHostUART::UartReadThread, &mockHostUART, &mySession);
    readThread5.join();
}

/*
 * @tc.name: WaitUartIdle
 * @tc.desc: Check the behavior of the WaitUartIdle function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, WaitUartIdle, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, ReadUartDev(_, 1, Ref(myUART))).WillOnce(Return(0));
    EXPECT_EQ(mockHostUART.WaitUartIdle(myUART), true);

    EXPECT_CALL(mockHostUART, ReadUartDev(_, 1, Ref(myUART)))
        .WillOnce(Return(1))
        .WillOnce(Return(0));
    EXPECT_EQ(mockHostUART.WaitUartIdle(myUART), true);

    EXPECT_CALL(mockHostUART, ReadUartDev(_, 1, Ref(myUART)))
        .WillOnce(Return(1))
        .WillOnce(Return(1));
    EXPECT_EQ(mockHostUART.WaitUartIdle(myUART), false);
}

/*
 * @tc.name: ConnectDaemon
 * @tc.desc: Check the behavior of the ConnectDaemon function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, ConnectDaemon, TestSize.Level1)
{
    std::string connectKey = "dummykey";
    EXPECT_CALL(mockHostUART, OpenSerialPort(connectKey)).WillOnce(Return(0));
    EXPECT_EQ(mockHostUART.ConnectDaemon(connectKey), nullptr);
}

/*
 * @tc.name: GetSession
 * @tc.desc: Check the behavior of the GetSession function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, GetSession, TestSize.Level1)
{
    constexpr uint32_t sessionId = 1234;
    HdcSession mySession;
    HdcUART myUART;
    mySession.hUART = &myUART;
    EXPECT_CALL(mockServer, AdminSession(OP_QUERY, sessionId, nullptr))
        .Times(1)
        .WillOnce(Return(&mySession));
    mockHostUART.GetSession(sessionId, false);
}

/*
 * @tc.name: CloseSerialPort
 * @tc.desc: Check the behavior of the CloseSerialPort function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, CloseSerialPort, TestSize.Level1)
{
    myUART.devUartHandle = -1;
    mockHostUART.CloseSerialPort(&myUART);
}

/*
 * @tc.name: GetPortFromKey
 * @tc.desc: Check the behavior of the GetPortFromKey function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, GetPortFromKey, TestSize.Level1)
{
    std::string portName;
    uint32_t baudRate;
    const uint32_t baudRateTest = 111u;
    EXPECT_EQ(mockHostUART.GetPortFromKey("COM1", portName, baudRate), true);
    EXPECT_STREQ(portName.c_str(), "COM1");
    EXPECT_EQ(baudRate, mockHostUART.DEFAULT_BAUD_RATE_VALUE);

    EXPECT_EQ(mockHostUART.GetPortFromKey("COM1,aaa", portName, baudRate), false);

    EXPECT_EQ(mockHostUART.GetPortFromKey("COM1,111", portName, baudRate), true);
    EXPECT_STREQ(portName.c_str(), "COM1");
    EXPECT_EQ(baudRate, baudRateTest);
}

/*
 * @tc.name: Restartession
 * @tc.desc: Check the behavior of the Restartession function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, Restartession, TestSize.Level1)
{
    const uint32_t sessionId = 12345;
    mySession.sessionId = sessionId;
    EXPECT_CALL(mockHostUART, CloseSerialPort(mySession.hUART)).WillOnce(Return());
    EXPECT_CALL(mockServer, EchoToClientsForSession(sessionId, _)).WillOnce(Return());
    mockHostUART.Restartession(&mySession);
}

/*
 * @tc.name: StopSession
 * @tc.desc: Check the behavior of the StopSession function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, StopSession, TestSize.Level1)
{
    EXPECT_EQ(mySession.hUART->ioCancel, false);
    mockHostUART.StopSession(nullptr);
    EXPECT_EQ(mySession.hUART->ioCancel, false);
    mockHostUART.StopSession(&mySession);
    EXPECT_EQ(mySession.hUART->ioCancel, true);
}

/*
 * @tc.name: OnTransferError
 * @tc.desc: Check the behavior of the OnTransferError function
 * @tc.type: FUNC
 */
HWTEST_F(HdcHostUARTTest, OnTransferError, TestSize.Level1)
{
    EXPECT_CALL(mockHostUART, OnTransferError).WillRepeatedly(Invoke([&](const HSession session) {
        return mockHostUART.HdcHostUART::OnTransferError(session);
    }));
    mockHostUART.OnTransferError(nullptr);
    const uint32_t sessionId = mySession.sessionId;

    EXPECT_CALL(mockHostUART, IsDeviceOpened(Ref(*mySession.hUART))).WillOnce(Return(true));
    EXPECT_CALL(mockServer, EchoToClientsForSession(sessionId, _)).WillOnce(Return());
    EXPECT_CALL(mockHostUART, CloseSerialPort(mySession.hUART)).WillOnce(Return());
    EXPECT_CALL(mockHostUART,
                UpdateUARTDaemonInfo(mySession.connectKey, &mySession, STATUS_OFFLINE))
        .WillOnce(Return());
    EXPECT_CALL(mockServer, FreeSession(sessionId)).WillOnce(Return());
    EXPECT_CALL(mockHostUART, ClearUARTOutMap(sessionId)).WillOnce(Return());
    mockHostUART.OnTransferError(&mySession);
}
} // namespace Hdc