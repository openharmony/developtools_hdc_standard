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

#include "uart_test.h"

#include <random>

using namespace testing::ext;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using namespace testing;

namespace Hdc {
class HdcUARTBaseTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
    std::default_random_engine rnd;

    bool MakeData(std::vector<uint8_t> &data, UartHead &head);
    bool MakeRndData(std::vector<uint8_t> &data, uint32_t sessionId);
    bool MakeDemoData(std::vector<uint8_t> &data, uint32_t sessionId);

    static constexpr uint32_t serverId = 1235;
    static constexpr uint32_t daemonSessionId = 1236;
    static constexpr uint32_t packageIndex = 1237;
    std::unique_ptr<HdcUART> serverHdcUart;
    std::unique_ptr<HdcSession> server;
    std::unique_ptr<HdcUART> daemonHdcUart;
    std::unique_ptr<HdcSession> daemon;
    const std::string testString = "HDC_UART_TEST";

    class MockHdcSessionBase : public HdcSessionBase {
        explicit MockHdcSessionBase(bool serverOrDaemon) : HdcSessionBase(serverOrDaemon) {};
        MOCK_METHOD1(FreeSession, void(const uint32_t));
    } mockSessionBase;

    class MockBaseInterface : public ExternInterface {
    public:
        std::vector<uint8_t> expectUserData;

        MOCK_METHOD3(UvTcpInit, int(uv_loop_t *, uv_tcp_t *, int));
        MOCK_METHOD3(UvRead, int(uv_stream_t *, uv_alloc_cb, uv_read_cb));
        MOCK_METHOD3(SendToStream, int(uv_stream_t *, const uint8_t *, const int));
    } mockInterface;

    // this mock use to test SendUARTBlock
    // it will check from SendUARTRaw for data format and content
    class MockHdcUARTBase : public HdcUARTBase {
    public:
        std::vector<uint8_t> expectRawData;
        MOCK_METHOD2(SendUartSoftReset, void(HSession, uint32_t));
        MOCK_METHOD1(ResetOldSession, void(uint32_t));
        MOCK_METHOD3(RequestSendPackage, void(uint8_t *, const size_t, bool));
        MOCK_METHOD1(ProcessResponsePackage, void(const UartHead &));
        MOCK_METHOD3(ResponseUartTrans, void(uint32_t, uint32_t, UartProtocolOption));
        MOCK_METHOD3(UartToHdcProtocol, int(uv_stream_t *, uint8_t *, int));
        MOCK_METHOD1(OnTransferError, void(const HSession));
        MOCK_METHOD2(GetSession, HSession(uint32_t, bool));
        MOCK_METHOD1(ClearUARTOutMap, void(uint32_t));

        MockHdcUARTBase(HdcSessionBase &mockSessionBaseIn, MockBaseInterface &interfaceIn)
            : HdcUARTBase(mockSessionBaseIn, interfaceIn) {};
    } mockUARTBase;
#if HDC_HOST
    static constexpr bool serverOrDaemon = true;
#else
    static constexpr bool serverOrDaemon = false;
#endif
    HdcUARTBaseTest()
        : mockSessionBase(serverOrDaemon), mockUARTBase(mockSessionBase, mockInterface)
    {
    }
    const std::vector<size_t> testPackageSize = {
        0u,
        1u,
        MAX_UART_SIZE_IOBUF - 1u,
        MAX_UART_SIZE_IOBUF,
        MAX_UART_SIZE_IOBUF + 1u,
        MAX_UART_SIZE_IOBUF * 2u - 1u,
        MAX_UART_SIZE_IOBUF * 2u,
        MAX_UART_SIZE_IOBUF * 2u + 1u,
        MAX_UART_SIZE_IOBUF * 3u + 1u,
        MAX_UART_SIZE_IOBUF * 4u + 1u,
    };
};

void HdcUARTBaseTest::SetUpTestCase()
{
#ifdef UT_DEBUG
    Hdc::Base::SetLogLevel(LOG_ALL);
#else
    Hdc::Base::SetLogLevel(LOG_OFF);
#endif
}

void HdcUARTBaseTest::TearDownTestCase() {}

