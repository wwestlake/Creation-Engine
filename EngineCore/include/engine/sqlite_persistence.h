#pragma once

#include "engine/persistence.h"

#include <filesystem>

namespace ce::persistence
{
enum class SQLiteDurability {
    Full,   // Synchronize the WAL on every successful commit.
    Normal  // Process-crash safe; recent commits can be lost on power loss.
};

struct SQLiteOptions {
    std::filesystem::path directory;
    int cacheKiB = 2048; // Per connection.
    int busyTimeoutMs = 5000;
    SQLiteDurability durability = SQLiteDurability::Full;
};

class SQLiteStorageProvider : public StorageProvider {
public:
    // The destination must be a new directory. A completed backup can be
    // published while the source database is open for reads and writes.
    virtual Status CreateBackup(const std::filesystem::path& destination) const = 0;
};

Result<std::unique_ptr<SQLiteStorageProvider>> CreateSQLiteProvider(const SQLiteOptions& options);

// Restore into a new directory; the caller selects when to open it as a provider.
Status RestoreSQLiteBackup(const std::filesystem::path& backup,
                           const std::filesystem::path& destination);
} // namespace ce::persistence
