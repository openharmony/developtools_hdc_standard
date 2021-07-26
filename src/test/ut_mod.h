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
#ifndef HDC_UT_MOD_H
#define HDC_UT_MOD_H
#include "ut_common.h"

namespace HdcTest {
bool TestBaseCommand(void *runtimePtr);
bool TestShellExecute(void *runtimePtr);
bool TestFileCommand(void *runtimePtr);
bool TestForwardCommand(void *runtimePtr);
bool TestAppCommand(void *runtimePtr);

}  // namespace HdcTest
#endif  // HDC_FUNC_TEST_H