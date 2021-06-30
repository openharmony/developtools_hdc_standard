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
#ifndef HDC_TCP_H
#define HDC_TCP_H
#include "common.h"

namespace Hdc {
class HdcTCPBase {
public:
    HdcTCPBase(const bool serverOrDaemonIn, void *ptrMainBase);
    virtual ~HdcTCPBase();
    static void ReadStream(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf);

protected:
    virtual void RecvUDPEntry(const sockaddr *addrSrc, uv_udp_t *handle, const uv_buf_t *rcvbuf)
    {
    }
    static void RecvUDP(
        uv_udp_t *handle, ssize_t nread, const uv_buf_t *rcvbuf, const struct sockaddr *addr, unsigned flags);
    static void SendUDPFinish(uv_udp_send_t *req, int status);
    static void AllocStreamUDP(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf);

    void *clsMainBase;
    bool serverOrDaemon;

private:
    void InitialChildClass(const bool serverOrDaemonIn, void *ptrMainBase);
};
}  // namespace Hdc

#endif