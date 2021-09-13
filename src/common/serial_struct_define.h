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
#ifndef HDC_SERIAL_STRUCT_DEFINE_H
#define HDC_SERIAL_STRUCT_DEFINE_H
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

// static file define. No need not modify. by zako
namespace Hdc {
// clang-format off
namespace SerialStruct {
    namespace SerialDetail {
        template<class MemPtrT> struct MemPtr {
        };
        template<class T, class U> struct MemPtr<U T::*> {
            using type = T;
            using MemberType = U;
        };
        template<class... Fields> struct MessageImpl {
        public:
            MessageImpl(Fields &&... fields)
                : _fields(std::move(fields)...)
            {
            }

            template<class Handler> void Visit(Handler &&handler) const
            {
                VisitImpl(std::forward<Handler>(handler), std::make_index_sequence<sizeof...(Fields)>());
            }

        private:
            std::tuple<Fields...> _fields;

            template<class Handler, size_t... I> void VisitImpl(Handler &&handler, std::index_sequence<I...>) const
            {
                (handler(std::get<I>(_fields)), ...);
            }
        };

        template<uint32_t Tag, class MemPtrT, MemPtrT MemPtr, uint32_t Flags> struct FieldImpl {
            using type = typename SerialDetail::MemPtr<MemPtrT>::type;
            using MemberType = typename SerialDetail::MemPtr<MemPtrT>::MemberType;
            constexpr static const uint32_t tag = Tag;
            constexpr static const uint32_t flags = Flags;
            const std::string field_name;

            static decltype(auto) get(const type &value)
            {
                return value.*MemPtr;
            }

            static decltype(auto) get(type &value)
            {
                return value.*MemPtr;
            }
        };

        template<uint32_t Tag, size_t Index, class MemPtrT, MemPtrT MemPtr, uint32_t Flags> struct OneofFieldImpl {
            using type = typename SerialDetail::MemPtr<MemPtrT>::type;
            using MemberType = typename SerialDetail::MemPtr<MemPtrT>::MemberType;
            constexpr static const uint32_t tag = Tag;
            constexpr static const size_t index = Index;
            constexpr static const uint32_t flags = Flags;
            const std::string field_name;

            static decltype(auto) get(const type &value)
            {
                return value.*MemPtr;
            }

            static decltype(auto) get(type &value)
            {
                return value.*MemPtr;
            }
        };

        template<uint32_t Tag, class MemPtrT, MemPtrT MemPtr, uint32_t KeyFlags, uint32_t ValueFlags>
        struct MapFieldImpl {
            using type = typename SerialDetail::MemPtr<MemPtrT>::type;
            using MemberType = typename SerialDetail::MemPtr<MemPtrT>::MemberType;
            constexpr static const uint32_t tag = Tag;
            constexpr static const uint32_t KEY_FLAGS = KeyFlags;
            constexpr static const uint32_t VALUE_FLAGS = ValueFlags;

            const std::string field_name;

            static decltype(auto) get(const type &value)
            {
                return value.*MemPtr;
            }

            static decltype(auto) get(type &value)
            {
                return value.*MemPtr;
            }
        };
    }

    enum class WireType : uint32_t {
        VARINT = 0,
        FIXED64 = 1,
        LENGTH_DELIMETED = 2,
        START_GROUP = 3,
        END_GROUP = 4,
        FIXED32 = 5,
    };
    enum flags { no = 0, s = 1, f = 2 };
    template<uint32_t flags = flags::no> struct FlagsType {
    };

    template<class T> struct Descriptor {
        static_assert(sizeof(T) == 0, "You need to implement descriptor for your own types");
        static void type()
        {
        }
    };

    template<class... Fields> constexpr auto Message(Fields &&... fields)
    {
        return SerialDetail::MessageImpl<Fields...>(std::forward<Fields>(fields)...);
    }

    template<uint32_t Tag, auto MemPtr, uint32_t Flags = flags::no> constexpr auto Field(const std::string &fieldName)
    {
        return SerialDetail::FieldImpl<Tag, decltype(MemPtr), MemPtr, Flags> { fieldName };
    }

    template<uint32_t Tag, size_t Index, auto MemPtr, uint32_t Flags = flags::no>
    constexpr auto OneofField(const std::string &fieldName)
    {
        return SerialDetail::OneofFieldImpl<Tag, Index, decltype(MemPtr), MemPtr, Flags> { fieldName };
    }

    template<uint32_t Tag, auto MemPtr, uint32_t KeyFlags = flags::no, uint32_t ValueFlags = flags::no>
    constexpr auto MapField(const std::string &fieldName)
    {
        return SerialDetail::MapFieldImpl<Tag, decltype(MemPtr), MemPtr, KeyFlags, ValueFlags> { fieldName };
    }

    template<class T> const auto &MessageType()
    {
        static const auto message = Descriptor<T>::type();
        return message;
    }

    template<class T, class Enable = void> struct Serializer;

    struct Writer {
        virtual void Write(const void *bytes, size_t size) = 0;
    };

    struct reader {
        virtual size_t Read(void *bytes, size_t size) = 0;
    };

    namespace SerialDetail {
        template<class T, class V, class F, class W, class Enable = void>
        struct HasSerializePacked : public std::false_type {
        };

