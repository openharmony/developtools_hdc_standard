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
#ifndef HDC_CLIENT_H
#define HDC_CLIENT_H
#include "host_common.h"

namespace Hdc {
class HdcClient : public HdcChannelBase {
public:
    HdcClient(const bool serverOrClient, const string &addrString, uv_loop_t *loopMainIn);
    virtual ~HdcClient();
    int Initial(const string &connectKeyIn);
    int ExecuteCommand(const string &commandIn);
    int CtrlServiceWork(const char *commandIn);

protected:
private:
    static void DoCtrlServiceWork(uv_check_t *handle);
    static void Connect(uv_connect_t *connection, int status);
    int ConnectServerForClient(const char *stringIP, uint16_t port);
    void BindLocalStd();
    int ReadChannel(HChannel hChannel, uint8_t *buf, const int bytesIO);
    void BindLocalStd(HChannel hChannel);
    static void AllocStdbuf(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf);
    static void ReadStd(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    static void CommandWorker(uv_timer_t *handle);
    string AutoConnectKey(string &doCommand, const string &preConnectKey) const;
    uint32_t GetLastPID();
    bool StartKillServer(const char *cmd, bool startOrKill);
    void ModifyTty(bool setOrRestore, uv_tty_t *tty);
    int PreHandshake(HChannel hChannel, const uint8_t *buf);

#ifndef _WIN32
    termios terminalState;
#endif

    HChannel channel;
    string connectKey;
    uint16_t debugRetryCount;
    uv_timer_t waitTimeDoCmd;
    uv_check_t ctrlServerWork;
    string command;
    bool bShellInteractive = false;
};
}  // namespace Hdc
#endif