#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ce::persistence
{
template <typename Tag>
struct Identifier {
    std::string value;
    auto operator<=>(const Identifier&) const = default;
};

using SessionId = Identifier<struct SessionTag>;
using NamespaceId = Identifier<struct NamespaceTag>;
using CollectionId = Identifier<struct CollectionTag>;
using RecordId = Identifier<struct RecordTag>;
using FieldId = Identifier<struct FieldTag>;
using IndexId = Identifier<struct IndexTag>;

template <typename Tag>
struct Version {
    std::uint64_t value = 0;
    auto operator<=>(const Version&) const = default;
};

using SchemaVersion = Version<struct SchemaTag>;
using RecordVersion = Version<struct RecordVersionTag>;
using CommitRevision = Version<struct CommitTag>;
using Binary = std::vector<std::uint8_t>;
using Null = std::monostate;

// Strings and binary values sort by bytes. Real values must be finite. Null
// sorts before any declared field type. A missing field differs from Null.
enum class FieldType { Boolean, Integer, Real, String, Binary };
using FieldValue = std::variant<Null, bool, std::int64_t, double, std::string, Binary>;
using Fields = std::map<FieldId, FieldValue>;
using IndexKey = std::vector<FieldValue>;

struct CollectionKey {
    NamespaceId nameSpace;
    CollectionId collection;
    auto operator<=>(const CollectionKey&) const = default;
};

struct RecordKey {
    CollectionKey collection;
    RecordId id;
    auto operator<=>(const RecordKey&) const = default;
};

struct FieldDefinition {
    FieldType type = FieldType::Boolean;
    bool required = true;
    bool nullable = false;
    bool operator==(const FieldDefinition&) const = default;
};

struct IndexDefinition {
    std::vector<FieldId> fields;
    bool unique = false; // Uniqueness applies to complete keys, including indexed Null.
    bool operator==(const IndexDefinition&) const = default;
};

struct CollectionSchema {
    SchemaVersion version;
    std::map<FieldId, FieldDefinition> fields;
    std::map<IndexId, IndexDefinition> indexes;
    bool operator==(const CollectionSchema&) const = default;
};
using SessionSchema = std::map<CollectionKey, CollectionSchema>;

struct RecordData {
    SchemaVersion schemaVersion;
    Fields fields;
    std::optional<Binary> binary;
    bool operator==(const RecordData&) const = default;
};

struct Record {
    RecordId id;
    // A conditional-write token. Recreating an erased ID must not reuse it.
    RecordVersion version;
    RecordData data;
    bool operator==(const Record&) const = default;
};

enum class MutationKind { Create, Replace, Erase };

struct Mutation {
    MutationKind kind = MutationKind::Create;
    RecordKey key;
    // Required for Create and Replace. Erase takes no payload.
    std::optional<RecordData> data;
    // For Replace or Erase. When absent, the record must still exist.
    std::optional<RecordVersion> expectedVersion;
};

struct Transaction {
    // A nonempty batch may address each record at most once.
    std::vector<Mutation> mutations;
    // A guard for decisions based on a range or the absence of a set of records.
    std::optional<CommitRevision> expectedRevision;
};

struct QueryBound {
    FieldValue value;
    bool inclusive = true;
};

struct QueryCursor {
    SessionId session;
    CommitRevision revision;
    CollectionKey collection;
    std::optional<IndexId> index;
    IndexKey key;
    RecordId id;
};

struct QueryDescription {
    CollectionKey collection;
    // With no secondary index, the key has one string field containing the ID.
    std::optional<IndexId> index;
    // Equality prefix over the leading index fields. Bounds apply to the next
    // field, if any. Missing indexed fields exclude a record from the index.
    IndexKey prefix;
    std::optional<QueryBound> lower;
    std::optional<QueryBound> upper;
    std::optional<QueryCursor> after;
    std::size_t limit = 100;
};

struct QueryPage {
    std::vector<Record> records;
    // Continue the same query on the same session revision.
    std::optional<QueryCursor> next;
};

struct RecordChange {
    MutationKind kind;
    RecordKey key;
    std::optional<RecordVersion> previousVersion;
    std::optional<Record> record; // Post-image, absent on erase.
};

struct ChangeBatch {
    CommitRevision revision;
    std::vector<RecordChange> changes;
};

struct ChangePage {
    CommitRevision latestRevision;
    // Ascending batches after the requested revision, inclusive of all changes
    // in a commit. Callers resume after the last returned batch.
    std::vector<ChangeBatch> batches;
};

enum class Capability {
    PrimaryQueries,
    SecondaryIndexes,
    AtomicTransactions,
    ConsistentSnapshots,
    LogicalChangeJournal,
    // Records and their journal survive provider/process restart.
    DurablePersistence
};
using CapabilitySet = std::set<Capability>;

enum class ErrorCode {
    InvalidArgument,
    UnsupportedOperation,
    SessionAlreadyOpen,
    SessionClosed,
    UnknownCollection,
    UnknownIndex,
    RecordNotFound,
    RecordAlreadyExists,
    SchemaMismatch,
    VersionConflict,
    UniqueConstraint,
    RevisionExhausted,
    HistoryUnavailable,
    StorageFailure
};

struct OperationError {
    ErrorCode code;
    std::string message;
};

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : result_(std::move(value)) {}
    Result(OperationError error) : result_(std::move(error)) {}

    explicit operator bool() const noexcept { return std::holds_alternative<T>(result_); }
    T& Value() & { return std::get<T>(result_); }
    const T& Value() const & { return std::get<T>(result_); }
    T&& Value() && { return std::get<T>(std::move(result_)); }
    const OperationError& Error() const { return std::get<OperationError>(result_); }

private:
    std::variant<T, OperationError> result_;
};
using Status = Result<std::monostate>;

class ReadSnapshot {
public:
    virtual ~ReadSnapshot() = default;
    virtual CommitRevision Revision() const noexcept = 0;
    virtual Result<std::optional<Record>> Get(const RecordKey& key) const = 0;
    // Results align with input keys, including duplicates and missing records.
    virtual Result<std::vector<std::optional<Record>>> GetMany(std::span<const RecordKey> keys) const;
    virtual Result<QueryPage> Query(const QueryDescription& query) const = 0;
};

// Session reads each use one committed snapshot. An explicitly opened snapshot
// owns its view, which remains valid across later commits and close.
class Session {
public:
    virtual ~Session() = default;
    virtual Status Close() = 0;
    virtual Result<std::unique_ptr<ReadSnapshot>> OpenSnapshot() const = 0;
    // A successful batch advances the revision once. Records, required indexes,
    // and journal entries become visible together. A rejected batch changes none.
    virtual Result<CommitRevision> Commit(const Transaction& transaction) = 0;
    virtual Result<ChangePage> ReadChanges(CommitRevision after, std::size_t limit) const = 0;

    Result<std::optional<Record>> Get(const RecordKey& key) const;
    Result<std::vector<std::optional<Record>>> GetMany(std::span<const RecordKey> keys) const;
    Result<QueryPage> Query(const QueryDescription& query) const;
};

struct OpenSessionRequest {
    SessionId id;
    SessionSchema schema;
    CapabilitySet requiredCapabilities;
};

class StorageProvider {
public:
    virtual ~StorageProvider() = default;
    virtual CapabilitySet Capabilities() const = 0;
    // Open an existing session with a matching schema or initialize a new one.
    // A provider grants only one open writer for a particular session ID.
    virtual Result<std::unique_ptr<Session>> OpenSession(const OpenSessionRequest& request) = 0;
};
} // namespace ce::persistence
