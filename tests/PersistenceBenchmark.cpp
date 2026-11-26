#include "engine/rocksdb_persistence.h"
#include "engine/sqlite_persistence.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace
{
using namespace ce::persistence;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const CollectionKey kCollection{{"synthetic"}, {"samples"}};
const FieldId kGroup{"group"};
const FieldId kRank{"rank"};
const IndexId kOrder{"group_rank"};
constexpr int kThreads = 4;
constexpr int kBatchSize = 8;
constexpr int kCacheKiB = 4096;
constexpr int kRocksBufferKiB = 256;

template <typename T>
T Take(Result<T> result)
{
    if (!result)
        throw std::runtime_error(result.Error().message);
    return std::move(result).Value();
}

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

double Microseconds(Clock::duration elapsed)
{
    return std::chrono::duration<double, std::micro>(elapsed).count();
}

template <typename Function>
double Measure(Function&& function)
{
    const auto start = Clock::now();
    function();
    return Microseconds(Clock::now() - start);
}

struct Samples {
    std::vector<double> values;

    void Print(const char* phase, double elapsed = 0)
    {
        std::ranges::sort(values);
        const auto quantile = [&](double fraction) {
            return values[static_cast<std::size_t>(std::ceil(values.size() * fraction)) - 1];
        };
        double sum = 0;
        for (double value : values)
            sum += value;
        if (elapsed == 0)
            elapsed = sum;
        std::cout << phase << ',' << values.size() << ',' << quantile(.5) << ',' << quantile(.95) << ','
                  << quantile(.99) << ',' << values.size() * 1000000.0 / elapsed << '\n';
    }
};

std::uintmax_t BytesOnDisk(const fs::path& directory, std::string_view extension = {})
{
    std::uintmax_t bytes = 0;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        std::error_code error;
        if (entry.is_regular_file(error) && (extension.empty() || entry.path().extension() == extension)) {
            const auto size = entry.file_size(error);
            if (!error)
                bytes += size;
        }
    }
    return bytes;
}

std::uintmax_t FileBytes(const fs::path& path)
{
    return fs::exists(path) ? fs::file_size(path) : 0;
}

std::uintmax_t PeakMemoryBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    Check(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0,
          "Could not read peak working set.");
    return counters.PeakWorkingSetSize;
#else
    rusage usage{};
    Check(getrusage(RUSAGE_SELF, &usage) == 0, "Could not read peak RSS.");
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return static_cast<std::uintmax_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

struct SqliteCheckpoint {
    int frames = 0;
    int checkpointed = 0;
};

SqliteCheckpoint Checkpoint(const fs::path& directory, int mode)
{
    const auto path = (directory / "state.sqlite3").u8string();
    sqlite3* raw = nullptr;
    const int open = sqlite3_open_v2(reinterpret_cast<const char*>(path.c_str()), &raw, SQLITE_OPEN_READWRITE, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)> db(raw, sqlite3_close_v2);
    Check(open == SQLITE_OK, "Could not open SQLite checkpoint probe.");
    Check(sqlite3_exec(raw, "SELECT 1 FROM sessions LIMIT 1", nullptr, nullptr, nullptr) == SQLITE_OK,
          "Could not initialize SQLite checkpoint probe.");
    SqliteCheckpoint checkpoint;
    Check(sqlite3_wal_checkpoint_v2(raw, nullptr, mode, &checkpoint.frames, &checkpoint.checkpointed) == SQLITE_OK,
          "SQLite checkpoint probe failed.");
    return checkpoint;
}

OpenSessionRequest Request()
{
    CollectionSchema schema{{1}, {{kGroup, {FieldType::Integer}}, {kRank, {FieldType::Integer}}},
                             {{kOrder, {{kGroup, kRank}}}}};
    OpenSessionRequest request{{"comparison"}, {{kCollection, schema}}, {Capability::DurablePersistence}};
    request.policy.journalRevisions = 64;
    return request;
}

RecordKey RecordAt(int offset)
{
    return {kCollection, {std::to_string(offset)}};
}

RecordData Data(int offset, int phase)
{
    Binary payload(192);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::uint8_t>((offset * 13 + i * 19 + phase) % 256);
    return {{1}, {{kGroup, static_cast<std::int64_t>(offset % 16)},
                  {kRank, static_cast<std::int64_t>(offset)}}, std::move(payload)};
}

