#pragma once

#include "engine/persistence.h"
#include <filesystem>

namespace ce::persistence
{
enum class RocksDBDurability { Sync, Process };
enum class RocksDBCompression { None, Zlib };

struct RocksDBOptions {
    std::filesystem::path directory;
    std::size_t blockCacheKiB = 2048;
    std::size_t writeBufferKiB = 4096;
    RocksDBCompression compression = RocksDBCompression::None;
    RocksDBDurability durability = RocksDBDurability::Sync;
};

struct RocksDBMetrics {
    std::uint64_t writeBatchBytes = 0;
    std::uint64_t walBytes = 0;
    std::uint64_t flushBytes = 0;
    std::uint64_t compactionBytes = 0;
    std::size_t blockCacheBytes = 0;
};

class RocksDBStorageProvider : public StorageProvider {
public:
    virtual Status CreateBackup(const std::filesystem::path& newDirectory) const = 0;
    virtual RocksDBMetrics Metrics() const = 0;
    virtual Status FlushAndCompact() = 0;
};

Result<std::unique_ptr<RocksDBStorageProvider>> CreateRocksDBProvider(const RocksDBOptions& options);
Status RestoreRocksDBBackup(const std::filesystem::path& backup, const std::filesystem::path& newDirectory);
} // namespace ce::persistence
