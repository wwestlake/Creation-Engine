#pragma once

#include "engine/sqlite_persistence.h"
#include "persistence_validation.h"

#include <sqlite3.h>

#include <string_view>

namespace ce::persistence::sqlite_detail
{
using detail::Fail;

template <typename Function>
auto ResultOf(Function&& function) -> Result<decltype(function())>
{
    try {
        return function();
    } catch (const OperationError& error) {
        return error;
    }
}

[[noreturn]] inline void StorageFailure(std::string message)
{
    throw OperationError{ErrorCode::StorageFailure, std::move(message)};
}

struct CloseDatabase {
    void operator()(sqlite3* db) const noexcept { sqlite3_close_v2(db); }
};

class Database {
public:
    static Database Open(const std::filesystem::path& path, int flags, int busyTimeoutMs)
    {
        sqlite3* handle = nullptr;
        const auto filename = path.u8string();
        const auto status = sqlite3_open_v2(reinterpret_cast<const char*>(filename.c_str()),
                                            &handle, flags | SQLITE_OPEN_FULLMUTEX, nullptr);
        Database database(handle);
        database.Check(status, "open database");
        database.Check(sqlite3_extended_result_codes(handle, 1), "enable extended result codes");
        database.Check(sqlite3_busy_timeout(handle, busyTimeoutMs), "configure database busy timeout");
        return database;
    }

    sqlite3* Get() const noexcept { return db_.get(); }

    void Check(int status, std::string_view operation) const
    {
        if (status == SQLITE_OK)
            return;
        const auto code = status == SQLITE_CONSTRAINT_UNIQUE ? ErrorCode::UniqueConstraint : ErrorCode::StorageFailure;
        const char* reason = db_ && sqlite3_extended_errcode(db_.get()) == status ? sqlite3_errmsg(db_.get())
                                                                                : sqlite3_errstr(status);
        throw OperationError{code, "SQLite " + std::string(operation) + ": " + reason
                                   + " (code " + std::to_string(status) + ")."};
    }

    void Execute(const char* sql, std::string_view operation) const
    {
        Check(sqlite3_exec(Get(), sql, nullptr, nullptr, nullptr), operation);
    }

private:
    explicit Database(sqlite3* handle) : db_(handle) {}
    std::unique_ptr<sqlite3, CloseDatabase> db_;
};

struct FinalizeStatement {
    void operator()(sqlite3_stmt* statement) const noexcept { sqlite3_finalize(statement); }
};

class Statement {
public:
    Statement(const Database& db, const char* sql, std::string_view operation)
        : db_(db), operation_(operation)
    {
        sqlite3_stmt* raw = nullptr;
        const auto status = sqlite3_prepare_v2(db.Get(), sql, -1, &raw, nullptr);
        statement_.reset(raw);
        db_.Check(status, operation_);
    }

    void Bind(int index, std::span<const std::uint8_t> blob)
    {
        static constexpr std::uint8_t emptyBlob = 0;
        db_.Check(sqlite3_bind_blob64(statement_.get(), index, blob.empty() ? &emptyBlob : blob.data(),
                                    blob.size(), SQLITE_TRANSIENT), operation_);
    }

    void Bind(int index, const std::string& text)
    {
        Bind(index, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }

    void Bind(int index, std::int64_t value)
    {
        db_.Check(sqlite3_bind_int64(statement_.get(), index, value), operation_);
    }

    bool Step()
    {
        const auto status = sqlite3_step(statement_.get());
        if (status == SQLITE_ROW)
            return true;
        if (status != SQLITE_DONE)
            db_.Check(status, operation_);
        return false;
    }

    void Run()
    {
        if (Step())
            StorageFailure("Unexpected SQLite result row.");
    }

    std::span<const std::uint8_t> Blob(int column) const
    {
        if (sqlite3_column_type(statement_.get(), column) != SQLITE_BLOB)
            StorageFailure("Unexpected SQLite BLOB column type.");
        const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement_.get(), column));
        const auto size = sqlite3_column_bytes(statement_.get(), column);
        if (sqlite3_errcode(db_.Get()) == SQLITE_NOMEM)
            db_.Check(SQLITE_NOMEM, operation_);
        return {data, static_cast<std::size_t>(size)};
    }

    std::string Bytes(int column) const
    {
        const auto data = Blob(column);
        if (data.empty())
            return {};
        return {reinterpret_cast<const char*>(data.data()), data.size()};
    }

    std::uint64_t Unsigned(int column) const
    {
        if (sqlite3_column_type(statement_.get(), column) != SQLITE_INTEGER)
            StorageFailure("Unexpected SQLite integer column type.");
        const auto value = sqlite3_column_int64(statement_.get(), column);
        if (value < 0)
            StorageFailure("Unexpected negative SQLite metadata value.");
        return static_cast<std::uint64_t>(value);
    }

    std::string_view Text(int column) const
    {
        if (sqlite3_column_type(statement_.get(), column) != SQLITE_TEXT)
            StorageFailure("Unexpected SQLite text column type.");
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement_.get(), column));
        if (!value)
            db_.Check(SQLITE_NOMEM, operation_);
        return {value, static_cast<std::size_t>(sqlite3_column_bytes(statement_.get(), column))};
    }

private:
    const Database& db_;
    std::string_view operation_;
    std::unique_ptr<sqlite3_stmt, FinalizeStatement> statement_;
};

class WriteTransaction {
public:
    explicit WriteTransaction(const Database& db) : db_(db)
    {
        db_.Execute("BEGIN IMMEDIATE", "begin write transaction");
    }
    WriteTransaction(const WriteTransaction&) = delete;
    WriteTransaction& operator=(const WriteTransaction&) = delete;
    ~WriteTransaction()
    {
        if (active_ && !sqlite3_get_autocommit(db_.Get()))
            sqlite3_exec(db_.Get(), "ROLLBACK", nullptr, nullptr, nullptr);
    }

    void Commit()
    {
        db_.Execute("COMMIT", "commit write transaction");
        active_ = false;
    }

private:
    const Database& db_;
    bool active_ = true;
};

inline constexpr auto kDatabaseName = "state.sqlite3";
inline constexpr auto kApplicationId = 0x444A5053; // DJPS
inline constexpr auto kFormatVersion = 1;

void InitializeDatabase(const SQLiteOptions& options);
Database Connect(const SQLiteOptions& options, int flags);
void CopyDatabase(const std::filesystem::path& source, const std::filesystem::path& destination);
} // namespace ce::persistence::sqlite_detail
