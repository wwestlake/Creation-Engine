#pragma once

#include "engine/persistence.h"

#include <algorithm>
#include <cmath>

namespace ce::persistence::detail
{
inline OperationError Fail(ErrorCode code, const char* message)
{
    return {code, message};
}

inline bool MatchesType(const FieldValue& value, const FieldDefinition& field)
{
    if (std::holds_alternative<Null>(value))
        return field.nullable;

    switch (field.type) {
        case FieldType::Boolean: return std::holds_alternative<bool>(value);
        case FieldType::Integer: return std::holds_alternative<std::int64_t>(value);
        case FieldType::Real: {
            const auto* real = std::get_if<double>(&value);
            return real && std::isfinite(*real);
        }
        case FieldType::String: return std::holds_alternative<std::string>(value);
        case FieldType::Binary: return std::holds_alternative<Binary>(value);
    }
    return false;
}

inline Status ValidateSchema(const SessionSchema& schema)
{
    for (const auto& [key, collection] : schema) {
        if (key.nameSpace.value.empty() || key.collection.value.empty() || collection.version.value == 0)
            return Fail(ErrorCode::InvalidArgument, "Collections require a namespace, name, and schema version.");
        for (const auto& [name, field] : collection.fields) {
            if (name.value.empty() || field.type < FieldType::Boolean || field.type > FieldType::Binary)
                return Fail(ErrorCode::InvalidArgument, "Invalid field definition.");
        }
        for (const auto& [name, index] : collection.indexes) {
            if (name.value.empty() || index.fields.empty())
                return Fail(ErrorCode::InvalidArgument, "Indexes require a name and at least one field.");
            std::set<FieldId> indexed;
            for (const auto& field : index.fields) {
                if (!collection.fields.contains(field) || !indexed.insert(field).second)
                    return Fail(ErrorCode::InvalidArgument, "Index fields must be declared and distinct.");
            }
        }
    }
    return Null{};
}

inline Status ValidateData(const RecordData& data, const CollectionSchema& schema)
{
    if (data.schemaVersion != schema.version)
        return Fail(ErrorCode::SchemaMismatch, "Record schema version differs from the collection.");
    for (const auto& [name, value] : data.fields) {
        const auto field = schema.fields.find(name);
        if (field == schema.fields.end() || !MatchesType(value, field->second))
            return Fail(ErrorCode::InvalidArgument, "Record contains an undeclared field or an invalid field value.");
    }
    for (const auto& [name, field] : schema.fields) {
        if (field.required && !data.fields.contains(name))
            return Fail(ErrorCode::InvalidArgument, "Record is missing a required field.");
    }
    return Null{};
}

inline bool MatchesPrefix(const IndexKey& prefix, const IndexKey& key)
{
    return prefix.size() <= key.size() && std::equal(prefix.begin(), prefix.end(), key.begin());
}

inline bool BelowLower(const QueryDescription& query, const IndexKey& key)
{
    if (!query.lower)
        return false;
    const auto& value = key[query.prefix.size()];
    return value < query.lower->value || (value == query.lower->value && !query.lower->inclusive);
}

inline bool AboveUpper(const QueryDescription& query, const IndexKey& key)
{
    if (!query.upper)
        return false;
    const auto& value = key[query.prefix.size()];
    return value > query.upper->value || (value == query.upper->value && !query.upper->inclusive);
}

inline Status ValidateQuery(const QueryDescription& query, const std::vector<FieldDefinition>& fields,
                     const SessionId& session, CommitRevision revision)
{
    if (query.limit == 0 || query.prefix.size() > fields.size())
        return Fail(ErrorCode::InvalidArgument, "Invalid query limit or prefix length.");
    for (std::size_t i = 0; i < query.prefix.size(); ++i) {
        if (!MatchesType(query.prefix[i], fields[i]))
            return Fail(ErrorCode::InvalidArgument, "Query prefix value has the wrong type.");
    }
    const auto nextField = query.prefix.size();
    if (query.lower || query.upper) {
        if (nextField == fields.size())
            return Fail(ErrorCode::InvalidArgument, "Range bounds require a field after the equality prefix.");
        if ((query.lower && !MatchesType(query.lower->value, fields[nextField]))
            || (query.upper && !MatchesType(query.upper->value, fields[nextField])))
            return Fail(ErrorCode::InvalidArgument, "Query bound has the wrong type.");
        if (query.lower && query.upper && query.lower->value > query.upper->value)
            return Fail(ErrorCode::InvalidArgument, "Query lower bound exceeds its upper bound.");
    }
    if (!query.after)
        return Null{};

    const auto& cursor = *query.after;
    if (cursor.session != session || cursor.collection != query.collection || cursor.index != query.index
        || cursor.key.size() != fields.size() || cursor.id.value.empty())
        return Fail(ErrorCode::InvalidArgument, "Cursor does not belong to this query target.");
    if (cursor.revision != revision)
        return Fail(ErrorCode::HistoryUnavailable, "Cursor requires its original snapshot revision.");
    for (std::size_t i = 0; i < cursor.key.size(); ++i) {
        if (!MatchesType(cursor.key[i], fields[i]))
            return Fail(ErrorCode::InvalidArgument, "Cursor key has the wrong type.");
    }
    if (!MatchesPrefix(query.prefix, cursor.key) || BelowLower(query, cursor.key) || AboveUpper(query, cursor.key)
        || (!query.index && cursor.key.front() != FieldValue{cursor.id.value}))
        return Fail(ErrorCode::InvalidArgument, "Cursor is outside the query bounds.");
    return Null{};
}

inline std::optional<IndexKey> ProjectIndexKey(const IndexDefinition& index, const RecordData& data)
{
    IndexKey key;
    key.reserve(index.fields.size());
    for (const auto& field : index.fields) {
        const auto value = data.fields.find(field);
        if (value == data.fields.end())
            return std::nullopt;
        key.push_back(value->second);
    }
    return key;
}

inline Result<std::vector<FieldDefinition>> QueryFields(const QueryDescription& query,
                                                        const CollectionSchema& schema)
{
    if (!query.index)
        return std::vector<FieldDefinition>{{FieldType::String}};
    const auto index = schema.indexes.find(*query.index);
    if (index == schema.indexes.end())
        return Fail(ErrorCode::UnknownIndex, "Unknown secondary index.");

    std::vector<FieldDefinition> fields;
    fields.reserve(index->second.fields.size());
    for (const auto& field : index->second.fields)
        fields.push_back(schema.fields.at(field));
    return fields;
}

inline Status ValidateMutation(const Mutation& mutation, const CollectionSchema& schema,
                               std::optional<RecordVersion> existing)
{
    if (mutation.key.id.value.empty())
        return Fail(ErrorCode::InvalidArgument, "Record ID cannot be empty.");

    switch (mutation.kind) {
        case MutationKind::Create:
            if (!mutation.data || mutation.expectedVersion)
                return Fail(ErrorCode::InvalidArgument, "Create requires data and accepts no expected version.");
            if (existing)
                return Fail(ErrorCode::RecordAlreadyExists, "Record already exists.");
            break;
        case MutationKind::Replace:
        case MutationKind::Erase:
            if ((mutation.kind == MutationKind::Replace) != mutation.data.has_value())
                return Fail(ErrorCode::InvalidArgument, "Replace requires data and erase accepts no data.");
            if (!existing)
                return Fail(ErrorCode::RecordNotFound, "Record does not exist.");
            if (mutation.expectedVersion && *mutation.expectedVersion != *existing)
                return Fail(ErrorCode::VersionConflict, "Record version changed.");
            break;
        default:
            return Fail(ErrorCode::InvalidArgument, "Invalid mutation kind.");
    }
    return mutation.data ? ValidateData(*mutation.data, schema) : Status{Null{}};
}

inline constexpr std::uint64_t kPortableRevisionLimit = (std::uint64_t{1} << 63) - 1;

inline Status ValidatePolicy(const PersistencePolicy& policy)
{
    if (policy.openSnapshots == 0 || policy.recordsPerTransaction == 0 || policy.pageEntries == 0
        || policy.snapshotLifetime <= std::chrono::milliseconds::zero()
        || policy.snapshotLifetime > std::chrono::hours(24))
        return Fail(ErrorCode::InvalidArgument, "Persistence limits must be positive and snapshot lifetime at most 24 hours.");
    return Null{};
}

inline CommitRevision RetentionFloor(CommitRevision latest, std::size_t keep)
{
    return {latest.value - std::min<std::uint64_t>(latest.value, keep)};
}

inline Status ValidateHistoryCursor(CommitRevision after, CommitRevision latest, CommitRevision floor, std::size_t limit)
{
    if (limit == 0 || after > latest)
        return Fail(ErrorCode::InvalidArgument, "Invalid journal limit or starting revision.");
    if (after < floor)
        return {OperationError{ErrorCode::HistoryUnavailable,
                               "Journal retention gap. Earliest cursor: " + std::to_string(floor.value) + "."}};
    return Null{};
}

inline Status ValidateArchive(const SessionArchive& archive)
{
    if (archive.id.value.empty() || archive.revision.value > kPortableRevisionLimit
        || archive.historyFloor > archive.revision
        || archive.journal.size() != archive.revision.value - archive.historyFloor.value)
        return Fail(ErrorCode::InvalidArgument, "Invalid archive identity, revision, or retained journal bounds.");
    if (auto valid = ValidateSchema(archive.schema); !valid)
        return valid.Error();

    std::map<RecordKey, const Record*> visible;
    for (const auto& entry : archive.records) {
        const auto schema = archive.schema.find(entry.collection);
        const auto& record = entry.record;
        if (schema == archive.schema.end() || record.id.value.empty() || record.version.value == 0
            || record.version.value > archive.revision.value || !ValidateData(record.data, schema->second)
            || !visible.emplace(RecordKey{entry.collection, record.id}, &record).second)
            return Fail(ErrorCode::InvalidArgument, "Invalid or duplicate archived record.");
    }

    std::map<RecordKey, const Record*> lastChange;
    auto revision = archive.historyFloor.value;
    for (const auto& batch : archive.journal) {
        if (batch.revision.value != ++revision || batch.changes.empty())
            return Fail(ErrorCode::InvalidArgument, "Archived journal has a missing or invalid revision.");
        std::set<RecordKey> changed;
        for (const auto& change : batch.changes) {
            const auto schema = archive.schema.find(change.key.collection);
            if (schema == archive.schema.end() || change.key.id.value.empty()
                || !changed.insert(change.key).second
                || change.kind < MutationKind::Create || change.kind > MutationKind::Erase
                || change.previousVersion.has_value() != (change.kind != MutationKind::Create)
                || change.record.has_value() != (change.kind != MutationKind::Erase)
                || (change.previousVersion && (change.previousVersion->value == 0
                                               || change.previousVersion->value >= revision))
                || (change.record && (change.record->id != change.key.id || change.record->version.value != revision
                                      || !ValidateData(change.record->data, schema->second))))
                return Fail(ErrorCode::InvalidArgument, "Invalid archived journal change.");
            const auto earlier = lastChange.find(change.key);
            if (earlier != lastChange.end()) {
                const auto expected = earlier->second ? std::optional{earlier->second->version} : std::nullopt;
                if (change.previousVersion != expected)
                    return Fail(ErrorCode::InvalidArgument, "Archived record version chain is inconsistent.");
            } else if (change.previousVersion && change.previousVersion->value > archive.historyFloor.value) {
                return Fail(ErrorCode::InvalidArgument, "Archived journal is missing a prior record change.");
            }
            lastChange[change.key] = change.record ? &*change.record : nullptr;
        }
    }
    for (const auto& [key, record] : lastChange) {
        const auto found = visible.find(key);
        if ((record == nullptr && found != visible.end())
            || (record && (found == visible.end() || *found->second != *record)))
            return Fail(ErrorCode::InvalidArgument, "Archived state differs from its retained journal.");
    }
    for (const auto& [key, record] : visible) {
        if (record->version.value > archive.historyFloor.value && !lastChange.contains(key))
            return Fail(ErrorCode::InvalidArgument, "Archived record is missing its retained journal change.");
    }
    return Null{};
}

} // namespace ce::persistence::detail
