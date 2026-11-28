#include "PersistenceCommands.h"

#include "engine/in_memory_persistence.h"
#include "engine/rocksdb_persistence.h"
#include "engine/sqlite_persistence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>

namespace ce::developer
{
namespace
{
using namespace persistence;

Value Base64(std::span<const std::uint8_t> data)
{
    return data.empty() ? String{} : juce::Base64::toBase64(data.data(), data.size());
}

Value Base64(const std::string& data)
{
    return Base64({reinterpret_cast<const std::uint8_t*>(data.data()), data.size()});
}

Binary ReadBytes(const Value& value)
{
    if (!value.isString())
        Reject("invalid_input", "Expected a base64 string.");
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, value.toString()))
        Reject("invalid_input", "Invalid base64 value.");
    const auto* data = static_cast<const std::uint8_t*>(decoded.getData());
    Binary result;
    if (decoded.getDataSize() != 0)
        result.assign(data, data + decoded.getDataSize());
    if (Base64(result) != value)
        Reject("invalid_input", "Expected canonical base64.");
    return result;
}

std::string ReadId(const Value& value)
{
    const auto bytes = ReadBytes(value);
    if (bytes.empty() || bytes.size() > 1024)
        Reject("invalid_input", "Opaque identifier must contain 1 to 1024 bytes.");
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[noreturn]] void StorageError(const OperationError& error, const char* history = "retention_gap")
{
    switch (error.code) {
        case ErrorCode::HistoryUnavailable: Reject(history, "The requested persistence view is unavailable.", 410);
        case ErrorCode::ResourceLimit: Reject("resource_limit", "Persistence resource limit reached.", 429);
        case ErrorCode::UnknownCollection:
        case ErrorCode::UnknownIndex:
        case ErrorCode::RecordNotFound: Reject("not_found", "Unknown persistence record or index.", 404);
        case ErrorCode::InvalidArgument: Reject("invalid_input", "Invalid persistence query parameters.");
        case ErrorCode::VersionConflict: Reject("conflict", "Persistence version conflict.", 409);
        case ErrorCode::UnsupportedOperation: Reject("unsupported", "Provider operation is unavailable.", 404);
        case ErrorCode::Cancelled: Reject("cancelled", "The query was cancelled.", 409);
        case ErrorCode::ExecutionTimeout: Reject("timeout", "The query reached its execution deadline.", 408);
        case ErrorCode::ScanRequired: Reject("scan_required", "The plan requires allowScan for bounded execution.", 409);
        default: Reject("operation_failed", "The persistence operation failed.", 500);
    }
}

template <typename T>
T Take(Result<T> result, const char* history = "retention_gap")
{
    if (!result)
        StorageError(result.Error(), history);
    return std::move(result).Value();
}

const char* CapabilityName(Capability capability)
{
    switch (capability) {
        case Capability::PrimaryQueries: return "primary_queries";
        case Capability::SecondaryIndexes: return "secondary_indexes";
        case Capability::AtomicTransactions: return "atomic_transactions";
        case Capability::ConsistentSnapshots: return "consistent_snapshots";
        case Capability::LogicalChangeJournal: return "logical_journal";
        case Capability::DurablePersistence: return "durable";
        case Capability::PortableQueries: return "portable_queries";
        case Capability::IndexMaintenance: return "native_index_maintenance";
    }
    return "unknown";
}

Value Field(const FieldValue& value)
{
    if (const auto* boolean = std::get_if<bool>(&value))
        return Object({{"type", "bool"}, {"value", *boolean}});
    if (const auto* integer = std::get_if<std::int64_t>(&value))
        return Object({{"type", "i64"}, {"value", String(std::to_string(*integer))}});
    if (const auto* real = std::get_if<double>(&value)) {
        std::array<char, 32> buffer{};
        const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *real);
        if (error != std::errc{} || !std::isfinite(*real))
            Reject("operation_failed", "The stored real value cannot be serialized.", 500);
        return Object({{"type", "real"}, {"value", Utf8({buffer.data(), static_cast<std::size_t>(end - buffer.data())})}});
    }
    if (const auto* text = std::get_if<std::string>(&value))
        return Object({{"type", "text"}, {"value", Base64(*text)}});
    if (const auto* bytes = std::get_if<Binary>(&value))
        return Object({{"type", "bytes"}, {"value", Base64(*bytes)}});
    return Object({{"type", "null"}});
}

