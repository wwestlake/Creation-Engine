#include "sqlite_persistence_database.h"

#include <chrono>
#include <thread>

namespace ce::persistence::sqlite_detail
{
static_assert(SQLITE_VERSION_NUMBER == 3054001, "Build with the pinned SQLite 3.54.1 source.");
namespace
{
constexpr auto kSchema = R"sql(
    CREATE TABLE sessions (
        id BLOB PRIMARY KEY,
        definition BLOB NOT NULL,
        revision INTEGER NOT NULL CHECK(revision >= 0)
    ) STRICT, WITHOUT ROWID;
    CREATE TABLE records (
        session_id BLOB NOT NULL REFERENCES sessions(id),
        namespace_id BLOB NOT NULL,
        collection_id BLOB NOT NULL,
        record_id BLOB NOT NULL,
        version INTEGER NOT NULL CHECK(version > 0),
        payload BLOB NOT NULL,
        PRIMARY KEY(session_id, namespace_id, collection_id, record_id)
    ) STRICT, WITHOUT ROWID;
    CREATE TABLE record_indexes (
        session_id BLOB NOT NULL,
        namespace_id BLOB NOT NULL,
        collection_id BLOB NOT NULL,
        index_id BLOB NOT NULL,
        sort_key BLOB NOT NULL,
        record_id BLOB NOT NULL,
        unique_key INTEGER NOT NULL CHECK(unique_key IN (0, 1)),
        PRIMARY KEY(session_id, namespace_id, collection_id, index_id, sort_key, record_id),
        FOREIGN KEY(session_id, namespace_id, collection_id, record_id)
            REFERENCES records(session_id, namespace_id, collection_id, record_id) ON DELETE CASCADE
    ) STRICT, WITHOUT ROWID;
    CREATE UNIQUE INDEX record_indexes_unique ON
        record_indexes(session_id, namespace_id, collection_id, index_id, sort_key) WHERE unique_key = 1;
    CREATE INDEX record_indexes_by_record ON
        record_indexes(session_id, namespace_id, collection_id, record_id);
    CREATE TABLE changes (
        session_id BLOB NOT NULL REFERENCES sessions(id),
        revision INTEGER NOT NULL CHECK(revision > 0),
        payload BLOB NOT NULL,
        PRIMARY KEY(session_id, revision)
    ) STRICT, WITHOUT ROWID;
)sql";

std::uint64_t ReadPragma(const Database& database, const char* sql)
{
    auto statement = Statement(database, sql, "read storage format");
    if (!statement.Step())
        StorageFailure("SQLite format metadata is missing.");
    return statement.Unsigned(0);
}

void RequireFormat(const Database& database, std::uint64_t application, std::uint64_t format)
{
    if (application != kApplicationId)
        StorageFailure("SQLite file is not a Djehuti persistence database.");
    if (format != kFormatVersion)
        StorageFailure("Unsupported Djehuti persistence database format version " + std::to_string(format) + ".");

    // Confirm the expected data stores exist before permitting sessions or publishing a backup.
    database.Execute("SELECT id, definition, revision FROM sessions LIMIT 0;"
                     "SELECT session_id, namespace_id, collection_id, record_id, version, payload FROM records LIMIT 0;"
                     "SELECT session_id, namespace_id, collection_id, index_id, sort_key, record_id, unique_key "
                     "FROM record_indexes LIMIT 0;"
                     "SELECT session_id, revision, payload FROM changes LIMIT 0;",
                     "validate storage format");
}

void RequireFormat(const Database& database)
{
    RequireFormat(database, ReadPragma(database, "PRAGMA application_id"), ReadPragma(database, "PRAGMA user_version"));
}

void SetJournalMode(const Database& database, const char* pragma, std::string_view expected)
{
    auto statement = Statement(database, pragma, "set journal mode");
    if (!statement.Step() || statement.Text(0) != expected)
        StorageFailure("SQLite could not enable the requested journal mode.");
}

void VerifyCopy(const Database& database)
{
    RequireFormat(database);
    {
        auto check = Statement(database, "PRAGMA quick_check", "verify copied database");
        if (!check.Step() || check.Text(0) != "ok" || check.Step())
            StorageFailure("SQLite backup failed its integrity check.");
    }
    auto check = Statement(database, "PRAGMA foreign_key_check", "verify copied references");
    if (check.Step())
        StorageFailure("SQLite backup contains inconsistent references.");
}

void CopyPages(const Database& source, const Database& destination)
{
    auto* backup = sqlite3_backup_init(destination.Get(), "main", source.Get(), "main");
    if (!backup) {
        destination.Check(sqlite3_extended_errcode(destination.Get()), "start online backup");
        StorageFailure("SQLite online backup could not be started.");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    int status;
    do {
        // Each call copies a bounded number of pages. A pinned source read
        // transaction keeps the copy on the same revision while writers commit.
        status = sqlite3_backup_step(backup, 256);
        if (status == SQLITE_BUSY || status == SQLITE_LOCKED)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (status == SQLITE_OK || ((status == SQLITE_BUSY || status == SQLITE_LOCKED)
                                    && std::chrono::steady_clock::now() < deadline));

    const auto finish = sqlite3_backup_finish(backup);
    if (status != SQLITE_DONE)
        destination.Check(status, "copy database pages");
    destination.Check(finish, "finish online backup");
}
} // namespace

Database Connect(const SQLiteOptions& options, int flags)
{
    auto database = Database::Open(options.directory / kDatabaseName, flags, options.busyTimeoutMs);
    const auto cache = "PRAGMA cache_size = -" + std::to_string(options.cacheKiB);
    database.Execute(cache.c_str(), "configure database cache");
    database.Execute("PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF", "configure database constraints");
    if (flags & SQLITE_OPEN_READWRITE)
        database.Execute(options.durability == SQLiteDurability::Full ? "PRAGMA synchronous=FULL" : "PRAGMA synchronous=NORMAL",
                         "configure commit durability");
    return database;
}

void InitializeDatabase(const SQLiteOptions& options)
{
    auto database = Connect(options, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    {
        WriteTransaction transaction(database);
        const auto application = ReadPragma(database, "PRAGMA application_id");
        const auto format = ReadPragma(database, "PRAGMA user_version");
        if (application == 0 && format == 0) {
            auto entries = Statement(database, "SELECT 1 FROM sqlite_schema LIMIT 1", "check empty database");
            if (entries.Step())
                StorageFailure("Refusing to initialize a nonempty SQLite database owned by another application.");
            database.Execute(kSchema, "initialize storage format");
            const auto app = "PRAGMA application_id = " + std::to_string(kApplicationId);
            const auto version = "PRAGMA user_version = " + std::to_string(kFormatVersion);
            database.Execute(app.c_str(), "set storage application ID");
            database.Execute(version.c_str(), "set storage format version");
        } else {
            RequireFormat(database, application, format);
        }
        transaction.Commit();
    }
    SetJournalMode(database, "PRAGMA journal_mode=WAL", "wal");
}

void CopyDatabase(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    auto input = Database::Open(source / kDatabaseName, SQLITE_OPEN_READONLY, 5000);
    input.Execute("BEGIN", "pin backup snapshot");
    RequireFormat(input);

    std::error_code error;
    if (!std::filesystem::create_directory(destination, error)) {
        const auto message = error ? error.message() : "destination already exists";
        StorageFailure("SQLite backup or restore requires a new directory: " + message + ".");
    }

    const auto pending = destination / "pending.sqlite3";
    {
        auto output = Database::Open(pending, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 5000);
        output.Execute("PRAGMA synchronous=FULL; PRAGMA trusted_schema=OFF", "configure backup destination");
        CopyPages(input, output);
        SetJournalMode(output, "PRAGMA journal_mode=DELETE", "delete");
        VerifyCopy(output);
    }
    std::filesystem::rename(pending, destination / kDatabaseName, error);
    if (error)
        StorageFailure("Publish SQLite backup or restore: " + error.message() + ".");
}
} // namespace ce::persistence::sqlite_detail
