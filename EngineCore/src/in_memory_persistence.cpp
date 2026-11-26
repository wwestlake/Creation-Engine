#include "engine/in_memory_persistence.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace ce::persistence
{
namespace
{
OperationError Fail(ErrorCode code, const char* message)
{
    return {code, message};
}

bool MatchesType(const FieldValue& value, const FieldDefinition& field)
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

Status ValidateSchema(const SessionSchema& schema)
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

Status ValidateData(const RecordData& data, const CollectionSchema& schema)
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

struct IndexEntry {
    IndexKey key;
    RecordId id;
    auto operator<=>(const IndexEntry&) const = default;
};

struct CollectionData {
    CollectionSchema schema;
    std::map<RecordId, Record> records;
    std::map<IndexId, std::set<IndexEntry>> indexes;
};

struct RevisionData {
    CommitRevision revision;
    std::map<CollectionKey, std::shared_ptr<const CollectionData>> collections;
};

struct SessionData {
    explicit SessionData(const SessionSchema& definition) : schema(definition)
    {
        auto initial = std::make_shared<RevisionData>();
        for (const auto& [key, collection] : schema) {
            auto data = std::make_shared<CollectionData>();
            data->schema = collection;
            for (const auto& [index, unused] : collection.indexes)
                data->indexes.try_emplace(index);
            initial->collections.emplace(key, std::move(data));
        }
        current = std::move(initial);
    }

    const SessionSchema schema;
    std::mutex mutex;
    std::shared_ptr<const RevisionData> current;
    std::vector<ChangeBatch> journal;
    bool open = false;
};

std::optional<IndexEntry> MakeEntry(const IndexDefinition& index, const Record& record)
{
    IndexEntry entry{{}, record.id};
    entry.key.reserve(index.fields.size());
    for (const auto& field : index.fields) {
        const auto value = record.data.fields.find(field);
        if (value == record.data.fields.end())
            return std::nullopt;
        entry.key.push_back(value->second);
    }
    return entry;
}

void RemoveIndexEntries(CollectionData& collection, const Record& record)
{
    for (const auto& [index, definition] : collection.schema.indexes) {
        if (auto entry = MakeEntry(definition, record))
            collection.indexes.at(index).erase(*entry);
    }
}

Status InsertIndexEntries(CollectionData& collection, const Record& record)
{
    for (const auto& [index, definition] : collection.schema.indexes) {
        auto entry = MakeEntry(definition, record);
        if (!entry)
            continue;

        auto& entries = collection.indexes.at(index);
        if (definition.unique) {
            const auto first = entries.lower_bound(IndexEntry{entry->key, {}});
            if (first != entries.end() && first->key == entry->key)
                return Fail(ErrorCode::UniqueConstraint, "Unique index key already exists.");
        }
        entries.insert(std::move(*entry));
    }
    return Null{};
}

bool MatchesPrefix(const IndexKey& prefix, const IndexKey& key)
{
    return prefix.size() <= key.size() && std::equal(prefix.begin(), prefix.end(), key.begin());
}

bool BelowLower(const QueryDescription& query, const IndexKey& key)
{
    if (!query.lower)
        return false;
    const auto& value = key[query.prefix.size()];
    return value < query.lower->value || (value == query.lower->value && !query.lower->inclusive);
}

bool AboveUpper(const QueryDescription& query, const IndexKey& key)
{
    if (!query.upper)
        return false;
    const auto& value = key[query.prefix.size()];
    return value > query.upper->value || (value == query.upper->value && !query.upper->inclusive);
}

Status ValidateQuery(const QueryDescription& query, const std::vector<FieldDefinition>& fields,
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

class MemorySnapshot final : public ReadSnapshot {
public:
    MemorySnapshot(SessionId session, std::shared_ptr<const RevisionData> state)
        : session_(std::move(session)), state_(std::move(state)) {}

    CommitRevision Revision() const noexcept override { return state_->revision; }

    Result<std::optional<Record>> Get(const RecordKey& key) const override
    {
        if (key.id.value.empty())
            return Fail(ErrorCode::InvalidArgument, "Record ID cannot be empty.");
        const auto collection = state_->collections.find(key.collection);
        if (collection == state_->collections.end())
            return Fail(ErrorCode::UnknownCollection, "Unknown collection.");

        const auto& records = collection->second->records;
        const auto found = records.find(key.id);
        if (found == records.end())
            return std::optional<Record>{};
        return std::optional<Record>{found->second};
    }

    Result<QueryPage> Query(const QueryDescription& query) const override
    {
        const auto target = state_->collections.find(query.collection);
        if (target == state_->collections.end())
            return Fail(ErrorCode::UnknownCollection, "Unknown collection.");
        const auto& collection = *target->second;

        std::vector<FieldDefinition> fields;
        if (query.index) {
            const auto index = collection.schema.indexes.find(*query.index);
            if (index == collection.schema.indexes.end())
                return Fail(ErrorCode::UnknownIndex, "Unknown secondary index.");
            for (const auto& field : index->second.fields)
                fields.push_back(collection.schema.fields.at(field));
        } else {
            fields.push_back({FieldType::String});
        }
        if (const auto valid = ValidateQuery(query, fields, session_, Revision()); !valid)
            return valid.Error();

        auto start = query.prefix;
        if (query.lower)
            start.push_back(query.lower->value);

        QueryPage result;
        IndexKey lastKey;
        const auto append = [&](const IndexKey& key, const Record& record) {
            if (!MatchesPrefix(query.prefix, key) || AboveUpper(query, key))
                return false;
            if (BelowLower(query, key))
                return true;
            if (result.records.size() == query.limit) {
                result.next = QueryCursor{session_, Revision(), query.collection, query.index,
                                          std::move(lastKey), result.records.back().id};
                return false;
            }
            lastKey = key;
            result.records.push_back(record);
            return true;
        };

        if (query.index) {
            const auto& entries = collection.indexes.at(*query.index);
            auto it = query.after ? entries.upper_bound({query.after->key, query.after->id})
                                  : entries.lower_bound({std::move(start), {}});
            for (; it != entries.end(); ++it) {
                if (!append(it->key, collection.records.at(it->id)))
                    break;
            }
        } else {
            const auto& records = collection.records;
            auto it = start.empty() ? records.begin() : records.lower_bound({std::get<std::string>(start.front())});
            if (query.after)
                it = records.upper_bound(query.after->id);
            for (; it != records.end(); ++it) {
                if (!append({it->first.value}, it->second))
                    break;
            }
        }
        return result;
    }

private:
    SessionId session_;
    std::shared_ptr<const RevisionData> state_;
};

Result<RecordChange> ApplyMutation(CollectionData& collection, const Mutation& mutation,
                                   RecordVersion version)
{
    if (mutation.key.id.value.empty())
        return Fail(ErrorCode::InvalidArgument, "Record ID cannot be empty.");
    const auto existing = collection.records.find(mutation.key.id);

    switch (mutation.kind) {
        case MutationKind::Create:
            if (!mutation.data || mutation.expectedVersion)
                return Fail(ErrorCode::InvalidArgument, "Create requires data and accepts no expected version.");
            if (existing != collection.records.end())
                return Fail(ErrorCode::RecordAlreadyExists, "Record already exists.");
            break;
        case MutationKind::Replace:
        case MutationKind::Erase:
            if ((mutation.kind == MutationKind::Replace) != mutation.data.has_value())
                return Fail(ErrorCode::InvalidArgument, "Replace requires data and erase accepts no data.");
            if (existing == collection.records.end())
                return Fail(ErrorCode::RecordNotFound, "Record does not exist.");
            if (mutation.expectedVersion && *mutation.expectedVersion != existing->second.version)
                return Fail(ErrorCode::VersionConflict, "Record version changed.");
            break;
        default:
            return Fail(ErrorCode::InvalidArgument, "Invalid mutation kind.");
    }
    if (mutation.data) {
        if (const auto valid = ValidateData(*mutation.data, collection.schema); !valid)
            return valid.Error();
    }

    RecordChange change{mutation.kind, mutation.key, std::nullopt, std::nullopt};
    if (existing != collection.records.end()) {
        change.previousVersion = existing->second.version;
        RemoveIndexEntries(collection, existing->second);
        collection.records.erase(existing);
    }
    if (mutation.data) {
        change.record = Record{mutation.key.id, version, *mutation.data};
        collection.records.emplace(mutation.key.id, *change.record);
    }
    return change;
}

class MemorySession final : public Session {
public:
    MemorySession(SessionId id, std::shared_ptr<SessionData> data)
        : id_(std::move(id)), data_(std::move(data)) {}

    ~MemorySession() override { static_cast<void>(Close()); }

    Status Close() override
    {
        std::lock_guard lock(data_->mutex);
        if (!closed_) {
            closed_ = true;
            data_->open = false;
        }
        return Null{};
    }

    Result<std::unique_ptr<ReadSnapshot>> OpenSnapshot() const override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        return std::unique_ptr<ReadSnapshot>{std::make_unique<MemorySnapshot>(id_, data_->current)};
    }

    Result<CommitRevision> Commit(const Transaction& transaction) override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        const auto current = data_->current->revision;
        if (transaction.mutations.empty())
            return Fail(ErrorCode::InvalidArgument, "Cannot commit an empty transaction.");
        if (transaction.expectedRevision && *transaction.expectedRevision != current)
            return Fail(ErrorCode::VersionConflict, "Session revision changed.");
        if (current.value == std::numeric_limits<std::uint64_t>::max())
            return Fail(ErrorCode::RevisionExhausted, "Commit revision exhausted.");

        const CommitRevision revision{current.value + 1};
        auto next = std::make_shared<RevisionData>(*data_->current);
        std::map<CollectionKey, std::shared_ptr<CollectionData>> staged;
        std::set<RecordKey> changed;
        ChangeBatch batch{revision, {}};
        batch.changes.reserve(transaction.mutations.size());

        for (const auto& mutation : transaction.mutations) {
            if (!changed.insert(mutation.key).second)
                return Fail(ErrorCode::InvalidArgument, "A transaction may change a record only once.");
            const auto collection = next->collections.find(mutation.key.collection);
            if (collection == next->collections.end())
                return Fail(ErrorCode::UnknownCollection, "Unknown collection.");
            auto [working, inserted] = staged.try_emplace(mutation.key.collection);
            if (inserted) {
                working->second = std::make_shared<CollectionData>(*collection->second);
                collection->second = working->second;
            }

            auto change = ApplyMutation(*working->second, mutation, RecordVersion{revision.value});
            if (!change)
                return change.Error();
            batch.changes.push_back(std::move(change).Value());
        }

        // Remove all old entries before inserting new ones so a transaction can
        // exchange two unique keys, with uniqueness checked on the final state.
        for (const auto& change : batch.changes) {
            if (change.record) {
                auto& collection = *staged.at(change.key.collection);
                if (const auto indexed = InsertIndexEntries(collection, *change.record); !indexed)
                    return indexed.Error();
            }
        }

        // Until the journal append succeeds, the old revision remains current.
        next->revision = revision;
        data_->journal.push_back(std::move(batch));
        data_->current = std::move(next);
        return revision;
    }

    Result<ChangePage> ReadChanges(CommitRevision after, std::size_t limit) const override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        if (limit == 0 || after > data_->current->revision)
            return Fail(ErrorCode::InvalidArgument, "Invalid journal limit or starting revision.");

        const auto begin = data_->journal.begin() + static_cast<std::ptrdiff_t>(after.value);
        const auto count = std::min(limit, static_cast<std::size_t>(data_->journal.end() - begin));
        return ChangePage{data_->current->revision,
                          {begin, begin + static_cast<std::ptrdiff_t>(count)}};
    }

private:
    SessionId id_;
    std::shared_ptr<SessionData> data_;
    bool closed_ = false; // Protected by data_->mutex.
};

