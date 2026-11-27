#include "engine/rocksdb_persistence.h"
#include "engine/sqlite_persistence.h"
#include "PersistenceTestDirectory.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace ce::persistence;
namespace fs = std::filesystem;

const CollectionKey kCollection{{"native"}, {"records"}};
const CollectionKey kOther{{"other"}, {"records"}};
const RecordKey kFirst{kCollection, {std::string("opaque\0id", 9)}};
const RecordKey kSecond{kOther, {"second"}};
const FieldId kValue{"value"};
const IndexId kOrdering{"ordering"};

void Check(bool condition, std::string_view detail)
{
    if (!condition)
        throw std::runtime_error(std::string(detail));
}

template <typename T>
T Take(Result<T> result)
{
    if (!result)
        throw std::runtime_error(result.Error().message);
    return std::move(result).Value();
}

template <typename T>
void ExpectError(const Result<T>& result, ErrorCode code, std::string_view message)
{
    Check(!result && result.Error().code == code && !result.Error().message.empty(), message);
}

OpenSessionRequest Request()
{
    const CollectionSchema schema{{3}, {{kValue, {FieldType::Integer}}}, {{kOrdering, {{kValue}, true}}}};
    OpenSessionRequest request{{"transfer"}, {{kCollection, schema}, {kOther, schema}}, {Capability::DurablePersistence}};
    request.policy.journalRevisions = 2;
    return request;
}

RecordData Data(std::int64_t value)
{
    return {{3}, {{kValue, value}}, Binary{0, 255, 0, 128}};
}

void Seed(Session& session)
{
    Take(session.Commit({{{MutationKind::Create, kFirst, Data(-4), {}},
                         {MutationKind::Create, kSecond, Data(15), {}}}}));
    Take(session.Commit({{{MutationKind::Replace, kFirst, Data(-3), RecordVersion{1}}}}));
    Take(session.Commit({{{MutationKind::Replace, kFirst, Data(-2), RecordVersion{2}}}}));
}

void Inspect(Session& session)
{
    const auto first = Take(session.Get(kFirst));
    const auto second = Take(session.Get(kSecond));
    Check(first && first->id == kFirst.id && first->data == Data(-2) && first->version == RecordVersion{3},
          "Independent restart did not preserve the last recovered record.");
    Check(second && second->data == Data(15) && second->version == RecordVersion{1},
          "Independent restart changed a record outside the retained journal.");
    auto journal = Take(session.ReadChanges({1}, 3));
    Check(journal.historyFloor == CommitRevision{1} && journal.latestRevision == CommitRevision{3}
          && journal.batches.size() == 2 && journal.batches.back().changes.front().record == first,
          "Recovered revision and retained journal differ.");
    ExpectError(session.ReadChanges({}, 1), ErrorCode::HistoryUnavailable, "Retention boundary disappeared.");
    const auto indexed = Take(session.Query({kCollection, kOrdering, {std::int64_t{-2}}}));
    Check(indexed.records.size() == 1 && indexed.records.front() == *first, "Recovered index differs.");
}

void Crash(const fs::path& root, RocksDBDurability durability)
{
    auto provider = Take(CreateRocksDBProvider({root, 2048, 4096, RocksDBCompression::None, durability}));
    auto session = Take(provider->OpenSession(Request()));
    Seed(*session);
    std::_Exit(71);
}

void ProvisionalWrite(const fs::path& root)
{
    auto provider = Take(CreateRocksDBProvider({root, 2048, 4096, RocksDBCompression::None, RocksDBDurability::Process}));
    auto session = Take(provider->OpenSession(Request()));
    Take(session->Commit({{{MutationKind::Replace, kFirst, Data(99), RecordVersion{3}}}}));
    // Without a synchronized commit, tear the last WAL frame in a later
    // fixture process to model an interrupted physical write, not an acked sync commit.
    std::_Exit(72);
}

void ProvisionalIndexWrite(const fs::path& root)
{
    auto provider = Take(CreateRocksDBProvider({root, 2048, 4096, RocksDBCompression::None, RocksDBDurability::Process}));
    auto session = Take(provider->OpenSession(Request()));
    const IndexDefinition descending{{kValue}, true, {2}, {SortDirection::Descending}};
    Take(session->MaintainIndex({kCollection, kOrdering, IndexOperation::Rebuild, descending, IndexVersion{1}}));
    std::_Exit(74);
}

void Abandon(const fs::path& root)
{
    auto provider = Take(CreateRocksDBProvider({root}));
    auto session = Take(provider->OpenSession(Request()));
    const Transaction pending{{{MutationKind::Replace, kFirst, Data(999), RecordVersion{3}}}};
    Check(Take(session->Get(kFirst))->data == Data(-2) && pending.mutations.size() == 1,
          "Fixture could not stage its abandoned transaction.");
    std::_Exit(73);
}