FieldValue Field(const Value& value)
{
    const auto* object = value.getDynamicObject();
    if (!object || !Get(value, "type").isString())
        Reject("invalid_input", "Expected a typed indexed field.");
    const auto type = Get(value, "type").toString();
    const auto data = Get(value, "value");
    if (type == "null" && object->getProperties().size() == 1)
        return Null{};
    if (object->getProperties().size() != 2)
        Reject("invalid_input", "Unexpected indexed field property.");
    if (type == "bool" && data.isBool())
        return static_cast<bool>(data);
    if (type == "i64")
        return ParseDecimal<std::int64_t>(data);
    if (type == "real" && data.isString()) {
        const auto text = Text(data);
        double number = 0.0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (error == std::errc{} && end == text.data() + text.size() && std::isfinite(number))
            return number;
        Reject("invalid_input", "Invalid or out-of-range real value.");
    }
    if (type == "bytes" || type == "text") {
        const auto bytes = ReadBytes(data);
        if (type == "bytes")
            return bytes;
        return bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    Reject("invalid_input", "Unsupported indexed field type.");
}

std::optional<QueryBound> Bound(const Value& params, const char* name)
{
    const auto data = Get(params, name);
    if (data.isVoid())
        return {};
    const auto* object = data.getDynamicObject();
    if (!object || object->getProperties().size() != 2 || !Get(data, "inclusive").isBool())
        Reject("invalid_input", "A range bound requires a value and inclusive flag.");
    return QueryBound{Field(Get(data, "value")), static_cast<bool>(Get(data, "inclusive"))};
}

Value RecordJson(const Record& record)
{
    juce::Array<Value> fields;
    for (const auto& [id, field] : record.data.fields)
        fields.add(Object({{"name", Base64(id.value)}, {"field", Field(field)}}));
    return Object({{"id", Base64(record.id.value)}, {"version", Decimal(record.version.value)},
                   {"schemaVersion", Decimal(record.data.schemaVersion.value)}, {"fields", Value(std::move(fields))},
                   {"binary", record.data.binary ? Base64(*record.data.binary) : Value{}}});
}

CollectionKey Keys(const Value& params)
{
    return {{ReadId(Get(params, "namespace"))}, {ReadId(Get(params, "collection"))}};
}

Value ChangeJson(const ChangeBatch& batch)
{
    juce::Array<Value> changes;
    for (const auto& change : batch.changes) {
        const char* kind = change.kind == MutationKind::Create ? "create" : change.kind == MutationKind::Replace ? "replace" : "erase";
        changes.add(Object({{"kind", kind}, {"namespace", Base64(change.key.collection.nameSpace.value)},
                            {"collection", Base64(change.key.collection.collection.value)},
                            {"id", Base64(change.key.id.value)}, {"record", change.record ? RecordJson(*change.record) : Value{}},
                            {"previousVersion", change.previousVersion ? Decimal(change.previousVersion->value) : Value{}}}));
    }
    return Object({{"revision", Decimal(batch.revision.value)}, {"changes", Value(std::move(changes))}});
}

std::string QueryFingerprint(const Value& parameters)
{
    auto copy = Object();
    for (const auto* key : {"snapshot", "namespace", "collection", "index", "prefix", "lower", "upper", "limit"}) {
        const auto value = Get(parameters, key);
        if (!value.isVoid())
            Put(copy, key, Canonical(value));
    }
    return juce::JSON::toString(copy, true).toStdString();
}

Predicate ParsePredicate(const Value& value)
{
    constexpr std::pair<std::string_view, PredicateOp> operators[] = {
        {"and", PredicateOp::And}, {"or", PredicateOp::Or}, {"not", PredicateOp::Not},
        {"eq", PredicateOp::Equal}, {"ne", PredicateOp::NotEqual},
        {"lt", PredicateOp::Less}, {"le", PredicateOp::LessEqual},
        {"gt", PredicateOp::Greater}, {"ge", PredicateOp::GreaterEqual},
        {"isNull", PredicateOp::IsNull}, {"isMissing", PredicateOp::IsMissing}, {"isPresent", PredicateOp::IsPresent}
    };
    const auto name = Text(Get(value, "op"));
    const auto op = std::ranges::find_if(operators, [&](const auto& entry) { return entry.first == name; });
    if (op == std::end(operators))
        Reject("invalid_input", "Unknown portable predicate operator.");
    Predicate result;
    result.op = op->second;
    if (const auto field = Get(value, "field"); !field.isVoid()) result.field = FieldId{ReadId(field)};
    if (const auto operand = Get(value, "value"); !operand.isVoid()) result.value = Field(operand);
    if (const auto children = Get(value, "children"); children.isArray())
        for (const auto& child : *children.getArray()) result.children.push_back(ParsePredicate(child));
    return result;
}

const char* DirectionName(SortDirection direction)
{
    return direction == SortDirection::Ascending ? "asc" : "desc";
}

const char* AccessName(QueryAccess access)
{
    switch (access) {
        case QueryAccess::Empty: return "empty";
        case QueryAccess::PrimarySeek: return "primary_seek";
        case QueryAccess::IndexSeek: return "index_seek";
        case QueryAccess::PrimaryScan: return "primary_scan";
        case QueryAccess::IndexScan: return "index_scan";
    }
    return "unknown";
}

Value PlanJson(const PortableQueryPlan& plan)
{
    juce::Array<Value> prefix;
    for (const auto& value : plan.prefix) prefix.add(Field(value));
    const auto bound = [](const std::optional<QueryBound>& edge) {
        return edge ? Object({{"value", Field(edge->value)}, {"inclusive", edge->inclusive}}) : Value{};
    };
    return Object({{"access", AccessName(plan.access)}, {"index", plan.index ? Base64(plan.index->value) : Value{}},
                   {"indexVersion", plan.indexVersion ? Decimal(plan.indexVersion->value) : Value{}},
                   {"direction", DirectionName(plan.direction)}, {"prefix", Value(std::move(prefix))},
                   {"lower", bound(plan.lower)}, {"upper", bound(plan.upper)}, {"residualFilter", plan.residualFilter}});
}

Value IndexCatalog(const ReadSnapshot& view, const Value& arguments)
{
    const auto schema = Take(view.Describe(Keys(arguments)), "snapshot_expired");
    const auto selected = Get(arguments, "index");
    const auto requested = selected.isVoid() ? std::optional<IndexId>{} : std::optional{IndexId{ReadId(selected)}};
    if (requested && !schema.indexes.contains(*requested))
        Reject("not_found", "Unknown persistence index.", 404);
    constexpr const char* types[] = {"bool", "i64", "real", "text", "bytes"};
    juce::Array<Value> indexes;
    for (const auto& [id, index] : schema.indexes) {
        if (requested && id != *requested) continue;
        juce::Array<Value> fields;
        for (std::size_t i = 0; i < index.fields.size(); ++i) {
            const auto& field = index.fields[i];
            const auto& type = schema.fields.at(field);
            const auto direction = index.directions.empty() ? SortDirection::Ascending : index.directions[i];
            fields.add(Object({{"field", Base64(field.value)}, {"type", types[static_cast<std::size_t>(type.type)]},
                               {"direction", DirectionName(direction)}, {"required", type.required}, {"nullable", type.nullable}}));
        }
        indexes.add(Object({{"id", Base64(id.value)}, {"version", Decimal(index.version.value)}, {"unique", index.unique},
                            {"status", "ready"}, {"fields", Value(std::move(fields))}}));
    }
    return Object({{"revision", Decimal(view.Revision().value)}, {"indexes", Value(std::move(indexes))}});
}
} // namespace

