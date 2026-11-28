#include "engine/rocksdb_persistence.h"

#include "persistence_encoding.h"
#include "persistence_validation.h"

#include <rocksdb/advanced_cache.h>
#include <rocksdb/cache.h>
#include <rocksdb/db.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/checkpoint.h>
#include <rocksdb/utilities/write_batch_with_index.h>

#include <fstream>
#include <mutex>
#include <string_view>

namespace ce::persistence
{
namespace
{
using namespace detail;
using namespace encoding;
namespace fs = std::filesystem;

constexpr auto kFormatKey = "Djehuti persistence format";
constexpr auto kFormatValue = "rocksdb/1";
constexpr auto kDatabase = "rocks";
constexpr auto kCheckpointMarker = "djehuti-checkpoint";

template <typename Function>
auto Attempt(Function&& function) -> Result<decltype(function())>
{
    try {
        return function();
    } catch (const OperationError& error) {
        return error;
    } catch (const fs::filesystem_error& error) {
        return OperationError{ErrorCode::StorageFailure, error.what()};
    }
}

void Check(const rocksdb::Status& status, std::string_view operation)
{
    if (!status.ok())
        throw OperationError{ErrorCode::StorageFailure,
                             "RocksDB " + std::string(operation) + ": " + status.ToString()};
}

std::string Path(const fs::path& path)
{
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::span<const std::uint8_t> Bytes(std::string_view value)
{
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}

std::string String(std::span<const std::uint8_t> value)
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

Binary Key(char family, const SessionId& session)
{
    Binary key{static_cast<std::uint8_t>(family)};
    AppendOrderedBytes(key, Bytes(session.value));
    return key;
}

Binary CollectionPrefix(char family, const SessionId& session, const CollectionKey& collection)
{
    auto key = Key(family, session);
    AppendOrderedBytes(key, Bytes(collection.nameSpace.value));
    AppendOrderedBytes(key, Bytes(collection.collection.value));
    return key;
}

std::string RecordKeyBytes(const SessionId& session, const RecordKey& record)
{
    auto key = CollectionPrefix('R', session, record.collection);
    AppendOrderedField(key, record.id.value);
    return String(key);
}

std::string JournalKey(const SessionId& session, std::uint64_t revision)
{
    auto key = Key('J', session);
    AppendInteger(key, revision);
    return String(key);
}

std::string EncodeVersion(CommitRevision revision)
{
    Encoder out;
    out.Integer(revision.value);
    return String(std::move(out).Finish());
}

CommitRevision DecodeVersion(std::string_view bytes)
{
    Decoder in(Bytes(bytes));
    const CommitRevision revision{in.Integer()};
    in.Finish();
    if (revision.value > kPortableRevisionLimit)
        MalformedData();
    return revision;
}

std::string EncodeStoredRecord(const Record& record)
{
    Encoder out;
    out.String(record.id.value);
    out.Integer(record.version.value);
    EncodeData(out, record.data);
    return String(std::move(out).Finish());
}

Record DecodeStoredRecord(std::string_view bytes, const CollectionSchema& schema)
{
    Decoder in(Bytes(bytes));
    Record record{{in.String()}, {in.Integer()}, DecodeData(in)};
    in.Finish();
    if (record.id.value.empty() || record.version.value == 0 || record.version.value > kPortableRevisionLimit
        || !ValidateData(record.data, schema))
        MalformedData();
    return record;
}

const CollectionSchema& Collection(const SessionSchema& schema, const CollectionKey& key)
{
    const auto found = schema.find(key);
    if (found == schema.end())
        throw Fail(ErrorCode::UnknownCollection, "Unknown collection.");
    return found->second;
}

struct Store {
    explicit Store(const RocksDBOptions& configured)
        : options(configured), cache(rocksdb::NewLRUCache(configured.blockCacheKiB * 1024)),
          statistics(rocksdb::CreateDBStatistics()) {}

    RocksDBOptions options;
    std::shared_ptr<rocksdb::Cache> cache;
    std::shared_ptr<rocksdb::Statistics> statistics;
    std::unique_ptr<rocksdb::DB> database;
    std::mutex writer;
    std::set<SessionId> active;
};

rocksdb::WriteOptions Writes(const Store& store)
{
    rocksdb::WriteOptions options;
    options.sync = store.options.durability == RocksDBDurability::Sync;
    return options;
}

std::optional<std::string> Read(rocksdb::DB& db, const rocksdb::ReadOptions& options, const std::string& key)
{
    std::string value;
    const auto status = db.Get(options, key, &value);
    if (status.IsNotFound())
        return std::nullopt;
    Check(status, "read key");
    return value;
}

void RequireFormat(rocksdb::DB& db)
{
    const auto format = Read(db, {}, kFormatKey);
    if (!format || *format != kFormatValue)
        throw Fail(ErrorCode::StorageFailure, "Unsupported or foreign RocksDB persistence format.");
}

std::unique_ptr<rocksdb::DB> OpenReadOnly(const fs::path& path)
{
    std::unique_ptr<rocksdb::DB> db;
    Check(rocksdb::DB::OpenForReadOnly(rocksdb::Options{}, Path(path), &db), "open checkpoint");
    return db;
}

std::shared_ptr<Store> OpenStore(const RocksDBOptions& configured)
{
    auto store = std::make_shared<Store>(configured);
    const auto path = configured.directory / kDatabase;
    fs::create_directories(configured.directory);
    if (fs::exists(path) && !fs::exists(path / "CURRENT") && !fs::is_empty(path))
        throw Fail(ErrorCode::StorageFailure, "Refusing to initialize a nonempty RocksDB directory without a manifest.");

    rocksdb::Options options;
    options.create_if_missing = true;
    options.paranoid_checks = true;
    options.statistics = store->statistics;
    options.write_buffer_size = configured.writeBufferKiB * 1024;
    options.compression = configured.compression == RocksDBCompression::Zlib
                            ? rocksdb::kZlibCompression : rocksdb::kNoCompression;
    rocksdb::BlockBasedTableOptions table;
    table.block_cache = store->cache;
    options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table));
    Check(rocksdb::DB::Open(options, Path(path), &store->database), "open database");
    auto* db = store->database.get();

