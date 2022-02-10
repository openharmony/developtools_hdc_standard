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

#include "daemon_uart_test.h"

#include <chrono>
#include <random>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace testing::ext;
using namespace testing;

namespace Hdc {
class HdcDaemonUARTTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

    constexpr static int PAIR = 2;
    constexpr static int SENDER = 0;
    constexpr static int RECEIVER = 1;

    std::default_random_engine rnd;
    class MockHdcDaemon : public HdcDaemon {
        MockHdcDaemon() : HdcDaemon(false) {};
        MOCK_METHOD4(MallocSession, HSession(bool, const ConnType, void *, uint32_t));
        MOCK_METHOD4(PushAsyncMessage,
                     void(const uint32_t, const uint8_t, const void *, const int));
        MOCK_METHOD1(FreeSession, void(const uint32_t));
        MOCK_METHOD3(AdminSession, HSession(const uint8_t, const uint32_t, HSession));
    } mockDaemon;
    const std::string nullDevPath = "/dev/null";

    class MockBaseInterface : public ExternInterface {
    public:
        MOCK_METHOD4(StartWorkThread, int(uv_loop_t *, uv_work_cb, uv_after_work_cb, void *));
        MOCK_METHOD2(TryCloseHandle, void(const uv_handle_t *, uv_close_cb));
        MOCK_METHOD3(TimerUvTask, bool(uv_loop_t *, void *, uv_timer_cb));
    } mockInterface;

    class MockHdcDaemonUART : public HdcDaemonUART {
        explicit MockHdcDaemonUART(HdcDaemon &daemonIn,
                                   ExternInterface &externInterface = HdcUARTBase::defaultInterface)
            : HdcDaemonUART(daemonIn, externInterface)
        {
        }
        MOCK_METHOD0(CloseUartDevice, int(void));
        MOCK_METHOD0(OpenUartDevice, int(void));
        MOCK_METHOD0(LoopUARTRead, int(void));
        MOCK_METHOD0(LoopUARTWrite, int(void));
        MOCK_METHOD0(PrepareBufForRead, int(void));
        MOCK_METHOD0(WatcherTimerCallBack, void(void));
        MOCK_METHOD0(DeamonReadThread, void(void));
        MOCK_METHOD0(DeamonWriteThread, void(void));
        MOCK_METHOD1(IsSendReady, bool(HSession));
        MOCK_METHOD1(PrepareNewSession, HSession(uint32_t));
        MOCK_METHOD2(PackageProcess, size_t(vector<uint8_t> &data, HSession hSession));
        MOCK_METHOD3(SendUARTDev, size_t(HSession, uint8_t *, const size_t));
        MOCK_METHOD3(UartSendToHdcStream, bool(HSession, uint8_t *, size_t));
        MOCK_METHOD4(ValidateUartPacket,
                     RetErrCode(vector<uint8_t> &, uint32_t &, uint32_t &, size_t &));
    } mockDaemonUART;

    HdcDaemonUARTTest() : mockDaemonUART(mockDaemon, mockInterface) {};

    void MockUartDevSender(int fd, uint8_t *mockData, size_t length, milliseconds timeout,
                           bool closeThreadAfterSend = true)
    {
        std::this_thread::sleep_for(timeout);
        ASSERT_EQ(write(fd, mockData, length), signed(length));
        if (closeThreadAfterSend) {
            std::this_thread::sleep_for(1000ms);
            mockDaemonUART.isAlive = false; // break DeamonReadThread while
            close(fd);                      // break select
        }
    }

    // input data(high level), append the head , prepare mockDaemonUART
    void PrepareDeamonReadThreadData(std::vector<uint8_t> &data, int fds[PAIR])
    {
        ON_CALL(mockDaemonUART, DeamonReadThread).WillByDefault([&]() {
            return mockDaemonUART.HdcDaemonUART::DeamonReadThread();
        });
        EXPECT_CALL(mockDaemonUART, DeamonReadThread).Times(AnyNumber());
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds), 0);

        // copy to raw data
        data.resize(sizeof(UartHead) + data.size());

        mockDaemonUART.isAlive = true;
        mockDaemonUART.dataReadBuf.resize(MAX_SIZE_IOBUF);
        mockDaemonUART.uartHandle = fds[RECEIVER];
    }
    HdcSession mySession;
};

void HdcDaemonUARTTest::SetUpTestCase()
{
#ifdef UT_DEBUG
    Hdc::Base::SetLogLevel(LOG_ALL);
#else
    Hdc::Base::SetLogLevel(LOG_OFF);
#endif
}

