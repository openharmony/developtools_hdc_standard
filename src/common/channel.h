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
#ifndef HDC_CHANNEL_H
#define HDC_CHANNEL_H
#include "common.h"

namespace Hdc {
class HdcChannelBase {
public:
    HdcChannelBase(const bool serverOrClient, const string &addrString, uv_loop_t *loopMainIn);
    virtual ~HdcChannelBase();
    HChannel AdminChannel(const uint8_t op, const uint32_t channelId, HChannel hInput);
    static void AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf);
    static void ReadStream(uv_stream_t *tcp, ssize_t nread, const uv_buf_t *buf);
    void PushAsyncMessage(const uint32_t channelId, const uint8_t method, const void *data, const int dataSize);
    void WorkerPendding();
    void FreeChannel(const uint32_t channelId);
    vector<uint8_t> GetChannelHandshake(string &connectKey) const;

protected:
    struct ChannelHandShake {
        char banner[12];  // must first index
        union {
            uint32_t channelId;
            char connectKey[24];
        };
    } __attribute__((packed));
    uint32_t MallocChannel(HChannel *hOutChannel);
    virtual int ReadChannel(HChannel hChannel, uint8_t *bufPtr, const int bytesIO)
    {
        return 0;
    }
    virtual void NotifyInstanceChannelFree(HChannel hChannel) {};
    void Send(const uint32_t channelId, uint8_t *bufPtr, const int size);

    string channelHostPort;
    string channelHost;
    uint16_t channelPort;
    uv_loop_t *loopMain;
    bool isServerOrClient;
    uv_rwlock_t mainAsync;
    uv_async_t asyncMainLoop;
    list<void *> lstMainThreadOP;
    uv_mutex_t freeChannel;

private:
    static void MainAsyncCallback(uv_async_t *handle);
    static void WriteCallback(uv_write_t *req, int status);
    static void AsyncMainLoopTask(uv_idle_t *handle);
    void ClearChannels();
    void FreeChannelContinue(HChannel hChannel);
    bool SetChannelTCPString(const string &addrString);

    uv_rwlock_t lockMapChannel;  // protect mapChannel
    map<uint32_t, HChannel> mapChannel;
};
}  // namespace Hdc

#endif  // HDC_CHANNEL_H