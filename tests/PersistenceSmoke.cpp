#include "engine/in_memory_persistence.h"
#include "engine/sqlite_persistence.h"
#include "PersistenceTestDirectory.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
using namespace ce::persistence;

const CollectionKey kMain{{"alpha"}, {"records"}};
const CollectionKey kOtherCollection{{"alpha"}, {"other"}};
const CollectionKey kOtherNamespace{{"beta"}, {"records"}};
const FieldId kGroup{"group"};
const FieldId kOrder{"order"};
const FieldId kLabel{"label"};
const FieldId kNote{"note"};
const FieldId kRatio{"ratio"};
const FieldId kFlag{"flag"};
const FieldId kBytes{"bytes"};
const IndexId kByGroupOrder{"group_order"};
const IndexId kByLabel{"label_unique"};
const IndexId kByNote{"note"};
const IndexId kByBytes{"bytes"};
const IndexId kByRatio{"ratio"};

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
void ExpectError(const Result<T>& result, ErrorCode code)
{
    if (result)
        throw std::runtime_error("An operation unexpectedly succeeded.");
    Check(result.Error().code == code, result.Error().message);
}

SessionSchema MakeSchema()
{
    CollectionSchema schema;
    schema.version = {1};
    schema.fields = {{kGroup, {FieldType::String}}, {kOrder, {FieldType::Integer}},
                     {kLabel, {FieldType::String, false}}, {kNote, {FieldType::String, false, true}},
                     {kRatio, {FieldType::Real, false}}, {kFlag, {FieldType::Boolean, false}},
                     {kBytes, {FieldType::Binary, false}}};
    schema.indexes = {{kByGroupOrder, {{kGroup, kOrder}}},
                      {kByLabel, {{kLabel}, true}}, {kByNote, {{kNote}}},
                      {kByBytes, {{kBytes}}}, {kByRatio, {{kRatio}}}};
    return {{kMain, schema}, {kOtherCollection, schema}, {kOtherNamespace, schema}};
}

RecordData Data(std::int64_t order, std::string group = "g")
{
    return {{1}, {{kGroup, std::move(group)}, {kOrder, order}}, std::nullopt};
}

Mutation Create(RecordKey key, RecordData data)
{
    return {MutationKind::Create, std::move(key), std::move(data), std::nullopt};
}

Mutation Replace(RecordKey key, RecordData data, RecordVersion expected)
{
    return {MutationKind::Replace, std::move(key), std::move(data), expected};
}

Record Existing(const Session& session, const RecordKey& key)
{
    return Take(session.Get(key)).value();
}

std::vector<RecordId> Ids(const QueryPage& page)
{
    std::vector<RecordId> result;
    for (const auto& record : page.records)
        result.push_back(record.id);
    return result;
}

struct TestBackend {
    std::optional<std::filesystem::path> directory;
    std::size_t nextProvider = 0;

    std::unique_ptr<StorageProvider> Create()
    {
        if (!directory)
            return CreateInMemoryProvider();
        return Take(CreateSQLiteProvider({*directory / std::to_string(++nextProvider)}));
    }
};

struct Fixture {
    explicit Fixture(TestBackend& backend)
        : provider(backend.Create()), session(Take(provider->OpenSession({{"fixture"}, MakeSchema(), {}}))) {}

    std::unique_ptr<StorageProvider> provider;
    std::unique_ptr<Session> session;
};

