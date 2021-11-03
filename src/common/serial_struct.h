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
    constexpr int fieldOne = 1;
    constexpr int fieldTwo = 2;
    constexpr int fieldThree = 3;
    constexpr int fieldFour = 4;
    constexpr int fieldFive = 5;
    constexpr int fieldSix = 6;
    constexpr int fieldSeven = 7;
    constexpr int fieldEight = 8;
    constexpr int fieldNine = 9;
    constexpr int fieldTen = 10;
    constexpr int field11 = 11;
    constexpr int field12 = 12;
    constexpr int field13 = 13;

    template<> struct Descriptor<Hdc::HdcTransferBase::TransferConfig> {
        static auto type()
        {
            return Message(Field<fieldOne, &Hdc::HdcTransferBase::TransferConfig::fileSize>("fileSize"),
                           Field<fieldTwo, &Hdc::HdcTransferBase::TransferConfig::atime>("atime"),
                           Field<fieldThree, &Hdc::HdcTransferBase::TransferConfig::mtime>("mtime"),
                           Field<fieldFour, &Hdc::HdcTransferBase::TransferConfig::options>("options"),
                           Field<fieldFive, &Hdc::HdcTransferBase::TransferConfig::path>("path"),
                           Field<fieldSix, &Hdc::HdcTransferBase::TransferConfig::optionalName>("optionalName"),
                           Field<fieldSeven, &Hdc::HdcTransferBase::TransferConfig::updateIfNew>("updateIfNew"),
                           Field<fieldEight, &Hdc::HdcTransferBase::TransferConfig::compressType>("compressType"),
                           Field<fieldNine, &Hdc::HdcTransferBase::TransferConfig::holdTimestamp>("holdTimestamp"),
                           Field<fieldTen, &Hdc::HdcTransferBase::TransferConfig::functionName>("functionName"),
                           Field<field11, &Hdc::HdcTransferBase::TransferConfig::clientCwd>("clientCwd"),
                           Field<field12, &Hdc::HdcTransferBase::TransferConfig::reserve1>("reserve1"),
                           Field<field13, &Hdc::HdcTransferBase::TransferConfig::reserve2>("reserve2"));
        }
    };

    template<> struct Descriptor<Hdc::HdcTransferBase::TransferPayload> {
        static auto type()
        {
            return Message(Field<fieldOne, &Hdc::HdcTransferBase::TransferPayload::index>("index"),
                           Field<fieldTwo, &Hdc::HdcTransferBase::TransferPayload::compressType>("compressType"),
                           Field<fieldThree, &Hdc::HdcTransferBase::TransferPayload::compressSize>("compressSize"),
                           Field<fieldFour, &Hdc::HdcTransferBase::TransferPayload::uncompressSize>("uncompressSize"));
        }
    };

    template<> struct Descriptor<Hdc::HdcSessionBase::SessionHandShake> {
        static auto type()
        {
            return Message(Field<fieldOne, &Hdc::HdcSessionBase::SessionHandShake::banner>("banner"),
                           Field<fieldTwo, &Hdc::HdcSessionBase::SessionHandShake::authType>("authType"),
                           Field<fieldThree, &Hdc::HdcSessionBase::SessionHandShake::sessionId>("sessionId"),
                           Field<fieldFour, &Hdc::HdcSessionBase::SessionHandShake::connectKey>("connectKey"),
                           Field<fieldFive, &Hdc::HdcSessionBase::SessionHandShake::buf>("buf"));
        }
    };

    template<> struct Descriptor<Hdc::HdcSessionBase::PayloadProtect> {
        static auto type()
        {
            return Message(Field<fieldOne, &Hdc::HdcSessionBase::PayloadProtect::channelId>("channelId"),
                           Field<fieldTwo, &Hdc::HdcSessionBase::PayloadProtect::commandFlag>("commandFlag"),
                           Field<fieldThree, &Hdc::HdcSessionBase::PayloadProtect::checkSum>("checkSum"),
                           Field<fieldFour, &Hdc::HdcSessionBase::PayloadProtect::vCode>("vCode"));
        }
    };
}  // SerialStruct
}  // Hdc
#endif  // HDC_SERIAL_STRUCT_H