PersistenceMount MakeTestMount(const Settings& settings)
{
    if (!settings.allowFixture)
        return {};
    const String provider = settings.provider.isEmpty() ? "memory" : settings.provider;
    std::unique_ptr<StorageProvider> store;
    if (provider == "memory") {
        store = CreateInMemoryProvider();
    } else {
        if (settings.store.isEmpty())
            Reject("invalid_input", "A durable developer fixture requires its own storage directory.");
        const auto path = std::filesystem::u8path(settings.store.toStdString());
        if (provider == "sqlite")
            store = Take(CreateSQLiteProvider({path}));
        else if (provider == "rocksdb")
            store = Take(CreateRocksDBProvider({path}));
        else
            Reject("invalid_input", "Unknown developer fixture provider.");
    }

    const CollectionKey collection{{"automation"}, {"records"}};
    SessionSchema schema;
    CollectionSchema definition;
    definition.version = {1};
    definition.fields.emplace(FieldId{"value"}, FieldDefinition{FieldType::Integer});
    definition.fields.emplace(FieldId{"measurement"}, FieldDefinition{FieldType::Real, false});
    definition.indexes.emplace(IndexId{"value"}, IndexDefinition{{FieldId{"value"}}});
    schema.emplace(collection, std::move(definition));
    OpenSessionRequest request{{"developer-fixture"}, schema, {}};
    request.policy.journalRevisions = 2;
    request.policy.openSnapshots = static_cast<std::size_t>(settings.limits.snapshotCount) + 2;
    request.policy.snapshotLifetime = std::chrono::milliseconds(settings.limits.snapshotMs);
    auto session = Take(store->OpenSession(request));
    if (Take(session->OpenSnapshot())->Revision().value == 0) {
        for (int i = 0; i < 4; ++i) {
            const RecordId id{i == 0 ? std::string("opaque\0\xff", 8) : std::to_string(i)};
            const auto value = i == 0 ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max() - i;
            RecordData record{{1}, {{{"value"}, value}, {{"measurement"}, std::nextafter(1.0, 2.0)}}, Binary{0, 127, 255}};
            Take(session->Commit({{{MutationKind::Create, {collection, id}, std::move(record), {}}}}));
        }
    }
    return {provider, request.id, std::move(store), std::move(session)};
}