void Lifecycle(TestBackend& backend)
{
    Fixture fixture(backend);
    auto& session = *fixture.session;
    // IDs are opaque bytes. The same ID may appear in different collections.
    const RecordId id{std::string("X/\0z", 4)};
    const RecordKey main{kMain, id};
    const RecordKey second{kOtherCollection, id};
    const RecordKey other{kOtherNamespace, id};
    const RecordKey missing{kMain, {"missing"}};

    auto data = Data(-3);
    data.fields.emplace(kBytes, Binary{0, 128, 255});
    data.fields.emplace(kFlag, true);
    data.fields.emplace(kRatio, 1.5);
    data.fields.emplace(kNote, Null{});
    data.binary = Binary{254, 0, 1};

    Check(Take(session.Commit({{Create(main, data), Create(second, Data(5)), Create(other, Data(6))}})).value == 1,
          "First revision is one.");
    const auto original = Existing(session, main);
    Check(original.id == id && original.data == data, "Opaque ID and typed data must round trip.");
    const auto values = Take(session.GetMany(std::array{other, main, missing, second, main}));
    Check(values.size() == 5 && values[0] && values[0]->data == Data(6) && values[1] == original && !values[2]
          && values[3] && values[3]->data == Data(5) && values[4] == original, "Bulk reads must preserve request order and isolation.");
    ExpectError(session.Get({{{"other"}, {"records"}}, id}), ErrorCode::UnknownCollection);
    ExpectError(session.Commit({{Create(main, data)}}), ErrorCode::RecordAlreadyExists);

    auto replacement = Data(4);
    replacement.binary = Binary{};
    Check(Take(session.Commit({{Replace(main, replacement, original.version)}})).value == 2,
          "Replacement advances the revision.");
    const auto current = Existing(session, main);
    Check(current.id == id && current.version != original.version && current.data == replacement,
          "Replacement preserves the ID and advances the version.");
    ExpectError(session.Commit({{Replace(main, data, original.version)}}), ErrorCode::VersionConflict);
    ExpectError(session.Commit({{Create(missing, Data(1))}, CommitRevision{1}}), ErrorCode::VersionConflict);

    Check(Take(session.Commit({{{MutationKind::Erase, main, std::nullopt, current.version}}})).value == 3,
          "Deletion advances the revision.");
    Check(!Take(session.Get(main)) && Existing(session, other).data == Data(6), "Deletion is scoped to the key.");
    Check(Take(session.Commit({{Create(main, data)}})).value == 4, "An erased ID can be recreated.");
    Check(Existing(session, main).version != current.version, "Recreation must not reuse a previous version.");
    ExpectError(session.Commit({{Replace(main, replacement, current.version)}}), ErrorCode::VersionConflict);
}

void AtomicityAndJournal(TestBackend& backend)
{
    Fixture fixture(backend);
    auto& session = *fixture.session;
    const RecordKey a{kMain, {"a"}};
    const RecordKey b{kMain, {"b"}};
    const RecordKey c{kOtherCollection, {"c"}};
    auto first = Data(1);
    auto second = Data(2);
    first.fields.emplace(kLabel, std::string("first"));
    second.fields.emplace(kLabel, std::string("second"));

    Take(session.Commit({{Create(a, first), Create(b, second)}}));
    const auto a1 = Existing(session, a);
    const auto b1 = Existing(session, b);

    auto invalid = Data(9);
    invalid.fields[kOrder] = true;
    ExpectError(session.Commit({{Create(c, Data(3)), Replace(b, invalid, b1.version)}}), ErrorCode::InvalidArgument);
    invalid = Data(9);
    invalid.schemaVersion = {2};
    ExpectError(session.Commit({{Create(c, Data(3)), Replace(b, invalid, b1.version)}}), ErrorCode::SchemaMismatch);
    invalid = Data(9);
    invalid.fields.emplace(kRatio, std::numeric_limits<double>::quiet_NaN());
    ExpectError(session.Commit({{Replace(b, invalid, b1.version)}}), ErrorCode::InvalidArgument);
    ExpectError(session.Commit({{Create(c, Data(3)), Create(c, Data(3))}}), ErrorCode::InvalidArgument);
    ExpectError(session.Commit({{Create(c, Data(3)), Replace(a, second, a1.version)}}), ErrorCode::UniqueConstraint);
    Check(!Take(session.Get(c)) && Existing(session, a) == a1, "Failed batches must not change any collection.");
    const auto initialJournal = Take(session.ReadChanges({}, 10));
    Check(initialJournal.latestRevision == CommitRevision{1} && initialJournal.batches.size() == 1,
          "Failed batches must not consume a revision or enter the journal.");

    auto snapshot = Take(session.OpenSnapshot());
    const auto commit = Take(session.Commit({{Create(c, Data(3)), Replace(a, second, a1.version),
                                              Replace(b, first, b1.version)}, CommitRevision{1}}));
    Check(commit == CommitRevision{2}, "A cross-collection commit advances exactly one revision.");
    Check(snapshot->Revision() == CommitRevision{1} && Take(snapshot->Get(a)) == a1
          && !Take(snapshot->Get(c)), "Old snapshot must see a consistent prior revision.");
    Check(Existing(session, a).data == second && Existing(session, b).data == first,
          "Unique-key swaps are valid when the final state is unique.");
    const auto oldSnapshotQuery = Take(snapshot->Query({kMain, kByLabel, {std::string("first")}}));
    Check(Ids(oldSnapshotQuery) == std::vector<RecordId>{a.id}, "Snapshot index must match snapshot records.");

    const auto oldVersion = Existing(session, b).version;
    Take(session.Commit({{{MutationKind::Erase, b, {}, oldVersion}}}));
    auto page = Take(session.ReadChanges({}, 1));
    Check(page.latestRevision == CommitRevision{3} && page.batches.size() == 1
          && page.batches[0].revision == CommitRevision{1}, "First journal page must be ordered and bounded.");
    page = Take(session.ReadChanges(page.batches[0].revision, 4));
    Check(page.batches.size() == 2 && page.batches[0].revision == CommitRevision{2}
          && page.batches[1].revision == CommitRevision{3}, "Journal must resume strictly after the cursor.");
    const auto& changes = page.batches[0].changes;
    Check(changes.size() == 3 && changes[0].key == c && changes[1].key == a
          && changes[1].previousVersion == a1.version && changes[1].record && changes[1].record->data == second,
          "Journal preserves batch order and contains the post-image.");
    Check(page.batches[1].changes.size() == 1, "The erase commit has one journal change.");
    const auto& deletion = page.batches[1].changes[0];
    Check(deletion.key == b && deletion.kind == MutationKind::Erase && deletion.previousVersion == oldVersion
          && !deletion.record, "Journal deletion has a previous version and no post-image.");
    ExpectError(session.ReadChanges({4}, 1), ErrorCode::InvalidArgument);
}

