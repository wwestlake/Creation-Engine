#include "engine/sqlite_persistence.h"

#include <sqlite3.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
using namespace ce::persistence;
namespace fs = std::filesystem;

const RecordKey kKey{{{"fixtures"}, {"durable"}}, {"record"}};
const FieldId kNumber{"number"};
const IndexId kByNumber{"number"};

void Check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename T>
T Take(Result<T> result)
{
    if (!result)
        throw std::runtime_error(result.Error().message);
    return std::move(result).Value();
}

template <typename T>
void ExpectStorageError(const Result<T>& result, std::string_view detail)
{
    Check(!result, "Storage operation unexpectedly succeeded.");
    Check(result.Error().code == ErrorCode::StorageFailure, result.Error().message);
    Check(result.Error().message.find(detail) != std::string::npos, result.Error().message);
}

OpenSessionRequest Request()
{
    CollectionSchema schema{{1}, {{kNumber, {FieldType::Integer}}}, {{kByNumber, {{kNumber}, true}}}};
    return {{"recovery"}, {{kKey.collection, schema}}, {Capability::DurablePersistence}};
}

RecordData Data(std::int64_t number)
{
    return {{1}, {{kNumber, number}}, Binary{0, 1, 128, 255}};
}

CommitRevision Create(Session& session)
{
    return Take(session.Commit({{{MutationKind::Create, kKey, Data(1), std::nullopt}}}));
}

CommitRevision Replace(Session& session, std::int64_t number)
{
    const auto old = Take(session.Get(kKey)).value();
    return Take(session.Commit({{{MutationKind::Replace, kKey, Data(number), old.version}}}));
}

Record Inspect(const Session& session, std::int64_t expected)
{
    auto state = Take(session.OpenSnapshot());
    const auto record = Take(state->Get(kKey)).value();
    Check(record.data == Data(expected), "Recovered structured and binary payloads differ.");
    Check(state->Revision().value == static_cast<std::uint64_t>(expected)
          && record.version.value == state->Revision().value, "Record and snapshot revisions differ.");
    const auto index = Take(state->Query({kKey.collection, kByNumber, {expected}}));
    Check(index.records == std::vector<Record>{record}, "The recovered secondary index disagrees with the record.");
    return record;
}

struct CloseSql {
    void operator()(sqlite3* db) const noexcept { sqlite3_close_v2(db); }
};
using Native = std::unique_ptr<sqlite3, CloseSql>;

Native OpenNative(const fs::path& directory, bool create = false)
{
    sqlite3* raw = nullptr;
    const auto filename = (directory / "state.sqlite3").u8string();
    const auto code = sqlite3_open_v2(reinterpret_cast<const char*>(filename.c_str()), &raw,
                                     SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), nullptr);
    Native database(raw);
    if (code != SQLITE_OK)
        throw std::runtime_error(sqlite3_errstr(code));
    return database;
}

void Sql(const Native& database, const char* statement)
{
    const auto code = sqlite3_exec(database.get(), statement, nullptr, nullptr, nullptr);
    if (code != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database.get()));
}

void SeedAndExit(const fs::path& directory, SQLiteDurability durability)
{
    auto provider = Take(CreateSQLiteProvider({directory, 1024, 1000, durability}));
    auto session = Take(provider->OpenSession(Request()));
    Check(Create(*session) == CommitRevision{1} && Replace(*session, 2) == CommitRevision{2},
          "Expected both revisions to commit before process exit.");
    Check(fs::file_size(directory / "state.sqlite3-wal") > 32, "Expected uncheckpointed WAL data.");
    std::_Exit(71); // Deliberately bypass SQLite close and all destructors.
}

void InterruptAndExit(const fs::path& directory)
{
    auto db = OpenNative(directory);
    Sql(db, "PRAGMA cache_size=-32; BEGIN IMMEDIATE;");
    const auto wal = directory / "state.sqlite3-wal";
    const auto before = fs::exists(wal) ? fs::file_size(wal) : 0;
    // Dirty all durable components and spill pages to the WAL without COMMIT.
    Sql(db, "UPDATE records SET version=3, payload=zeroblob(524288);"
            "DELETE FROM record_indexes;"
            "INSERT INTO changes SELECT id, 3, zeroblob(262144) FROM sessions;"
            "UPDATE sessions SET revision=3;");
    Check(sqlite3_get_autocommit(db.get()) == 0, "The interrupted transaction must still be open.");
    Check(sqlite3_db_cacheflush(db.get()) == SQLITE_OK, "Could not spill dirty transaction pages.");
    Check(fs::file_size(wal) > before, "The interrupted transaction must actually reach the WAL.");
    std::_Exit(72);
}

void VerifyRecovery(const fs::path& directory)
{
    const auto provider = Take(CreateSQLiteProvider({directory}));
    const auto session = Take(provider->OpenSession(Request()));
    const auto record = Inspect(*session, 2);
    auto journal = Take(session->ReadChanges({}, 10));
    Check(journal.latestRevision == CommitRevision{2} && journal.batches.size() == 2
          && journal.batches[0].revision == CommitRevision{1} && journal.batches[1].revision == CommitRevision{2}
          && journal.batches[1].changes.size() == 1 && journal.batches[1].changes[0].record == record,
          "The journal must contain both committed revisions and exclude the interrupted transaction.");
}