PersistenceCommands::PersistenceCommands(PersistenceMount mount, Limits limits)
    : mount_(std::move(mount)), limits_(limits),
      capabilities_(mount_.owner ? mount_.owner->Capabilities() : CapabilitySet{}) {}

bool PersistenceCommands::Available() const noexcept { return mount_.session && mount_.owner; }

bool PersistenceCommands::Supports(PersistenceAction action) const noexcept
{
    if (action == PersistenceAction::indexes || action == PersistenceAction::explain || action == PersistenceAction::execute)
        return Available() && capabilities_.contains(Capability::PortableQueries);
    return Available();
}

void PersistenceCommands::Expire()
{
    const auto now = Clock::now();
    std::erase_if(snapshots_, [&](const auto& entry) { return entry.second.expires <= now; });
    std::erase_if(continuations_, [&](const auto& entry) {
        return entry.second.expires <= now || !snapshots_.contains(entry.second.snapshot);
    });
}

PersistenceCommands::Snapshot& PersistenceCommands::Find(const std::string& token, Access access)
{
    const auto found = snapshots_.find(token);
    if (found == snapshots_.end() || found->second.expires <= Clock::now())
        Reject("snapshot_expired", "The requested snapshot is unavailable.", 410);
    if (found->second.owner != access)
        Reject("forbidden", "This credential does not own the snapshot.", 403);
    return found->second;
}

const PersistenceCommands::Continuation* PersistenceCommands::Resume(const Value& parameters, Access access,
    const std::string& snapshot, const std::string& fingerprint) const
{
    const auto token = Text(Get(parameters, "cursor"));
    if (token.empty()) return nullptr;
    const auto found = continuations_.find(token);
    if (found == continuations_.end())
        Reject("cursor_expired", "The continuation token is unavailable.", 410);
    const auto& entry = found->second;
    if (entry.owner != access)
        Reject("forbidden", "This credential does not own the continuation token.", 403);
    if (entry.snapshot != snapshot || entry.query != fingerprint)
        Reject("conflict", "The continuation token belongs to a different snapshot or query.", 409);
    return &entry;
}

