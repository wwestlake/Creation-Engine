#include "engine/in_memory_persistence.h"

#include "persistence_validation.h"
#include "persistence_encoding.h"
#include <deque>
#include <iterator>
#include <mutex>

namespace ce::persistence
{
namespace
{
using namespace detail;
using namespace encoding;

struct IndexEntry {
    Binary key;
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

    SessionSchema schema;
    std::mutex mutex;
    std::shared_ptr<const RevisionData> current;
    std::deque<ChangeBatch> journal;
    CommitRevision HistoryFloor() const
    {
        return journal.empty() ? current->revision : CommitRevision{journal.front().revision.value - 1};
    }
    bool open = false;
};

std::optional<IndexEntry> MakeEntry(const IndexDefinition& index, const Record& record)
{
    auto key = ProjectIndexKey(index, record.data);
    if (!key)
        return std::nullopt;
    return IndexEntry{EncodeOrderedKey(*key, &index), record.id};
}

void RemoveIndexEntries(CollectionData& collection, const Record& record)
{
    for (const auto& [index, definition] : collection.schema.indexes) {
        if (auto entry = MakeEntry(definition, record))
            collection.indexes.at(index).erase(*entry);
    }
}

Status InsertIndexEntry(std::set<IndexEntry>& entries, const IndexDefinition& definition, const Record& record)
{
    auto entry = MakeEntry(definition, record);
    if (!entry) return Null{};
    if (definition.unique) {
        const auto first = entries.lower_bound(IndexEntry{entry->key, {}});
        if (first != entries.end() && first->key == entry->key)
            return Fail(ErrorCode::UniqueConstraint, "Unique index key already exists.");
    }
    entries.insert(std::move(*entry));
    return Null{};
}

Status InsertIndexEntries(CollectionData& collection, const Record& record)
{
    for (const auto& [index, definition] : collection.schema.indexes) {
        if (const auto inserted = InsertIndexEntry(collection.indexes.at(index), definition, record); !inserted)
            return inserted.Error();
    }
    return Null{};
}

class MemorySnapshot final : public ReadSnapshot {
public:
    MemorySnapshot(SessionId session, std::shared_ptr<const RevisionData> state)
        : session_(std::move(session)), state_(std::move(state)) {}

    CommitRevision Revision() const noexcept override { return state_->revision; }

    Result<CollectionSchema> Describe(const CollectionKey& key) const override
    {
        const auto collection = state_->collections.find(key);
        if (collection == state_->collections.end())
            return Fail(ErrorCode::UnknownCollection, "Unknown collection.");
        return collection->second->schema;
    }

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

        auto fields = QueryFields(query, collection.schema);
        if (!fields)
            return fields.Error();
        const auto* definition = SelectedIndex(query, collection.schema);
        if (const auto valid = ValidateQuery(query, fields.Value(), session_, Revision(), definition); !valid)
            return valid.Error();

        QueryPage result;
        const auto append = [&](const Record& record) {
            if (result.records.size() == query.limit) {
                const auto& last = result.records.back();
                auto key = definition ? *ProjectIndexKey(*definition, last.data) : IndexKey{last.id.value};
                result.next = MakeCursor(query, session_, Revision(), definition, std::move(key), last.id);
                return false;
            }
            result.records.push_back(record);
            return true;
        };
        const bool reverse = query.direction == SortDirection::Descending;
        const auto visit = [&](auto first, auto past, const auto& recordAt) {
            const auto loop = [&](auto it, auto end) {
                for (; it != end && append(recordAt(*it)); ++it) {}
            };
            if (reverse)
                loop(std::make_reverse_iterator(past), std::make_reverse_iterator(first));
            else
                loop(first, past);
        };

        if (definition) {
            const auto& entries = collection.indexes.at(*query.index);
            const auto range = MakeOrderedRange({}, query, definition, false);
            if (range.empty) return result;
            auto first = entries.lower_bound({range.first, {}});
            auto past = range.past ? entries.lower_bound({*range.past, {}}) : entries.end();
            if (query.after) {
                IndexEntry anchor{EncodeOrderedKey(query.after->key, definition), query.after->id};
                if (reverse) past = entries.lower_bound(anchor);
                else first = entries.upper_bound(anchor);
            }
            visit(first, past, [&](const auto& entry) -> const Record& { return collection.records.at(entry.id); });
        } else {
            const auto& records = collection.records;
            auto first = records.begin();
            auto past = records.end();
            if (!query.prefix.empty()) {
                const RecordId key{std::get<std::string>(query.prefix.front())};
                first = records.lower_bound(key);
                past = records.upper_bound(key);
            } else {
                if (query.lower && query.upper && query.lower->value == query.upper->value
                    && (!query.lower->inclusive || !query.upper->inclusive)) return result;
                if (query.lower) {
                    const RecordId key{std::get<std::string>(query.lower->value)};
                    first = query.lower->inclusive ? records.lower_bound(key) : records.upper_bound(key);
                }
                if (query.upper) {
                    const RecordId key{std::get<std::string>(query.upper->value)};
                    past = query.upper->inclusive ? records.upper_bound(key) : records.lower_bound(key);
                }
            }
            if (query.after) {
                if (reverse) past = records.lower_bound(query.after->id);
                else first = records.upper_bound(query.after->id);
            }
            visit(first, past, [](const auto& row) -> const Record& { return row.second; });
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
    const auto existing = collection.records.find(mutation.key.id);
    const auto previous = existing == collection.records.end() ? std::nullopt
                                                                : std::optional{existing->second.version};
    if (const auto valid = ValidateMutation(mutation, collection.schema, previous); !valid)
        return valid.Error();

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
    MemorySession(SessionId id, std::shared_ptr<SessionData> data, PersistencePolicy policy)
        : Session(policy), id_(std::move(id)), data_(std::move(data)) {}

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

    Result<std::unique_ptr<ReadSnapshot>> CaptureSnapshot() const override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        return std::unique_ptr<ReadSnapshot>{std::make_unique<MemorySnapshot>(id_, data_->current)};
    }

