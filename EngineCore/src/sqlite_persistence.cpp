#include "sqlite_persistence_database.h"
#include "sqlite_persistence_codec.h"

#include <mutex>

namespace ce::persistence
{
namespace
{
using namespace detail;
using namespace sqlite_detail;

constexpr std::uint64_t kMaxRevision = std::numeric_limits<std::int64_t>::max();

const CollectionSchema& GetCollection(const SessionSchema& schema, const CollectionKey& key)
{
    const auto found = schema.find(key);
    if (found == schema.end())
        throw Fail(ErrorCode::UnknownCollection, "Unknown collection.");
    return found->second;
}

void BindCollection(Statement& statement, const SessionId& session, const CollectionKey& collection)
{
    statement.Bind(1, session.value);
    statement.Bind(2, collection.nameSpace.value);
    statement.Bind(3, collection.collection.value);
}

void BindRecord(Statement& statement, const SessionId& session, const RecordKey& key)
{
    BindCollection(statement, session, key.collection);
    statement.Bind(4, key.id.value);
}

CommitRevision ReadRevision(const Database& database, const SessionId& session)
{
    Statement read(database, "SELECT revision FROM sessions WHERE id=?", "read session revision");
    read.Bind(1, session.value);
    if (!read.Step())
        StorageFailure("SQLite persistence session metadata is missing.");
    return {read.Unsigned(0)};
}

Record ReadRecord(const Statement& row, const CollectionSchema& schema)
{
    Record record{{row.Bytes(0)}, {row.Unsigned(1)}, DecodeRecord(row.Blob(2))};
    if (record.id.value.empty() || record.version.value == 0 || !ValidateData(record.data, schema))
        MalformedData();
    return record;
}

Binary AsBytes(const std::string& value)
{
    return {value.begin(), value.end()};
}

std::int64_t SqlLimit(std::size_t limit, bool probeNext = false)
{
    const auto extra = probeNext ? 1u : 0u;
    return static_cast<std::int64_t>(std::min<std::uint64_t>(limit, kMaxRevision - extra) + extra);
}

struct QueryPlan {
    std::string sql;
    std::vector<Binary> arguments;

    void Bound(std::string_view column, std::string_view condition, Binary value)
    {
        sql += " AND ";
        sql += column;
        sql += condition;
        sql += "?";
        arguments.push_back(std::move(value));
    }
};

QueryPlan PlanPrimary(const QueryDescription& query)
{
    QueryPlan plan{"SELECT record_id, version, payload FROM records "
                   "WHERE session_id=? AND namespace_id=? AND collection_id=?", {}};
    if (!query.prefix.empty())
        plan.Bound("record_id", "=", AsBytes(std::get<std::string>(query.prefix.front())));
    if (query.lower)
        plan.Bound("record_id", query.lower->inclusive ? ">=" : ">", AsBytes(std::get<std::string>(query.lower->value)));
    if (query.upper)
        plan.Bound("record_id", query.upper->inclusive ? "<=" : "<", AsBytes(std::get<std::string>(query.upper->value)));
    if (query.after)
        plan.Bound("record_id", ">", AsBytes(query.after->id.value));
    plan.sql += " ORDER BY record_id LIMIT ?";
    return plan;
}

QueryPlan PlanSecondary(const QueryDescription& query)
{
    QueryPlan plan{"SELECT r.record_id, r.version, r.payload FROM record_indexes AS i "
                   "JOIN records AS r USING (session_id, namespace_id, collection_id, record_id) "
                   "WHERE i.session_id=? AND i.namespace_id=? AND i.collection_id=? AND i.index_id=?",
                   {AsBytes(query.index->value)}};
    const auto prefix = EncodeOrderedKey(query.prefix);
    auto lower = prefix;
    bool lowerIsExclusive = false;
    if (query.lower) {
        AppendOrderedField(lower, query.lower->value);
        lowerIsExclusive = !query.lower->inclusive;
    }
    auto upper = PrefixEnd(prefix);
    if (query.upper) {
        auto bound = prefix;
        AppendOrderedField(bound, query.upper->value);
        upper = query.upper->inclusive ? PrefixEnd(std::move(bound)) : std::optional{std::move(bound)};
    }

    // Exclusive lower bounds skip every tuple with the excluded component,
    // including keys that continue with more fields.
    if (lowerIsExclusive) {
        if (auto next = PrefixEnd(std::move(lower)))
            lower = std::move(*next);
        else
            plan.sql += " AND 0";
    }
    if (!lower.empty())
        plan.Bound("i.sort_key", ">=", std::move(lower));
    if (upper)
        plan.Bound("i.sort_key", "<", std::move(*upper));
    if (query.after) {
        plan.sql += " AND (i.sort_key, i.record_id) > (?, ?)";
        plan.arguments.push_back(EncodeOrderedKey(query.after->key));
        plan.arguments.push_back(AsBytes(query.after->id.value));
    }
    plan.sql += " ORDER BY i.sort_key, i.record_id LIMIT ?";
    return plan;
}

class SQLiteSnapshot final : public ReadSnapshot {
public:
    SQLiteSnapshot(const SQLiteOptions& options, SessionId id, std::shared_ptr<const SessionSchema> schema)
        : db_(Connect(options, SQLITE_OPEN_READONLY)), id_(std::move(id)), schema_(std::move(schema))
    {
        db_.Execute("BEGIN", "begin read snapshot");
        revision_ = ReadRevision(db_, id_);
    }