    if (!Read(*db, {}, kFormatKey)) {
        std::unique_ptr<rocksdb::Iterator> cursor(db->NewIterator({}));
        cursor->SeekToFirst();
        Check(cursor->status(), "check initial database");
        if (cursor->Valid())
            throw Fail(ErrorCode::StorageFailure, "Refusing to initialize a foreign RocksDB database.");
        auto write = Writes(*store);
        write.sync = true;
        Check(db->Put(write, kFormatKey, kFormatValue), "initialize format");
    }
    RequireFormat(*db);
    return store;
}

class PinnedRead {
public:
    explicit PinnedRead(std::shared_ptr<Store> store) : store_(std::move(store))
    {
        options.snapshot = store_->database->GetSnapshot();
        if (!options.snapshot)
            throw Fail(ErrorCode::StorageFailure, "RocksDB could not pin a read snapshot.");
    }
    PinnedRead(const PinnedRead&) = delete;
    ~PinnedRead() { store_->database->ReleaseSnapshot(options.snapshot); }

    rocksdb::DB& Database() const { return *store_->database; }
    rocksdb::ReadOptions options;

private:
    std::shared_ptr<Store> store_;
};

CommitRevision ReadRevision(rocksdb::DB& db, const rocksdb::ReadOptions& read, const SessionId& session)
{
    const auto value = Read(db, read, String(Key('V', session)));
    if (!value)
        MalformedData();
    return DecodeVersion(*value);
}

std::optional<Record> ReadRecord(rocksdb::DB& db, const rocksdb::ReadOptions& options,
                                 const SessionId& id, const RecordKey& key, const CollectionSchema& def)
{
    const auto value = Read(db, options, RecordKeyBytes(id, key));
    if (!value)
        return std::nullopt;
    auto record = DecodeStoredRecord(*value, def);
    if (record.id != key.id)
        MalformedData();
    return record;
}

bool AtPrefix(const rocksdb::Iterator& iterator, const std::string& prefix)
{
    return iterator.Valid() && iterator.key().starts_with(prefix);
}

class RocksSnapshot final : public ReadSnapshot {
public:
    RocksSnapshot(std::shared_ptr<Store> store, SessionId id, std::shared_ptr<const SessionSchema> schema)
        : read_(std::move(store)), id_(std::move(id)), schema_(std::move(schema)),
          revision_(ReadRevision(read_.Database(), read_.options, id_)) {}