    Result<IndexMaintenanceResult> MaintainIndex(const IndexMaintenance& request) override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_) return Fail(ErrorCode::SessionClosed, "Session is closed.");
        const auto target = data_->current->collections.find(request.collection);
        if (target == data_->current->collections.end())
            return Fail(ErrorCode::UnknownCollection, "Unknown collection.");
        auto schema = MaintainDefinition(target->second->schema, request);
        if (!schema) return schema.Error();
        if (request.operation != IndexOperation::Remove && target->second->records.size() > request.maxRecords)
            return Fail(ErrorCode::ResourceLimit, "Index maintenance exceeded the configured record budget.");

        auto next = std::make_shared<RevisionData>(*data_->current);
        auto collection = std::make_shared<CollectionData>(*target->second);
        collection->schema = schema.Value();
        collection->indexes.erase(request.index);
        IndexMaintenanceResult result{next->revision, {}, 0};
        if (const auto index = collection->schema.indexes.find(request.index); index != collection->schema.indexes.end()) {
            result.definition = index->second;
            auto& entries = collection->indexes[request.index];
            for (const auto& [id, record] : collection->records) {
                if (const auto active = CheckMaintenance(request, result.recordsExamined, true); !active)
                    return active.Error();
                if (auto added = InsertIndexEntry(entries, index->second, record); !added) return added.Error();
                ++result.recordsExamined;
            }
        }
        if (const auto active = CheckMaintenance(request, result.recordsExamined); !active) return active.Error();
        next->collections[request.collection] = std::move(collection);
        data_->schema.at(request.collection) = std::move(schema).Value();
        data_->current = std::move(next);
        return result;
    }

    Result<CommitRevision> Apply(const Transaction& transaction) override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        const auto current = data_->current->revision;
        if (transaction.mutations.empty())
            return Fail(ErrorCode::InvalidArgument, "Cannot commit an empty transaction.");
        if (transaction.expectedRevision && *transaction.expectedRevision != current)
            return Fail(ErrorCode::VersionConflict, "Session revision changed.");
        if (current.value == kPortableRevisionLimit)
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
        while (data_->journal.size() > Policy().journalRevisions)
            data_->journal.pop_front();
        data_->current = std::move(next);
        return revision;
    }

    Result<ChangePage> CaptureChanges(CommitRevision after, std::size_t limit) const override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        const auto floor = data_->HistoryFloor();
        if (auto valid = ValidateHistoryCursor(after, data_->current->revision, floor, limit); !valid)
            return valid.Error();
        const auto begin = data_->journal.begin() + static_cast<std::ptrdiff_t>(after.value - floor.value);
        const auto count = std::min(limit, static_cast<std::size_t>(data_->journal.end() - begin));
        return ChangePage{data_->current->revision, {begin, begin + static_cast<std::ptrdiff_t>(count)}, floor};
    }

    Result<SessionArchive> Export() const override
    {
        std::lock_guard lock(data_->mutex);
        if (closed_)
            return Fail(ErrorCode::SessionClosed, "Session is closed.");
        SessionArchive archive{id_, data_->schema, data_->current->revision, data_->HistoryFloor(),
                               {}, {data_->journal.begin(), data_->journal.end()}};
        for (const auto& [collection, data] : data_->current->collections)
            for (const auto& [id, record] : data->records)
                archive.records.push_back({collection, record});
        return archive;
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
                Capability::ConsistentSnapshots, Capability::LogicalChangeJournal, Capability::PortableQueries,
                Capability::IndexMaintenance};
    }

    Result<std::unique_ptr<Session>> OpenSession(const OpenSessionRequest& request) override
    {
        if (request.id.value.empty())
            return Fail(ErrorCode::InvalidArgument, "Session ID cannot be empty.");
        if (auto valid = ValidatePolicy(request.policy); !valid)
            return valid.Error();
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

        std::unique_ptr<Session> session = std::make_unique<MemorySession>(request.id, data, request.policy);
        data->open = true;
        return session;
    }

    Status Import(const SessionArchive& archive) override
    {
        if (auto valid = ValidateArchive(archive); !valid)
            return valid.Error();
        auto data = std::make_shared<SessionData>(archive.schema);
        auto state = std::make_shared<RevisionData>(*data->current);
        std::map<CollectionKey, std::shared_ptr<CollectionData>> staged;
        for (const auto& entry : archive.records) {
            auto [working, inserted] = staged.try_emplace(entry.collection);
            if (inserted) {
                working->second = std::make_shared<CollectionData>(*state->collections.at(entry.collection));
                state->collections.at(entry.collection) = working->second;
            }
            working->second->records.emplace(entry.record.id, entry.record);
            if (auto indexed = InsertIndexEntries(*working->second, entry.record); !indexed)
                return indexed.Error();
        }
        state->revision = archive.revision;
        data->current = std::move(state);
        data->journal.assign(archive.journal.begin(), archive.journal.end());

        std::lock_guard lock(mutex_);
        if (!sessions_.try_emplace(archive.id, std::move(data)).second)
            return Fail(ErrorCode::RecordAlreadyExists, "Import requires a new session ID.");
        return Null{};
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
