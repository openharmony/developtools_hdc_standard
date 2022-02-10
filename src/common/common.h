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
#ifndef HDC_COMMON_H
#define HDC_COMMON_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

using std::condition_variable;
using std::list;
using std::map;
using std::mutex;
using std::string;
using std::vector;

// clang-format off
#include <uv.h>  // libuv 1.35
#ifdef HDC_HOST

#ifdef HARMONY_PROJECT
#include <libusb/libusb.h>
#else  // NOT HARMONY_PROJECT
#include <libusb-1.0/libusb.h>
#endif // END HARMONY_PROJECT

#else // NOT HDC_HOST
#endif // HDC_HOST

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <securec.h>
#include <limits.h>

#include "define.h"
#include "debug.h"
#include "base.h"
#include "task.h"
#include "channel.h"
#include "session.h"
#include "auth.h"

#include "tcp.h"
#include "usb.h"
#ifdef HDC_SUPPORT_UART
#include "uart.h"
#endif
#include "file_descriptor.h"

// clang-format on

#endif  // !defined(COMMON_H_INCLUDED)