void HdcUARTBaseTest::SetUp()
{
    serverHdcUart = std::make_unique<HdcUART>();
    server = std::make_unique<HdcSession>();
    server->serverOrDaemon = true;
    server->sessionId = serverId;
    server->hUART = serverHdcUart.get();

    daemonHdcUart = std::make_unique<HdcUART>();
    daemon = std::make_unique<HdcSession>();
    daemon->serverOrDaemon = false;
    daemon->sessionId = daemonSessionId;
    daemon->hUART = daemonHdcUart.get();

    mockInterface.expectUserData.clear();
}

void HdcUARTBaseTest::TearDown() {}

bool HdcUARTBaseTest::MakeRndData(std::vector<uint8_t> &data, uint32_t sessionId)
{
    UartHead head;
    head.option = PKG_OPTION_TAIL;
    head.sessionId = sessionId;
    head.packageIndex = packageIndex;

    if (data.empty()) {
        const int MaxTestBufSize = MAX_UART_SIZE_IOBUF * 2 + 2;
        data.resize(MaxTestBufSize);
    }
    const constexpr int mod = 100;
    std::generate(data.begin(), data.end(), [&]() { return rnd() % mod; });
    return MakeData(data, head);
}

bool HdcUARTBaseTest::MakeDemoData(std::vector<uint8_t> &data, uint32_t sessionId)
{
    UartHead head;
    head.option = PKG_OPTION_TAIL;
    head.sessionId = sessionId;
    head.packageIndex = packageIndex;

    data.resize(sizeof(UartHead) + testString.size());
    head.dataSize = testString.size();

    return MakeData(data, head);
}

bool HdcUARTBaseTest::MakeData(std::vector<uint8_t> &data, UartHead &head)
{
    // head
    if (memcpy_s(data.data(), data.size(), &head, sizeof(UartHead)) != EOK) {
        return false;
    }

    // data
    unsigned char *dataPtr = data.data() + sizeof(UartHead);
    size_t dataSize = data.size() - sizeof(UartHead);
    if (memcpy_s(dataPtr, dataSize, testString.data(), testString.size()) != EOK) {
        return false;
    }

    return true;
}

