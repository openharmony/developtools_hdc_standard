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
#include "ut_common.h"
using namespace Hdc;

namespace HdcTest {
TEST(HdcBaseFunction, HandleNoneZeroInput)
{
    char bufString[256] = "";
    uint16_t num;
    int argc = 0;
    GTEST_ASSERT_LE(1, Base::GetRuntimeMSec());
    GTEST_ASSERT_LE(10, Base::GetRandomNum(10, 12));
    GTEST_ASSERT_EQ(0, Base::ConnectKey2IPPort("127.0.0.1:8080", bufString, &num));

    Base::SplitCommandToArgs("xx p1 p2 p3", &argc);
    GTEST_ASSERT_EQ(4, argc);
}

TEST(HdcBaseCommand, HandleNoneZeroInput)
{
    Runtime *ftest = new Runtime();
    GTEST_ASSERT_EQ(true, ftest->Initial(false));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_BASE));
    delete ftest;
}

TEST(HdcShellMod, HandleNoneZeroInput)
{
    Runtime *ftest = new Runtime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_SHELL));
    delete ftest;
}

TEST(HdcFileCommand, HandleNoneZeroInput)
{
    Runtime *ftest = new Runtime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_FILE));
    delete ftest;
}

TEST(HdcForwardCommand, HandleNoneZeroInput)
{
    Runtime *ftest = new Runtime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_FORWARD));
    delete ftest;
}

TEST(AppCommand, HandleNoneZeroInput)
{
    Runtime *ftest = new Runtime();
    GTEST_ASSERT_EQ(true, ftest->Initial(true));
    GTEST_ASSERT_EQ(true, ftest->CheckEntry(ftest->UT_MOD_APP));
    delete ftest;
}
}  // namespace HdcTest

int main(int argc, const char *argv[])
{
    int ret = 0;
    // many feature under Win32 UT is not supported, so we cannot support it when unit test
#ifdef _WIN32
    printf("Unit test not support win32 platform\r\n");
    return 0;
#else
    testing::InitGoogleTest(&argc, (char **)argv);
    ret = RUN_ALL_TESTS();
    WRITE_LOG(LOG_INFO, "Test all finish");
#endif
    return ret;
}