void IndexedQueries(TestBackend& backend)
{
    Fixture fixture(backend);
    auto& session = *fixture.session;
    auto nullNote = Data(-1);
    nullNote.fields.emplace(kNote, Null{});
    Take(session.Commit({{Create({kMain, {"c"}}, Data(3)), Create({kMain, {"b"}}, nullNote),
                         Create({kMain, {"d"}}, Data(1, "h")), Create({kMain, {"a"}}, Data(-1))}}));
    auto old = Take(session.OpenSnapshot());

    QueryDescription query{kMain};
    query.lower = QueryBound{std::string("a"), false};
    query.upper = QueryBound{std::string("c")};
    Check(Ids(Take(old->Query(query))) == std::vector<RecordId>{{"b"}, {"c"}}, "Primary key range is byte sorted.");

    query = {kMain, kByGroupOrder, {std::string("g")}, QueryBound{std::int64_t{-1}},
             QueryBound{std::int64_t{3}, false}, std::nullopt, 1};
    auto page = Take(old->Query(query));
    Check(Ids(page) == std::vector<RecordId>{{"a"}} && page.next, "Equal composite values use the record ID tie breaker.");
    query.after = page.next;
    page = Take(old->Query(query));
    Check(Ids(page) == std::vector<RecordId>{{"b"}} && !page.next, "Continuations preserve range bounds.");
    Take(session.Commit({{Create({kMain, {"aa"}}, Data(-1))}}));
    ExpectError(session.Query(query), ErrorCode::HistoryUnavailable);
    Check(Ids(Take(old->Query(query))) == std::vector<RecordId>{{"b"}}, "A pinned cursor remains usable on its snapshot.");

    Check(Ids(Take(session.Query({kMain, kByNote, {Null{}}}))) == std::vector<RecordId>{{"b"}},
          "A missing value is distinct from an indexed null.");
    ExpectError(session.Query({kMain, IndexId{"absent"}}), ErrorCode::UnknownIndex);
    ExpectError(session.Query({kMain, kByGroupOrder, {false}}), ErrorCode::InvalidArgument);
    ExpectError(session.Query({kMain, kByGroupOrder, {}, QueryBound{std::int64_t{2}}}), ErrorCode::InvalidArgument);
}

void IndexedScalarOrdering(TestBackend& backend)
{
    Fixture fixture(backend);
    auto& session = *fixture.session;
    const auto record = [](const char* id, Binary bytes, double ratio) {
        auto data = Data(0);
        data.fields.emplace(kBytes, std::move(bytes));
        data.fields.emplace(kRatio, ratio);
        return Create({kMain, {id}}, std::move(data));
    };
    Take(session.Commit({{record("d", {1}, 2.5), record("b", {0}, -0.0),
                         record("c", {0, 255}, 0.0), record("a", {}, -2.5)}}));

    QueryDescription binary{kMain, kByBytes};
    binary.lower = QueryBound{Binary{0}, false};
    binary.upper = QueryBound{Binary{0, 255}, true};
    Check(Ids(Take(session.Query(binary))) == std::vector<RecordId>{{"c"}},
          "Binary ranges must order empty values, embedded zeros, and prefixes correctly.");

    QueryDescription ratio{kMain, kByRatio};
    Check(Ids(Take(session.Query(ratio))) == std::vector<RecordId>{{"a"}, {"b"}, {"c"}, {"d"}},
          "Real keys must sort numerically and treat signed zero as an equal key.");
    ratio.prefix = {0.0};
    ratio.limit = 1;
    auto page = Take(session.Query(ratio));
    Check(Ids(page) == std::vector<RecordId>{{"b"}} && page.next, "Zero must find both signed zero keys.");
    ratio.after = page.next;
    page = Take(session.Query(ratio));
    Check(Ids(page) == std::vector<RecordId>{{"c"}} && !page.next, "Signed zero ties must page by ID.");
}