    CommitRevision Revision() const noexcept override { return revision_; }

    Result<std::optional<Record>> Get(const RecordKey& key) const override
    {
        return ResultOf([&]() -> std::optional<Record> {
            std::lock_guard lock(mutex_);
            if (key.id.value.empty())
                throw Fail(ErrorCode::InvalidArgument, "Record ID cannot be empty.");
            const auto& schema = GetCollection(*schema_, key.collection);
            Statement read(db_, "SELECT record_id, version, payload FROM records "
                                "WHERE session_id=? AND namespace_id=? AND collection_id=? AND record_id=?",
                           "read record");
            BindRecord(read, id_, key);
            if (!read.Step())
                return std::nullopt;
            return ReadRecord(read, schema);
        });
    }

    Result<QueryPage> Query(const QueryDescription& query) const override
    {
        return ResultOf([&] {
            std::lock_guard lock(mutex_);
            const auto& schema = GetCollection(*schema_, query.collection);
            const auto fields = QueryFields(query, schema);
            if (!fields)
                throw fields.Error();
            if (const auto valid = ValidateQuery(query, fields.Value(), id_, revision_); !valid)
                throw valid.Error();

            const auto plan = query.index ? PlanSecondary(query) : PlanPrimary(query);
            Statement scan(db_, plan.sql.c_str(), "query indexed records");
            BindCollection(scan, id_, query.collection);
            int parameter = 4;
            for (const auto& argument : plan.arguments)
                scan.Bind(parameter++, argument);
            scan.Bind(parameter, SqlLimit(query.limit, true));

            QueryPage result;
            while (scan.Step()) {
                if (result.records.size() == query.limit) {
                    const auto& last = result.records.back();
                    auto key = query.index ? ProjectIndexKey(schema.indexes.at(*query.index), last.data)
                                           : std::optional<IndexKey>{{last.id.value}};
                    if (!key)
                        MalformedData();
                    result.next = QueryCursor{id_, revision_, query.collection, query.index, std::move(*key), last.id};
                    break;
                }
                result.records.push_back(ReadRecord(scan, schema));
            }
            return result;
        });
    }

private:
    Database db_;
    SessionId id_;
    std::shared_ptr<const SessionSchema> schema_;
    CommitRevision revision_;
    mutable std::mutex mutex_;
};

std::optional<RecordVersion> FindRecordVersion(const Database& database, const SessionId& id,
                                              const RecordKey& key)
{
    Statement read(database, "SELECT version FROM records "
                             "WHERE session_id=? AND namespace_id=? AND collection_id=? AND record_id=?",
                   "read record version");
    BindRecord(read, id, key);
    if (!read.Step())
        return std::nullopt;
    return RecordVersion{read.Unsigned(0)};
}

void ApplyRecord(const Database& database, const SessionId& session, const RecordChange& change)
{
    if (!change.record) {
        Statement erase(database, "DELETE FROM records WHERE session_id=? AND namespace_id=? "
                                  "AND collection_id=? AND record_id=?", "erase record");
        BindRecord(erase, session, change.key);
        erase.Run();
        return;
    }
    if (change.previousVersion) {
        Statement remove(database, "DELETE FROM record_indexes WHERE session_id=? AND namespace_id=? "
                                   "AND collection_id=? AND record_id=?", "remove prior index keys");
        BindRecord(remove, session, change.key);
        remove.Run();
    }

    Statement store(database, "INSERT INTO records VALUES (?, ?, ?, ?, ?, ?) "
                              "ON CONFLICT(session_id, namespace_id, collection_id, record_id) "
                              "DO UPDATE SET version=excluded.version, payload=excluded.payload",
                    "store record");
    BindRecord(store, session, change.key);
    store.Bind(5, static_cast<std::int64_t>(change.record->version.value));
    store.Bind(6, EncodeRecord(change.record->data));
    store.Run();
}

void IndexRecord(const Database& database, const SessionId& session, const CollectionSchema& schema,
                 const RecordChange& change)
{
    if (!change.record)
        return;
    for (const auto& [id, definition] : schema.indexes) {
        const auto key = ProjectIndexKey(definition, change.record->data);
        if (!key)
            continue;
        Statement insert(database, "INSERT INTO record_indexes VALUES (?, ?, ?, ?, ?, ?, ?)", "store index key");
        BindCollection(insert, session, change.key.collection);
        insert.Bind(4, id.value);
        insert.Bind(5, EncodeOrderedKey(*key));
        insert.Bind(6, change.key.id.value);
        insert.Bind(7, static_cast<std::int64_t>(definition.unique));
        insert.Run();
    }
}

void WriteRevision(const Database& database, const SessionId& session, const ChangeBatch& batch)
{
    const auto revision = static_cast<std::int64_t>(batch.revision.value);
    Statement insert(database, "INSERT INTO changes VALUES (?, ?, ?)", "append change journal");
    insert.Bind(1, session.value);
    insert.Bind(2, revision);
    insert.Bind(3, EncodeChanges(batch));
    insert.Run();

    Statement update(database, "UPDATE sessions SET revision=? WHERE id=?", "advance session revision");
    update.Bind(1, revision);
    update.Bind(2, session.value);
    update.Run();
}

struct WriterRegistry {
    std::mutex mutex;
    std::set<SessionId> open;
};

class WriterLease {
public:
    WriterLease(std::shared_ptr<WriterRegistry> registry, const SessionId& id)
        : registry_(std::move(registry)), id_(id)
    {
        std::lock_guard lock(registry_->mutex);
        if (!registry_->open.insert(id_).second)
            throw Fail(ErrorCode::SessionAlreadyOpen, "Session already has an open writer.");
    }
    WriterLease(const WriterLease&) = delete;
    WriterLease& operator=(const WriterLease&) = delete;
    ~WriterLease()
    {
        std::lock_guard lock(registry_->mutex);
        registry_->open.erase(id_);
    }

private:
    std::shared_ptr<WriterRegistry> registry_;
    SessionId id_;
};

class SQLiteSession final : public Session {
public:
    SQLiteSession(SQLiteOptions options, const OpenSessionRequest& request,
                  std::unique_ptr<WriterLease> lease, Database database)
        : options_(std::move(options)), id_(request.id), schema_(std::make_shared<const SessionSchema>(request.schema)),
          lease_(std::move(lease)), writer_(std::move(database)) {}