    CommitRevision Revision() const noexcept override { return revision_; }
    Result<CollectionSchema> Describe(const CollectionKey& key) const override
    {
        return Attempt([&]() -> CollectionSchema { return Collection(*schema_, key); });
    }

    Result<std::optional<Record>> Get(const RecordKey& key) const override
    {
        return Attempt([&] {
            if (key.id.value.empty())
                throw Fail(ErrorCode::InvalidArgument, "Record ID cannot be empty.");
            return ReadRecord(read_.Database(), read_.options, id_, key, Collection(*schema_, key.collection));
        });
    }

    Result<std::vector<std::optional<Record>>> GetMany(std::span<const RecordKey> keys) const override
    {
        return Attempt([&] {
            std::vector<std::string> names;
            names.reserve(keys.size());
            for (const auto& key : keys) {
                if (key.id.value.empty())
                    throw Fail(ErrorCode::InvalidArgument, "Record ID cannot be empty.");
                Collection(*schema_, key.collection);
                names.push_back(RecordKeyBytes(id_, key));
            }
            std::vector<rocksdb::Slice> slices(names.begin(), names.end());
            std::vector<std::string> values;
            const auto statuses = read_.Database().MultiGet(read_.options, slices, &values);
            std::vector<std::optional<Record>> result;
            result.reserve(keys.size());
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if (statuses[i].IsNotFound()) {
                    result.push_back(std::nullopt);
                    continue;
                }
                Check(statuses[i], "bulk read");
                auto record = DecodeStoredRecord(values[i], schema_->at(keys[i].collection));
                if (record.id != keys[i].id)
                    MalformedData();
                result.push_back(std::move(record));
            }
            return result;
        });
    }

    Result<QueryPage> Query(const QueryDescription& query) const override
    {
        return Attempt([&] {
            const auto& def = Collection(*schema_, query.collection);
            const auto fields = QueryFields(query, def);
            if (!fields)
                throw fields.Error();
            const auto* definition = SelectedIndex(query, def);
            if (auto valid = ValidateQuery(query, fields.Value(), id_, revision_, definition); !valid)
                throw valid.Error();
            auto base = CollectionPrefix(query.index ? 'I' : 'R', id_, query.collection);
            if (query.index)
                AppendOrderedBytes(base, Bytes(query.index->value));
            const auto prefix = String(base);
            const auto range = MakeOrderedRange(std::move(base), query, definition, true);
            QueryPage page;
            if (range.empty) return page;
            const auto first = String(range.first);
            const auto past = range.past ? std::optional{String(*range.past)} : std::nullopt;
            const bool reverse = query.direction == SortDirection::Descending;
            std::unique_ptr<rocksdb::Iterator> rows(read_.Database().NewIterator(read_.options));
            if (!reverse) {
                rows->Seek(first);
            } else if (past) {
                rows->SeekForPrev(*past);
                if (rows->Valid() && rows->key() == rocksdb::Slice(*past))
                    rows->Prev();
            } else {
                rows->SeekToLast();
            }
            while (AtPrefix(*rows, prefix) && rows->key().compare(first) >= 0
                   && (!past || rows->key().compare(*past) < 0)) {
                if (page.records.size() == query.limit) {
                    const auto& last = page.records.back();
                    auto key = definition ? ProjectIndexKey(*definition, last.data)
                                           : std::optional<IndexKey>{{last.id.value}};
                    if (!key)
                        MalformedData();
                    page.next = MakeCursor(query, id_, revision_, definition, std::move(*key), last.id);
                    break;
                }
                if (query.index) {
                    const RecordKey key{query.collection, {rows->value().ToString()}};
                    auto record = ReadRecord(read_.Database(), read_.options, id_, key, def);
                    if (!record)
                        MalformedData();
                    page.records.push_back(std::move(*record));
                } else {
                    page.records.push_back(DecodeStoredRecord(rows->value().ToStringView(), def));
                }
                if (reverse) rows->Prev();
                else rows->Next();
            }
            Check(rows->status(), "scan index");
            return page;
        });
    }

private:
    PinnedRead read_;
    SessionId id_;
    std::shared_ptr<const SessionSchema> schema_;
    CommitRevision revision_;
};