        template<class T, class V, class F, class W>
        struct HasSerializePacked<T, V, F, W,
            std::void_t<decltype(std::declval<T>().SerializePacked(
                std::declval<V>(), std::declval<F>(), std::declval<W &>()))>> : public std::true_type {
        };

        template<class T, class V, class F, class W>
        constexpr bool HAS_SERIALIZE_PACKED_V = HasSerializePacked<T, V, F, W>::value;

        template<class T, class V, class F, class R, class Enable = void>
        struct HasParsePacked : public std::false_type {
        };

        template<class T, class V, class F, class R>
        struct HasParsePacked<T, V, F, R,
            std::void_t<decltype(std::declval<T>().ParsePacked(
                std::declval<V &>(), std::declval<F>(), std::declval<R &>()))>> : public std::true_type {
        };

        template<class T, class V, class F, class R>
        constexpr bool HAS_PARSE_PACKED_V = HasParsePacked<T, V, F, R>::value;

        static uint32_t MakeTagWireType(uint32_t tag, WireType wireType)
        {
            return (tag << 3) | static_cast<uint32_t>(wireType);
        }

        static inline void ReadTagWireType(uint32_t tagKey, uint32_t &tag, WireType &wireType)
        {
            wireType = static_cast<WireType>(tagKey & 0b0111);
            tag = tagKey >> 3;
        }

        static uint32_t MakeZigzagValue(int32_t value)
        {
            return (static_cast<uint32_t>(value) << 1) ^ static_cast<uint32_t>(value >> 31);
        }

