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
#ifndef HDC_FORWARD_H
#define HDC_FORWARD_H
#include "common.h"

namespace Hdc {
class HdcForwardBase : public HdcTaskBase {
public:
    HdcForwardBase(HTaskInfo hTaskInfo);
    virtual ~HdcForwardBase();
    bool CommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);
    bool BeginForward(const char *command, string &sError);
    void StopTask();
    bool ReadyForRelease();

protected:
    enum FORWARD_TYPE {
        FORWARD_TCP,
        FORWARD_DEVICE,
        FORWARD_JDWP,
        FORWARD_ABSTRACT,
        FORWARD_RESERVED,
        FORWARD_FILESYSTEM,
    };
    struct ContextForward {
        FORWARD_TYPE type;
        bool masterSlave;
        bool checkPoint;
        bool ready;
        bool finish;
        int fd;
        uint32_t id;
        uv_tcp_t tcp;
        uv_pipe_t pipe;
        HdcFileDescriptor *fdClass;
        HdcForwardBase *thisClass;
        string path;
        string lastError;
        string localArgs[2];
        string remoteArgs[2];
        string remoteParamenters;
    };
    using HCtxForward = struct ContextForward *;
    struct ContextForwardIO {
        HCtxForward ctxForward;
        uint8_t *bufIO;
    };

    virtual bool SetupJdwpPoint(HCtxForward ctxPoint)
    {
        return false;
    }
    bool SetupPointContinue(HCtxForward ctx, int status);

private:
    static void ListenCallback(uv_stream_t *server, const int status);
    static void ConnectTarget(uv_connect_t *connection, int status);
    static void ReadForwardBuf(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
    static void AllocForwardBuf(uv_handle_t *handle, size_t sizeSuggested, uv_buf_t *buf);
    static void SendCallbackForwardBuf(uv_write_t *req, int status);
    static void OnFdRead(uv_fs_t *req);

    bool SetupPoint(HCtxForward ctxPoint);
    void *MallocContext(bool masterSlave);
    bool SlaveConnect(uint8_t *bufCmd, bool bCheckPoint, string &sError);
    bool SendToTask(const uint32_t cid, const uint16_t command, uint8_t *bufPtr, const int bufSize);
    bool FilterCommand(uint8_t *bufCmdIn, uint32_t *idOut, uint8_t **pContentBuf);
    void *AdminContext(const uint8_t op, const uint32_t id, HCtxForward hInput);
    bool DoForwardBegin(HCtxForward ctx);
    int SendForwardBuf(HCtxForward ctx, uint8_t *bufPtr, const int size);
    bool CheckNodeInfo(const char *nodeInfo, string as[2]);
    void FreeContext(HCtxForward ctxIn, const uint32_t id, bool bNotifyRemote);
    int LoopFdRead(HCtxForward ctx);
    void FreeContextCallBack(HCtxForward ctx);
    void FreeJDWP(HCtxForward ctx);
    void OnAccept(uv_stream_t *server, HCtxForward ctxClient, uv_stream_t *client);
    bool DetechForwardType(HCtxForward ctxPoint);
    bool SetupTCPPoint(HCtxForward ctxPoint);
    bool SetupDevicePoint(HCtxForward ctxPoint);
    bool SetupFilePoint(HCtxForward ctxPoint);
    bool ForwardCommandDispatch(const uint16_t command, uint8_t *payload, const int payloadSize);
    bool CommandForwardCheckResult(HCtxForward ctx, uint8_t *payload);
    bool LocalAbstractConnect(uv_pipe_t *pipe, string &sNodeCfg);

    map<uint32_t, HCtxForward> mapCtxPoint;
    string taskCommand;
    const uint8_t FORWARD_PARAMENTER_BUFSIZE = 8;
    const string FILESYSTEM_SOCKET_PREFIX = "/tmp/";
    const string HARMONY_RESERVED_SOCKET_PREFIX = "/dev/socket/";
    // set true to enable slave check when forward create
    const bool slaveCheckWhenBegin = false;
};
}  // namespace Hdc
#endif