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
#include "hdc_runtime_command.h"
#include "hdc_runtime_frame.h"
#include "ut_common.h"
using namespace Hdc;

namespace HdcTest {
// Base module test
TEST(HdcBaseFunction, HandleNoneZeroInput)
{
    char bufString[256] = "";
    uint16_t num;
    int argc = 0;
    char **argv = nullptr;

    GTEST_ASSERT_LE(1, Base::GetRuntimeMSec());
    GTEST_ASSERT_LE(10, Base::GetRandomNum(10, 12));
    GTEST_ASSERT_EQ(0, Base::ConnectKey2IPPort("127.0.0.1:8080", bufString, &num));

    Base::SplitCommandToArgs("xx p1 p2 p3", &argc);
    GTEST_ASSERT_EQ(4, argc);
}

// Task-shell
TEST(HdcShellMod, HandleNoneZeroInput)
{
    FrameRuntime *ftest = new FrameRuntime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_SHELL));
    delete ftest;

    // Add interactive shell cases if ready
}

// Basic support command test
TEST(HdcBaseCommand, HandleNoneZeroInput)
{
    FrameRuntime *ftest = new FrameRuntime();
    GTEST_ASSERT_EQ(true, ftest->Initial(false));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_BASE));
    delete ftest;
}

// File Transfer Command Test
TEST(HdcFileCommand, HandleNoneZeroInput)
{
    FrameRuntime *ftest = new FrameRuntime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_FILE));
    delete ftest;
}

// Task-shellFunction point test
TEST(HdcShellMod_Test, HandleNoneZeroInput)
{
    WRITE_LOG(LOG_INFO, "Begincheck Shell execute");
    FrameRuntime *ftest = new FrameRuntime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_SHELL));
    delete ftest;
}
}  // namespace HdcTest

int main(int argc, const char *argv[])
{
#ifdef DEBUG_PROTOCOL
    HdcTest::DdmCallCommandEntry(argc, argv);
    return 0;
#endif
    int ret = 0;
    // many feature under Win32 UT is not supported, so we cannot support it when unit test
#ifdef _WIN32
    printf("Not support platform\r\n");
    return 0;
#endif
    testing::InitGoogleTest(&argc, (char **)argv);
    ret = RUN_ALL_TESTS();
    WRITE_LOG(LOG_INFO, "Test all finish");
    return ret;
}