std::unique_ptr<StorageProvider> Create(bool rocks, const fs::path& directory)
{
    if (rocks)
        return Take(CreateRocksDBProvider({directory, kCacheKiB, kRocksBufferKiB,
                                          RocksDBCompression::None, RocksDBDurability::Sync}));
    return Take(CreateSQLiteProvider({directory, kCacheKiB, 5000, SQLiteDurability::Full}));
}

void Seed(Session& session, int count)
{
    for (int i = 0; i < count; i += 32) {
        Transaction transaction;
        for (int p = i; p < std::min(i + 32, count); ++p)
            transaction.mutations.push_back({MutationKind::Create, RecordAt(p), Data(p, 0), {}});
        Take(session.Commit(transaction));
    }
}

std::uintmax_t LogBytes(bool rocks, const fs::path& directory)
{
    return rocks ? BytesOnDisk(directory / "rocks", ".log") : FileBytes(directory / "state.sqlite3-wal");
}

void Compare(bool rocks, const fs::path& directory, int count)
{
    auto provider = Create(rocks, directory);
    auto session = Take(provider->OpenSession(Request()));
    Seed(*session, count);
    auto* rocksProvider = rocks ? static_cast<RocksDBStorageProvider*>(provider.get()) : nullptr;
    if (rocks)
        Take(rocksProvider->FlushAndCompact());
    else
        Checkpoint(directory, SQLITE_CHECKPOINT_TRUNCATE);

    Samples snapshots;
    for (int i = 0; i < 64; ++i)
        snapshots.values.push_back(Measure([&] { Take(session->OpenSnapshot()); }));
    auto pinned = Take(session->OpenSnapshot());
    const auto initialRevision = pinned->Revision();
    const auto baselineBytes = LogBytes(rocks, directory);
    Samples points, bulk, ranges, writes;
    for (int i = 0; i < 2048; ++i) {
        const auto key = RecordAt((i * 17) % count);
        points.values.push_back(Measure([&] { Check(Take(pinned->Get(key)).has_value(), "Missing benchmark record."); }));
    }
    for (int i = 0; i < 128; ++i) {
        std::vector<RecordKey> keys;
        for (int j = 0; j < 32; ++j)
            keys.push_back(RecordAt((i * 41 + j * 13) % count));
        bulk.values.push_back(Measure([&] { Check(Take(pinned->GetMany(keys)).size() == keys.size(), "Bulk read differs."); }));
    }
    for (int i = 0; i < 512; ++i) {
        QueryDescription query{kCollection, kOrder, {static_cast<std::int64_t>(i % 16)},
                               QueryBound{static_cast<std::int64_t>(i % (count / 2))}, {}, {}, 16};
        ranges.values.push_back(Measure([&] { Check(Take(pinned->Query(query)).records.size() == 16, "Range read differs."); }));
    }
    for (int i = 0; i < 256; ++i) {
        Transaction transaction;
        for (int j = 0; j < kBatchSize; ++j) {
            const auto offset = (i * kBatchSize + j) % count;
            transaction.mutations.push_back({MutationKind::Replace, RecordAt(offset), Data(offset, i + 1), {}});
        }
        writes.values.push_back(Measure([&] { Take(session->Commit(transaction)); }));
    }
    Check(pinned->Revision() == initialRevision && Take(pinned->Get(RecordAt(0)))->data == Data(0, 0),
          "Benchmark snapshot lost isolation.");
    const auto pinnedLogBytes = LogBytes(rocks, directory);
    const auto pinnedDiskBytes = BytesOnDisk(directory);
    SqliteCheckpoint sqlitePinned;
    if (!rocks)
        sqlitePinned = Checkpoint(directory, SQLITE_CHECKPOINT_PASSIVE);
    pinned.reset();

    auto retained = Take(session->Export());
    Check(retained.journal.size() == 64, "Expected the retained journal policy.");
    const auto retainedCount = retained.journal.size();
    retained = {};

    Samples readers[kThreads];
    std::exception_ptr failures[kThreads];
    std::latch ready(kThreads);
    std::latch start(1);
    std::vector<std::jthread> threads;
    for (int thread = 0; thread < kThreads; ++thread) {
        threads.emplace_back([&, thread] {
            ready.count_down();
            start.wait();
            try {
                auto view = Take(session->OpenSnapshot());
                for (int i = 0; i < 512; ++i) {
                    const auto key = RecordAt((thread * 47 + i * 13) % count);
                    readers[thread].values.push_back(Measure([&] {
                        Check(Take(view->Get(key)).has_value(), "Concurrent reader lost a record.");
                    }));
                }
            } catch (...) { failures[thread] = std::current_exception(); }
        });
    }
    ready.wait();
    const auto concurrentElapsed = Measure([&] { start.count_down(); threads.clear(); });
    Samples concurrent;
    for (int thread = 0; thread < kThreads; ++thread) {
        if (failures[thread])
            std::rethrow_exception(failures[thread]);
        concurrent.values.insert(concurrent.values.end(), readers[thread].values.begin(), readers[thread].values.end());
    }

    const auto cleanupTime = Measure([&] {
        if (rocks)
            Take(rocksProvider->FlushAndCompact());
        else
            Checkpoint(directory, SQLITE_CHECKPOINT_TRUNCATE);
    });
    RocksDBMetrics metrics;
    if (rocks)
        metrics = rocksProvider->Metrics();
    const auto postLogBytes = LogBytes(rocks, directory);
    const auto postDiskBytes = BytesOnDisk(directory);
    session.reset();
    provider.reset();
    const auto reopenUs = Measure([&] {
        provider = Create(rocks, directory);
        session = Take(provider->OpenSession(Request()));
        Check(Take(session->Get(RecordAt(0))).has_value(), "Normal reopen lost the record.");
    });

    std::cout << "phase,operations,median_us,p95_us,p99_us,operations_per_sec\n";
    snapshots.Print("snapshot_open_close");
    points.Print("point");
    bulk.Print("bulk32");
    ranges.Print("range16");
    writes.Print("commit8");
    concurrent.Print("reader4", concurrentElapsed);
    std::cout << "bytes_before_log," << baselineBytes << "\nbytes_with_pinned_log," << pinnedLogBytes
              << "\nbytes_with_pinned_disk," << pinnedDiskBytes << "\nbytes_after_log," << postLogBytes
              << "\nbytes_after_disk," << postDiskBytes << "\npeak_rss_bytes," << PeakMemoryBytes()
              << "\nretained_revisions," << retainedCount << "\nmaintenance_us," << cleanupTime
              << "\nreopen_us," << reopenUs << '\n';
    if (rocks)
        std::cout << "rocks_batch_bytes," << metrics.writeBatchBytes << "\nrocks_wal_bytes," << metrics.walBytes
                  << "\nrocks_flush_bytes," << metrics.flushBytes << "\nrocks_compact_bytes," << metrics.compactionBytes
                  << "\nrocks_block_cache_bytes," << metrics.blockCacheBytes << '\n';
    else
        std::cout << "sqlite_pinned_frames," << sqlitePinned.frames << "\nsqlite_checkpointed_frames,"
                  << sqlitePinned.checkpointed << '\n';
}
} // namespace