    Status Close() override
    {
        std::lock_guard lock(mutex_);
        writer_.reset();
        lease_.reset();
        return Null{};
    }

    Result<std::unique_ptr<ReadSnapshot>> OpenSnapshot() const override
    {
        return ResultOf([&]() -> std::unique_ptr<ReadSnapshot> {
            std::lock_guard lock(mutex_);
            RequireOpen();
            return std::make_unique<SQLiteSnapshot>(options_, id_, schema_);
        });
    }

    Result<CommitRevision> Commit(const Transaction& transaction) override
    {
        return ResultOf([&] {
            std::lock_guard lock(mutex_);
            RequireOpen();
            if (transaction.mutations.empty())
                throw Fail(ErrorCode::InvalidArgument, "Cannot commit an empty transaction.");
            auto& database = *writer_;
            WriteTransaction write(database);
            const auto current = ReadRevision(database, id_);
            if (transaction.expectedRevision && *transaction.expectedRevision != current)
                throw Fail(ErrorCode::VersionConflict, "Session revision changed.");
            if (current.value == kMaxRevision)
                throw Fail(ErrorCode::RevisionExhausted, "SQLite commit revision exhausted.");

            ChangeBatch batch{{current.value + 1}, {}};
            batch.changes.reserve(transaction.mutations.size());
            std::set<RecordKey> changed;
            for (const auto& mutation : transaction.mutations) {
                if (!changed.insert(mutation.key).second)
                    throw Fail(ErrorCode::InvalidArgument, "A transaction may change a record only once.");
                const auto& schema = GetCollection(*schema_, mutation.key.collection);
                const auto previous = FindRecordVersion(database, id_, mutation.key);
                if (const auto valid = ValidateMutation(mutation, schema, previous); !valid)
                    throw valid.Error();

                RecordChange change{mutation.kind, mutation.key, previous, std::nullopt};
                if (mutation.data)
                    change.record = Record{mutation.key.id, {batch.revision.value}, *mutation.data};
                ApplyRecord(database, id_, change);
                batch.changes.push_back(std::move(change));
            }
            // Check uniqueness after all former entries have been removed.
            for (const auto& change : batch.changes)
                IndexRecord(database, id_, schema_->at(change.key.collection), change);
            WriteRevision(database, id_, batch);
            write.Commit();
            return batch.revision;
        });
    }