Value PersistenceCommands::Publish(Continuation continuation)
{
    if (continuations_.size() >= static_cast<std::size_t>(limits_.cursorCount))
        Reject("resource_limit", "Continuation token limit reached.", 429);
    const auto token = juce::Uuid().toString();
    continuations_.emplace(token.toStdString(), std::move(continuation));
    return token;
}

Value PersistenceCommands::Query(Access access, const Value& params, Snapshot& snapshot, const std::string& id)
{
    QueryDescription query;
    query.collection = Keys(params);
    if (!Get(params, "index").isVoid())
        query.index = IndexId{ReadId(Get(params, "index"))};
    if (const auto prefix = Get(params, "prefix"); prefix.isArray())
        for (const auto& value : *prefix.getArray())
            query.prefix.push_back(Field(value));
    query.lower = Bound(params, "lower");
    query.upper = Bound(params, "upper");
    query.limit = static_cast<std::size_t>(static_cast<int>(Get(params, "limit")));
    const auto fingerprint = QueryFingerprint(params);
    auto expiry = std::min(snapshot.expires, Clock::now() + std::chrono::milliseconds(limits_.cursorMs));
    if (const auto* prior = Resume(params, access, id, fingerprint)) {
        query.after = std::get<QueryCursor>(prior->cursor);
        expiry = std::min(expiry, prior->expires);
    }
    const auto page = Take(snapshot.read->Query(query), "snapshot_expired");
    juce::Array<Value> records;
    for (const auto& record : page.records)
        records.add(RecordJson(record));
    Value next;
    if (page.next)
        next = Publish({access, id, fingerprint, expiry, *page.next});
    return Object({{"records", Value(std::move(records))}, {"revision", Decimal(snapshot.read->Revision().value)},
                   {"cursor", next}});
}

Value PersistenceCommands::PlannedQuery(PersistenceAction action, Access access, const Value& params, Snapshot& snapshot,
                                       const std::string& id, Clock::time_point deadline, std::stop_token stop)
{
    PortableQuery query;
    query.collection = Keys(params);
    if (const auto index = Get(params, "index"); !index.isVoid()) query.index = IndexId{ReadId(index)};
    if (const auto predicate = Get(params, "where"); !predicate.isVoid()) query.where = ParsePredicate(predicate);
    query.primaryOnly = static_cast<bool>(Get(params, "primaryOnly"));
    query.allowScan = static_cast<bool>(Get(params, "allowScan"));
    query.direction = Get(params, "direction").toString() == "desc" ? SortDirection::Descending : SortDirection::Ascending;
    query.limit = static_cast<int>(Get(params, "limit"));

    QueryLimits limits;
    limits.pageEntries = limits_.pageEntries;
    limits.candidates = Get(params, "candidates").isVoid() ? limits_.queryCandidates : static_cast<int>(Get(params, "candidates"));
    limits.predicateNodes = limits_.queryPredicateNodes;
    limits.predicateDepth = limits_.queryPredicateDepth;
    limits.cursorLifetime = std::chrono::milliseconds(limits_.cursorMs);
    limits.deadline = deadline;
    limits.stop = stop;
    const auto revision = Decimal(snapshot.read->Revision().value);
    if (action == PersistenceAction::explain)
        return Object({{"revision", revision}, {"plan", PlanJson(Take(ExplainQuery(*snapshot.read, query, limits), "snapshot_expired"))}});

    auto arguments = Object();
    for (const auto& entry : params.getDynamicObject()->getProperties())
        if (entry.name.toString() != "cursor") arguments.getDynamicObject()->setProperty(entry.name, entry.value);
    const auto fingerprint = "portable:" + juce::JSON::toString(Canonical(arguments), true).toStdString();
    auto expiry = std::min(snapshot.expires, Clock::now() + limits.cursorLifetime);
    const PortableCursor* cursor = nullptr;
    if (const auto* prior = Resume(params, access, id, fingerprint)) {
        cursor = &std::get<PortableCursor>(prior->cursor);
        expiry = std::min(expiry, prior->expires);
    }
    const auto page = Take(ExecuteQuery(*snapshot.read, query, limits, cursor), "snapshot_expired");
    juce::Array<Value> records;
    for (const auto& record : page.records) records.add(RecordJson(record));
    const auto& stats = page.statistics;
    auto statistics = Object({{"candidates", Decimal(stats.candidates)}, {"providerPages", Decimal(stats.providerPages)},
                              {"returned", Decimal(stats.returned)}, {"elapsedNs", Decimal(stats.elapsed.count())},
                              {"candidateBudgetReached", stats.candidateBudgetReached}});
    Value next;
    if (page.next) next = Publish({access, id, fingerprint, expiry, *page.next});
    return Object({{"revision", revision}, {"plan", PlanJson(page.plan)}, {"statistics", statistics},
                   {"records", Value(std::move(records))}, {"cursor", next}});
}