/*
 * @tc.name: SendUARTRaw
 * @tc.desc: Virtual function verification
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, SendUARTRaw, TestSize.Level1)
{
    HSession hSession = nullptr;
    unsigned char dummyData[] = "1234567980";
    uint8_t *dummyPtr = static_cast<unsigned char *>(&dummyData[0]);
    int dummySize = sizeof(dummyData);
    EXPECT_CALL(mockUARTBase, GetSession).Times(1);
    EXPECT_EQ(mockUARTBase.SendUARTRaw(hSession, dummyPtr, dummySize), 0);
}

/*
 * @tc.name: ResetOldSession
 * @tc.desc: Virtual function verification
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ResetOldSession, TestSize.Level1)
{
    EXPECT_CALL(mockUARTBase, ResetOldSession).WillRepeatedly([&](uint32_t sessionId) {
        mockUARTBase.HdcUARTBase::ResetOldSession(sessionId);
    });
    const uint32_t sessionId = 12345;
    EXPECT_CALL(mockUARTBase, ResetOldSession(sessionId)).Times(1);
    mockUARTBase.ResetOldSession(sessionId);
}

/*
 * @tc.name: SendUartSoftReset
 * @tc.desc: Virtual function verification
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, SendUartSoftReset, TestSize.Level1)
{
    EXPECT_CALL(mockUARTBase, SendUartSoftReset)
        .WillRepeatedly([&](HSession hUART, uint32_t sessionId) {
            mockUARTBase.HdcUARTBase::SendUartSoftReset(hUART, sessionId);
        });
    HSession hSession = nullptr;
    uint32_t sessionId = 1234567980;
    EXPECT_CALL(mockUARTBase, SendUartSoftReset(hSession, sessionId)).Times(1);
    mockUARTBase.SendUartSoftReset(hSession, sessionId);
}

/*
 * @tc.name: SendUARTBlock
 * @tc.desc: Check the data sub-package function, package content, header content.
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, SendUARTBlock, TestSize.Level1)
{
    std::vector<uint8_t> sourceData(testPackageSize.back());
    MakeRndData(sourceData, server->sessionId);
    ASSERT_GE(sourceData.size(), testPackageSize.back());
    for (size_t i = 0; i < testPackageSize.size(); i++) {
        size_t maxSendSize = MAX_UART_SIZE_IOBUF - sizeof(UartHead);
        int sendTimes =
            (testPackageSize[i] / maxSendSize) + (testPackageSize[i] % maxSendSize > 0 ? 1 : 0);
        if (testPackageSize[i] == 0) {
            sendTimes = 1; // we allow send empty package
        }
        const uint8_t *sourceDataPoint = sourceData.data();
        size_t sendOffset = 0;
        EXPECT_CALL(mockUARTBase, RequestSendPackage(_, Le(MAX_UART_SIZE_IOBUF), true))
            .Times(sendTimes)
            .WillRepeatedly(Invoke([&](uint8_t *data, const size_t length, bool queue) {
                // must big thean head
                ASSERT_GE(length, sizeof(UartHead));

                // check head
                const void *pHead = static_cast<const void *>(data);
                const UartHead *pUARTHead = static_cast<const UartHead *>(pHead);

                // magic check
                ASSERT_EQ(pUARTHead->flag[0], 'H');
                ASSERT_EQ(pUARTHead->flag[1], 'W');

                // sessionId always should this one
                ASSERT_EQ(pUARTHead->sessionId, server->sessionId);

                // check data size in head
                ASSERT_EQ(pUARTHead->dataSize, length - sizeof(UartHead));
                sendOffset += pUARTHead->dataSize;
                ASSERT_LE(sendOffset, testPackageSize[i]);

                // check data
                const uint8_t *pData = (data + sizeof(UartHead));

                // expectData_ only have data info . not include head
                for (uint32_t i = 0; i < pUARTHead->dataSize; i++) {
                    ASSERT_EQ(sourceDataPoint[i], pData[i]);
                }
                // after check ,move the pointer for next
                sourceDataPoint += pUARTHead->dataSize;

                printf("check %zu bytes\n", length);
            }));
        ASSERT_EQ(mockUARTBase.SendUARTData(server.get(), sourceData.data(), testPackageSize[i]),
                  static_cast<int>(testPackageSize[i]));
        ASSERT_EQ(sendOffset, testPackageSize[i]);
    }
}

/*
 * @tc.name: UartSendToHdcStream
 * @tc.desc: Check the behavior of the UartSendToHdcStream function
 *           buf does not reach the head length
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, UartSendToHdcStreamLessBuff, TestSize.Level1)
{
    std::vector<uint8_t> data;
    MakeRndData(data, server->sessionId);

    for (unsigned int i = 0; i < sizeof(UartHead); i++) {
        EXPECT_CALL(mockUARTBase, ResponseUartTrans).Times(0);
        ASSERT_EQ(mockUARTBase.UartSendToHdcStream(server.get(), data.data(), i), true);
    }
    for (unsigned int i = 0; i < sizeof(UartHead); i++) {
        EXPECT_CALL(mockUARTBase, ResponseUartTrans).Times(0);
        ASSERT_EQ(mockUARTBase.UartSendToHdcStream(daemon.get(), data.data(), i), true);
    }
}

/*
 * @tc.name: UartSendToHdcStream
 * @tc.desc: Check the behavior of the UartSendToHdcStream function
 *           magic head is not correct
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, UartSendToHdcStreamBadMagic, TestSize.Level1)
{
    std::vector<uint8_t> sourceData(testPackageSize.back());
    std::generate(sourceData.begin(), sourceData.end(), [&]() { return rnd() % 100; });
    for (size_t i = 0; i < testPackageSize.size() and testPackageSize[i] >= sizeof(UartHead); i++) {
        EXPECT_CALL(mockUARTBase, ResponseUartTrans(_, _, PKG_OPTION_NAK)).Times(1);
        ASSERT_EQ(
            mockUARTBase.UartSendToHdcStream(server.get(), sourceData.data(), testPackageSize[i]),
            false);
    }

    for (size_t i = 0; i < testPackageSize.size() and testPackageSize[i] >= sizeof(UartHead); i++) {
        EXPECT_CALL(mockUARTBase, ResponseUartTrans(_, _, PKG_OPTION_NAK))
            .Times(testPackageSize[i] > sizeof(UartHead) ? 1 : 0);
        ASSERT_EQ(
            mockUARTBase.UartSendToHdcStream(daemon.get(), sourceData.data(), testPackageSize[i]),
            false);
    }
}

/*
 * @tc.name: UartSendToHdcStream
 * @tc.desc: Check the behavior of the UartSendToHdcStream function
 *           head buffer merge multiple times
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, UartSendToHdcStreamAppend, TestSize.Level1)
{
    std::vector<uint8_t> data;
    ASSERT_TRUE(MakeDemoData(data, server->sessionId));

    // send head one by one
    for (unsigned int i = 0; i < sizeof(UartHead); i++) {
        ASSERT_TRUE(mockUARTBase.UartSendToHdcStream(server.get(), &data.data()[i],
                                                     sizeof(data.data()[i])));
    }

    // send content data  one by one
#if HDC_HOST
    EXPECT_CALL(mockUARTBase, UartToHdcProtocol).Times(0);
#else
    EXPECT_CALL(mockInterface, SendToStream).Times(0);
#endif
    for (unsigned int i = sizeof(UartHead); i < data.size(); i++) {
        ASSERT_TRUE(mockUARTBase.UartSendToHdcStream(server.get(), &data.data()[i],
                                                     sizeof(data.data()[i])));
        if (i + 1 == data.size()) {
            // if this is the last one , buf will clear after send
        } else {
        }
    }
}

/*
 * @tc.name: UartSendToHdcStream
 * @tc.desc: Check the behavior of the UartSendToHdcStream function
 *           soft reset when session id is not correct
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, UartSendToHdcStreamDiffSession, TestSize.Level1)
{
    bool sendResult = false;

    std::vector<uint8_t> data;
    ASSERT_TRUE(MakeDemoData(data, server->sessionId));

    std::vector<uint8_t> dataDiffSession;
    // here we make a server session
    uint32_t diffSessionId = server->sessionId + 1;
    ASSERT_TRUE(MakeDemoData(dataDiffSession, diffSessionId));

    // same session
    EXPECT_CALL(mockUARTBase, SendUartSoftReset(server.get(), server->sessionId)).Times(0);
    EXPECT_CALL(mockUARTBase, UartToHdcProtocol).Times(1);

    sendResult = mockUARTBase.UartSendToHdcStream(server.get(), data.data(), data.size());
    ASSERT_TRUE(sendResult);
    ASSERT_FALSE(server->hUART->resetIO);

    // diff session but not server serversession
    // SendUartSoftReset should only happend from server to daemon
    EXPECT_CALL(mockUARTBase, SendUartSoftReset(daemon.get(), server->sessionId)).Times(0);
    EXPECT_CALL(mockInterface, SendToStream).Times(0);
    sendResult = mockUARTBase.UartSendToHdcStream(daemon.get(), data.data(), data.size());
    ASSERT_TRUE(sendResult);
    ASSERT_FALSE(server->hUART->resetIO);

    // diff session should set reset resetIO to true
    // and also call SendUartSoftReset
    // here we use daemonSession_ with server_->sessionId (it's different)
    EXPECT_CALL(mockUARTBase, SendUartSoftReset(server.get(), diffSessionId)).Times(1);

    sendResult = mockUARTBase.UartSendToHdcStream(server.get(), dataDiffSession.data(),
                                                  dataDiffSession.size());
    ASSERT_FALSE(sendResult);
    ASSERT_TRUE(server->hUART->resetIO);
}
/*
 * @tc.name: ReadyForWorkThread
 * @tc.desc: Check the behavior of the ReadyForWorkThread function
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ReadyForWorkThread, TestSize.Level1)
{
    HdcSession session;
    HdcSessionBase sessionBase(true);
    session.classInstance = &sessionBase;
    auto loop = &session.childLoop;
    auto tcp = &session.dataPipe[STREAM_WORK];
    session.dataFd[STREAM_WORK] = rnd();
    auto socket = session.dataFd[STREAM_WORK];
    auto alloc = sessionBase.AllocCallback;
    auto cb = HdcUARTBase::ReadDataFromUARTStream;

    EXPECT_CALL(mockInterface, UvTcpInit(loop, tcp, socket)).Times(1);
    EXPECT_CALL(mockInterface, UvRead((uv_stream_t *)tcp, alloc, cb)).Times(1);
    EXPECT_EQ(mockUARTBase.ReadyForWorkThread(&session), true);

    EXPECT_CALL(mockInterface, UvTcpInit(loop, tcp, socket)).Times(1).WillOnce(Return(-1));
    EXPECT_CALL(mockInterface, UvRead).Times(0);
    EXPECT_EQ(mockUARTBase.ReadyForWorkThread(&session), false);

    EXPECT_CALL(mockInterface, UvTcpInit(loop, tcp, socket)).Times(1);
    EXPECT_CALL(mockInterface, UvRead).Times(1).WillOnce(Return(-1));
    EXPECT_EQ(mockUARTBase.ReadyForWorkThread(&session), false);
}

/*
 * @tc.name: ReadDataFromUARTStream
 * @tc.desc: Check the behavior of the ReadDataFromUARTStream function
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ReadDataFromUARTStream, TestSize.Level1)
{
    uv_stream_t uvStream;
    HdcSession hdcSession;
    HdcUART uart;
    constexpr uint32_t testSessionId = 0x1234;
    hdcSession.sessionId = testSessionId;
    uint8_t dummyArray[] = {1, 2, 3, 4};
    uart.streamSize = sizeof(dummyArray);
    uint8_t *dummPtr = dummyArray;
    ssize_t dummySize = sizeof(dummyArray);
    class MockHdcSessionBase : public HdcSessionBase {
    public:
        MOCK_METHOD3(FetchIOBuf, int(HSession, uint8_t *, int));
        explicit MockHdcSessionBase(bool server) : HdcSessionBase(server) {}
    } mockSession(true);
    hdcSession.classInstance = static_cast<void *>(&mockSession);
    hdcSession.classModule = &mockUARTBase;
    hdcSession.ioBuf = dummPtr;
    hdcSession.hUART = &uart;

    uvStream.data = static_cast<void *>(&hdcSession);

    EXPECT_CALL(mockSession, FetchIOBuf(&hdcSession, dummPtr, dummySize)).Times(1);
    HdcUARTBase::ReadDataFromUARTStream(&uvStream, dummySize, nullptr);

    uart.streamSize = sizeof(dummyArray);
    EXPECT_CALL(mockSession, FetchIOBuf(&hdcSession, dummPtr, dummySize))
        .Times(1)
        .WillOnce(Return(-1));
    EXPECT_CALL(mockUARTBase, ResponseUartTrans(_, _, PKG_OPTION_FREE)).Times(1);
    HdcUARTBase::ReadDataFromUARTStream(&uvStream, dummySize, nullptr);
}

/*
 * @tc.name: UartToHdcProtocol
 * @tc.desc: Check the behavior of the UartToHdcProtocol function
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, UartToHdcProtocol, TestSize.Level1)
{
    uv_stream_t stream;
    HdcSession session;
    const int MaxTestBufSize = MAX_UART_SIZE_IOBUF * 2 + 2;
    std::vector<uint8_t> data(MaxTestBufSize);
    std::generate(data.begin(), data.end(), [&]() { return rnd() % 100; });
    EXPECT_CALL(mockUARTBase, UartToHdcProtocol)
        .WillRepeatedly([&](uv_stream_t *stream, uint8_t *data, int dataSize) {
            return mockUARTBase.HdcUARTBase::UartToHdcProtocol(stream, data, dataSize);
        });

    stream.data = &session;

    // have socket
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, session.dataFd), 0);
    EXPECT_EQ(mockUARTBase.UartToHdcProtocol(&stream, data.data(), data.size()),
              signed(data.size()));
    std::string recvBuf;
    recvBuf.resize(data.size());
    read(session.dataFd[STREAM_WORK], recvBuf.data(), recvBuf.size());
    EXPECT_EQ(memcpy_s(recvBuf.data(), recvBuf.size(), data.data(), data.size()), 0);

    // close one of pair
    EXPECT_EQ(close(session.dataFd[STREAM_MAIN]), 0);
    EXPECT_EQ(mockUARTBase.UartToHdcProtocol(&stream, data.data(), data.size()), ERR_IO_FAIL);

    // close two of pair
    EXPECT_EQ(close(session.dataFd[STREAM_WORK]), 0);
    EXPECT_EQ(mockUARTBase.UartToHdcProtocol(&stream, data.data(), data.size()), ERR_IO_FAIL);
}

/*
 * @tc.name: ValidateUartPacket
 * @tc.desc: Check the behavior of the ValidateUartPacket function
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ValidateUartPacket, TestSize.Level1)
{
    uint32_t sessionId = 0;
    uint32_t packageIndex = 0;
    constexpr uint32_t sessionIdTest = 1234;
    constexpr uint32_t dataSizeTest = MAX_UART_SIZE_IOBUF / 2;
    constexpr uint32_t packageIndexTest = 123;
    size_t pkgLenth = 0;
    UartHead testHead;
    testHead.flag[0] = PACKET_FLAG.at(0);
    testHead.flag[1] = PACKET_FLAG.at(1);
    uint8_t *bufPtr = reinterpret_cast<uint8_t *>(&testHead);
    testHead.sessionId = sessionIdTest;
    testHead.dataSize = dataSizeTest;
    testHead.packageIndex = packageIndexTest;
    UartHead *headPointer = nullptr;

    std::vector<uint8_t> buffer(MAX_UART_SIZE_IOBUF * 3);

    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_CALL(mockUARTBase, ProcessResponsePackage).Times(AnyNumber());
    EXPECT_CALL(mockUARTBase, ResponseUartTrans).Times(AnyNumber());
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              RET_SUCCESS);

    testHead.flag[0] = PACKET_FLAG.at(0);
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              RET_SUCCESS);

    testHead.flag[1] = PACKET_FLAG.at(1);
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              RET_SUCCESS);
    EXPECT_EQ(sessionId, testHead.sessionId);
    EXPECT_EQ(pkgLenth, testHead.dataSize + sizeof(UartHead));

    testHead.dataSize = MAX_UART_SIZE_IOBUF * 2;
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    buffer.resize(testHead.dataSize + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              ERR_BUF_OVERFLOW);

    testHead.dataSize = MAX_UART_SIZE_IOBUF * 1;
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    buffer.resize(testHead.dataSize + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              ERR_BUF_OVERFLOW);

    testHead.dataSize = MAX_UART_SIZE_IOBUF / 2;
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    buffer.resize(testHead.dataSize + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              RET_SUCCESS);
    EXPECT_EQ(sessionId, testHead.sessionId);
    EXPECT_EQ(pkgLenth, testHead.dataSize + sizeof(UartHead));

    testHead.option = PKG_OPTION_RESET;
    testHead.dataSize = MAX_UART_SIZE_IOBUF - sizeof(UartHead);
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    buffer.resize(testHead.dataSize + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();

    EXPECT_CALL(mockUARTBase, ResetOldSession(sessionIdTest)).Times(1);
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              ERR_IO_SOFT_RESET);

    testHead.option = PKG_OPTION_ACK;
    testHead.dataSize = MAX_UART_SIZE_IOBUF - sizeof(UartHead);
    buffer.assign(bufPtr, bufPtr + sizeof(UartHead));
    buffer.resize(testHead.dataSize + sizeof(UartHead));
    headPointer = (UartHead *)buffer.data();
    headPointer->UpdateCheckSum();
    EXPECT_CALL(mockUARTBase, ProcessResponsePackage).Times(1);
    EXPECT_EQ(mockUARTBase.ValidateUartPacket(buffer, sessionId, packageIndex, pkgLenth),
              RET_SUCCESS);
}

/*
 * @tc.name: ExternInterface
 * @tc.desc: Too many free functions, forcing increased coverage , just check not crash or not
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ExternInterface, TestSize.Level1)
{
    ExternInterface defaultInterface;
    uv_loop_t dummyLoop;
    uv_tcp_t server;
    uv_pipe_t dummyPip;
    uv_loop_init(&dummyLoop);
    uv_pipe_init(uv_default_loop(), &dummyPip, 0);

    defaultInterface.SetTcpOptions(nullptr);
    EXPECT_NE(defaultInterface.SendToStream(nullptr, nullptr, 0), 0);
    EXPECT_NE(defaultInterface.UvTcpInit(uv_default_loop(), &server, -1), 0);
    EXPECT_NE(defaultInterface.UvRead((uv_stream_t *)&dummyPip, nullptr, nullptr), 0);
    EXPECT_EQ(defaultInterface.StartWorkThread(nullptr, nullptr, nullptr, nullptr), 0);
    EXPECT_NE(defaultInterface.TimerUvTask(uv_default_loop(), nullptr, nullptr), 0);
    EXPECT_NE(defaultInterface.UvTimerStart(nullptr, nullptr, 0, 0), 0);
    EXPECT_NE(defaultInterface.DelayDo(uv_default_loop(), 0, 0, "", nullptr, nullptr), 0);
    defaultInterface.TryCloseHandle((uv_handle_t *)&dummyPip, nullptr);
}

/*
 * @tc.name: GetUartSpeed
 * @tc.desc: Check the behavior of the GetUartSpeed function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, GetUartSpeed, TestSize.Level1)
{
    EXPECT_EQ(mockUARTBase.GetUartSpeed(UART_SPEED2400), B2400);
    EXPECT_EQ(mockUARTBase.GetUartSpeed(UART_SPEED4800), B4800);
    EXPECT_EQ(mockUARTBase.GetUartSpeed(UART_SPEED9600), B9600);
    EXPECT_EQ(mockUARTBase.GetUartSpeed(UART_SPEED115200), B115200);
    EXPECT_EQ(mockUARTBase.GetUartSpeed(UART_SPEED921600), B921600);
    EXPECT_EQ(mockUARTBase.GetUartSpeed(-1), B921600);
}

/*
 * @tc.name: GetUartBits
 * @tc.desc: Check the behavior of the GetUartSpeed function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, GetUartBits, TestSize.Level1)
{
    EXPECT_EQ(mockUARTBase.GetUartBits(UART_BIT1), CS7);
    EXPECT_EQ(mockUARTBase.GetUartBits(UART_BIT2), CS8);
    EXPECT_EQ(mockUARTBase.GetUartBits(-1), CS8);
}

/*
 * @tc.name: ToPkgIdentityString
 * @tc.desc: Check the behavior of the ToPkgIdentityString function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ToPkgIdentityString, TestSize.Level1)
{
    const uint32_t sessionId = 12345;
    const uint32_t packageIndex = 54321;

    UartHead head;
    head.sessionId = sessionId;
    head.packageIndex = packageIndex;
    EXPECT_STREQ(head.ToPkgIdentityString().c_str(), "Id:12345pkgIdx:54321");
    EXPECT_STREQ(head.ToPkgIdentityString(true).c_str(), "R-Id:12345pkgIdx:54321");
}

/*
 * @tc.name: HandleOutputPkg
 * @tc.desc: Check the behavior of the HandleOutputPkg  function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, HandleOutputPkg, TestSize.Level1)
{
    const uint32_t sessionId = 12345;
    HdcUARTBase::HandleOutputPkg testPackage("key", sessionId, nullptr, 0);
    testPackage.sendTimePoint = std::chrono::steady_clock::now();
    std::string debugString = testPackage.ToDebugString();
    EXPECT_THAT(debugString, HasSubstr("pkgStatus"));
    EXPECT_THAT(debugString, Not(HasSubstr("sent")));

    testPackage.pkgStatus = HdcUARTBase::PKG_WAIT_RESPONSE;
    debugString = testPackage.ToDebugString();
    EXPECT_THAT(debugString, HasSubstr("pkgStatus"));
    EXPECT_THAT(debugString, HasSubstr("sent"));

    testPackage.response = true;
    debugString = testPackage.ToDebugString();
    EXPECT_THAT(debugString, HasSubstr("pkgStatus"));
    EXPECT_THAT(debugString, HasSubstr("NAK"));

    testPackage.response = true;
    testPackage.ack = true;
    debugString = testPackage.ToDebugString();
    EXPECT_THAT(debugString, HasSubstr("pkgStatus"));
    EXPECT_THAT(debugString, HasSubstr("ACK"));
}

/*
 * @tc.name: TransferStateMachine
 * @tc.desc: Check the behavior of the TransferStateMachine function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, TransferStateMachine, TestSize.Level1)
{
    HdcUARTBase::TransferStateMachine tsm;
    // case 1 timeout
    tsm.Request();
    EXPECT_EQ(tsm.requested, true);
    EXPECT_EQ(tsm.timeout, false);
    tsm.Sent();
    EXPECT_EQ(tsm.requested, true);
    EXPECT_EQ(tsm.timeout, true);
    tsm.Wait(); // not timeout
    EXPECT_EQ(tsm.requested, false);
    EXPECT_EQ(tsm.timeout, true);
    tsm.Wait(); // wait again until timeout
    EXPECT_EQ(tsm.timeout, false);

    // case 2 not timeout
    tsm.Request();
    EXPECT_EQ(tsm.requested, true);
    EXPECT_EQ(tsm.timeout, false);
    tsm.Wait();
    EXPECT_EQ(tsm.requested, false);
    EXPECT_EQ(tsm.timeout, false);
}

/*
 * @tc.name: TransferSlot
 * @tc.desc: Check the behavior of the TransferSlot   function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, TransferSlot, TestSize.Level1)
{
    const uint32_t sessionId = 12345;
    HdcUARTBase::TransferSlot slot;
    slot.Free(sessionId);
    EXPECT_THAT(slot.hasWaitPkg, Not(Contains(sessionId)));
    slot.Wait(sessionId);
    EXPECT_THAT(slot.hasWaitPkg, Contains(sessionId));
    slot.WaitFree();
    EXPECT_THAT(slot.hasWaitPkg, Contains(sessionId));
}

/*
 * @tc.name: HandleOutputPkgKeyFinder
 * @tc.desc: Check the behavior of the HandleOutputPkgKeyFinder function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, HandleOutputPkgKeyFinder, TestSize.Level1)
{
    vector<HdcUARTBase::HandleOutputPkg> outPkgs; // Pkg label, HOutPkg
    outPkgs.emplace_back("A", 0, nullptr, 0);
    outPkgs.emplace_back("B", 0, nullptr, 0);
    EXPECT_NE(
        std::find_if(outPkgs.begin(), outPkgs.end(), HdcUARTBase::HandleOutputPkgKeyFinder("A")),
        outPkgs.end());
    EXPECT_NE(
        std::find_if(outPkgs.begin(), outPkgs.end(), HdcUARTBase::HandleOutputPkgKeyFinder("B")),
        outPkgs.end());
    EXPECT_EQ(
        std::find_if(outPkgs.begin(), outPkgs.end(), HdcUARTBase::HandleOutputPkgKeyFinder("C")),
        outPkgs.end());
}

/*
 * @tc.name: Restartession
 * @tc.desc: Check the behavior of the Restartession function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, StopSession, TestSize.Level1)
{
    const uint32_t sessionId = 12345;
    HdcSession session;
    session.sessionId = sessionId;
    EXPECT_CALL(mockUARTBase, ClearUARTOutMap(sessionId)).WillOnce(Return());
    EXPECT_CALL(mockSessionBase, FreeSession(sessionId)).WillOnce(Return());
    mockUARTBase.Restartession(&session);

    EXPECT_CALL(mockUARTBase, ClearUARTOutMap).Times(0);
    EXPECT_CALL(mockSessionBase, FreeSession).Times(0);
    mockUARTBase.Restartession(nullptr);
}

/*
 * @tc.name: StopSession
 * @tc.desc: Check the behavior of the Restartession function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, Restartession, TestSize.Level1)
{
    const uint32_t sessionId = 12345;
    HdcSession session;
    session.sessionId = sessionId;
    EXPECT_CALL(mockUARTBase, ClearUARTOutMap(sessionId)).WillOnce(Return());
    EXPECT_CALL(mockSessionBase, FreeSession).Times(0);
    mockUARTBase.StopSession(&session);
}

/*
 * @tc.name: ResponseUartTrans
 * @tc.desc: Check the behavior of the ResponseUartTrans function
 * successed
 * @tc.type: FUNC
 */
HWTEST_F(HdcUARTBaseTest, ResponseUartTrans, TestSize.Level1)
{
    EXPECT_CALL(mockUARTBase, ResponseUartTrans)
        .WillRepeatedly([&](uint32_t sessionId, uint32_t packageIndex, UartProtocolOption option) {
            return mockUARTBase.HdcUARTBase::ResponseUartTrans(sessionId, packageIndex, option);
        });
    const uint32_t sessionId = 12345;
    const uint32_t packageIndex = 54321;
    EXPECT_CALL(mockUARTBase, RequestSendPackage(_, sizeof(UartHead), false));
    mockUARTBase.ResponseUartTrans(sessionId, packageIndex, PKG_OPTION_FREE);
}
} // namespace Hdc