void TearWal(const fs::path& root)
{
    fs::path newest;
    for (const auto& entry : fs::directory_iterator(root / "rocks")) {
        if (entry.is_regular_file() && entry.path().extension() == ".log" && entry.path() > newest)
            newest = entry.path();
    }
    Check(!newest.empty() && fs::file_size(newest) > 8, "Expected a WAL with the provisional transaction.");
    fs::resize_file(newest, fs::file_size(newest) - 8);
}

void PortabilityAndBackup(const fs::path& root)
{
    auto request = Request();
    auto sqlite = Take(CreateSQLiteProvider({root / "sqlite"}));
    auto input = Take(sqlite->OpenSession(request));
    Seed(*input);
    const auto archive = Take(input->Export());
    auto rocks = Take(CreateRocksDBProvider({root / "rocks-source", 2048, 4096, RocksDBCompression::Zlib}));

    auto invalid = archive;
    invalid.records.front().record.version = {500};
    ExpectError(rocks->Import(invalid), ErrorCode::InvalidArgument, "Malformed import changed provider state.");
    Take(rocks->Import(archive));
    ExpectError(rocks->Import(archive), ErrorCode::RecordAlreadyExists, "Import replaced an existing session.");
    auto middle = Take(rocks->OpenSession(request));
    Check(Take(middle->Export()) == archive, "SQLite to RocksDB changed the logical image.");
    Inspect(*middle);

    auto pinned = Take(middle->OpenSnapshot());
    std::exception_ptr readerFailure;
    {
        std::jthread reader([&] {
            try {
                for (int i = 0; i < 20; ++i)
                    Check(Take(pinned->Get(kFirst))->data == Data(-2), "Concurrent snapshot changed.");
            } catch (...) { readerFailure = std::current_exception(); }
        });
        Take(middle->Commit({{{MutationKind::Replace, kFirst, Data(44), RecordVersion{3}}}}));
    }
    if (readerFailure)
        std::rethrow_exception(readerFailure);

    const auto updated = Take(middle->Export());
    auto output = Take(CreateSQLiteProvider({root / "sqlite-restored"}));
    Take(output->Import(updated));
    auto restored = Take(output->OpenSession(request));
    Check(Take(restored->Export()) == updated && updated.historyFloor == CommitRevision{2},
          "RocksDB to SQLite changed revisions, history, or payloads.");
    const QueryDescription query{kCollection, kOrdering, {std::int64_t{44}}};
    Check(Take(restored->Query(query)).records == Take(middle->Query(query)).records
          && Take(middle->Query(query)).records.size() == 1, "Imported indexes disagree.");

    Take(rocks->FlushAndCompact());
    const auto checkpoint = root / "checkpoint";
    Take(rocks->CreateBackup(checkpoint));
    ExpectError(rocks->CreateBackup(checkpoint), ErrorCode::StorageFailure, "Backup overwrote an existing destination.");
    const auto restoredPath = root / "rock-restored";
    Take(RestoreRocksDBBackup(checkpoint, restoredPath));
    auto newProvider = Take(CreateRocksDBProvider({restoredPath}));
    auto newSession = Take(newProvider->OpenSession(request));
    Check(Take(newSession->Export()) == updated, "Checkpoint restoration changed logical state.");

    ExpectError(CreateRocksDBProvider({root / "rocks-source"}), ErrorCode::StorageFailure,
                "A second database handle must report lock contention.");
    const auto foreign = root / "foreign";
    fs::create_directories(foreign / "rocks");
    std::ofstream(foreign / "rocks" / "garbage") << "invalid format";
    ExpectError(CreateRocksDBProvider({foreign}), ErrorCode::StorageFailure, "Foreign database was overwritten.");
    ExpectError(CreateRocksDBProvider({foreign / "rocks" / "garbage"}), ErrorCode::StorageFailure,
                "Invalid storage destination must report an error.");
    ExpectError(RestoreRocksDBBackup(root / "rocks-source", root / "bad-backup"), ErrorCode::StorageFailure,
                "Restore accepted an active non-checkpoint directory.");
}
} // namespace

int main(int argc, char* argv[])
{
    try {
        Check(argc == 3, "Usage: RocksDB recovery MODE DIRECTORY");
        const std::string_view mode = argv[1];
        const fs::path root = argv[2];
        if (mode == "sync")
            Crash(root, RocksDBDurability::Sync);
        else if (mode == "process")
            Crash(root, RocksDBDurability::Process);
        else if (mode == "provisional")
            ProvisionalWrite(root);
        else if (mode == "provisional-index")
            ProvisionalIndexWrite(root);
        else if (mode == "abandon")
            Abandon(root);
        else if (mode == "tear")
            TearWal(root);
        else if (mode == "verify") {
            auto provider = Take(CreateRocksDBProvider({root}));
            auto session = Take(provider->OpenSession(Request()));
            Inspect(*session);
        } else if (mode == "portability") {
            fs::create_directories(root);
            PortabilityAndBackup(root);
        } else {
            throw std::runtime_error("Unknown RocksDB recovery mode.");
        }
    } catch (const std::exception& error) {
        std::cerr << "RocksDB test failed: " << error.what() << '\n';
        return 1;
    }
}