CommitRevision HistoryFloor(const PinnedRead& read, const SessionId& id, CommitRevision latest)
{
    const auto prefix = String(Key('J', id));
    std::unique_ptr<rocksdb::Iterator> rows(read.Database().NewIterator(read.options));
    rows->Seek(prefix);
    Check(rows->status(), "read journal floor");
    if (!AtPrefix(*rows, prefix))
        return latest;
    const auto first = DecodeVersion(rows->key().ToStringView().substr(prefix.size()));
    if (first.value == 0 || first > latest)
        MalformedData();
    return {first.value - 1};
}

ChangePage Journal(const PinnedRead& read, const SessionId& id, CommitRevision after,
                   CommitRevision latest, CommitRevision floor, std::size_t limit)
{
    if (auto valid = ValidateHistoryCursor(after, latest, floor, limit); !valid)
        throw valid.Error();
    const auto prefix = String(Key('J', id));
    std::unique_ptr<rocksdb::Iterator> rows(read.Database().NewIterator(read.options));
    ChangePage page{latest, {}, floor};
    for (rows->Seek(JournalKey(id, after.value + 1)); AtPrefix(*rows, prefix) && page.batches.size() < limit; rows->Next()) {
        const auto revision = DecodeVersion(rows->key().ToStringView().substr(prefix.size()));
        if (revision.value != after.value + 1 || revision > latest)
            MalformedData();
        page.batches.push_back(DecodeChanges(revision, Bytes(rows->value().ToStringView())));
        after = revision;
    }
    Check(rows->status(), "read journal");
    if (page.batches.size() < limit && after != latest)
        MalformedData();
    return page;
}

Binary IndexPrefix(char family, const SessionId& session, const CollectionKey& collection, const IndexId& index)
{
    auto key = CollectionPrefix(family, session, collection);
    AppendOrderedBytes(key, Bytes(index.value));
    return key;
}

std::string IndexEntryKey(char family, const SessionId& session, const RecordKey& record,
                          const IndexId& index, const IndexDefinition& definition, const IndexKey& fields)
{
    auto key = IndexPrefix(family, session, record.collection, index);
    AppendOrderedKey(key, fields, &definition);
    if (family == 'I')
        AppendOrderedField(key, record.id.value);
    return String(key);
}

void RemoveKeys(rocksdb::WriteBatchWithIndex& batch, const SessionId& session, const RecordKey& key,
                const CollectionSchema& def, const Record& previous)
{
    for (const auto& [id, index] : def.indexes) {
        if (const auto fields = ProjectIndexKey(index, previous.data)) {
            Check(batch.Delete(IndexEntryKey('I', session, key, id, index, *fields)), "stage index erase");
            if (index.unique)
                Check(batch.Delete(IndexEntryKey('U', session, key, id, index, *fields)), "stage unique key erase");
        }
    }
}

void AddIndexKeys(rocksdb::DB& database, rocksdb::WriteBatchWithIndex& batch, const SessionId& session,
                  const RecordKey& key, const IndexId& id, const IndexDefinition& index, const Record& record)
{
    const auto fields = ProjectIndexKey(index, record.data);
    if (!fields) return;
    if (index.unique) {
        const auto unique = IndexEntryKey('U', session, key, id, index, *fields);
        std::string existing;
        const auto found = batch.GetFromBatchAndDB(&database, rocksdb::ReadOptions{}, unique, &existing);
        if (found.ok())
            throw Fail(ErrorCode::UniqueConstraint, "Unique index key already exists.");
        if (!found.IsNotFound())
            Check(found, "validate unique key");
        Check(batch.Put(unique, key.id.value), "stage unique key");
    }
    Check(batch.Put(IndexEntryKey('I', session, key, id, index, *fields), key.id.value), "stage index key");
}