void BackupRestoreAndReaders(const fs::path& root)
{
    const auto source = root / "live";
    auto provider = Take(CreateSQLiteProvider({source, 256, 1000, SQLiteDurability::Normal}));
    auto session = Take(provider->OpenSession(Request()));
    Check(Create(*session) == CommitRevision{1}, "Initial revision failed.");
    auto reader = Take(session->OpenSnapshot());
    const auto first = Take(reader->Get(kKey)).value();

    std::atomic<bool> readerFailed{false};
    {
        std::jthread concurrent([&] {
            for (int i = 0; i < 20; ++i) {
                const auto record = reader->Get(kKey);
                if (!record || record.Value() != first || reader->Revision() != CommitRevision{1})
                    readerFailed = true;
            }
        });
        Check(Replace(*session, 2) == CommitRevision{2}, "A pinned reader must not block a WAL writer.");
        Inspect(*session, 2);
    }
    Check(!readerFailed, "The concurrent reader saw a newer revision.");

    const auto backup = root / "backup";
    const auto restored = root / "restored";
    Take(provider->CreateBackup(backup));
    ExpectStorageError(provider->CreateBackup(backup), "destination already exists");
    Check(Replace(*session, 3) == CommitRevision{3}, "A backup must not prevent future commits.");
    Take(RestoreSQLiteBackup(backup, restored));
    VerifyRecovery(restored);

    Take(session->Close());
    Check(Take(reader->Get(kKey)) == first, "Pinned reader did not outlive its session.");
    reader.reset();
    provider.reset();
    auto reopened = Take(CreateSQLiteProvider({source}));
    auto live = Take(reopened->OpenSession(Request()));
    Inspect(*live, 3);
    Check(Take(live->ReadChanges({}, 10)).batches.size() == 3, "Reopen lost the durable journal.");
}

void InvalidFormatsAndWriteFailure(const fs::path& root)
{
    const auto garbage = root / "non-database";
    fs::create_directory(garbage);
    { std::ofstream(garbage / "state.sqlite3", std::ios::binary) << "synthetic invalid persistence file"; }
    ExpectStorageError(CreateSQLiteProvider({garbage}), "not a database");

    const auto foreign = root / "foreign";
    fs::create_directory(foreign);
    {
        auto raw = OpenNative(foreign, true);
        Sql(raw, "CREATE TABLE application_data(value); INSERT INTO application_data VALUES (7);");
    }
    ExpectStorageError(CreateSQLiteProvider({foreign}), "owned by another application");
    ExpectStorageError(RestoreSQLiteBackup(foreign, root / "invalid-restore"), "not a Djehuti persistence database");

    const auto future = root / "future";
    Take(CreateSQLiteProvider({future}));
    { auto raw = OpenNative(future); Sql(raw, "PRAGMA user_version=99"); }
    ExpectStorageError(CreateSQLiteProvider({future}), "format version 99");
    ExpectStorageError(CreateSQLiteProvider({garbage / "state.sqlite3"}), "storage directory");

    const auto failing = root / "write-failure";
    auto provider = Take(CreateSQLiteProvider({failing, 256, 20}));
    auto session = Take(provider->OpenSession(Request()));
    Create(*session);
    auto raw = OpenNative(failing);
    Sql(raw, "BEGIN IMMEDIATE");
    const Transaction update{{{MutationKind::Replace, kKey, Data(2), RecordVersion{1}}}};
    ExpectStorageError(session->Commit(update), "locked");
    Sql(raw, "ROLLBACK");

    // Fail after SQLite has changed records and indexes, before it can publish a revision.
    Sql(raw, "CREATE TRIGGER fail_journal BEFORE INSERT ON changes BEGIN "
             "SELECT RAISE(ABORT, 'injected journal write failure'); END;");
    ExpectStorageError(session->Commit(update), "injected journal write failure");
    Inspect(*session, 1);
    Check(Take(session->ReadChanges({}, 10)).batches.size() == 1, "Failed writes changed the journal.");
    Sql(raw, "DROP TRIGGER fail_journal");
    Sql(raw, "CREATE TRIGGER fail_index_catalog BEFORE UPDATE OF definition ON sessions BEGIN "
             "SELECT RAISE(ABORT, 'injected index catalog write failure'); END;");
    const IndexDefinition descending{{kNumber}, true, {2}, {SortDirection::Descending}};
    ExpectStorageError(session->MaintainIndex({kKey.collection, kByNumber, IndexOperation::Rebuild,
                                               descending, IndexVersion{1}}), "injected index catalog write failure");
    Inspect(*session, 1);
    const auto snapshot = Take(session->OpenSnapshot());
    Check(Take(snapshot->Describe(kKey.collection)).indexes.at(kByNumber).version == IndexVersion{1},
          "Failed index maintenance published its catalog.");
    Sql(raw, "DROP TRIGGER fail_index_catalog");
    Check(Take(session->Commit(update)) == CommitRevision{2}, "Writes must resume after the failure clears.");
    Inspect(*session, 2);
}
} // namespace

int main(int argc, char* argv[])
{
    try {
        Check(argc == 3, "Usage: SQLite recovery test MODE DIRECTORY");
        const std::string_view mode = argv[1];
        const fs::path directory = argv[2];
        if (mode == "seed-full")
            SeedAndExit(directory, SQLiteDurability::Full);
        else if (mode == "seed-normal")
            SeedAndExit(directory, SQLiteDurability::Normal);
        else if (mode == "interrupt")
            InterruptAndExit(directory);
        else if (mode == "verify")
            VerifyRecovery(directory);
        else if (mode == "exercise") {
            fs::create_directories(directory);
            BackupRestoreAndReaders(directory);
            InvalidFormatsAndWriteFailure(directory);
        } else {
            throw std::runtime_error("Unknown SQLite recovery test mode.");
        }
        std::cout << mode << " passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SQLite recovery test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