void HdcDaemonUARTTest::TearDownTestCase() {}

void HdcDaemonUARTTest::SetUp() {}

void HdcDaemonUARTTest::TearDown() {}

/*
 * @tc.name: HdcDaemonUART
 * @tc.desc: Check the behavior of the HdcDaemonUART function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, HdcDaemonUART, TestSize.Level1)
{
    const std::string testDevPath = "testDevPath";
    HdcDaemon daemon(false);
    HdcDaemonUART daemonUART(daemon);
    EXPECT_EQ(daemonUART.uartHandle, -1);
    EXPECT_EQ(daemonUART.currentSessionId, 0u);
    EXPECT_EQ(daemonUART.isAlive, false);
    EXPECT_EQ(daemonUART.dataReadBuf.size(), 0u);
    EXPECT_EQ(&daemonUART.daemon, &daemon);
    EXPECT_EQ(daemonUART.devPath.empty(), true);
}

/*
 * @tc.name: Initial
 * @tc.desc: Check the behavior of the Initial function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, Initial, TestSize.Level1)
{
    const std::string testDevPath = "testDevPath";
    HdcDaemon daemon(false);
    HdcDaemonUART daemonUARTFailed(daemon);
    EXPECT_EQ(daemonUARTFailed.Initial(testDevPath), -1);

    // timeout + repeate
    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).Times(1);
    EXPECT_EQ(mockDaemonUART.Initial(nullDevPath), 0);
    EXPECT_EQ(mockDaemonUART.checkSerialPort.data, &mockDaemonUART);
    std::this_thread::sleep_for(100ms);

    // we only test once , because we have not stop the time ,so uv_run will NOT return 0;
    EXPECT_NE(uv_run(&mockDaemon.loopMain, UV_RUN_NOWAIT), 0);

    // if we wait one more time ?
    std::this_thread::sleep_for(2000ms);
    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).Times(1);
    EXPECT_NE(uv_run(&mockDaemon.loopMain, UV_RUN_NOWAIT), 0);
    EXPECT_CALL(mockInterface, TryCloseHandle).Times(1);

    EXPECT_EQ(uv_timer_stop(&mockDaemonUART.checkSerialPort), 0);
}

/*
 * @tc.name: WatcherTimerCallBack
 * @tc.desc: Check the behavior of the WatcherTimerCallBack function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, WatcherTimerCallBack, TestSize.Level1)
{
    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).WillRepeatedly(Invoke([&] {
        mockDaemonUART.HdcDaemonUART::WatcherTimerCallBack();
    }));

    // case 1 isAlive not valid
    mockDaemonUART.isAlive = true;
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(0);
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(0);
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(0);
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(0);
    EXPECT_CALL(mockDaemonUART, LoopUARTWrite).Times(0);

    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, true);

    // case 2 uartHandle not valid
    mockDaemonUART.isAlive = false;
    mockDaemonUART.uartHandle = -1;
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(0);
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(1);
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(1);
    EXPECT_CALL(mockDaemonUART, LoopUARTWrite).Times(1);
    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, true);

    // case 3 uartHandle valid
    mockDaemonUART.isAlive = false;
    mockDaemonUART.uartHandle = 0; // 0 is a valid fd !
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(1);
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(1);
    EXPECT_CALL(mockDaemonUART, LoopUARTWrite).Times(1);
    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, true);
}
/*
 * @tc.name: UvWatchTimer
 * @tc.desc: Check the behavior of the ConnectDaemonByUart function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, UvWatchTimer, TestSize.Level1)
{
    uv_timer_t handle;

    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).Times(0);
    HdcDaemonUART::UvWatchTimer(nullptr);

    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).Times(0);
    handle.data = nullptr;
    HdcDaemonUART::UvWatchTimer(&handle);

    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).Times(1);
    handle.data = &mockDaemonUART;
    HdcDaemonUART::UvWatchTimer(&handle);
}

/*
 * @tc.name: WatcherTimerCallBack
 * @tc.desc: Check the behavior of the WatcherTimerCallBack function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, WatcherTimerCallBackDeviceFailed, TestSize.Level1)
{
    ON_CALL(mockDaemonUART, WatcherTimerCallBack).WillByDefault([&] {
        mockDaemonUART.HdcDaemonUART::WatcherTimerCallBack();
    });
    EXPECT_CALL(mockDaemonUART, WatcherTimerCallBack).Times(AnyNumber());

    // case 1 CloseUartDevice failed
    mockDaemonUART.isAlive = false;
    mockDaemonUART.uartHandle = 0;
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(0);
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(0);
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(0);
    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    // case 2 OpenUartDevice failed
    mockDaemonUART.isAlive = false;
    mockDaemonUART.uartHandle = 0;
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(0);
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(0);
    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    // case 3 PrepareBufForRead failed
    mockDaemonUART.isAlive = false;
    mockDaemonUART.uartHandle = 0;
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(0);
    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    // case 4 LoopUARTRead failed
    mockDaemonUART.isAlive = false;
    mockDaemonUART.uartHandle = 0;
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, OpenUartDevice).Times(1);
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(1);
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(1).WillOnce(Return(-1));
    mockDaemonUART.WatcherTimerCallBack();
    EXPECT_EQ(mockDaemonUART.isAlive, false);
}

/*
 * @tc.name: CloseUartDevice
 * @tc.desc: Check the behavior of the CloseUartDevice function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, CloseUartDevice, TestSize.Level1)
{
    HdcDaemon daemon(false);
    HdcDaemonUART daemonUART(daemon);

    daemonUART.uartHandle = -1;
    EXPECT_LT(daemonUART.CloseUartDevice(), 0);
    EXPECT_EQ(daemonUART.uartHandle, -1);

    daemonUART.uartHandle = open(nullDevPath.c_str(), O_RDWR);
    EXPECT_EQ(daemonUART.CloseUartDevice(), 0);
    EXPECT_EQ(daemonUART.uartHandle, -1);
}

/*
 * @tc.name: LoopUARTRead
 * @tc.desc: Check the behavior of the LoopUARTRead function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, LoopUARTRead, TestSize.Level1)
{
    ON_CALL(mockDaemonUART, LoopUARTRead).WillByDefault([&] {
        return mockDaemonUART.HdcDaemonUART::LoopUARTRead();
    });
    EXPECT_CALL(mockDaemonUART, LoopUARTRead).Times(AnyNumber());
    EXPECT_CALL(mockDaemonUART, DeamonReadThread).Times(1);
    EXPECT_EQ(mockDaemonUART.LoopUARTRead(), 0);
    // wait thread work
    std::this_thread::sleep_for(1000ms);
}

/*
 * @tc.name: LoopUARTWrite
 * @tc.desc: Check the behavior of the LoopUARTWrite function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, LoopUARTWrite, TestSize.Level1)
{
    ON_CALL(mockDaemonUART, LoopUARTWrite).WillByDefault([&] {
        return mockDaemonUART.HdcDaemonUART::LoopUARTWrite();
    });
    EXPECT_CALL(mockDaemonUART, LoopUARTWrite).Times(AnyNumber());
    EXPECT_CALL(mockDaemonUART, DeamonWriteThread).Times(1);
    EXPECT_EQ(mockDaemonUART.LoopUARTWrite(), 0);
    // wait thread work
    std::this_thread::sleep_for(1000ms);
}

/*
 * @tc.name: IsSendReady
 * @tc.desc: Check the behavior of the IsSendReady function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, IsSendReady, TestSize.Level1)
{
    ON_CALL(mockDaemonUART, IsSendReady).WillByDefault([&](HSession hSession) {
        return mockDaemonUART.HdcDaemonUART::IsSendReady(hSession);
    });
    EXPECT_CALL(mockDaemonUART, IsSendReady).Times(AtLeast(1));

    HdcSession mySession;
    HdcUART myUART;
    mySession.hUART = &myUART;

    mockDaemonUART.isAlive = true;
    mySession.isDead = false;
    mySession.hUART->resetIO = false;
    mockDaemonUART.uartHandle = 0;
    EXPECT_EQ(mockDaemonUART.IsSendReady(&mySession), true);

    mockDaemonUART.isAlive = false;
    EXPECT_EQ(mockDaemonUART.IsSendReady(&mySession), false);
    mockDaemonUART.isAlive = true;

    mySession.isDead = true;
    EXPECT_EQ(mockDaemonUART.IsSendReady(&mySession), false);
    mySession.isDead = false;

    mySession.hUART->resetIO = true;
    EXPECT_EQ(mockDaemonUART.IsSendReady(&mySession), false);
    mySession.hUART->resetIO = false;

    mockDaemonUART.uartHandle = -1;
    EXPECT_EQ(mockDaemonUART.IsSendReady(&mySession), false);
    mockDaemonUART.uartHandle = 0;
}

/*
 * @tc.name: PrepareBufForRead
 * @tc.desc: Check the behavior of the PrepareBufForRead function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, PrepareBufForRead, TestSize.Level1)
{
    ON_CALL(mockDaemonUART, PrepareBufForRead).WillByDefault([&]() {
        return mockDaemonUART.HdcDaemonUART::PrepareBufForRead();
    });
    EXPECT_CALL(mockDaemonUART, PrepareBufForRead).Times(AtLeast(1));

    mockDaemonUART.PrepareBufForRead();
    EXPECT_EQ(mockDaemonUART.PrepareBufForRead(), RET_SUCCESS);
}

/*
 * @tc.name: PrepareNewSession
 * @tc.desc: Check the behavior of the PrepareNewSession function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, PrepareNewSession, TestSize.Level1)
{
    constexpr uint32_t sessionTestId = 1234;
    ON_CALL(mockDaemonUART, PrepareNewSession).WillByDefault([&](uint32_t sessionId) {
        return mockDaemonUART.HdcDaemonUART::PrepareNewSession(sessionId);
    });
    EXPECT_CALL(mockDaemonUART, PrepareNewSession).Times(AnyNumber());

    HdcSession mySession;

    mockDaemonUART.currentSessionId = 0;
    EXPECT_CALL(mockDaemon, MallocSession).Times(1).WillOnce(Return(&mySession));
    EXPECT_CALL(mockInterface, StartWorkThread).Times(1);
    EXPECT_CALL(mockInterface, TimerUvTask).Times(1);
    EXPECT_EQ(mockDaemonUART.PrepareNewSession(sessionTestId), &mySession);

    mockDaemonUART.currentSessionId = sessionTestId;
    EXPECT_CALL(mockDaemon, MallocSession).Times(1).WillOnce(Return(&mySession));
    EXPECT_CALL(mockDaemon, PushAsyncMessage).Times(1);
    EXPECT_CALL(mockInterface, StartWorkThread).Times(1);
    EXPECT_CALL(mockInterface, TimerUvTask).Times(1);
    EXPECT_EQ(mockDaemonUART.PrepareNewSession(sessionTestId), &mySession);

    mockDaemonUART.currentSessionId = sessionTestId;
    EXPECT_CALL(mockDaemon, MallocSession).Times(1).WillOnce(Return(nullptr));
    EXPECT_EQ(mockDaemonUART.PrepareNewSession(sessionTestId), nullptr);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThread, TestSize.Level1)
{
    std::vector<uint8_t> data;
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(1).WillOnce(Return(sizeof(UartHead)));

    ASSERT_EQ(mockDaemonUART.isAlive, true);
    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);
    MockUartDevSender(fds[SENDER], data.data(), data.size(), 0ms, true);
    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadHaveData, TestSize.Level1)
{
    constexpr int fewDataSize = 4;
    std::vector<uint8_t> data(fewDataSize);
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(1).WillOnce(Return(sizeof(UartHead)));

    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);
    MockUartDevSender(fds[SENDER], data.data(), data.size(), 0ms, true);
    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadDelay, TestSize.Level1)
{
    constexpr int fewDataSize = 4;
    std::vector<uint8_t> data(fewDataSize);
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(1).WillOnce(Return(sizeof(UartHead)));

    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);
    MockUartDevSender(fds[SENDER], data.data(), data.size(), 1000ms, true);
    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadMorePackage, TestSize.Level1)
{
    constexpr int fewDataSize = 4;
    constexpr int PackageNumber = 3;
    std::vector<uint8_t> data(fewDataSize);
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);
    // we need send much more package
    data.resize(data.size() * PackageNumber);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(1).WillOnce(Return(sizeof(UartHead)));

    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);
    MockUartDevSender(fds[SENDER], data.data(), data.size(), 0ms, true);
    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadHaveLargeData, TestSize.Level1)
{
    std::vector<uint8_t> data(MAX_SIZE_IOBUF);
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(4);

    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);
    MockUartDevSender(fds[SENDER], data.data(), data.size(), 0ms, true);
    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadMoreRead, TestSize.Level1)
{
    constexpr int firstReadSize = 10;
    constexpr int secondReadSize = 20;
    constexpr int thirdReadSize = 30;
    std::vector<uint8_t> data(firstReadSize + secondReadSize + thirdReadSize);
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(AnyNumber());

    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);

    MockUartDevSender(fds[SENDER], data.data(), sizeof(UartHead), 1000ms, false);
    MockUartDevSender(fds[SENDER], data.data(), firstReadSize, 1000ms, false);
    MockUartDevSender(fds[SENDER], data.data(), secondReadSize, 1000ms, false);
    MockUartDevSender(fds[SENDER], data.data(), thirdReadSize, 1000ms, true);
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadPacketFailed, TestSize.Level1)
{
    std::vector<uint8_t> data;
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(1).WillOnce(Return(sizeof(UartHead)));

    ASSERT_EQ(mockDaemonUART.isAlive, true);
    std::thread deamonRead(&MockHdcDaemonUART::DeamonReadThread, &mockDaemonUART);

    MockUartDevSender(fds[SENDER], data.data(), data.size(), 0ms, true);
    deamonRead.join();
    EXPECT_EQ(mockDaemonUART.isAlive, false);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: DeamonReadThread
 * @tc.desc: Check the behavior of the DeamonReadThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, DeamonReadThreadDispatchFailed, TestSize.Level1)
{
    std::vector<uint8_t> data;
    int fds[PAIR];

    // after this call , data will include head and data
    PrepareDeamonReadThreadData(data, fds);

    EXPECT_CALL(mockDaemonUART, PackageProcess).Times(0);

    ASSERT_EQ(mockDaemonUART.isAlive, true);

    // will not set isAlive false , wait DeamonReadThread exit first
    MockUartDevSender(fds[SENDER], data.data(), data.size(), 0ms, false);

    EXPECT_EQ(mockDaemonUART.isAlive, true);

    close(fds[SENDER]);
    close(fds[RECEIVER]);
}

/*
 * @tc.name: ResetOldSession
 * @tc.desc: Check the behavior of the ResetOldSession function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, ResetOldSession, TestSize.Level1)
{
    constexpr uint32_t sessionId = 1234;
    HdcSession mySession;
    HdcUART myUART;
    mySession.hUART = &myUART;

    EXPECT_CALL(mockDaemon, AdminSession(OP_QUERY, sessionId, nullptr))
        .Times(1)
        .WillOnce(Return(&mySession));
    EXPECT_CALL(mockDaemon, FreeSession).Times(1);
    mockDaemonUART.ResetOldSession(sessionId);
    EXPECT_EQ(mySession.hUART->resetIO, true);
    mySession.hUART->resetIO = false;

    EXPECT_CALL(mockDaemon, AdminSession(OP_QUERY, sessionId, nullptr))
        .Times(1)
        .WillOnce(Return(nullptr));
    mockDaemonUART.ResetOldSession(sessionId);
    EXPECT_EQ(mySession.hUART->resetIO, false);
}

/*
 * @tc.name: OnTransferError
 * @tc.desc: Check the behavior of the OnTransferError function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, OnTransferError, TestSize.Level1)
{
    HdcSession mySession;
    HdcUART myUART;
    mySession.hUART = &myUART;
    EXPECT_CALL(mockDaemon, FreeSession).Times(1);
    mockDaemonUART.OnTransferError(&mySession);
}

/*
 * @tc.name: GetSession
 * @tc.desc: Check the behavior of the GetSession function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, GetSession, TestSize.Level1)
{
    constexpr uint32_t sessionId = 1234;
    HdcSession mySession;
    HdcUART myUART;
    mySession.hUART = &myUART;
    EXPECT_CALL(mockDaemon, AdminSession(OP_QUERY, sessionId, nullptr))
        .Times(1)
        .WillOnce(Return(&mySession));
    mockDaemonUART.GetSession(sessionId, false);
}

/*
 * @tc.name: OnNewHandshakeOK
 * @tc.desc: Check the behavior of the OnNewHandshakeOK function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, OnNewHandshakeOK, TestSize.Level1)
{
    uint32_t sessionId = 1234;
    mockDaemonUART.OnNewHandshakeOK(sessionId);
    EXPECT_EQ(mockDaemonUART.currentSessionId, sessionId);

    uint32_t rndId = rnd() % 100;
    mockDaemonUART.OnNewHandshakeOK(rndId);
    EXPECT_EQ(mockDaemonUART.currentSessionId, rndId);
}

/*
 * @tc.name: Stop
 * @tc.desc: Check the behavior of the Stop function
 * @tc.type: FUNC
 */
HWTEST_F(HdcDaemonUARTTest, Stop, TestSize.Level1)
{
    mockDaemonUART.checkSerialPort.data = this;
    EXPECT_CALL(mockInterface, TryCloseHandle(reinterpret_cast<const uv_handle_t *>(
                                                  &mockDaemonUART.checkSerialPort),
                                              nullptr))
        .Times(1);
    EXPECT_CALL(mockDaemonUART, CloseUartDevice).Times(1);
    mockDaemonUART.Stop();
}
} // namespace Hdc