void AddKeys(rocksdb::DB& database, rocksdb::WriteBatchWithIndex& batch, const SessionId& session,
             const RecordKey& key, const CollectionSchema& def, const Record& record)
{
    for (const auto& [id, index] : def.indexes)
        AddIndexKeys(database, batch, session, key, id, index, record);
}

void StoreSchema(rocksdb::WriteBatch& batch, const SessionId& id, const SessionSchema& schema, CommitRevision revision)
{
    Check(batch.Put(String(Key('S', id)), String(EncodeSchema(schema))), "stage session schema");
    Check(batch.Put(String(Key('V', id)), EncodeVersion(revision)), "stage session revision");
}

void StoreJournal(rocksdb::WriteBatch& batch, const SessionId& id, const ChangeBatch& changes)
{
    Check(batch.Put(JournalKey(id, changes.revision.value), String(EncodeChanges(changes))), "stage journal entry");
}

class RocksSession final : public Session {
public:
    RocksSession(std::shared_ptr<Store> store, const OpenSessionRequest& request)
        : Session(request.policy), store_(std::move(store)), id_(request.id),
          schema_(std::make_shared<const SessionSchema>(request.schema)) {}

    ~RocksSession() override { static_cast<void>(Close()); }

    Status Close() override
    {
        std::lock_guard lock(store_->writer);
        if (!closed_) {
            store_->active.erase(id_);
            closed_ = true;
        }
        return Null{};
    }

    Result<SessionArchive> Export() const override
    {
        return Attempt([&] {
            std::unique_lock lock(store_->writer);
            RequireOpen();
            PinnedRead read(store_);
            const auto schemaView = schema_;
            lock.unlock();
            const auto latest = ReadRevision(read.Database(), read.options, id_);
            const auto floor = HistoryFloor(read, id_, latest);
            SessionArchive image{id_, *schemaView, latest, floor, {},
                                 Journal(read, id_, floor, latest, floor, kPortableRevisionLimit).batches};
            std::unique_ptr<rocksdb::Iterator> rows(read.Database().NewIterator(read.options));
            for (const auto& [collection, schema] : *schemaView) {
                const auto prefix = String(CollectionPrefix('R', id_, collection));
                for (rows->Seek(prefix); AtPrefix(*rows, prefix); rows->Next())
                    image.records.push_back({collection, DecodeStoredRecord(rows->value().ToStringView(), schema)});
                Check(rows->status(), "export collection");
            }
            return image;
        });
    }

private:
    Result<std::unique_ptr<ReadSnapshot>> CaptureSnapshot() const override
    {
        return Attempt([&]() -> std::unique_ptr<ReadSnapshot> {
            std::lock_guard lock(store_->writer);
            RequireOpen();
            return std::make_unique<RocksSnapshot>(store_, id_, schema_);
        });
    }

    Result<ChangePage> CaptureChanges(CommitRevision after, std::size_t limit) const override
    {
        return Attempt([&] {
            PinnedRead read(ReadStore());
            const auto latest = ReadRevision(read.Database(), read.options, id_);
            return Journal(read, id_, after, latest, HistoryFloor(read, id_, latest), limit);
        });
    }