    Result<ChangePage> ReadChanges(CommitRevision after, std::size_t limit) const override
    {
        return ResultOf([&] {
            auto database = OpenReader();
            database.Execute("BEGIN", "begin journal snapshot");
            const auto latest = ReadRevision(database, id_);
            if (limit == 0 || after > latest)
                throw Fail(ErrorCode::InvalidArgument, "Invalid journal limit or starting revision.");

            ChangePage result{latest, {}};
            Statement read(database, "SELECT revision, payload FROM changes WHERE session_id=? AND revision>? "
                                     "ORDER BY revision LIMIT ?", "read change journal");
            read.Bind(1, id_.value);
            read.Bind(2, static_cast<std::int64_t>(after.value));
            read.Bind(3, SqlLimit(limit));
            while (read.Step()) {
                const CommitRevision revision{read.Unsigned(0)};
                if (revision.value != after.value + 1 || revision > latest)
                    MalformedData();
                result.batches.push_back(DecodeChanges(revision, read.Blob(1)));
                after = revision;
            }
            if (result.batches.size() < limit && after != latest)
                MalformedData();
            return result;
        });
    }

private:
    void RequireOpen() const
    {
        if (!writer_)
            throw Fail(ErrorCode::SessionClosed, "Session is closed.");
    }

    Database OpenReader() const
    {
        std::lock_guard lock(mutex_);
        RequireOpen();
        return Connect(options_, SQLITE_OPEN_READONLY);
    }

