#include "engine/persistence_query.h"

#include "persistence_validation.h"

#include <tuple>

namespace ce::persistence
{
namespace
{
using Clock = std::chrono::steady_clock;
using detail::Fail;

Status CheckExecution(const QueryLimits& limits)
{
    if (limits.stop.stop_requested())
        return Fail(ErrorCode::Cancelled, "Query execution was cancelled.");
    if (limits.deadline && Clock::now() >= *limits.deadline)
        return Fail(ErrorCode::ExecutionTimeout, "Query execution reached its deadline.");
    return Null{};
}

bool IsComparison(PredicateOp op)
{
    return op >= PredicateOp::Equal && op <= PredicateOp::GreaterEqual;
}

Status ValidatePredicate(const Predicate& node, const CollectionSchema& schema, const QueryLimits& limits,
                         std::size_t depth, std::size_t& nodes)
{
    if (++nodes > limits.predicateNodes || depth > limits.predicateDepth)
        return Fail(ErrorCode::ResourceLimit, "Predicate exceeds its node or depth limit.");
    if (node.op == PredicateOp::And || node.op == PredicateOp::Or || node.op == PredicateOp::Not) {
        if (node.field || node.value || node.children.empty()
            || (node.op == PredicateOp::Not && node.children.size() != 1))
            return Fail(ErrorCode::InvalidArgument, "Invalid Boolean predicate operands.");
        for (const auto& child : node.children)
            if (auto valid = ValidatePredicate(child, schema, limits, depth + 1, nodes); !valid)
                return valid.Error();
        return Null{};
    }
    if (!node.children.empty() || node.op < PredicateOp::Equal || node.op > PredicateOp::IsPresent)
        return Fail(ErrorCode::InvalidArgument, "Invalid comparison predicate.");
    FieldDefinition type{FieldType::String};
    if (node.field) {
        const auto field = schema.fields.find(*node.field);
        if (field == schema.fields.end())
            return Fail(ErrorCode::InvalidArgument, "Predicate references an undeclared field.");
        type = field->second;
    }
    if (IsComparison(node.op)) {
        if (!node.value || !detail::MatchesType(*node.value, type))
            return Fail(ErrorCode::InvalidArgument, "Predicate operand has the wrong field type.");
    } else if (node.value) {
        return Fail(ErrorCode::InvalidArgument, "Presence and null predicates accept no comparison value.");
    }
    return Null{};
}

enum class Truth { False, True, Unknown };

Truth Boolean(bool value) { return value ? Truth::True : Truth::False; }

Truth Compare(PredicateOp op, const FieldValue& actual, const FieldValue& expected)
{
    switch (op) {
        case PredicateOp::Equal: return Boolean(actual == expected);
        case PredicateOp::NotEqual: return Boolean(actual != expected);
        case PredicateOp::Less: return Boolean(actual < expected);
        case PredicateOp::LessEqual: return Boolean(actual <= expected);
        case PredicateOp::Greater: return Boolean(actual > expected);
        case PredicateOp::GreaterEqual: return Boolean(actual >= expected);
        default: return Truth::False;
    }
}

Truth Evaluate(const Predicate& node, const Record& record)
{
    switch (node.op) {
        case PredicateOp::And:
        case PredicateOp::Or: {
            const auto decisive = node.op == PredicateOp::And ? Truth::False : Truth::True;
            bool unknown = false;
            for (const auto& child : node.children) {
                const auto value = Evaluate(child, record);
                if (value == decisive) return value;
                unknown |= value == Truth::Unknown;
            }
            return unknown ? Truth::Unknown : Boolean(node.op == PredicateOp::And);
        }
        case PredicateOp::Not: {
            const auto value = Evaluate(node.children.front(), record);
            return value == Truth::Unknown ? value : Boolean(value == Truth::False);
        }
        default: break;
    }
    const auto value = node.field ? record.data.fields.find(*node.field) : record.data.fields.end();
    const bool present = !node.field || value != record.data.fields.end();
    if (node.op == PredicateOp::IsPresent) return Boolean(present);
    if (node.op == PredicateOp::IsMissing) return Boolean(!present);
    if (node.op == PredicateOp::IsNull) return Boolean(node.field && present && std::holds_alternative<Null>(value->second));
    if (!present) return Truth::Unknown;
    return node.field ? Compare(node.op, value->second, *node.value)
                      : Compare(node.op, FieldValue{record.id.value}, *node.value);
}

struct Constraint {
    bool present = false;
    bool impossible = false;
    std::optional<FieldValue> exact;
    std::optional<QueryBound> lower;
    std::optional<QueryBound> upper;
};

using Constraints = std::map<std::optional<FieldId>, Constraint>;

void RefineLower(Constraint& domain, const FieldValue& value, bool inclusive)
{
    if (!domain.lower || value > domain.lower->value
        || (value == domain.lower->value && !inclusive))
        domain.lower = QueryBound{value, inclusive};
}

void RefineUpper(Constraint& domain, const FieldValue& value, bool inclusive)
{
    if (!domain.upper || value < domain.upper->value
        || (value == domain.upper->value && !inclusive))
        domain.upper = QueryBound{value, inclusive};
}

void Collect(const Predicate& node, Constraints& constraints)
{
    if (node.op == PredicateOp::And) {
        for (const auto& child : node.children) Collect(child, constraints);
        return;
    }
    if (!IsComparison(node.op) && node.op != PredicateOp::IsPresent && node.op != PredicateOp::IsNull)
        return; // OR, NOT and missing tests are evaluated as residual predicates.
    auto& domain = constraints[node.field];
    domain.present = true;
    if (node.op == PredicateOp::IsNull || node.op == PredicateOp::Equal) {
        const auto value = node.op == PredicateOp::IsNull ? FieldValue{Null{}} : *node.value;
        domain.impossible |= domain.exact && *domain.exact != value;
        domain.exact = value;
    } else if (node.op == PredicateOp::Less || node.op == PredicateOp::LessEqual) {
        RefineUpper(domain, *node.value, node.op == PredicateOp::LessEqual);
    } else if (node.op == PredicateOp::Greater || node.op == PredicateOp::GreaterEqual) {
        RefineLower(domain, *node.value, node.op == PredicateOp::GreaterEqual);
    }
}

bool Contradiction(const Constraint& domain)
{
    if (domain.impossible) return true;
    if (domain.lower && domain.upper &&
        (domain.lower->value > domain.upper->value ||
         (domain.lower->value == domain.upper->value && (!domain.lower->inclusive || !domain.upper->inclusive))))
        return true;
    if (domain.exact) {
        QueryDescription bounds;
        bounds.lower = domain.lower;
        bounds.upper = domain.upper;
        const IndexKey key{*domain.exact};
        return detail::BelowLower(bounds, key) || detail::AboveUpper(bounds, key);
    }
    return false;
}

PortableQueryPlan Seek(const PortableQuery& query, const Constraints& constraints,
                       const std::optional<IndexId>& id, const IndexDefinition* index)
{
    PortableQueryPlan plan;
    plan.index = id;
    plan.direction = query.direction;
    plan.residualFilter = query.where.has_value();
    if (index) plan.indexVersion = index->version;
    const auto components = index ? index->fields.size() : 1;
    for (std::size_t i = 0; i < components; ++i) {
        const auto field = index ? std::optional{index->fields[i]} : std::nullopt;
        const auto term = constraints.find(field);
        if (term == constraints.end()) break;
        if (term->second.exact) {
            plan.prefix.push_back(*term->second.exact);
            continue;
        }
        plan.lower = term->second.lower;
        plan.upper = term->second.upper;
        break;
    }
    const bool seek = !plan.prefix.empty() || plan.lower || plan.upper;
    plan.access = index ? (seek ? QueryAccess::IndexSeek : QueryAccess::IndexScan)
                        : (seek ? QueryAccess::PrimarySeek : QueryAccess::PrimaryScan);
    return plan;
}

bool Covers(const IndexDefinition& index, const CollectionSchema& schema, const Constraints& terms)
{
    return std::ranges::all_of(index.fields, [&](const auto& field) {
        const auto term = terms.find(field);
        return schema.fields.at(field).required || (term != terms.end() && term->second.present);
    });
}

auto Rank(const PortableQueryPlan& plan, const IndexDefinition* index)
{
    const auto components = index ? index->fields.size() : 1;
    const bool unique = plan.prefix.size() == components && (!index || index->unique);
    return std::tuple{unique, plan.prefix.size(), static_cast<int>(plan.lower.has_value()) + plan.upper.has_value()};
}

bool NeedsScan(QueryAccess access)
{
    return access == QueryAccess::PrimaryScan || access == QueryAccess::IndexScan;
}
} // namespace

struct PortableCursor::State {
    std::weak_ptr<const void> view;
    PortableQuery query;
    QueryCursor native;
    Clock::time_point expiry;
};

Result<PortableQueryPlan> ExplainQuery(const ReadSnapshot& snapshot, const PortableQuery& query, const QueryLimits& limits)
{
    if (auto active = CheckExecution(limits); !active) return active.Error();
    if (!query.limit || query.limit > limits.pageEntries || !limits.candidates)
        return Fail(ErrorCode::ResourceLimit, "Query page size or candidate budget is outside its limits.");
    if (query.index && query.primaryOnly)
        return Fail(ErrorCode::InvalidArgument, "Choose either a secondary index or primary ordering.");
    if (query.direction != SortDirection::Ascending && query.direction != SortDirection::Descending)
        return Fail(ErrorCode::InvalidArgument, "Invalid query direction.");
    if (!limits.predicateNodes || limits.predicateNodes > 1024 || !limits.predicateDepth || limits.predicateDepth > 32
        || limits.cursorLifetime <= std::chrono::milliseconds::zero() || limits.cursorLifetime > std::chrono::hours(24))
        return Fail(ErrorCode::InvalidArgument, "Invalid predicate or cursor limits.");

    auto schema = snapshot.Describe(query.collection);
    if (!schema) return schema.Error();
    Constraints terms;
    if (query.where) {
        std::size_t nodes = 0;
        if (auto valid = ValidatePredicate(*query.where, schema.Value(), limits, 1, nodes); !valid)
            return valid.Error();
        Collect(*query.where, terms);
    }
    const auto& indexes = schema.Value().indexes;
    const auto requested = query.index ? indexes.find(*query.index) : indexes.end();
    if (query.index && requested == indexes.end())
        return Fail(ErrorCode::UnknownIndex, "Unknown secondary index.");
    const bool empty = std::ranges::any_of(terms, [&](const auto& term) {
        const auto& [field, constraint] = term;
        const bool cannotBeNull = !field || !schema.Value().fields.at(*field).nullable;
        return Contradiction(constraint) || (cannotBeNull && constraint.exact &&
               std::holds_alternative<Null>(*constraint.exact));
    });
    if (empty) {
        PortableQueryPlan plan;
        plan.direction = query.direction;
        return plan;
    }
    if (requested != indexes.end()) {
        if (!Covers(requested->second, schema.Value(), terms))
            return Fail(ErrorCode::InvalidArgument, "The query can match records missing fields in the requested index.");
        return Seek(query, terms, requested->first, &requested->second);
    }

    auto plan = Seek(query, terms, {}, nullptr);
    auto rank = Rank(plan, nullptr);
    if (!query.primaryOnly) {
        for (const auto& [id, index] : indexes) {
            if (!Covers(index, schema.Value(), terms)) continue;
            auto candidate = Seek(query, terms, id, &index);
            const auto score = Rank(candidate, &index);
            if (score > rank) {
                plan = std::move(candidate);
                rank = score;
            }
        }
    }
    return plan;
}

Result<PortableQueryPage> ExecuteQuery(const ReadSnapshot& snapshot, const PortableQuery& query,
                                       const QueryLimits& limits, const PortableCursor* after)
{
    const auto start = Clock::now();
    auto explained = ExplainQuery(snapshot, query, limits);
    if (!explained) return explained.Error();
    PortableQueryPage result;
    result.plan = std::move(explained).Value();
    if (NeedsScan(result.plan.access) && !query.allowScan)
        return Fail(ErrorCode::ScanRequired, "The query needs a scan. Explicitly allow a bounded scan to execute it.");

    QueryDescription providerQuery;
    providerQuery.collection = query.collection;
    providerQuery.index = result.plan.index;
    providerQuery.direction = query.direction;
    providerQuery.prefix = result.plan.prefix;
    providerQuery.lower = result.plan.lower;
    providerQuery.upper = result.plan.upper;
    auto expiry = start + limits.cursorLifetime;
    const auto view = snapshot.ViewIdentity();
    if (after) {
        const auto owner = after->state_->view.lock();
        if (!owner || start >= after->state_->expiry)
            return Fail(ErrorCode::HistoryUnavailable, "Portable query cursor expired.");
        if (owner != view.lock() || after->state_->query != query)
            return Fail(ErrorCode::InvalidArgument, "Portable query cursor requires its original snapshot and query.");
        expiry = std::min(expiry, after->state_->expiry);
        providerQuery.after = after->state_->native;
    }

    if (result.plan.access != QueryAccess::Empty) {
        const auto capacity = snapshot.PageCapacity();
        if (!capacity) return Fail(ErrorCode::ResourceLimit, "Provider query pages are unavailable.");
        do {
            if (auto active = CheckExecution(limits); !active) return active.Error();
            providerQuery.limit = std::min({capacity, std::size_t{64}, query.limit - result.records.size(),
                                            limits.candidates - result.statistics.candidates});
            auto page = snapshot.Query(providerQuery);
            if (!page) return page.Error();
            ++result.statistics.providerPages;
            if (page.Value().records.size() > providerQuery.limit ||
                (page.Value().records.empty() && page.Value().next))
                return Fail(ErrorCode::StorageFailure, "Provider returned an invalid query page.");
            for (auto& record : page.Value().records) {
                if (auto active = CheckExecution(limits); !active) return active.Error();
                ++result.statistics.candidates;
                if (!query.where || Evaluate(*query.where, record) == Truth::True)
                    result.records.push_back(std::move(record));
            }
            providerQuery.after = std::move(page.Value().next);
        } while (providerQuery.after && result.records.size() < query.limit
                 && result.statistics.candidates < limits.candidates);
        if (providerQuery.after) {
            if (providerQuery.after->expiresAt) expiry = std::min(expiry, *providerQuery.after->expiresAt);
            auto state = std::make_shared<PortableCursor::State>(PortableCursor::State{view, query, *providerQuery.after, expiry});
            result.next = PortableCursor(std::move(state));
            result.statistics.candidateBudgetReached = result.statistics.candidates == limits.candidates;
        }
    }
    if (auto active = CheckExecution(limits); !active) return active.Error();
    result.statistics.returned = result.records.size();
    result.statistics.elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);
    return result;
}
} // namespace ce::persistence
