#pragma once

#include "engine/persistence.h"

#include <bit>
#include <cmath>
#include <limits>

namespace ce::persistence::encoding
{
[[noreturn]] inline void MalformedData()
{
    throw OperationError{ErrorCode::StorageFailure, "Malformed persistence data in the store."};
}

inline void AppendInteger(Binary& bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

class Encoder {
public:
    void Byte(std::uint8_t value) { bytes_.push_back(value); }
    void Integer(std::uint64_t value) { AppendInteger(bytes_, value); }
    void Blob(std::span<const std::uint8_t> value)
    {
        Integer(value.size());
        if (!value.empty())
            bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void String(const std::string& value)
    {
        Blob({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
    }
    Binary Finish() && { return std::move(bytes_); }

private:
    Binary bytes_;
};

class Decoder {
public:
    explicit Decoder(std::span<const std::uint8_t> bytes) : rest_(bytes) {}

    std::uint8_t Byte() { return Take(1)[0]; }

    bool Boolean()
    {
        const auto value = Byte();
        if (value > 1)
            MalformedData();
        return value == 1;
    }

    std::uint64_t Integer()
    {
        std::uint64_t value = 0;
        for (auto byte : Take(8))
            value = (value << 8) | byte;
        return value;
    }

    std::size_t Count()
    {
        const auto size = Integer();
        if (size > rest_.size())
            MalformedData();
        return static_cast<std::size_t>(size);
    }

    std::span<const std::uint8_t> Blob() { return Take(Count()); }

    std::string String()
    {
        const auto bytes = Blob();
        if (bytes.empty())
            return {};
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    void Finish() const
    {
        if (!rest_.empty())
            MalformedData();
    }

private:
    std::span<const std::uint8_t> Take(std::size_t count)
    {
        if (count > rest_.size())
            MalformedData();
        const auto bytes = rest_.first(count);
        rest_ = rest_.subspan(count);
        return bytes;
    }

    std::span<const std::uint8_t> rest_;
};

inline Binary EncodeSchema(const SessionSchema& schema)
{
    Encoder out;
    out.Integer(schema.size());
    for (const auto& [key, collection] : schema) {
        out.String(key.nameSpace.value);
        out.String(key.collection.value);
        out.Integer(collection.version.value);
        out.Integer(collection.fields.size());
        for (const auto& [field, definition] : collection.fields) {
            out.String(field.value);
            out.Byte(static_cast<std::uint8_t>(definition.type));
            out.Byte(definition.required);
            out.Byte(definition.nullable);
        }
        out.Integer(collection.indexes.size());
        for (const auto& [index, definition] : collection.indexes) {
            out.String(index.value);
            out.Byte(definition.unique);
            out.Integer(definition.fields.size());
            for (const auto& field : definition.fields)
                out.String(field.value);
        }
    }
    // Keep the original manifest bytes for existing ascending, version-1
    // definitions. The extension stores only non-default definitions.
    bool extended = false;
    for (const auto& [key, collection] : schema) {
        for (const auto& [id, index] : collection.indexes) {
            if (index.version.value == 1 && index.directions.empty())
                continue;
            if (!extended) {
                out.Byte(1);
                extended = true;
            }
            out.String(key.nameSpace.value);
            out.String(key.collection.value);
            out.String(id.value);
            out.Integer(index.version.value);
            out.Integer(index.directions.size());
            for (const auto direction : index.directions)
                out.Byte(static_cast<std::uint8_t>(direction));
        }
    }
    return std::move(out).Finish();
}

inline void EncodeField(Encoder& out, const FieldValue& value)
{
    // Tags are part of the database format. Keep them independent of the variant order.
    if (const auto* boolean = std::get_if<bool>(&value)) {
        out.Byte(1);
        out.Byte(*boolean);
    } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        out.Byte(2);
        out.Integer(std::bit_cast<std::uint64_t>(*integer));
    } else if (const auto* real = std::get_if<double>(&value)) {
        out.Byte(3);
        out.Integer(std::bit_cast<std::uint64_t>(*real));
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        out.Byte(4);
        out.String(*text);
    } else if (const auto* blob = std::get_if<Binary>(&value)) {
        out.Byte(5);
        out.Blob(*blob);
    } else {
        out.Byte(0);
    }
}

inline FieldValue DecodeField(Decoder& in)
{
    switch (in.Byte()) {
        case 0: return Null{};
        case 1: return in.Boolean();
        case 2: return std::bit_cast<std::int64_t>(in.Integer());
        case 3: {
            const auto real = std::bit_cast<double>(in.Integer());
            if (!std::isfinite(real))
                MalformedData();
            return real;
        }
        case 4: return in.String();
        case 5: {
            const auto blob = in.Blob();
            return Binary(blob.begin(), blob.end());
        }
        default: MalformedData();
    }
}

inline void EncodeData(Encoder& out, const RecordData& data)
{
    out.Integer(data.schemaVersion.value);
    out.Integer(data.fields.size());
    for (const auto& [field, value] : data.fields) {
        out.String(field.value);
        EncodeField(out, value);
    }
    out.Byte(data.binary.has_value());
    if (data.binary)
        out.Blob(*data.binary);
}

inline RecordData DecodeData(Decoder& in)
{
    RecordData data;
    data.schemaVersion = {in.Integer()};
    const auto count = in.Count();
    for (std::size_t i = 0; i < count; ++i) {
        FieldId id{in.String()};
        auto value = DecodeField(in);
        if (!data.fields.emplace(std::move(id), std::move(value)).second)
            MalformedData();
    }
    if (in.Boolean()) {
        const auto blob = in.Blob();
        data.binary.emplace(blob.begin(), blob.end());
    }
    return data;
}

inline Binary EncodeRecord(const RecordData& data)
{
    Encoder out;
    EncodeData(out, data);
    return std::move(out).Finish();
}

inline RecordData DecodeRecord(std::span<const std::uint8_t> bytes)
{
    Decoder in(bytes);
    auto data = DecodeData(in);
    in.Finish();
    return data;
}

inline Binary EncodeChanges(const ChangeBatch& batch)
{
    Encoder out;
    out.Integer(batch.changes.size());
    for (const auto& change : batch.changes) {
        out.String(change.key.collection.nameSpace.value);
        out.String(change.key.collection.collection.value);
        out.String(change.key.id.value);
        out.Byte(static_cast<std::uint8_t>(change.kind));
        out.Byte(change.previousVersion.has_value());
        if (change.previousVersion)
            out.Integer(change.previousVersion->value);
        out.Byte(change.record.has_value());
        if (change.record)
            EncodeData(out, change.record->data);
    }
    return std::move(out).Finish();
}

inline ChangeBatch DecodeChanges(CommitRevision revision, std::span<const std::uint8_t> bytes)
{
    Decoder in(bytes);
    ChangeBatch batch{revision, {}};
    const auto count = in.Count();
    for (std::size_t i = 0; i < count; ++i) {
        RecordChange change{};
        change.key = {{{in.String()}, {in.String()}}, {in.String()}};
        const auto kind = in.Byte();
        if (kind > static_cast<std::uint8_t>(MutationKind::Erase) || change.key.id.value.empty())
            MalformedData();
        change.kind = static_cast<MutationKind>(kind);
        if (in.Boolean())
            change.previousVersion = RecordVersion{in.Integer()};
        if (in.Boolean())
            change.record = Record{change.key.id, {revision.value}, DecodeData(in)};
        if (change.previousVersion.has_value() != (change.kind != MutationKind::Create)
            || change.record.has_value() != (change.kind != MutationKind::Erase))
            MalformedData();
        batch.changes.push_back(std::move(change));
    }
    in.Finish();
    if (batch.changes.empty())
        MalformedData();
    return batch;
}

inline void AppendOrderedBytes(Binary& out, std::span<const std::uint8_t> bytes)
{
    for (auto byte : bytes) {
        out.push_back(byte);
        if (byte == 0)
            out.push_back(255);
    }
    out.insert(out.end(), {0, 0});
}

inline void AppendOrderedField(Binary& out, const FieldValue& value)
{
    constexpr std::uint64_t sign = std::uint64_t{1} << 63;
    static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);
    if (const auto* boolean = std::get_if<bool>(&value)) {
        out.insert(out.end(), {1, static_cast<std::uint8_t>(*boolean)});
    } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        out.push_back(2);
        AppendInteger(out, std::bit_cast<std::uint64_t>(*integer) ^ sign);
    } else if (const auto* real = std::get_if<double>(&value)) {
        out.push_back(3);
        const auto bits = std::bit_cast<std::uint64_t>(*real == 0.0 ? 0.0 : *real);
        AppendInteger(out, (bits & sign) ? ~bits : bits ^ sign);
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        out.push_back(4);
        AppendOrderedBytes(out, {reinterpret_cast<const std::uint8_t*>(text->data()), text->size()});
    } else if (const auto* bytes = std::get_if<Binary>(&value)) {
        out.push_back(5);
        AppendOrderedBytes(out, *bytes);
    } else {
        out.push_back(0);
    }
}

inline SortDirection DirectionAt(const IndexDefinition* index, std::size_t component)
{
    return index && component < index->directions.size()
        ? index->directions[component] : SortDirection::Ascending;
}

inline void AppendOrderedField(Binary& out, const FieldValue& value, SortDirection direction)
{
    const auto start = out.size();
    AppendOrderedField(out, value);
    if (direction == SortDirection::Descending)
        for (auto i = start; i < out.size(); ++i)
            out[i] = static_cast<std::uint8_t>(~out[i]);
}

inline void AppendOrderedKey(Binary& out, const IndexKey& key, const IndexDefinition* index)
{
    for (std::size_t i = 0; i < key.size(); ++i)
        AppendOrderedField(out, key[i], DirectionAt(index, i));
}

inline Binary EncodeOrderedKey(const IndexKey& key, const IndexDefinition* index = nullptr)
{
    Binary bytes;
    AppendOrderedKey(bytes, key, index);
    return bytes;
}

// All values starting with the given prefix sort before this exclusive bound.
inline std::optional<Binary> PrefixEnd(Binary bytes)
{
    while (!bytes.empty() && bytes.back() == 255)
        bytes.pop_back();
    if (bytes.empty())
        return std::nullopt;
    ++bytes.back();
    return bytes;
}

struct OrderedRange {
    Binary first;
    std::optional<Binary> past;
    bool empty = false;
};

inline OrderedRange MakeOrderedRange(Binary base, const QueryDescription& query,
                                     const IndexDefinition* index, bool applyCursor)
{
    auto prefix = base;
    AppendOrderedKey(prefix, query.prefix, index);
    OrderedRange range{prefix, PrefixEnd(prefix)};
    const auto direction = DirectionAt(index, query.prefix.size());
    const bool reversed = direction == SortDirection::Descending;
    const auto& physicalLow = reversed ? query.upper : query.lower;
    const auto& physicalHigh = reversed ? query.lower : query.upper;
    if (physicalLow) {
        AppendOrderedField(range.first, physicalLow->value, direction);
        if (!physicalLow->inclusive) {
            auto next = PrefixEnd(range.first);
            if (!next) return {{}, {}, true};
            range.first = std::move(*next);
        }
    }
    if (physicalHigh) {
        auto high = prefix;
        AppendOrderedField(high, physicalHigh->value, direction);
        range.past = physicalHigh->inclusive ? PrefixEnd(std::move(high)) : std::optional{std::move(high)};
    }
    if (applyCursor && query.after) {
        AppendOrderedKey(base, query.after->key, index);
        if (index)
            AppendOrderedField(base, query.after->id.value);
        if (query.direction == SortDirection::Ascending) {
            auto next = PrefixEnd(std::move(base));
            if (!next) return {{}, {}, true};
            range.first = std::max(range.first, std::move(*next));
        } else if (!range.past || base < *range.past) {
            range.past = std::move(base);
        }
    }
    range.empty = range.past && *range.past <= range.first;
    return range;
}
} // namespace ce::persistence::encoding