    Result<IndexMaintenanceResult> MaintainIndex(const IndexMaintenance& request) override
    {
        return Attempt([&] {
            std::lock_guard lock(store_->writer);
            RequireOpen();
            auto& db = *store_->database;
            auto updated = MaintainDefinition(Collection(*schema_, request.collection), request);
            if (!updated) throw updated.Error();
            auto next = std::make_shared<SessionSchema>(*schema_);
            auto& collection = next->at(request.collection);
            collection = std::move(updated).Value();
            IndexMaintenanceResult result{ReadRevision(db, {}, id_), {}, 0};
            const auto index = collection.indexes.find(request.index);
            if (index == collection.indexes.end()) {
                rocksdb::WriteBatch remove;
                for (const char family : {'I', 'U'}) {
                    const auto prefix = IndexPrefix(family, id_, request.collection, request.index);
                    Check(remove.DeleteRange(String(prefix), String(*PrefixEnd(prefix))), "remove index key range");
                }
                StoreSchema(remove, id_, *next, result.revision);
                if (auto active = CheckMaintenance(request, 0); !active) throw active.Error();
                Check(db.Write(Writes(*store_), &remove), "remove index");
            } else {
                rocksdb::WriteBatchWithIndex batch(rocksdb::BytewiseComparator(), 0, true);
                std::unique_ptr<rocksdb::Iterator> rows(db.NewIterator({}));
                if (request.operation == IndexOperation::Rebuild) {
                    for (const char family : {'I', 'U'}) {
                        const auto prefix = String(IndexPrefix(family, id_, request.collection, request.index));
                        std::size_t erased = 0;
                        for (rows->Seek(prefix); AtPrefix(*rows, prefix); rows->Next()) {
                            if (auto active = CheckMaintenance(request, erased++, true); !active) throw active.Error();
                            Check(batch.Delete(rows->key()), "clear old index key");
                        }
                        Check(rows->status(), "read old index entries");
                    }
                }
                result.definition = index->second;
                const auto prefix = String(CollectionPrefix('R', id_, request.collection));
                for (rows->Seek(prefix); AtPrefix(*rows, prefix); rows->Next()) {
                    if (auto active = CheckMaintenance(request, result.recordsExamined, true); !active) throw active.Error();
                    const auto record = DecodeStoredRecord(rows->value().ToStringView(), collection);
                    AddIndexKeys(db, batch, id_, {request.collection, record.id}, request.index, index->second, record);
                    ++result.recordsExamined;
                }
                Check(rows->status(), "read index source records");
                StoreSchema(*batch.GetWriteBatch(), id_, *next, result.revision);
                if (auto active = CheckMaintenance(request, result.recordsExamined); !active) throw active.Error();
                Check(db.Write(Writes(*store_), batch.GetWriteBatch()), "publish index rebuild");
            }
            schema_ = std::move(next);
            return result;
        });
    }

    Result<CommitRevision> Apply(const Transaction& transaction) override
    {
        return Attempt([&] {
            std::lock_guard lock(store_->writer);
            RequireOpen();
            if (transaction.mutations.empty())
                throw Fail(ErrorCode::InvalidArgument, "Cannot commit an empty transaction.");
            auto& db = *store_->database;
            const auto current = ReadRevision(db, {}, id_);
            if (transaction.expectedRevision && *transaction.expectedRevision != current)
                throw Fail(ErrorCode::VersionConflict, "Session revision changed.");
            if (current.value == kPortableRevisionLimit)
                throw Fail(ErrorCode::RevisionExhausted, "RocksDB commit revision exhausted.");

            rocksdb::WriteBatchWithIndex batch(rocksdb::BytewiseComparator(), 0, true);
            ChangeBatch changes{{current.value + 1}, {}};
            std::set<RecordKey> touched;
            for (const auto& mutation : transaction.mutations) {
                if (!touched.insert(mutation.key).second)
                    throw Fail(ErrorCode::InvalidArgument, "A transaction may change a record only once.");
                const auto& def = Collection(*schema_, mutation.key.collection);
                const auto previous = ReadRecord(db, {}, id_, mutation.key, def);
                const auto version = previous ? std::optional{previous->version} : std::nullopt;
                if (auto valid = ValidateMutation(mutation, def, version); !valid)
                    throw valid.Error();
                const auto key = RecordKeyBytes(id_, mutation.key);
                if (previous)
                    RemoveKeys(batch, id_, mutation.key, def, *previous);
                RecordChange change{mutation.kind, mutation.key, version, std::nullopt};
                if (mutation.data) {
                    change.record = Record{mutation.key.id, {changes.revision.value}, *mutation.data};
                    Check(batch.Put(key, EncodeStoredRecord(*change.record)), "stage record");
                } else {
                    Check(batch.Delete(key), "stage record erase");
                }
                changes.changes.push_back(std::move(change));
            }
            // Remove the whole batch's old entries before checking final uniqueness.
            for (const auto& change : changes.changes)
                if (change.record)
                    AddKeys(db, batch, id_, change.key, schema_->at(change.key.collection), *change.record);
            auto& write = *batch.GetWriteBatch();
            Check(write.Put(String(Key('V', id_)), EncodeVersion(changes.revision)), "stage revision");
            StoreJournal(write, id_, changes);
            const auto floor = RetentionFloor(changes.revision, Policy().journalRevisions);
            if (floor.value)
                Check(write.DeleteRange(String(Key('J', id_)), JournalKey(id_, floor.value + 1)), "stage journal pruning");
            Check(db.Write(Writes(*store_), &write), "commit persistence transaction");
            return changes.revision;
        });
    }

