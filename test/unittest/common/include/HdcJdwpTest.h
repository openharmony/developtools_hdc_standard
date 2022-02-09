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

#ifndef HDC_JDWP_TEST_H
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "jdwp.h"
#include "session.h"
namespace Hdc {
class HdcJdwpTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();
    void TestInitjdwp();
    std::unique_ptr<HdcJdwp> InstanceHdcJdwp();

private:
};

class HdcSessionTest : public Hdc::HdcSessionBase {
public:
    bool ServerCommand(const uint32_t sessionId, const uint32_t channelId, const uint16_t command,
                       uint8_t *bufPtr, const int size)
    {
        return false;
    };
};
} // namespace Hdc
#endif // HDC_JDWP_TEST_H