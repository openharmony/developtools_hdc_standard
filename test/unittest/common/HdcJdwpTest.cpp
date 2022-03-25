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

#include <HdcJdwpTest.h>
using namespace testing::ext;
using namespace std;
namespace Hdc {
void HdcJdwpTest::SetUpTestCase() {}

void HdcJdwpTest::TearDownTestCase() {}

void HdcJdwpTest::SetUp() {}

void HdcJdwpTest::TearDown() {}

std::unique_ptr<HdcJdwp> HdcJdwpTest::InstanceHdcJdwp()
{
    uv_loop_t loopMain;
    uv_loop_init(&loopMain);
    // Base::SetLogLevel(Hdc::LOG_FULL);
    return std::make_unique<HdcJdwp>(&loopMain);
}

/*
 * @tc.name: TestCreateFdEventPoll
 * @tc.desc: Check the poll thread.
 * @tc.type: FUNC
 */
HWTEST_F(HdcJdwpTest, TestCreateFdEventPoll, TestSize.Level1)
{
    std::unique_ptr<HdcJdwp> mJdwpTest = HdcJdwpTest::InstanceHdcJdwp();
    ASSERT_NE(mJdwpTest, nullptr) << "Instanse HdcJdwp fail.";
    ASSERT_EQ(mJdwpTest->Initial(), 0) << " Jdwp init instance fail.";
    EXPECT_GE(mJdwpTest->pollNodeMap.size(), 0u) << " Jdwp pollNodeMap is null.";
    mJdwpTest->Stop();
}

/*
 * @tc.name: CreateJdwpTracker
 * @tc.desc: Check the poll thread.
 * @tc.type: FUNC
 */
HWTEST_F(HdcJdwpTest, TestCreateJdwpTracker, TestSize.Level1)
{
    std::unique_ptr<HdcJdwp> mJdwpTest = HdcJdwpTest::InstanceHdcJdwp();
    ASSERT_NE(mJdwpTest, nullptr) << "Instanse HdcJdwp fail.";
    ASSERT_EQ(mJdwpTest->Initial(), 0) << "Instanse HdcJdwp fail.";
    ASSERT_EQ(mJdwpTest->CreateJdwpTracker(nullptr), 0) << "CreateJdwpTracker nullptr fail.";
    HTaskInfo hTaskInfo = new TaskInformation();
    HdcSessionBase HdcSessionTest(false);
    hTaskInfo->ownerSessionClass = &HdcSessionTest;
    ASSERT_EQ(mJdwpTest->CreateJdwpTracker(hTaskInfo), 1) << "CreateJdwpTracker valid param fail.";
    delete hTaskInfo;
    mJdwpTest->Stop();
}

/*
 * @tc.name: TestReadStream
 * @tc.desc: Check ReadStream interface of pipe connection.
 * @tc.type: FUNC
 */
HWTEST_F(HdcJdwpTest, TestReadStream, TestSize.Level1)
{
    std::unique_ptr<HdcJdwp> mJdwpTest = HdcJdwpTest::InstanceHdcJdwp();
    mJdwpTest->Initial();
    uv_pipe_t pipe;
    uv_stream_t *stream = (uv_stream_t *)&pipe;
    HdcJdwp::HCtxJdwp ctxJdwp = (HdcJdwp::HCtxJdwp)mJdwpTest->MallocContext();
    ctxJdwp->finish = true; // For skip HdcJdwp::FreeContext, because the pipe
                            // connect hasn't actually established
    pipe.data = ctxJdwp;
    // invalid nread < min
    mJdwpTest->ReadStream(stream, (HdcJdwp::JS_PKG_MIN_SIZE - 1), nullptr);
    ASSERT_EQ(mJdwpTest->mapCtxJdwp.size(), 0u) << "Instanse HdcJdwp fail.";
    // invalid nread > max
    mJdwpTest->ReadStream(stream, (HdcJdwp::JS_PKG_MX_SIZE + 1), nullptr);
    ASSERT_EQ(mJdwpTest->mapCtxJdwp.size(), 0u) << "Instanse HdcJdwp fail.";

    // valid parameters
    string pkgName = "JDWP_UTEST";
    uint32_t pkgSize = pkgName.size() + sizeof(HdcJdwp::JsMsgHeader); // JsMsgHeader pkgName;
    uint8_t *info = reinterpret_cast<uint8_t *>(ctxJdwp->buf);
    ASSERT_NE(info, nullptr) << "TestReadStream info null.";
    memset_s(info, sizeof(ctxJdwp->buf), 0, sizeof(ctxJdwp->buf));
    HdcJdwp::JsMsgHeader *jsMsg = (HdcJdwp::JsMsgHeader *)info;
    jsMsg->pid = 1212;
    jsMsg->msgLen = pkgSize;
    ASSERT_EQ(
        memcpy_s(info + sizeof(HdcJdwp::JsMsgHeader), pkgName.size(), &pkgName[0], pkgName.size()),
        0)
        << "TestReadStream memcpy_s fail.";
    mJdwpTest->ReadStream(stream, pkgSize, nullptr);
    ASSERT_NE(mJdwpTest->AdminContext(OP_QUERY, 1212, nullptr), nullptr)
        << "TestReadStream query fail.";
    mJdwpTest->Stop();
}
} // namespace Hdc
