#pragma once

#include "engine/persistence.h"

namespace ce::persistence
{
enum class PredicateOp {
    And, Or, Not, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, IsNull, IsMissing, IsPresent
};

struct Predicate {
    PredicateOp op = PredicateOp::And;
    // For a leaf, absence means the opaque record ID (a byte string).
    std::optional<FieldId> field;
    std::optional<FieldValue> value;
    std::vector<Predicate> children;
    bool operator==(const Predicate&) const = default;
};

struct PortableQuery {
    CollectionKey collection;
    std::optional<Predicate> where;
    // Force a named secondary index or the primary ID order. With neither,
    // the planner chooses a deterministic order based on applicable seeks.
    std::optional<IndexId> index;
    bool primaryOnly = false;
    SortDirection direction = SortDirection::Ascending;
    bool allowScan = false;
    std::size_t limit = 100;
    bool operator==(const PortableQuery&) const = default;
};

enum class QueryAccess { Empty, PrimarySeek, IndexSeek, PrimaryScan, IndexScan };

struct PortableQueryPlan {
    QueryAccess access = QueryAccess::Empty;
    std::optional<IndexId> index;
    std::optional<IndexVersion> indexVersion;
    SortDirection direction = SortDirection::Ascending;
    IndexKey prefix;
    std::optional<QueryBound> lower;
    std::optional<QueryBound> upper;
    bool residualFilter = false;
};

struct QueryLimits {
    std::size_t pageEntries = 256;
    std::size_t candidates = 4096;
    std::size_t predicateNodes = 64;
    std::size_t predicateDepth = 12;
    std::chrono::milliseconds cursorLifetime = std::chrono::seconds(30);
    std::optional<std::chrono::steady_clock::time_point> deadline;
    std::stop_token stop;
};

struct QueryStatistics {
    std::size_t candidates = 0;
    std::size_t providerPages = 0;
    std::size_t returned = 0;
    bool candidateBudgetReached = false;
    std::chrono::nanoseconds elapsed{};
};

struct PortableQueryPage;

class PortableCursor {
public:
    PortableCursor(const PortableCursor&) = default;
    PortableCursor& operator=(const PortableCursor&) = default;

private:
    struct State;
    explicit PortableCursor(std::shared_ptr<const State> state) : state_(std::move(state)) {}
    std::shared_ptr<const State> state_;

    friend Result<PortableQueryPage> ExecuteQuery(const ReadSnapshot&, const PortableQuery&,
                                                  const QueryLimits&, const PortableCursor*);
};

struct PortableQueryPage {
    PortableQueryPlan plan;
    QueryStatistics statistics;
    std::vector<Record> records;
    std::optional<PortableCursor> next;
};

Result<PortableQueryPlan> ExplainQuery(const ReadSnapshot& snapshot, const PortableQuery& query,
                                      const QueryLimits& limits = {});
Result<PortableQueryPage> ExecuteQuery(const ReadSnapshot& snapshot, const PortableQuery& query,
                                       const QueryLimits& limits = {}, const PortableCursor* after = nullptr);
} // namespace ce::persistence
