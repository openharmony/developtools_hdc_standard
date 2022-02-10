#!/bin/bash
# Copyright (c) 2021 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
set -e
DIR=$(dirname $(realpath ${BASH_SOURCE[0]}))
TOP=$(realpath $DIR/../../../../../..)
CLANG_DIR=$TOP/prebuilts/clang/host/linux-x86/clang-r353983c
if [ ! -e "$CLANG_DIR" ]; then
    CLANG_DIR=$TOP/prebuilts/clang/ohos/linux-x86_64/llvm
fi
$CLANG_DIR/bin/llvm-cov gcov $@