    void RequireOpen() const
    {
        if (closed_)
            throw Fail(ErrorCode::SessionClosed, "Session is closed.");
    }

    std::shared_ptr<Store> ReadStore() const
    {
        std::lock_guard lock(store_->writer);
        RequireOpen();
        return store_;
    }

    std::shared_ptr<Store> store_;
    SessionId id_;
    std::shared_ptr<const SessionSchema> schema_;
    bool closed_ = false;
};

void ReserveDirectory(const fs::path& path)
{
    if (path.empty() || !fs::create_directory(path))
        throw Fail(ErrorCode::StorageFailure, "Backup or restore requires a new directory.");
}

class RocksProvider final : public RocksDBStorageProvider {
public:
    explicit RocksProvider(std::shared_ptr<Store> store) : store_(std::move(store)) {}

    CapabilitySet Capabilities() const override
    {
        return {Capability::PrimaryQueries, Capability::SecondaryIndexes, Capability::AtomicTransactions,
                Capability::ConsistentSnapshots, Capability::LogicalChangeJournal, Capability::DurablePersistence,
                Capability::PortableQueries, Capability::IndexMaintenance};
    }

    Result<std::unique_ptr<Session>> OpenSession(const OpenSessionRequest& request) override
    {
        return Attempt([&]() -> std::unique_ptr<Session> {
            if (request.id.value.empty())
                throw Fail(ErrorCode::InvalidArgument, "Session ID cannot be empty.");
            if (!std::ranges::includes(Capabilities(), request.requiredCapabilities))
                throw Fail(ErrorCode::UnsupportedOperation, "The provider lacks a required capability.");
            if (auto valid = ValidatePolicy(request.policy); !valid)
                throw valid.Error();
            if (auto valid = ValidateSchema(request.schema); !valid)
                throw valid.Error();

            std::lock_guard lock(store_->writer);
            if (store_->active.contains(request.id))
                throw Fail(ErrorCode::SessionAlreadyOpen, "Session already has an open writer.");
            auto& db = *store_->database;
            const auto stored = Read(db, {}, String(Key('S', request.id)));
            if (stored) {
                if (*stored != String(EncodeSchema(request.schema)))
                    throw Fail(ErrorCode::SchemaMismatch, "Session schema differs from the stored definition.");
                ReadRevision(db, {}, request.id);
            } else {
                rocksdb::WriteBatch batch;
                StoreSchema(batch, request.id, request.schema, {});
                Check(db.Write(Writes(*store_), &batch), "create persistence session");
            }
            store_->active.insert(request.id);
            try {
                return std::make_unique<RocksSession>(store_, request);
            } catch (...) {
                store_->active.erase(request.id);
                throw;
            }
        });
    }

    Status Import(const SessionArchive& archive) override
    {
        return Attempt([&] {
            if (auto valid = ValidateArchive(archive); !valid)
                throw valid.Error();
            std::lock_guard lock(store_->writer);
            auto& db = *store_->database;
            if (Read(db, {}, String(Key('S', archive.id))))
                throw Fail(ErrorCode::RecordAlreadyExists, "Import requires a new session ID.");
            rocksdb::WriteBatchWithIndex batch(rocksdb::BytewiseComparator(), 0, true);
            for (const auto& entry : archive.records) {
                const RecordKey key{entry.collection, entry.record.id};
                Check(batch.Put(RecordKeyBytes(archive.id, key), EncodeStoredRecord(entry.record)), "stage imported record");
                AddKeys(db, batch, archive.id, key, archive.schema.at(entry.collection), entry.record);
            }
            auto& write = *batch.GetWriteBatch();
            StoreSchema(write, archive.id, archive.schema, archive.revision);
            for (const auto& changes : archive.journal)
                StoreJournal(write, archive.id, changes);
            Check(db.Write(Writes(*store_), &write), "publish imported session");
            return Null{};
        });
    }