void CapabilitiesAndOpenClose(TestBackend& backend)
{
    auto provider = backend.Create();
    const auto caps = provider->Capabilities();
    Check(caps.contains(Capability::PrimaryQueries) && caps.contains(Capability::SecondaryIndexes)
          && caps.contains(Capability::AtomicTransactions) && caps.contains(Capability::ConsistentSnapshots)
          && caps.contains(Capability::LogicalChangeJournal)
          && caps.contains(Capability::DurablePersistence) == backend.directory.has_value(),
          "The provider must advertise its actual features without advertising durability.");

    OpenSessionRequest request{{"capabilities"}, MakeSchema(), caps};
    request.requiredCapabilities.insert(static_cast<Capability>(999));
    ExpectError(provider->OpenSession(request), ErrorCode::UnsupportedOperation);
    request.requiredCapabilities = {Capability::DurablePersistence};
    if (backend.directory)
        Take(provider->OpenSession(request));
    else
        ExpectError(provider->OpenSession(request), ErrorCode::UnsupportedOperation);
    request.requiredCapabilities = caps;
    const RecordKey key{kMain, {"key"}};

    auto first = Take(provider->OpenSession(request));
    auto different = request;
    different.id = {"different"};
    auto separate = Take(provider->OpenSession(different));
    Take(first->Commit({{Create(key, Data(1))}}));
    Check(!Take(separate->Get(key)), "Sessions have isolated records.");
    ExpectError(provider->OpenSession(request), ErrorCode::SessionAlreadyOpen);

    auto pinned = Take(first->OpenSnapshot());
    Take(first->Close());
    Check(Take(pinned->Get(key)).has_value(), "Closing a session keeps snapshots alive.");
    ExpectError(first->OpenSnapshot(), ErrorCode::SessionClosed);
    auto mismatch = request;
    mismatch.schema.at(kMain).version = {2};
    ExpectError(provider->OpenSession(mismatch), ErrorCode::SchemaMismatch);
    auto reopened = Take(provider->OpenSession(request));
    first.reset(); // The closed handle cannot release the replacement writer.
    ExpectError(provider->OpenSession(request), ErrorCode::SessionAlreadyOpen);
    Check(Existing(*reopened, key).data == Data(1) && Take(reopened->ReadChanges({}, 1)).batches.size() == 1,
          "Records and journal survive reopen within the provider's lifetime.");
    reopened.reset();
    Take(provider->OpenSession(request)); // Destruction releases the open writer.
    auto freshProvider = backend.Create();
    auto fresh = Take(freshProvider->OpenSession(request));
    Check(!Take(fresh->Get(key)), "A new store has no records from another store.");
}

void ConcurrentConditionalWrites(TestBackend& backend)
{
    Fixture fixture(backend);
    auto& session = *fixture.session;
    const RecordKey key{kMain, {"race"}};
    Take(session.Commit({{Create(key, Data(0))}}));
    const auto before = Existing(session, key).version;
    std::array<std::optional<Result<CommitRevision>>, 2> outcomes;
    const auto writer = [&](std::size_t index) {
        outcomes[index] = session.Commit({{Replace(key, Data(static_cast<std::int64_t>(index + 1)), before)}});
    };
    {
        std::jthread left(writer, 0);
        std::jthread right(writer, 1);
    }
    const bool firstWon = static_cast<bool>(*outcomes[0]);
    auto& winning = *outcomes[firstWon ? 0 : 1];
    const auto& losing = *outcomes[firstWon ? 1 : 0];
    Check(Take(std::move(winning)) == CommitRevision{2}, "One guarded writer must succeed.");
    ExpectError(losing, ErrorCode::VersionConflict);
    Check(Existing(session, key).data == Data(firstWon ? 1 : 2), "Exactly the winning writer's data is visible.");
}
} // namespace

int main(int argc, char* argv[])
{
    try {
        std::optional<persistence_test::TemporaryDirectory> directory;
        TestBackend backend;
        if (argc == 3 && std::string_view(argv[1]) == "--sqlite") {
            directory.emplace(argv[2]);
            backend.directory = directory->Path();
        } else if (argc != 1) {
            throw std::runtime_error("Usage: persistence smoke [--sqlite BUILD_TEST_DIRECTORY]");
        }
        Lifecycle(backend);
        AtomicityAndJournal(backend);
        IndexedQueries(backend);
        IndexedScalarOrdering(backend);
        CapabilitiesAndOpenClose(backend);
        ConcurrentConditionalWrites(backend);
    } catch (const std::exception& error) {
        std::cerr << "Persistence smoke test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