int main(int argc, char* argv[])
{
    try {
        Check(argc == 5, "Usage: persistence benchmark run|crash-seed|recover sqlite|rocksdb DIRECTORY RECORD_COUNT");
        const std::string_view mode = argv[1];
        const std::string_view backend = argv[2];
        Check(backend == "rocksdb" || backend == "sqlite", "Unknown benchmark provider.");
        const bool rocks = backend == "rocksdb";
        const fs::path directory = argv[3];
        const auto count = std::stoi(argv[4]);
        Check(count >= 1024 && count <= 100000, "Benchmark record count must be between 1024 and 100000.");
        if (mode == "run") {
            Compare(rocks, directory, count);
        } else if (mode == "crash-seed") {
            auto provider = Create(rocks, directory);
            auto session = Take(provider->OpenSession(Request()));
            Seed(*session, count);
            std::_Exit(71);
        } else if (mode == "recover") {
            const auto recoveryUs = Measure([&] {
                auto provider = Create(rocks, directory);
                auto session = Take(provider->OpenSession(Request()));
                Check(Take(session->Get(RecordAt(count - 1)))->data == Data(count - 1, 0),
                      "Recovery lost the last record.");
            });
            std::cout << "crash_recovery_us," << recoveryUs << '\n';
        } else {
            throw std::runtime_error("Unknown benchmark phase.");
        }
    } catch (const std::exception& error) {
        std::cerr << "Persistence benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
