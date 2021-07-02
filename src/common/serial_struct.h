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
#ifndef HDC_SERIAL_STRUCT_H
#define HDC_SERIAL_STRUCT_H
#include "common.h"
#include "serial_struct_define.h"
#include "transfer.h"

namespace Hdc {
namespace SerialStruct {
    template<> struct Descriptor<Hdc::HdcTransferBase::TransferConfig> {
        static auto type()
        {
            return Message(Field<1, &Hdc::HdcTransferBase::TransferConfig::fileSize>("fileSize"),
                           Field<2, &Hdc::HdcTransferBase::TransferConfig::atime>("atime"),
                           Field<3, &Hdc::HdcTransferBase::TransferConfig::mtime>("mtime"),
                           Field<4, &Hdc::HdcTransferBase::TransferConfig::options>("options"),
                           Field<5, &Hdc::HdcTransferBase::TransferConfig::path>("path"),
                           Field<6, &Hdc::HdcTransferBase::TransferConfig::optionalName>("optionalName"),
                           Field<7, &Hdc::HdcTransferBase::TransferConfig::updateIfNew>("updateIfNew"),
                           Field<8, &Hdc::HdcTransferBase::TransferConfig::compressType>("compressType"),
                           Field<9, &Hdc::HdcTransferBase::TransferConfig::holdTimestamp>("holdTimestamp"),
                           Field<10, &Hdc::HdcTransferBase::TransferConfig::functionName>("functionName"));
        }
    };

    template<> struct Descriptor<Hdc::HdcTransferBase::TransferPayload> {
        static auto type()
        {
            return Message(Field<1, &Hdc::HdcTransferBase::TransferPayload::index>("index"),
                           Field<2, &Hdc::HdcTransferBase::TransferPayload::compressType>("compressType"),
                           Field<3, &Hdc::HdcTransferBase::TransferPayload::compressSize>("compressSize"),
                           Field<4, &Hdc::HdcTransferBase::TransferPayload::uncompressSize>("uncompressSize"));
        }
    };

    template<> struct Descriptor<Hdc::HdcSessionBase::SessionHandShake> {
        static auto type()
        {
            return Message(Field<1, &Hdc::HdcSessionBase::SessionHandShake::banner>("banner"),
                           Field<2, &Hdc::HdcSessionBase::SessionHandShake::authType>("authType"),
                           Field<3, &Hdc::HdcSessionBase::SessionHandShake::sessionId>("sessionId"),
                           Field<4, &Hdc::HdcSessionBase::SessionHandShake::connectKey>("connectKey"),
                           Field<5, &Hdc::HdcSessionBase::SessionHandShake::buf>("buf"));
        }
    };

    template<> struct Descriptor<Hdc::HdcSessionBase::PayloadProtect> {
        static auto type()
        {
            return Message(Field<1, &Hdc::HdcSessionBase::PayloadProtect::channelId>("channelId"),
                           Field<2, &Hdc::HdcSessionBase::PayloadProtect::commandFlag>("commandFlag"),
                           Field<3, &Hdc::HdcSessionBase::PayloadProtect::checkSum>("checkSum"),
                           Field<4, &Hdc::HdcSessionBase::PayloadProtect::vCode>("vCode"));
        }
    };
}  // SerialStruct
}  // Hdc
#endif  // HDC_SERIAL_STRUCT_H