    Status CreateBackup(const fs::path& destination) const override
    {
        return Attempt([&] {
            ReserveDirectory(destination);
            rocksdb::Checkpoint* raw = nullptr;
            Check(rocksdb::Checkpoint::Create(store_->database.get(), &raw), "prepare checkpoint");
            const std::unique_ptr<rocksdb::Checkpoint> checkpoint(raw);
            const auto path = destination / kDatabase;
            Check(checkpoint->CreateCheckpoint(Path(path)), "create checkpoint");
            RequireFormat(*OpenReadOnly(path));
            std::ofstream marker(destination / kCheckpointMarker, std::ios::binary);
            marker << kFormatValue;
            marker.close();
            if (!marker)
                throw Fail(ErrorCode::StorageFailure, "Could not publish the RocksDB checkpoint marker.");
            return Null{};
        });
    }

    RocksDBMetrics Metrics() const override
    {
        const auto& stats = *store_->statistics;
        return {stats.getTickerCount(rocksdb::BYTES_WRITTEN), stats.getTickerCount(rocksdb::WAL_FILE_BYTES),
                stats.getTickerCount(rocksdb::FLUSH_WRITE_BYTES), stats.getTickerCount(rocksdb::COMPACT_WRITE_BYTES),
                store_->cache->GetUsage()};
    }

    Status FlushAndCompact() override
    {
        return Attempt([&] {
            rocksdb::FlushOptions flush;
            flush.wait = true;
            Check(store_->database->Flush(flush), "flush memtables");
            Check(store_->database->CompactRange(rocksdb::CompactRangeOptions{}, nullptr, nullptr), "compact store");
            return Null{};
        });
    }

private:
    std::shared_ptr<Store> store_;
};
} // namespace

Result<std::unique_ptr<RocksDBStorageProvider>> CreateRocksDBProvider(const RocksDBOptions& options)
{
    return Attempt([&]() -> std::unique_ptr<RocksDBStorageProvider> {
        constexpr auto maxKiB = std::numeric_limits<std::size_t>::max() / 1024;
        if (options.directory.empty() || options.blockCacheKiB == 0 || options.blockCacheKiB > maxKiB
            || options.writeBufferKiB == 0 || options.writeBufferKiB > maxKiB
            || (options.compression != RocksDBCompression::None && options.compression != RocksDBCompression::Zlib)
            || (options.durability != RocksDBDurability::Sync && options.durability != RocksDBDurability::Process))
            throw Fail(ErrorCode::InvalidArgument, "Invalid RocksDB directory, cache, buffer, compression, or durability.");
        auto configured = options;
        configured.directory = fs::absolute(options.directory);
        return std::make_unique<RocksProvider>(OpenStore(configured));
    });
}

Status RestoreRocksDBBackup(const fs::path& backup, const fs::path& destination)
{
    return Attempt([&] {
        if (backup.empty() || destination.empty())
            throw Fail(ErrorCode::InvalidArgument, "RocksDB backup and restore directories are required.");
        std::ifstream marker(backup / kCheckpointMarker, std::ios::binary);
        std::string version;
        if (!std::getline(marker, version) || version != kFormatValue)
            throw Fail(ErrorCode::StorageFailure, "Expected a completed RocksDB checkpoint.");
        const auto source = backup / kDatabase;
        RequireFormat(*OpenReadOnly(source));
        ReserveDirectory(destination);
        const auto staged = destination / "pending";
        fs::copy(source, staged, fs::copy_options::recursive | fs::copy_options::skip_symlinks);
        RequireFormat(*OpenReadOnly(staged));
        fs::rename(staged, destination / kDatabase);
        return Null{};
    });
}
} // namespace ce::persistence