        static uint64_t MakeZigzagValue(int64_t value)
        {
            return (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(value >> 63);
        }

        static int32_t ReadZigzagValue(uint32_t value)
        {
            return static_cast<int32_t>((value >> 1) ^ (~(value & 1) + 1));
        }

        static int64_t ReadZigzagValue(uint64_t value)
        {
            return static_cast<int64_t>((value >> 1) ^ (~(value & 1) + 1));
        }

        template<class To, class From> To BitCast(From from)
        {
            static_assert(sizeof(To) == sizeof(From), "");
            static_assert(std::is_trivially_copyable_v<To>, "");
            static_assert(std::is_trivially_copyable_v<From>, "");
            To to;
            memcpy_s(&to, sizeof(To), &from, sizeof(from));
            return to;
        }

        struct WriterSizeCollector : public Writer {
            void Write(const void *, size_t size) override
            {
                byte_size += size;
            }
            size_t byte_size = 0;
        };

        struct LimitedReader : public reader {
            LimitedReader(reader &parent, size_t sizeLimit)
                : _parent(parent), _size_limit(sizeLimit)
            {
            }

            size_t Read(void *bytes, size_t size)
            {
                auto sizeToRead = std::min(size, _size_limit);
                auto readSize = _parent.Read(bytes, sizeToRead);
                _size_limit -= readSize;
                return readSize;
            }

            size_t AvailableBytes() const
            {
                return _size_limit;
            }

        private:
            reader &_parent;
            size_t _size_limit;
        };

        static bool ReadByte(uint8_t &value, reader &in)
        {
            return in.Read(&value, 1) == 1;
        }

        static void WriteVarint(uint32_t value, Writer &out)
        {
            uint8_t b[5] {};
            for (size_t i = 0; i < 5; ++i) {
                b[i] = value & 0b0111'1111;
                value >>= 7;
                if (value) {
                    b[i] |= 0b1000'0000;
                } else {
                    out.Write(b, i + 1);
                    break;
                }
            }
        }

        static void WriteVarint(uint64_t value, Writer &out)
        {
            uint8_t b[10] {};
            for (size_t i = 0; i < 10; ++i) {
                b[i] = value & 0b0111'1111;
                value >>= 7;
                if (value) {
                    b[i] |= 0b1000'0000;
                } else {
                    out.Write(b, i + 1);
                    break;
                }
            }
        }

#if defined(HOST_MAC)
        static void WriteVarint(unsigned long value, Writer &out)
        {
            WriteVarint((uint64_t)value, out);
        }
#endif

        static bool ReadVarint(uint32_t &value, reader &in)
        {
            value = 0;
            for (size_t c = 0; c < 5; ++c) {
                uint8_t x;
                if (!ReadByte(x, in)) {
                    return false;
                }
                value |= static_cast<uint32_t>(x & 0b0111'1111) << 7 * c;
                if (!(x & 0b1000'0000)) {
                    return true;
                }
            }

            return false;
        }

        static bool ReadVarint(uint64_t &value, reader &in)
        {
            value &= 0;
            for (size_t c = 0; c < 10; ++c) {
                uint8_t x;
                if (!ReadByte(x, in)) {
                    return false;
                }
                value |= static_cast<uint64_t>(x & 0b0111'1111) << 7 * c;
                if (!(x & 0b1000'0000)) {
                    return true;
                }
            }
            return false;
        }

#if defined(HOST_MAC)
        static bool ReadVarint(unsigned long &value, reader &in)
        {
            return ReadVarint(value, in);
        }
#endif

        static void WriteFixed(uint32_t value, Writer &out)
        {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            out.Write(&value, sizeof(value));
#else
            static_assert(false, "Not a little-endian");
#endif
        }

        static void WriteFixed(uint64_t value, Writer &out)
        {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            out.Write(&value, sizeof(value));
#else
            static_assert(false, "Not a little-endian");
#endif
        }

        static void WriteFixed(double value, Writer &out)
        {
            WriteFixed(BitCast<uint64_t>(value), out);
        }

        static void WriteFixed(float value, Writer &out)
        {
            WriteFixed(BitCast<uint32_t>(value), out);
        }

        static void WriteVarint(int32_t value, Writer &out)
        {
            WriteVarint(BitCast<uint32_t>(value), out);
        }

        static void WriteVarint(int64_t value, Writer &out)
        {
            WriteVarint(BitCast<uint64_t>(value), out);
        }

        static void WriteSignedVarint(int32_t value, Writer &out)
        {
            WriteVarint(MakeZigzagValue(value), out);
        }

        static void WriteSignedVarint(int64_t value, Writer &out)
        {
            WriteVarint(MakeZigzagValue(value), out);
        }

        static void WriteSignedFixed(int32_t value, Writer &out)
        {
            WriteFixed(static_cast<uint32_t>(value), out);
        }

        static void WriteSignedFixed(int64_t value, Writer &out)
        {
            WriteFixed(static_cast<uint64_t>(value), out);
        }

        static void WriteTagWriteType(uint32_t tag, WireType wireType, Writer &out)
        {
            WriteVarint(MakeTagWireType(tag, wireType), out);
        }

        static bool ReadFixed(uint32_t &value, reader &in)
        {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            return in.Read(&value, sizeof(value)) == sizeof(value);
#else
            static_assert(false, "Not a little-endian");
#endif
        }

        static bool ReadFixed(uint64_t &value, reader &in)
        {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            return in.Read(&value, sizeof(value)) == sizeof(value);
#else
            static_assert(false, "Not a little-endian");
#endif
        }

        static bool ReadFixed(double &value, reader &in)
        {
            uint64_t intermediateValue;
            if (ReadFixed(intermediateValue, in)) {
                value = BitCast<double>(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadFixed(float &value, reader &in)
        {
            uint32_t intermediateValue;
            if (ReadFixed(intermediateValue, in)) {
                value = BitCast<float>(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadVarint(int32_t &value, reader &in)
        {
            uint32_t intermediateValue;
            if (ReadVarint(intermediateValue, in)) {
                value = BitCast<int32_t>(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadVarint(int64_t &value, reader &in)
        {
            uint64_t intermediateValue;
            if (ReadVarint(intermediateValue, in)) {
                value = BitCast<int64_t>(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadSignedVarint(int32_t &value, reader &in)
        {
            uint32_t intermediateValue;
            if (ReadVarint(intermediateValue, in)) {
                value = ReadZigzagValue(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadSignedVarint(int64_t &value, reader &in)
        {
            uint64_t intermediateValue;
            if (ReadVarint(intermediateValue, in)) {
                value = ReadZigzagValue(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadSignedFixed(int32_t &value, reader &in)
        {
            uint32_t intermediateValue;
            if (ReadFixed(intermediateValue, in)) {
                value = static_cast<int64_t>(intermediateValue);
                return true;
            }
            return false;
        }

        static bool ReadSignedFixed(int64_t &value, reader &in)
        {
            uint64_t intermediateValue;
            if (ReadFixed(intermediateValue, in)) {
                value = static_cast<int64_t>(intermediateValue);
                return true;
            }
            return false;
        }

        template<class T, uint32_t Tag, size_t Index, class MemPtrT, MemPtrT MemPtr, uint32_t Flags>
        void WriteField(const T &value,
            const SerialDetail::OneofFieldImpl<Tag, Index, MemPtrT, MemPtr, Flags> &, Writer &out)
        {
            using OneOf = SerialDetail::OneofFieldImpl<Tag, Index, MemPtrT, MemPtr, Flags>;
            Serializer<typename OneOf::MemberType>::template SerializeOneof<OneOf::index>(
                OneOf::tag, OneOf::get(value), FlagsType<OneOf::flags>(), out);
        }

        template<class T, uint32_t Tag, class MemPtrT, MemPtrT MemPtr, uint32_t KeyFlags, uint32_t ValueFlags>
        void WriteField(const T &value,
            const SerialDetail::MapFieldImpl<Tag, MemPtrT, MemPtr, KeyFlags, ValueFlags> &, Writer &out)
        {
            using Map = SerialDetail::MapFieldImpl<Tag, MemPtrT, MemPtr, KeyFlags, ValueFlags>;
            Serializer<typename Map::MemberType>::SerializeMap(
                Map::tag, Map::get(value), FlagsType<Map::KEY_FLAGS>(), FlagsType<Map::VALUE_FLAGS>(), out);
        }

        template<class T, uint32_t Tag, class MemPtrT, MemPtrT MemPtr, uint32_t Flags>
        void WriteField(const T &value, const SerialDetail::FieldImpl<Tag, MemPtrT, MemPtr, Flags> &, Writer &out)
        {
            using Field = SerialDetail::FieldImpl<Tag, MemPtrT, MemPtr, Flags>;
            Serializer<typename Field::MemberType>::Serialize(
                Field::tag, Field::get(value), FlagsType<Field::flags>(), out);
        }

        template<class T, class... Field>
        void WriteMessage(const T &value, const SerialDetail::MessageImpl<Field...> &message, Writer &out)
        {
            message.Visit([&](const auto &field) { WriteField(value, field, out); });
        }

        template<uint32_t Flags, class ValueType, class It>
        void WriteRepeated(uint32_t tag, It begin, It end, Writer &out)
        {
            if (begin == end) {
                return;
            }
            if constexpr (SerialDetail::HAS_SERIALIZE_PACKED_V<Serializer<ValueType>, ValueType, FlagsType<Flags>,
                Writer>) {
                WriteVarint(MakeTagWireType(tag, WireType::LENGTH_DELIMETED), out);
                WriterSizeCollector sizeCollector;
                for (auto it = begin; it != end; ++it) {
                    Serializer<ValueType>::SerializePacked(*it, FlagsType<Flags> {}, sizeCollector);
                }
                WriteVarint(sizeCollector.byte_size, out);
                for (auto it = begin; it != end; ++it) {
                    Serializer<ValueType>::SerializePacked(*it, FlagsType<Flags> {}, out);
                }
            } else {
                for (auto it = begin; it != end; ++it) {
                    Serializer<ValueType>::Serialize(tag, *it, FlagsType<Flags>(), out);
                }
            }
        }

        template<uint32_t KeyFlags, uint32_t ValueFlags, class Key, class Value>
        void WriteMapKeyValue(const std::pair<const Key, Value> &value, Writer &out)
        {
            Serializer<Key>::Serialize(1, value.first, FlagsType<KeyFlags> {}, out, true);
            Serializer<Value>::Serialize(2, value.second, FlagsType<ValueFlags> {}, out, true);
        }

        template<uint32_t KeyFlags, uint32_t ValueFlags, class T>
        void WriteMap(uint32_t tag, const T &value, Writer &out)
        {
            auto begin = std::begin(value);
            auto end = std::end(value);

            for (auto it = begin; it != end; ++it) {
                WriteTagWriteType(tag, WireType::LENGTH_DELIMETED, out);
                WriterSizeCollector sizeCollector;
                WriteMapKeyValue<KeyFlags, ValueFlags>(*it, sizeCollector);
                WriteVarint(sizeCollector.byte_size, out);
                WriteMapKeyValue<KeyFlags, ValueFlags>(*it, out);
            }
        }

        template<uint32_t KeyFlags, uint32_t ValueFlags, class Key, class Value>
        bool ReadMapKeyValue(std::pair<Key, Value> &value, reader &in)
        {
            static const auto pairAsMessage = Message(Field<1, &std::pair<Key, Value>::first, KeyFlags>("key"),
                Field<2, &std::pair<Key, Value>::second, ValueFlags>("value"));
            return ReadMessage(value, pairAsMessage, in);
        }

        template<uint32_t KeyFlags, uint32_t ValueFlags, class T>
        bool ReadMap(WireType wireType, T &value, reader &in)
        {
            if (wireType != WireType::LENGTH_DELIMETED) {
                return false;
            }
            size_t size;
            if (ReadVarint(size, in)) {
                LimitedReader limitedIn(in, size);
                while (limitedIn.AvailableBytes() > 0) {
                    std::pair<typename T::key_type, typename T::mapped_type> item;
                    if (!ReadMapKeyValue<KeyFlags, ValueFlags>(item, limitedIn)) {
                        return false;
                    }
                    value.insert(std::move(item));
                }
                return true;
            }
            return false;
        }

        template<uint32_t Flags, class ValueType, class OutputIt>
        bool ReadRepeated(WireType wireType, OutputIt output_it, reader &in)
        {
            if constexpr (SerialDetail::HAS_PARSE_PACKED_V<Serializer<ValueType>, ValueType, FlagsType<Flags>,
                reader>) {
                if (wireType != WireType::LENGTH_DELIMETED) {
                    return false;
                }

                size_t size;
                if (ReadVarint(size, in)) {
                    LimitedReader limitedIn(in, size);

                    while (limitedIn.AvailableBytes() > 0) {
                        ValueType value;
                        if (!Serializer<ValueType>::ParsePacked(value, FlagsType<Flags>(), limitedIn)) {
                            return false;
                        }
                        output_it = value;
                        ++output_it;
                    }
                    return true;
                }
                return false;
            } else {
                ValueType value;
                if (Serializer<ValueType>::Parse(wireType, value, FlagsType<Flags>(), in)) {
                    output_it = value;
                    ++output_it;
                    return true;
                }
                return false;
            }
        }

        template<class T, uint32_t Tag, size_t Index, class MemPtrT, MemPtrT MemPtr, uint32_t Flags>
        void ReadField(T &value, uint32_t tag, WireType wireType,
            const SerialDetail::OneofFieldImpl<Tag, Index, MemPtrT, MemPtr, Flags> &, reader &in)
        {
            if (Tag != tag) {
                return;
            }
            using OneOf = SerialDetail::OneofFieldImpl<Tag, Index, MemPtrT, MemPtr, Flags>;
            Serializer<typename OneOf::MemberType>::template ParseOneof<OneOf::index>(
                wireType, OneOf::get(value), FlagsType<OneOf::flags>(), in);
        }

        template<class T, uint32_t Tag, class MemPtrT, MemPtrT MemPtr, uint32_t KeyFlags, uint32_t ValueFlags>
        void ReadField(T &value, uint32_t tag, WireType wireType,
            const SerialDetail::MapFieldImpl<Tag, MemPtrT, MemPtr, KeyFlags, ValueFlags> &, reader &in)
        {
            if (Tag != tag) {
                return;
            }
            using Map = SerialDetail::MapFieldImpl<Tag, MemPtrT, MemPtr, KeyFlags, ValueFlags>;
            Serializer<typename Map::MemberType>::ParseMap(
                wireType, Map::get(value), FlagsType<Map::KEY_FLAGS>(), FlagsType<Map::VALUE_FLAGS>(), in);
        }

        template<class T, uint32_t Tag, class MemPtrT, MemPtrT MemPtr, uint32_t Flags>
        void ReadField(T &value, uint32_t tag, WireType wireType,
            const SerialDetail::FieldImpl<Tag, MemPtrT, MemPtr, Flags> &, reader &in)
        {
            if (Tag != tag) {
                return;
            }
            using Field = SerialDetail::FieldImpl<Tag, MemPtrT, MemPtr, Flags>;
            Serializer<typename Field::MemberType>::Parse(wireType, Field::get(value), FlagsType<Field::flags>(), in);
        }

        template<class T, class... Field> bool ReadMessage(T &value, const MessageImpl<Field...> &message, reader &in)
        {
            uint32_t tagKey;
            while (ReadVarint(tagKey, in)) {
                uint32_t tag;
                WireType wireType;
                ReadTagWireType(tagKey, tag, wireType);
                message.Visit([&](const auto &field) { ReadField(value, tag, wireType, field, in); });
            }
            return true;
        }
    }

    template<class T, class Enable> struct Serializer {
        // Commion Serializer threat type as Message
        static void Serialize(uint32_t tag, const T &value, FlagsType<>, Writer &out, bool force = false)
        {
            SerialDetail::WriterSizeCollector sizeCollector;
            SerialDetail::WriteMessage(value, MessageType<T>(), sizeCollector);
            if (!force && sizeCollector.byte_size == 0) {
                return;
            }
            SerialDetail::WriteTagWriteType(tag, WireType::LENGTH_DELIMETED, out);
            SerialDetail::WriteVarint(sizeCollector.byte_size, out);
            SerialDetail::WriteMessage(value, MessageType<T>(), out);
        }

        static bool Parse(WireType wireType, T &value, FlagsType<>, reader &in)
        {
            if (wireType != WireType::LENGTH_DELIMETED) {
                return false;
            }
            size_t size;
            if (SerialDetail::ReadVarint(size, in)) {
                SerialDetail::LimitedReader limitedIn(in, size);
                return SerialDetail::ReadMessage(value, MessageType<T>(), limitedIn);
            }
            return false;
        }
    };

    template<> struct Serializer<int32_t> {
        static void Serialize(uint32_t tag, int32_t value, FlagsType<>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::VARINT, out);
            SerialDetail::WriteVarint(value, out);
        }

        static void Serialize(uint32_t tag, int32_t value, FlagsType<flags::s>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::VARINT, out);
            SerialDetail::WriteSignedVarint(value, out);
        }

        static void Serialize(
            uint32_t tag, int32_t value, FlagsType<flags::s | flags::f>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::FIXED32, out);
            SerialDetail::WriteSignedFixed(value, out);
        }

        static void SerializePacked(int32_t value, FlagsType<>, Writer &out)
        {
            SerialDetail::WriteVarint(value, out);
        }

        static void SerializePacked(int32_t value, FlagsType<flags::s>, Writer &out)
        {
            SerialDetail::WriteSignedVarint(value, out);
        }

        static void SerializePacked(int32_t value, FlagsType<flags::s | flags::f>, Writer &out)
        {
            SerialDetail::WriteSignedFixed(value, out);
        }

        static bool Parse(WireType wire_type, int32_t &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::VARINT)
                return false;
            return SerialDetail::ReadVarint(value, in);
        }

        static bool Parse(WireType wire_type, int32_t &value, FlagsType<flags::s>, reader &in)
        {
            if (wire_type != WireType::VARINT)
                return false;
            return SerialDetail::ReadSignedVarint(value, in);
        }

        static bool Parse(WireType wire_type, int32_t &value, FlagsType<flags::s | flags::f>, reader &in)
        {
            if (wire_type != WireType::FIXED32)
                return false;
            return SerialDetail::ReadSignedFixed(value, in);
        }

        static bool ParsePacked(int32_t &value, FlagsType<>, reader &in)
        {
            return SerialDetail::ReadVarint(value, in);
        }

        static bool ParsePacked(int32_t &value, FlagsType<flags::s>, reader &in)
        {
            return SerialDetail::ReadSignedVarint(value, in);
        }

        static bool ParsePacked(int32_t &value, FlagsType<flags::s | flags::f>, reader &in)
        {
            return SerialDetail::ReadSignedFixed(value, in);
        }
    };

    template<> struct Serializer<uint32_t> {
        static void Serialize(uint32_t tag, uint32_t value, FlagsType<>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::VARINT, out);
            SerialDetail::WriteVarint(value, out);
        }

        static void Serialize(uint32_t tag, uint32_t value, FlagsType<flags::f>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::FIXED32, out);
            SerialDetail::WriteFixed(value, out);
        }

        static void SerializePacked(uint32_t value, FlagsType<>, Writer &out)
        {
            SerialDetail::WriteVarint(value, out);
        }

        static void SerializePacked(uint32_t value, FlagsType<flags::f>, Writer &out)
        {
            SerialDetail::WriteFixed(value, out);
        }

        static bool Parse(WireType wire_type, uint32_t &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::VARINT)
                return false;
            return SerialDetail::ReadVarint(value, in);
        }

        static bool Parse(WireType wire_type, uint32_t &value, FlagsType<flags::f>, reader &in)
        {
            if (wire_type != WireType::FIXED32)
                return false;
            return SerialDetail::ReadFixed(value, in);
        }

        static bool ParsePacked(uint32_t &value, FlagsType<>, reader &in)
        {
            return SerialDetail::ReadVarint(value, in);
        }

        static bool ParsePacked(uint32_t &value, FlagsType<flags::f>, reader &in)
        {
            return SerialDetail::ReadFixed(value, in);
        }
    };

    template<> struct Serializer<int64_t> {
        static void Serialize(uint32_t tag, int64_t value, FlagsType<>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::VARINT, out);
            SerialDetail::WriteVarint(value, out);
        }

        static void Serialize(uint32_t tag, int64_t value, FlagsType<flags::s>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::VARINT, out);
            SerialDetail::WriteSignedVarint(value, out);
        }

        static void Serialize(
            uint32_t tag, int64_t value, FlagsType<flags::s | flags::f>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::FIXED64, out);
            SerialDetail::WriteSignedFixed(value, out);
        }

        static void SerializePacked(int64_t value, FlagsType<>, Writer &out)
        {
            SerialDetail::WriteVarint(value, out);
        }

        static void SerializePacked(int64_t value, FlagsType<flags::s>, Writer &out)
        {
            SerialDetail::WriteSignedVarint(value, out);
        }

        static void SerializePacked(int64_t value, FlagsType<flags::s | flags::f>, Writer &out)
        {
            SerialDetail::WriteSignedFixed(value, out);
        }

        static bool Parse(WireType wire_type, int64_t &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::VARINT)
                return false;
            return SerialDetail::ReadVarint(value, in);
        }

        static bool Parse(WireType wire_type, int64_t &value, FlagsType<flags::s>, reader &in)
        {
            if (wire_type != WireType::VARINT)
                return false;
            return SerialDetail::ReadSignedVarint(value, in);
        }

        static bool Parse(WireType wire_type, int64_t &value, FlagsType<flags::s | flags::f>, reader &in)
        {
            if (wire_type != WireType::FIXED64)
                return false;
            return SerialDetail::ReadSignedFixed(value, in);
        }

        static bool ParsePacked(int64_t &value, FlagsType<>, reader &in)
        {
            return SerialDetail::ReadVarint(value, in);
        }

        static bool ParsePacked(int64_t &value, FlagsType<flags::s>, reader &in)
        {
            return SerialDetail::ReadSignedVarint(value, in);
        }

        static bool ParsePacked(int64_t &value, FlagsType<flags::s | flags::f>, reader &in)
        {
            return SerialDetail::ReadSignedFixed(value, in);
        }
    };

    template<> struct Serializer<uint64_t> {
        static void Serialize(uint32_t tag, uint64_t value, FlagsType<>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::VARINT, out);
            SerialDetail::WriteVarint(value, out);
        }

        static void Serialize(uint32_t tag, uint64_t value, FlagsType<flags::f>, Writer &out, bool force = false)
        {
            if (!force && value == UINT64_C(0))
                return;

            SerialDetail::WriteTagWriteType(tag, WireType::FIXED64, out);
            SerialDetail::WriteFixed(value, out);
        }

        static void SerializePacked(uint64_t value, FlagsType<>, Writer &out)
        {
            SerialDetail::WriteVarint(value, out);
        }

        static void SerializePacked(uint64_t value, FlagsType<flags::f>, Writer &out)
        {
            SerialDetail::WriteFixed(value, out);
        }

        static bool Parse(WireType wire_type, uint64_t &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::VARINT)
                return false;
            return SerialDetail::ReadVarint(value, in);
        }

        static bool Parse(WireType wire_type, uint64_t &value, FlagsType<flags::f>, reader &in)
        {
            if (wire_type != WireType::FIXED64)
                return false;
            return SerialDetail::ReadFixed(value, in);
        }

        static bool ParsePacked(uint64_t &value, FlagsType<>, reader &in)
        {
            return SerialDetail::ReadVarint(value, in);
        }

        static bool ParsePacked(uint64_t &value, FlagsType<flags::f>, reader &in)
        {
            return SerialDetail::ReadFixed(value, in);
        }
    };

    template<> struct Serializer<double> {
        static void Serialize(uint32_t tag, double value, FlagsType<>, Writer &out, bool force = false)
        {
            if (!force && std::fpclassify(value) == FP_ZERO) {
                return;
            }
            SerialDetail::WriteTagWriteType(tag, WireType::FIXED64, out);
            SerialDetail::WriteFixed(value, out);
        }

        static void SerializePacked(double value, FlagsType<>, Writer &out)
        {
            SerialDetail::WriteFixed(value, out);
        }

        static bool Parse(WireType wire_type, double &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::FIXED64) {
                return false;
            }
            return SerialDetail::ReadFixed(value, in);
        }

        static bool ParsePacked(double &value, FlagsType<>, reader &in)
        {
            return SerialDetail::ReadFixed(value, in);
        }
    };

    template<> struct Serializer<float> {
        static void Serialize(uint32_t tag, float value, FlagsType<>, Writer &out, bool force = false)
        {
            if (!force && std::fpclassify(value) == FP_ZERO) {
                return;
            }
            SerialDetail::WriteTagWriteType(tag, WireType::FIXED32, out);
            SerialDetail::WriteFixed(value, out);
        }

        static void SerializePacked(float value, FlagsType<>, Writer &out)
        {
            SerialDetail::WriteFixed(value, out);
        }

        static bool Parse(WireType wire_type, float &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::FIXED32) {
                return false;
            }
            return SerialDetail::ReadFixed(value, in);
        }

        static bool ParsePacked(float &value, FlagsType<>, reader &in)
        {
            return SerialDetail::ReadFixed(value, in);
        }
    };

    template<> struct Serializer<bool> {
        static void Serialize(uint32_t tag, bool value, FlagsType<>, Writer &out, bool force = false)
        {
            Serializer<uint32_t>::Serialize(tag, value ? 1 : 0, FlagsType(), out, force);
        }

        static void SerializePacked(bool value, FlagsType<>, Writer &out)
        {
            Serializer<uint32_t>::SerializePacked(value ? 1 : 0, FlagsType(), out);
        }

        static bool Parse(WireType wire_type, bool &value, FlagsType<>, reader &in)
        {
            uint32_t intermedaite_value;
            if (Serializer<uint32_t>::Parse(wire_type, intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<bool>(intermedaite_value);
                return true;
            }
            return false;
        }

        static bool ParsePacked(bool &value, FlagsType<>, reader &in)
        {
            uint32_t intermedaite_value;
            if (Serializer<uint32_t>::ParsePacked(intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<bool>(intermedaite_value);
                return true;
            }
            return false;
        }
    };

    template<class T> struct Serializer<T, std::enable_if_t<std::is_enum_v<T>>> {
        using U = std::underlying_type_t<T>;

        static void Serialize(uint32_t tag, T value, FlagsType<>, Writer &out, bool force = false)
        {
            Serializer<U>::Serialize(tag, static_cast<U>(value), FlagsType<>(), out, force);
        }

        static void SerializePacked(T value, FlagsType<>, Writer &out)
        {
            Serializer<U>::SerializePacked(static_cast<U>(value), FlagsType<>(), out);
        }

        static bool Parse(WireType wire_type, T &value, FlagsType<>, reader &in)
        {
            U intermedaite_value;
            if (Serializer<U>::Parse(wire_type, intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<T>(intermedaite_value);
                return true;
            }
            return false;
        }

        static bool ParsePacked(T &value, FlagsType<>, reader &in)
        {
            U intermedaite_value;
            if (Serializer<U>::ParsePacked(intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<T>(intermedaite_value);
                return true;
            }
            return false;
        }
    };

    template<> struct Serializer<std::string> {
        static void Serialize(uint32_t tag, const std::string &value, FlagsType<>, Writer &out, bool force = false)
        {
            SerialDetail::WriteTagWriteType(tag, WireType::LENGTH_DELIMETED, out);
            SerialDetail::WriteVarint(value.size(), out);
            out.Write(value.data(), value.size());
        }

        static bool Parse(WireType wire_type, std::string &value, FlagsType<>, reader &in)
        {
            if (wire_type != WireType::LENGTH_DELIMETED) {
                return false;
            }
            size_t size;
            if (SerialDetail::ReadVarint(size, in)) {
                value.resize(size);
                if (in.Read(value.data(), size) == size) {
                    return true;
                }
            }
            return false;
        }
    };

    template<class T> struct Serializer<std::vector<T>> {
        template<uint32_t Flags>
        static void Serialize(uint32_t tag, const std::vector<T> &value, FlagsType<Flags>, Writer &out)
        {
            SerialDetail::WriteRepeated<Flags, T>(tag, value.begin(), value.end(), out);
        }

        template<uint32_t Flags>
        static bool Parse(WireType wire_type, std::vector<T> &value, FlagsType<Flags>, reader &in)
        {
            return SerialDetail::ReadRepeated<Flags, T>(wire_type, std::back_inserter(value), in);
        }
    };

    template<class T> struct Serializer<std::optional<T>> {
        template<uint32_t Flags>
        static void Serialize(uint32_t tag, const std::optional<T> &value, FlagsType<Flags>, Writer &out)
        {
            if (!value.has_value()) {
                return;
            }
            Serializer<T>::Serialize(tag, *value, FlagsType<Flags>(), out);
        }

        template<uint32_t Flags>
        static bool Parse(WireType wire_type, std::optional<T> &value, FlagsType<Flags>, reader &in)
        {
            return Serializer<T>::Parse(wire_type, value.emplace(), FlagsType<Flags>(), in);
        }
    };

    template<class... T> struct Serializer<std::variant<T...>> {
        template<size_t Index, uint32_t Flags>
        static void SerializeOneof(uint32_t tag, const std::variant<T...> &value, FlagsType<Flags>, Writer &out)
        {
            if (value.index() != Index)
                return;

            Serializer<std::variant_alternative_t<Index, std::variant<T...>>>::Serialize(
                tag, std::get<Index>(value), FlagsType<Flags>(), out);
        }

        template<size_t Index, uint32_t Flags>
        static bool ParseOneof(WireType wire_type, std::variant<T...> &value, FlagsType<Flags>, reader &in)
        {
            return Serializer<std::variant_alternative_t<Index, std::variant<T...>>>::Parse(
                wire_type, value.template emplace<Index>(), FlagsType<Flags>(), in);
        }
    };

    template<class Key, class Value> struct Serializer<std::map<Key, Value>> {
        template<uint32_t KeyFlags, uint32_t ValueFlags>
        static void SerializeMap(
            uint32_t tag, const std::map<Key, Value> &value, FlagsType<KeyFlags>, FlagsType<ValueFlags>, Writer &out)
        {
            SerialDetail::WriteMap<KeyFlags, ValueFlags>(tag, value, out);
        }

        template<uint32_t KeyFlags, uint32_t ValueFlags>
        static bool ParseMap(
            WireType wire_type, std::map<Key, Value> &value, FlagsType<KeyFlags>, FlagsType<ValueFlags>, reader &in)
        {
            return SerialDetail::ReadMap<KeyFlags, ValueFlags>(wire_type, value, in);
        }
    };

    struct StringWriter : public Writer {
        StringWriter(std::string &out)
            : _out(out)
        {
        }

        void Write(const void *bytes, size_t size) override
        {
            _out.append(reinterpret_cast<const char *>(bytes), size);
        }

    private:
        std::string &_out;
    };

    struct StringReader : public reader {
        StringReader(const std::string &in)
            : _in(in), _pos(0)
        {
        }

        size_t Read(void *bytes, size_t size) override
        {
            size_t readSize = std::min(size, _in.size() - _pos);
            if (memcpy_s(bytes, size, _in.data() + _pos, readSize) != EOK) {
                return readSize;
            }
            _pos += readSize;
            return readSize;
        }

    private:
        const std::string &_in;
        size_t _pos;
    };
    // mytype begin, just support base type, but really use protobuf raw type(uint32)
    template<> struct Serializer<uint8_t> {
        static void Serialize(uint32_t tag, uint8_t value, FlagsType<>, Writer &out, bool force = false)
        {
            Serializer<uint32_t>::Serialize(tag, value, FlagsType(), out, force);
        }

        static void SerializePacked(uint8_t value, FlagsType<>, Writer &out)
        {
            Serializer<uint32_t>::SerializePacked(value, FlagsType(), out);
        }

        static bool Parse(WireType wire_type, uint8_t &value, FlagsType<>, reader &in)
        {
            uint32_t intermedaite_value;
            if (Serializer<uint32_t>::Parse(wire_type, intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<uint8_t>(intermedaite_value);
                return true;
            }
            return false;
        }

        static bool ParsePacked(uint8_t &value, FlagsType<>, reader &in)
        {
            uint32_t intermedaite_value;
            if (Serializer<uint32_t>::ParsePacked(intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<uint8_t>(intermedaite_value);
                return true;
            }
            return false;
        }
    };
     template<> struct Serializer<uint16_t> {
        static void Serialize(uint32_t tag, uint16_t value, FlagsType<>, Writer &out, bool force = false)
        {
            Serializer<uint32_t>::Serialize(tag, value, FlagsType(), out, force);
        }

        static void SerializePacked(uint16_t value, FlagsType<>, Writer &out)
        {
            Serializer<uint32_t>::SerializePacked(value, FlagsType(), out);
        }

        static bool Parse(WireType wire_type, uint16_t &value, FlagsType<>, reader &in)
        {
            uint32_t intermedaite_value;
            if (Serializer<uint32_t>::Parse(wire_type, intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<uint16_t>(intermedaite_value);
                return true;
            }
            return false;
        }

        static bool ParsePacked(uint16_t &value, FlagsType<>, reader &in)
        {
            uint32_t intermedaite_value;
            if (Serializer<uint32_t>::ParsePacked(intermedaite_value, FlagsType<>(), in)) {
                value = static_cast<uint16_t>(intermedaite_value);
                return true;
            }
            return false;
        }
    };
    // mytype finish

    template<class T> std::string SerializeToString(const T &value)
    {
        std::string out;
        StringWriter stringOut(out);
        SerialDetail::WriteMessage(value, MessageType<T>(), stringOut);
        return out;
    }

    template<class T> bool ParseFromString(T &value, const std::string &in)
    {
        StringReader stringIn(in);
        return SerialDetail::ReadMessage(value, MessageType<T>(), stringIn);
    }
}
// clang-format on
}  // Hdc
#endif  // HDC_SERIAL_STRUCT_DEFINE_H