Value PersistenceCommands::Execute(PersistenceAction action, Access access, const Value& params,
                                   Clock::time_point deadline, std::stop_token stop)
{
    if (!Supports(action))
        Reject("unsupported", "This persistence operation is unavailable on the mounted session.", 404);
    Expire();
    if (action == PersistenceAction::info) {
        const auto revision = Take(mount_.session->OpenSnapshot())->Revision();
        const auto changes = Take(mount_.session->ReadChanges(revision, 1));
        juce::Array<Value> capabilities;
        for (const auto cap : capabilities_)
            capabilities.add(CapabilityName(cap));
        return Object({{"provider", mount_.provider}, {"session", Base64(mount_.id.value)},
                       {"revision", Decimal(changes.latestRevision.value)}, {"historyFloor", Decimal(changes.historyFloor.value)},
                       {"capabilities", Value(std::move(capabilities))}});
    }
    if (action == PersistenceAction::open) {
        if (snapshots_.size() >= static_cast<std::size_t>(limits_.snapshotCount))
            Reject("resource_limit", "Snapshot limit reached.", 429);
        auto read = Take(mount_.session->OpenSnapshot());
        const auto revision = read->Revision();
        const auto token = juce::Uuid().toString();
        snapshots_.emplace(token.toStdString(),
            Snapshot{access, Clock::now() + std::chrono::milliseconds(limits_.snapshotMs), std::move(read)});
        return Object({{"snapshot", token}, {"revision", Decimal(revision.value)}, {"valid", true}});
    }
    if (action == PersistenceAction::changes) {
        const auto after = ParseDecimal<std::uint64_t>(Get(params, "after"));
        const auto page = Take(mount_.session->ReadChanges({after}, static_cast<int>(Get(params, "limit"))));
        juce::Array<Value> batches;
        for (const auto& batch : page.batches)
            batches.add(ChangeJson(batch));
        const auto next = page.batches.empty() ? after : page.batches.back().revision.value;
        return Object({{"batches", Value(std::move(batches))}, {"revision", Decimal(page.latestRevision.value)},
                       {"historyFloor", Decimal(page.historyFloor.value)}, {"next", Decimal(next)},
                       {"more", next < page.latestRevision.value}});
    }
    const auto id = Text(Get(params, "snapshot"));
    auto& snapshot = Find(id, access);
    switch (action) {
        case PersistenceAction::inspect:
            return Object({{"snapshot", Get(params, "snapshot")}, {"revision", Decimal(snapshot.read->Revision().value)}, {"valid", true}});
        case PersistenceAction::release:
            snapshots_.erase(id);
            std::erase_if(continuations_, [&](const auto& entry) { return entry.second.snapshot == id; });
            return Object({{"released", true}});
        case PersistenceAction::lookup: {
            const auto record = Take(snapshot.read->Get({Keys(params), {ReadId(Get(params, "id"))}}), "snapshot_expired");
            return Object({{"record", record ? RecordJson(*record) : Value{}},
                           {"revision", Decimal(snapshot.read->Revision().value)}});
        }
        case PersistenceAction::query:
            return Query(access, params, snapshot, id);
        case PersistenceAction::indexes:
            return IndexCatalog(*snapshot.read, params);
        case PersistenceAction::explain:
        case PersistenceAction::execute:
            return PlannedQuery(action, access, params, snapshot, id, deadline, stop);
        default:
            Reject("internal", "Invalid persistence command.", 500);
    }
}
} // namespace ce::developer
