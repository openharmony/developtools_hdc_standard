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
#ifndef HDC_JDWP_H
#define HDC_JDWP_H
#include "daemon_common.h"

namespace Hdc {
class HdcJdwp {
public:
    HdcJdwp(uv_loop_t *loopIn);
    virtual ~HdcJdwp();
    int Initial();
    void Stop();
    bool ReadyForRelease();

    string GetProcessList();
    bool SendJdwpNewFD(uint32_t targetPID, int fd);
    bool CheckPIDExist(uint32_t targetPID);

private:
    struct ContextJdwp {
        uint32_t pid;
        uv_pipe_t pipe;
        HdcJdwp *thisClass;
        bool finish;
        char buf[sizeof(uint32_t)];
        uint8_t dummy;
        uv_tcp_t jvmTCP;
    };
    using HCtxJdwp = struct ContextJdwp *;

    bool JdwpListen();
    static void AcceptClient(uv_stream_t *server, int status);
    static void ReadStream(uv_stream_t *pipe, ssize_t nread, const uv_buf_t *buf);
    static void SendCallbackJdwpNewFD(uv_write_t *req, int status);
    void *MallocContext();
    void FreeContext(HCtxJdwp ctx);
    void *AdminContext(const uint8_t op, const uint32_t pid, HCtxJdwp ctxJdwp);

    uv_loop_t *loop;
    uv_pipe_t listenPipe;
    uint32_t refCount;
    map<uint32_t, HCtxJdwp> mapCtxJdwp;
    uv_rwlock_t lockMapContext;
};
}  // namespace Hdc
#endif