class MemoryProvider final : public StorageProvider {
public:
    CapabilitySet Capabilities() const override
    {
        return {Capability::PrimaryQueries, Capability::SecondaryIndexes, Capability::AtomicTransactions,
                Capability::ConsistentSnapshots, Capability::LogicalChangeJournal};
    }

    Result<std::unique_ptr<Session>> OpenSession(const OpenSessionRequest& request) override
    {
        if (request.id.value.empty())
            return Fail(ErrorCode::InvalidArgument, "Session ID cannot be empty.");
        const auto supported = Capabilities();
        if (!std::includes(supported.begin(), supported.end(), request.requiredCapabilities.begin(),
                           request.requiredCapabilities.end()))
            return Fail(ErrorCode::UnsupportedOperation, "The provider lacks a required capability.");
        if (const auto valid = ValidateSchema(request.schema); !valid)
            return valid.Error();

        std::lock_guard providerLock(mutex_);
        auto entry = sessions_.find(request.id);
        if (entry == sessions_.end())
            entry = sessions_.emplace(request.id, std::make_shared<SessionData>(request.schema)).first;

        const auto& data = entry->second;
        std::lock_guard sessionLock(data->mutex);
        if (data->open)
            return Fail(ErrorCode::SessionAlreadyOpen, "Session already has an open writer.");
        if (request.schema != data->schema)
            return Fail(ErrorCode::SchemaMismatch, "Session schema differs from the stored definition.");

        std::unique_ptr<Session> session = std::make_unique<MemorySession>(request.id, data);
        data->open = true;
        return session;
    }

private:
    std::mutex mutex_;
    std::map<SessionId, std::shared_ptr<SessionData>> sessions_;
};
} // namespace

std::unique_ptr<StorageProvider> CreateInMemoryProvider()
{
    return std::make_unique<MemoryProvider>();
}
} // namespace ce::persistence