    SQLiteOptions options_;
    SessionId id_;
    std::shared_ptr<const SessionSchema> schema_;
    mutable std::mutex mutex_;
    std::unique_ptr<WriterLease> lease_;
    std::optional<Database> writer_;
};

void OpenOrCreateSession(const Database& database, const OpenSessionRequest& request)
{
    WriteTransaction transaction(database);
    const auto definition = EncodeSchema(request.schema);
    bool exists;
    {
        Statement read(database, "SELECT definition FROM sessions WHERE id=?", "read session definition");
        read.Bind(1, request.id.value);
        exists = read.Step();
        if (exists && !std::ranges::equal(definition, read.Blob(0)))
            throw Fail(ErrorCode::SchemaMismatch, "Session schema differs from the stored definition.");
    }
    if (!exists) {
        Statement insert(database, "INSERT INTO sessions VALUES (?, ?, 0)", "create persistence session");
        insert.Bind(1, request.id.value);
        insert.Bind(2, definition);
        insert.Run();
    }
    transaction.Commit();
}

class SQLiteProvider final : public SQLiteStorageProvider {
public:
    explicit SQLiteProvider(SQLiteOptions options) : options_(std::move(options)) {}

    CapabilitySet Capabilities() const override
    {
        return {Capability::PrimaryQueries, Capability::SecondaryIndexes, Capability::AtomicTransactions,
                Capability::ConsistentSnapshots, Capability::LogicalChangeJournal, Capability::DurablePersistence};
    }

    Result<std::unique_ptr<Session>> OpenSession(const OpenSessionRequest& request) override
    {
        return ResultOf([&]() -> std::unique_ptr<Session> {
            if (request.id.value.empty())
                throw Fail(ErrorCode::InvalidArgument, "Session ID cannot be empty.");
            const auto supported = Capabilities();
            if (!std::ranges::includes(supported, request.requiredCapabilities))
                throw Fail(ErrorCode::UnsupportedOperation, "The provider lacks a required capability.");
            if (const auto valid = ValidateSchema(request.schema); !valid)
                throw valid.Error();

            auto lease = std::make_unique<WriterLease>(registry_, request.id);
            auto database = Connect(options_, SQLITE_OPEN_READWRITE);
            OpenOrCreateSession(database, request);
            return std::make_unique<SQLiteSession>(options_, request, std::move(lease), std::move(database));
        });
    }

    Status CreateBackup(const std::filesystem::path& destination) const override
    {
        return ResultOf([&] {
            CopyDatabase(options_.directory, destination);
            return Null{};
        });
    }

private:
    SQLiteOptions options_;
    std::shared_ptr<WriterRegistry> registry_ = std::make_shared<WriterRegistry>();
};
} // namespace

Result<std::unique_ptr<SQLiteStorageProvider>> CreateSQLiteProvider(const SQLiteOptions& options)
{
    return ResultOf([&]() -> std::unique_ptr<SQLiteStorageProvider> {
        if (options.directory.empty() || options.cacheKiB < 1 || options.busyTimeoutMs < 0
            || (options.durability != SQLiteDurability::Full && options.durability != SQLiteDurability::Normal))
            throw Fail(ErrorCode::InvalidArgument, "Invalid SQLite directory, cache, timeout, or durability option.");

        auto configured = options;
        std::error_code error;
        configured.directory = std::filesystem::absolute(options.directory, error);
        if (error)
            StorageFailure("Resolve SQLite storage directory: " + error.message() + ".");
        std::filesystem::create_directories(configured.directory, error);
        if (error)
            StorageFailure("Create SQLite storage directory: " + error.message() + ".");
        InitializeDatabase(configured);
        return std::make_unique<SQLiteProvider>(std::move(configured));
    });
}

Status RestoreSQLiteBackup(const std::filesystem::path& backup, const std::filesystem::path& destination)
{
    return ResultOf([&] {
        if (backup.empty() || destination.empty())
            throw Fail(ErrorCode::InvalidArgument, "SQLite backup and restore directories are required.");
        CopyDatabase(backup, destination);
        return Null{};
    });
}
} // namespace ce::persistence
