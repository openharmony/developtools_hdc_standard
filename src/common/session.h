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
#ifndef HDC_SESSION_H
#define HDC_SESSION_H
#include "common.h"

namespace Hdc {
enum TaskType { TYPE_UNITY, TYPE_SHELL, TASK_FILE, TASK_FORWARD, TASK_APP };

class HdcSessionBase {
public:
    enum AuthType { AUTH_NONE, AUTH_TOKEN, AUTH_SIGNATURE, AUTH_PUBLICKEY, AUTH_OK };
    struct SessionHandShake {
        string banner;  // must first index
        // auth none
        uint8_t authType;
        uint32_t sessionId;
        string connectKey;
        string buf;
    };
    struct CtrlStruct {
        InnerCtrlCommand command;
        uint32_t channelId;
        uint8_t dataSize;
        uint8_t data[BUF_SIZE_MICRO];
    };
    struct PayloadProtect {  // reserve for encrypt and decrypt
        uint32_t channelId;
        uint32_t commandFlag;
        uint8_t checkSum;  // enable it will be lose about 20% speed
        uint8_t vCode;
    };

    HdcSessionBase(bool serverOrDaemonIn);
    virtual ~HdcSessionBase();
    virtual void AttachChannel(HSession hSession, const uint32_t channelId)
    {
    }
    virtual void DeatchChannel(HSession hSession, const uint32_t channelId)
    {
    }
    virtual void NotifyInstanceSessionFree(HSession hSession, bool freeOrClear)
    {
    }
    virtual bool RedirectToTask(HTaskInfo hTaskInfo, HSession hSession, const uint32_t channelId,
                                const uint16_t command, uint8_t *payload, const int payloadSize)
    {
        return true;
    }
    // Thread security interface for global stop programs
    void PostStopInstanceMessage(bool restart = false);
    void ReMainLoopForInstanceClear();
    // server, Two parameters in front of call can be empty
    void LogMsg(const uint32_t sessionId, const uint32_t channelId, MessageLevel level, const char *msg, ...);
    static void AllocCallback(uv_handle_t *handle, size_t sizeWanted, uv_buf_t *buf);
    static void MainAsyncCallback(uv_async_t *handle);
    static void FinishWriteSessionTCP(uv_write_t *req, int status);
    static void SessionWorkThread(uv_work_t *arg);
    static void ReadCtrlFromMain(uv_stream_t *uvpipe, ssize_t nread, const uv_buf_t *buf);
    static void ReadCtrlFromSession(uv_stream_t *uvpipe, ssize_t nread, const uv_buf_t *buf);
    HSession QueryUSBDeviceRegister(void *pDev, int busIDIn, int devIDIn);
    HSession MallocSession(bool serverOrDaemon, const ConnType connType, void *classModule, uint32_t sessionId = 0);
    void FreeSession(const uint32_t sessionId);
    void WorkerPendding();
    int OnRead(HSession hSession, uint8_t *bufPtr, const int bufLen);
    int Send(const uint32_t sessionId, const uint32_t channelId, const uint16_t commandFlag, const uint8_t *data,
             const int dataSize);
    int SendByProtocol(HSession hSession, uint8_t *bufPtr, const int bufLen);
    HSession AdminSession(const uint8_t op, const uint32_t sessionId, HSession hInput);
    int FetchIOBuf(HSession hSession, uint8_t *ioBuf, int read);
    void PushAsyncMessage(const uint32_t sessionId, const uint8_t method, const void *data, const int dataSize);
    HTaskInfo AdminTask(const uint8_t op, HSession hSession, const uint32_t channelId, HTaskInfo hInput);
    bool DispatchTaskData(HSession hSession, const uint32_t channelId, const uint16_t command, uint8_t *payload,
                          int payloadSize);
    void EnumUSBDeviceRegister(void (*pCallBack)(HSession hSession));
    void ClearOwnTasks(HSession hSession, const uint32_t channelIDInput);
    virtual bool FetchCommand(HSession hSession, const uint32_t channelId, const uint16_t command, uint8_t *payload,
                              int payloadSize)
    {
        return true;
    }
    virtual bool ServerCommand(const uint32_t sessionId, const uint32_t channelId, const uint16_t command,
                               uint8_t *bufPtr, const int size)
    {
        return true;
    }
    virtual bool RemoveInstanceTask(const uint8_t op, HTaskInfo hTask)
    {
        return true;
    }
    bool WantRestart()
    {
        return wantRestart;
    }
    static vector<uint8_t> BuildCtrlString(InnerCtrlCommand command, uint32_t channelId, uint8_t *data, int dataSize);
    uv_loop_t loopMain;
    bool serverOrDaemon;
    uv_async_t asyncMainLoop;
    uv_rwlock_t mainAsync;
    list<void *> lstMainThreadOP;
    void *ctxUSB;

protected:
    struct PayloadHead {
        uint8_t flag[2];
        uint8_t reserve[2];  // encrypt'flag or others options
        uint8_t protocolVer;
        uint16_t headSize;
        uint32_t dataSize;
    } __attribute__((packed));
    void ClearSessions();
    virtual void JdwpNewFileDescriptor(const uint8_t *buf, const int bytesIO)
    {
    }
    // must be define in haderfile, cannot in cpp file
    template<class T>
    bool TaskCommandDispatch(HTaskInfo hTaskInfo, uint8_t taskType, const uint16_t command, uint8_t *payload,
                             const int payloadSize)
    {
        bool ret = true;
        T *ptrTask = nullptr;
        if (!hTaskInfo->hasInitial) {
            hTaskInfo->taskType = taskType;
            ptrTask = new T(hTaskInfo);
            hTaskInfo->hasInitial = true;
            hTaskInfo->taskClass = ptrTask;
        } else {
            ptrTask = (T *)hTaskInfo->taskClass;
        }
        if (!ptrTask->CommandDispatch(command, payload, payloadSize)) {
            ptrTask->TaskFinish();
        }
        return ret;
    }
    template<class T> bool DoTaskRemove(HTaskInfo hTaskInfo, const uint8_t op)
    {
        T *ptrTask = (T *)hTaskInfo->taskClass;
        if (OP_CLEAR == op) {
            ptrTask->StopTask();
        } else if (OP_REMOVE == op) {
            if (!ptrTask->ReadyForRelease()) {
                return false;
            }
            delete ptrTask;
        }
        return true;
    }
    bool wantRestart;

private:
    virtual void ClearInstanceResource()
    {
    }
    int DecryptPayload(HSession hSession, PayloadHead *payloadHeadBe, uint8_t *encBuf);
    bool DispatchMainThreadCommand(HSession hSession, const CtrlStruct *ctrl);
    bool DispatchSessionThreadCommand(uv_stream_t *uvpipe, HSession hSession, const uint8_t *baseBuf,
                                      const int bytesIO);
    bool BeginRemoveTask(HTaskInfo hTask);
    bool TryRemoveTask(HTaskInfo hTask);
    void ReChildLoopForSessionClear(HSession hSession);
    void FreeSessionContinue(HSession hSession);
    static void FreeSessionFinally(uv_idle_t *handle);
    static void AsyncMainLoopTask(uv_idle_t *handle);
    static void FreeSessionOpeate(uv_timer_t *handle);
    int MallocSessionByConnectType(HSession hSession);
    void FreeSessionByConnectType(HSession hSession);
    bool WorkThreadStartSession(HSession hSession);
    uint32_t GetSessionPseudoUid();
    bool NeedNewTaskInfo(const uint16_t command, bool &masterTask);

    map<uint32_t, HSession> mapSession;
    uv_rwlock_t lockMapSession;
    std::atomic<uint32_t> sessionRef = 0;
    const uint8_t payloadProtectStaticVcode = 0x09;
    uv_thread_t threadSessionMain;
};
}  // namespace Hdc
#endif