#include "engine/in_memory_persistence.h"
#include "engine/persistence_query.h"
#include "engine/rocksdb_persistence.h"
#include "engine/sqlite_persistence.h"

#include <algorithm>
#include <cstdlib>
#include <future>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

namespace
{
using namespace ce::persistence;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

const CollectionKey collection{{"test"}, {"records"}};
const FieldId group{"group"}, rank{"rank"}, code{"code"}, tag{"tag"};
const IndexId composite{"by-group-rank"}, single{"by-rank"}, unique{"by-code"}, sparse{"by-tag"}, alternate{"alt"};

void Check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

template <typename T>
T Take(Result<T> result)
{
    if (!result) throw std::runtime_error(result.Error().message);
    return std::move(result).Value();
}

template <typename T>
void Expect(Result<T> result, ErrorCode error, const char* description)
{
    if (result || result.Error().code != error)
        throw std::runtime_error(std::string(description) + " Outcome: " +
                                 (result ? "success" : result.Error().message));
}

IndexDefinition GroupRank(IndexVersion version = {1}, SortDirection order = SortDirection::Descending)
{
    return {{group, rank}, false, version, {SortDirection::Ascending, order}};
}

OpenSessionRequest Request(bool includeComposite = false, IndexVersion version = {1},
                           SortDirection order = SortDirection::Descending)
{
    CollectionSchema schema{{1}, {{group, {FieldType::String}}, {rank, {FieldType::Integer}},
                                  {code, {FieldType::String}}, {tag, {FieldType::String, false, true}}},
                            {{single, {{rank}}}, {unique, {{code}, true}}, {sparse, {{tag}}}}};
    if (includeComposite) schema.indexes.emplace(composite, GroupRank(version, order));
    OpenSessionRequest request{{"portable-query"}, {{collection, schema}}, {}};
    request.policy.pageEntries = 3;
    return request;
}

std::unique_ptr<StorageProvider> MakeProvider(std::string_view name, const fs::path& path)
{
    if (name == "memory") return CreateInMemoryProvider();
    if (name == "sqlite") return Take(CreateSQLiteProvider({path}));
    if (name == "rocksdb") return Take(CreateRocksDBProvider({path}));
    throw std::runtime_error("Unknown test provider.");
}

RecordData Row(const std::string& id, std::string category, std::int64_t value, std::optional<FieldValue> optional = {})
{
    RecordData result{{1}, {{group, std::move(category)}, {rank, value}, {code, "key-" + id}}};
    if (optional) result.fields.emplace(tag, std::move(*optional));
    return result;
}

Mutation Create(std::string id, RecordData data)
{
    return {MutationKind::Create, {collection, {std::move(id)}}, std::move(data), {}};
}

void Seed(Session& session)
{
    Take(session.Commit({{
        Create("a", Row("a", "aa", 10, Null{})),
        Create("b", Row("b", "aa", 20)),
        Create("c", Row("c", "aa", 20, std::string("x"))),
        Create("d", Row("d", "bb", 30, std::string("y"))),
        Create("e", Row("e", "aa", 15, std::string("x")))
    }}));
}

Predicate Compare(PredicateOp op, FieldId field, FieldValue value)
{
    return {op, std::move(field), std::move(value), {}};
}

Predicate Logical(PredicateOp op, std::initializer_list<Predicate> nodes)
{
    return {op, {}, {}, nodes};
}

PortableQuery Select(Predicate where)
{
    PortableQuery query;
    query.collection = collection;
    query.where = std::move(where);
    query.limit = 12;
    return query;
}

std::vector<std::string> IDs(const std::vector<Record>& records)
{
    std::vector<std::string> ids;
    for (const auto& record : records) ids.push_back(record.id.value);
    return ids;
}

std::vector<std::string> Collect(const ReadSnapshot& view, const PortableQuery& query, const QueryLimits& limits = {})
{
    std::vector<std::string> all;
    auto page = Take(ExecuteQuery(view, query, limits));
    for (std::size_t pages = 0;; ++pages) {
        Check(pages < 100, "Pagination failed to make progress.");
        const auto ids = IDs(page.records);
        all.insert(all.end(), ids.begin(), ids.end());
        if (!page.next) break;
        page = Take(ExecuteQuery(view, query, limits, &*page.next));
    }
    return all;
}

class BlockedRead final : public ReadSnapshot {
public:
    explicit BlockedRead(const ReadSnapshot& snapshot) : snapshot_(snapshot) {}
    CommitRevision Revision() const noexcept override { return snapshot_.Revision(); }
    Result<CollectionSchema> Describe(const CollectionKey& collection) const override { return snapshot_.Describe(collection); }
    Result<std::optional<Record>> Get(const RecordKey& key) const override { return snapshot_.Get(key); }
    std::size_t PageCapacity() const noexcept override { return snapshot_.PageCapacity(); }
    Result<QueryPage> Query(const QueryDescription& query) const override
    {
        entered.count_down();
        resume.wait();
        return snapshot_.Query(query);
    }
    mutable std::latch entered{1}, resume{1};

private:
    const ReadSnapshot& snapshot_;
};

void CheckGuards(const ReadSnapshot& view, PortableQuery query)
{
    query.limit = 1;
    QueryLimits limits;
    limits.cursorLifetime = 2ms;
    const auto expired = Take(ExecuteQuery(view, query, limits));
    Check(expired.next.has_value(), "Expected a cursor to expire.");
    std::this_thread::sleep_for(12ms);
    Expect(ExecuteQuery(view, query, {}, &*expired.next), ErrorCode::HistoryUnavailable, "Expired cursor survived.");

    limits = {};
    limits.deadline = std::chrono::steady_clock::now() - 1ms;
    Expect(ExecuteQuery(view, query, limits), ErrorCode::ExecutionTimeout, "Expired execution deadline was ignored.");
    limits = {};
    limits.predicateNodes = 1;
    auto compound = query;
    compound.where = Logical(PredicateOp::And, {*query.where, *query.where});
    Expect(ExecuteQuery(view, compound, limits), ErrorCode::ResourceLimit, "Predicate node limit was ignored.");
    limits = {};
    limits.pageEntries = 1;
    compound.limit = 2;
    Expect(ExecuteQuery(view, compound, limits), ErrorCode::ResourceLimit, "Page size limit was ignored.");

    BlockedRead blocked(view);
    std::stop_source stop;
    limits = {};
    limits.stop = stop.get_token();
    auto job = std::async(std::launch::async, [&] { return ExecuteQuery(blocked, query, limits); });
    blocked.entered.wait();
    stop.request_stop();
    blocked.resume.count_down();
    Expect(job.get(), ErrorCode::Cancelled, "In-flight cancellation was ignored.");
}

void Conformance(std::string_view name, const fs::path& path)
{
    auto provider = MakeProvider(name, path);
    auto session = Take(provider->OpenSession(Request()));
    Seed(*session);
    const auto original = Take(session->OpenSnapshot());
    Check(!Take(original->Describe(collection)).indexes.contains(composite), "Unexpected initial compound index.");
    const auto added = Take(session->MaintainIndex({collection, composite, IndexOperation::Create, GroupRank()}));
    Check(added.revision == CommitRevision{1} && added.recordsExamined == 5, "Invalid create result.");
    const auto view = Take(session->OpenSnapshot());
    Check(Take(view->Describe(collection)).indexes.at(composite) == GroupRank(), "Catalog differs from created definition.");
    QueryDescription nativeIndex{collection, composite};
    nativeIndex.limit = 1;
    Expect(original->Query(nativeIndex), ErrorCode::UnknownIndex, "Index creation leaked into an old snapshot.");
    Check(Take(session->ReadChanges({}, 2)).batches.size() == 1, "Catalog change altered record journal.");

    auto grouped = Select(Compare(PredicateOp::Equal, group, std::string("aa")));
    grouped.limit = 1;
    Check(Collect(*view, grouped) == std::vector<std::string>{"b", "c", "e", "a"}, "Mixed composite order differs.");
    auto first = Take(ExecuteQuery(*view, grouped));
    Check(first.plan.access == QueryAccess::IndexSeek && first.plan.index == composite && first.next,
          "Planner did not use a compound index or produce a cursor.");
    auto reverse = grouped;
    reverse.direction = SortDirection::Descending;
    Check(Collect(*view, reverse) == std::vector<std::string>{"a", "e", "c", "b"}, "Reverse traversal differs.");
    Expect(ExecuteQuery(*view, reverse, {}, &*first.next), ErrorCode::InvalidArgument, "Changed query reused a cursor.");

    auto bands = Select(Logical(PredicateOp::And, {
        Compare(PredicateOp::Equal, group, std::string("aa")), Compare(PredicateOp::GreaterEqual, rank, std::int64_t{15}),
        Compare(PredicateOp::Less, rank, std::int64_t{20}),
        Logical(PredicateOp::Or, {{PredicateOp::IsMissing, tag}, Compare(PredicateOp::Equal, tag, std::string("x"))})
    }));
    const auto range = Take(ExecuteQuery(*view, bands));
    Check(range.plan.index == composite && range.plan.lower && range.plan.upper && range.plan.residualFilter &&
          IDs(range.records) == std::vector<std::string>{"e"} && range.statistics.candidates == 1,
          "Compound bounds or residual Boolean predicate differ.");
    bands.direction = SortDirection::Descending;
    Check(Collect(*view, bands) == std::vector<std::string>{"e"}, "Reverse range traversal differs.");

    auto ranked = Select(Compare(PredicateOp::Equal, rank, std::int64_t{20}));
    const auto byRank = Take(ExecuteQuery(*view, ranked));
    Check(byRank.plan.index == single && IDs(byRank.records) == std::vector<std::string>{"b", "c"}, "Duplicate keys differ.");
    auto primary = Select({PredicateOp::GreaterEqual, {}, std::string("c"), {}});
    primary.direction = SortDirection::Descending;
    primary.limit = 1;
    Check(Collect(*view, primary) == std::vector<std::string>{"e", "d", "c"}, "Reverse primary range differs.");

    auto byNull = Select({PredicateOp::IsNull, tag});
    Check(Collect(*view, byNull) == std::vector<std::string>{"a"}, "Null key was not treated as present.");
    auto missing = Select({PredicateOp::IsMissing, tag});
    Check(Take(ExplainQuery(*view, missing)).access == QueryAccess::PrimaryScan, "Missing predicate planned an incomplete index.");
    Expect(ExecuteQuery(*view, missing), ErrorCode::ScanRequired, "A scan was silently substituted.");
    missing.index = sparse;
    missing.allowScan = true;
    Expect(ExecuteQuery(*view, missing), ErrorCode::InvalidArgument, "Mandatory sparse order silently omitted records.");
    missing.index = {};
    QueryLimits limited;
    limited.candidates = 1;
    const auto yielding = Take(ExecuteQuery(*view, missing, limited));
    Check(yielding.next && yielding.records.empty() && yielding.statistics.candidateBudgetReached,
          "A bounded scan could not continue past a page with no matches.");
    Check(Collect(*view, missing, limited) == std::vector<std::string>{"b"}, "Missing keys or scan cursor differ.");
    auto negated = Select(Logical(PredicateOp::Not, {Compare(PredicateOp::Equal, tag, std::string("x"))}));
    negated.allowScan = true;
    Check(Collect(*view, negated) == std::vector<std::string>{"a", "d"}, "Missing values incorrectly matched NOT.");

    Expect(session->MaintainIndex({collection, alternate, IndexOperation::Create, IndexDefinition{{rank}, true}}),
           ErrorCode::UniqueConstraint, "Creating a unique index with duplicates succeeded.");
    Expect(session->MaintainIndex({collection, single, IndexOperation::Rebuild, IndexDefinition{{rank}, true, {2}},
                                   IndexVersion{1}}),
           ErrorCode::UniqueConstraint, "Replacing a non-unique index with a unique index did not roll back.");
    auto unchangedRank = Take(session->OpenSnapshot());
    Check(Collect(*unchangedRank, ranked) == std::vector<std::string>{"b", "c"} &&
          Take(unchangedRank->Describe(collection)).indexes.at(single).version == IndexVersion{1},
          "Failed replacement changed prior index entries or definition.");
    Expect(session->MaintainIndex({collection, alternate, IndexOperation::Create, IndexDefinition{{FieldId{"unknown"}}}}),
           ErrorCode::InvalidArgument, "Undeclared index field was accepted.");
    Expect(session->MaintainIndex({collection, composite, IndexOperation::Rebuild, GroupRank({1}, SortDirection::Ascending)}),
           ErrorCode::VersionConflict, "Changed key order accepted the previous version.");
    Check(!Take(Take(session->OpenSnapshot())->Describe(collection)).indexes.contains(alternate), "Failed index leaked into catalog.");

    auto duplicate = Row("f", "aa", 50);
    duplicate.fields[code] = std::string("key-a");
    Expect(session->Commit({{Create("new", Row("new", "aa", 60)), Create("f", duplicate)}}),
           ErrorCode::UniqueConstraint, "Duplicate transaction was not rejected.");
    auto rolledBack = Take(session->OpenSnapshot());
    auto probe = Select(Compare(PredicateOp::Equal, rank, std::int64_t{60}));
    Check(Collect(*rolledBack, probe).empty() && rolledBack->Revision() == CommitRevision{1},
          "Rejected transaction left index entries or advanced the revision.");

    BlockedRead blocked(*view);
    auto reader = std::async(std::launch::async, [&] { return ExecuteQuery(blocked, grouped); });
    blocked.entered.wait();
    auto rebuild = session->MaintainIndex({collection, composite, IndexOperation::Rebuild,
                                          GroupRank({2}, SortDirection::Ascending), IndexVersion{1}});
    blocked.resume.count_down();
    const auto upgraded = Take(std::move(rebuild));
    Check(upgraded.revision == CommitRevision{1} && upgraded.definition == GroupRank({2}, SortDirection::Ascending),
          "Changed definition was not published exactly once.");
    auto newest = Take(session->OpenSnapshot());
    Check(IDs(Take(reader.get()).records) == std::vector<std::string>{"b"} &&
          Collect(*view, grouped) == std::vector<std::string>{"b", "c", "e", "a"}, "Rebuild changed a pinned view.");
    Check(Collect(*newest, grouped) == std::vector<std::string>{"a", "e", "b", "c"}, "Rebuild order differs.");
    Expect(ExecuteQuery(*newest, grouped, {}, &*first.next), ErrorCode::InvalidArgument, "Cursor accepted another view at the same revision.");

    Take(session->Commit({{Create("new", Row("new", "aa", 50))}}));
    Check(Collect(*newest, grouped) == std::vector<std::string>{"a", "e", "b", "c"}, "Record write changed a pinned view.");
    auto current = Take(session->OpenSnapshot());
    const auto expected = std::vector<std::string>{"a", "e", "b", "c", "new"};
    Check(Collect(*current, grouped) == expected, "Record commit failed to maintain rebuilt index.");
    CheckGuards(*current, grouped);
    Take(session->MaintainIndex({collection, composite, IndexOperation::Rebuild}));
    Check(Collect(*Take(session->OpenSnapshot()), grouped) == expected, "Same-version rebuild changed results.");

    const auto image = Take(session->Export());
    Check(image.schema.at(collection).indexes.at(composite).version == IndexVersion{2}, "Export omitted published version.");
    auto imported = CreateInMemoryProvider();
    Take(imported->Import(image));
    auto importRequest = Request(true, {2}, SortDirection::Ascending);
    auto importedSession = Take(imported->OpenSession(importRequest));
    Check(Collect(*Take(importedSession->OpenSnapshot()), grouped) == expected, "Imported index order differs.");

    Take(session->MaintainIndex({collection, composite, IndexOperation::Remove, {}, IndexVersion{2}}));
    Expect(Take(session->OpenSnapshot())->Query(nativeIndex), ErrorCode::UnknownIndex, "Removed index remains active.");
    Check(Collect(*current, grouped) == expected, "Index removal changed a pinned view.");
    Take(session->Close());
    auto reopened = Take(provider->OpenSession(Request()));
    Check(!Take(Take(reopened->OpenSnapshot())->Describe(collection)).indexes.contains(composite), "Index removal was not retained.");
    std::cout << name << " portable query conformance passed\n";
}

void Recovery(std::string_view mode, std::string_view name, const fs::path& path)
{
    auto provider = MakeProvider(name, path);
    if (mode == "seed") {
        auto session = Take(provider->OpenSession(Request()));
        Seed(*session);
        Take(session->MaintainIndex({collection, composite, IndexOperation::Create, GroupRank()}));
        std::_Exit(71); // Published through the provider's durability mode.
    }
    const bool upgraded = mode == "verify-upgraded";
    auto session = Take(provider->OpenSession(Request(true, upgraded ? IndexVersion{2} : IndexVersion{1},
                                                      upgraded ? SortDirection::Ascending : SortDirection::Descending)));
    if (mode == "interrupt") {
        const IndexMaintenance limited{collection, composite, IndexOperation::Rebuild,
            GroupRank({2}, SortDirection::Ascending), IndexVersion{1}, 2};
        Expect(session->MaintainIndex(limited), ErrorCode::ResourceLimit, "Interrupted staged rebuild unexpectedly published.");
        std::_Exit(72); // Abort without a close or checkpoint after partial staging.
    }
    if (mode == "upgrade") {
        Take(session->MaintainIndex({collection, composite, IndexOperation::Rebuild,
                                     GroupRank({2}, SortDirection::Ascending), IndexVersion{1}}));
        std::_Exit(73);
    }
    Check(mode == "verify" || upgraded, "Unknown recovery mode.");
    auto grouped = Select(Compare(PredicateOp::Equal, group, std::string("aa")));
    grouped.limit = 1;
    auto view = Take(session->OpenSnapshot());
    Check(view->Revision() == CommitRevision{1} && Collect(*view, grouped) ==
          (upgraded ? std::vector<std::string>{"a", "e", "b", "c"} : std::vector<std::string>{"b", "c", "e", "a"}),
          "Reopened index or record revision differs from the committed state.");
}

void Benchmark(std::string_view name, const fs::path& path, int count)
{
    auto provider = MakeProvider(name, path);
    auto request = Request(true);
    request.policy.pageEntries = 256;
    auto session = Take(provider->OpenSession(request));
    for (int start = 0; start < count; start += 512) {
        Transaction batch;
        for (int i = start; i < std::min(count, start + 512); ++i) {
            const auto id = std::to_string(i);
            batch.mutations.push_back(Create(id, Row(id, "group-" + std::to_string(i % 64), i)));
        }
        Take(session->Commit(batch));
    }
    const auto view = Take(session->OpenSnapshot());
    auto query = Select(Logical(PredicateOp::And, {
        Compare(PredicateOp::Equal, group, std::string("group-13")),
        Compare(PredicateOp::GreaterEqual, rank, std::int64_t{64}),
        Compare(PredicateOp::Less, rank, std::int64_t{64 * 9})
    }));
    query.limit = 16;
    auto full = query;
    full.primaryOnly = true;
    full.allowScan = true;
    QueryLimits limits;
    limits.candidates = count + 1;
    auto sample = [&](const PortableQuery& variant, const char* strategy) {
        std::vector<double> times;
        std::vector<std::string> records;
        std::size_t candidates = 0;
        for (int round = -2; round < 21; ++round) {
            const auto page = Take(ExecuteQuery(*view, variant, limits));
            Check(!page.next && page.records.size() == 8, "Benchmark query did not fully execute.");
            candidates = page.statistics.candidates;
            records = IDs(page.records);
            if (round >= 0) times.push_back(std::chrono::duration<double, std::micro>(page.statistics.elapsed).count());
        }
        std::ranges::sort(times);
        std::ranges::sort(records);
        std::cout << name << ',' << count << ',' << strategy << ',' << candidates << ',' << records.size()
                  << ',' << times[10] << ',' << times[19] << '\n';
        return records;
    };
    const auto indexed = sample(query, "index_seek");
    Check(indexed == sample(full, "primary_scan"), "Indexed and full-scan benchmark results differ.");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        Check(argc >= 4, "Usage: query test MODE PROVIDER DIRECTORY [ROWS]");
        const std::string_view mode = argv[1];
        const std::string_view provider = argv[2];
        const auto path = fs::absolute(argv[3]);
        fs::create_directories(path);
        if (mode == "conformance") {
            Conformance(provider, path);
        } else if (mode == "benchmark") {
            Check(argc == 5, "Benchmark requires a record count.");
            const int records = std::stoi(argv[4]);
            Check(records >= 1024 && records <= 100000, "Benchmark record count must be between 1024 and 100000.");
            Benchmark(provider, path, records);
        } else {
            Recovery(mode, provider, path);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Portable persistence test failed: " << error.what() << '\n';
        return 1;
    }
}
