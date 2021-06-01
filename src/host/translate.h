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
#ifndef HDC_TRANSLATE_H
#define HDC_TRANSLATE_H
#include "host_common.h"

namespace Hdc {
namespace TranslateCommand {
    struct FormatCommand {
        uint16_t cmdFlag;
        string paraments;
        string inputRaw;
        bool bJumpDo;
    };

    string String2FormatCommand(const char *input, int sizeInput, FormatCommand *outCmd);
    string Usage();

    int LibraryCallEntryPoint(void *hwnd, void *hinst, uint16_t callMethod, char *lpcmdStringLine, int nCmdShow);
}
}
#endif