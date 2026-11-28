#include "GameplayStore.h"

#include "engine/in_memory_persistence.h"
#include "engine/rocksdb_persistence.h"
#include "engine/sqlite_persistence.h"

#include <limits>

namespace ce::gameplay
{
namespace
{
using namespace persistence;
const CollectionKey worlds{{"gameplay"}, {"worlds"}};
const CollectionKey scenes{{"gameplay"}, {"scenes"}};
const CollectionKey players{{"gameplay"}, {"players"}};
const CollectionKey application{{"gameplay"}, {"application"}};

RecordKey Key(const CollectionKey& collection, const juce::String& id)
{
    return {collection, {id.toStdString()}};
}

SessionSchema Schema()
{
    return {
        {worlds, {{1}, {{{"game"}, {FieldType::String}}, {{"scene"}, {FieldType::String}}, {{"tick"}, {FieldType::Integer}}}, {}}},
        {scenes, {{1}, {}, {}}},
        {players, {{1}, {{{"entity"}, {FieldType::String}}, {{"slot"}, {FieldType::String}}}, {}}},
        {application, {{1}, {{{"value"}, {FieldType::Integer}}}, {}}}
    };
}

template <typename T>
const T* Field(const Record& record, const char* name)
{
    const auto found = record.data.fields.find(FieldId{name});
    return found == record.data.fields.end() ? nullptr : std::get_if<T>(&found->second);
}

bool Error(juce::String& error, const OperationError& result)
{
    error = juce::String(result.message);
    return false;
}

bool InspectCollection(Session& session, const CollectionKey& collection, juce::String& error,
                       const std::function<bool(const Record&)>& read)
{
    auto snapshot = session.OpenSnapshot();
    if (!snapshot) return Error(error, snapshot.Error());
    QueryDescription query;
    query.collection = collection;
    std::size_t count = 0;
    do {
        auto page = snapshot.Value()->Query(query);
        if (!page) return Error(error, page.Error());
        for (const auto& record : page.Value().records) {
            if (++count > maximumPlayers || !read(record)) {
                error = "Invalid or oversized gameplay persistence records.";
                return false;
            }
        }
        query.after = std::move(page.Value().next);
    } while (query.after);
    return true;
}
} // namespace

GameplayStore::GameplayStore(StoreSettings settings) : settings_(std::move(settings)) {}

bool GameplayStore::Open(juce::String& error)
{
    if (session_) return true;
    if (!Identifier(settings_.worldId) || settings_.checkpointTicks < 1 || settings_.checkpointTicks > 3600) {
        error = "Invalid gameplay world identity or checkpoint interval.";
        return false;
    }
    if (settings_.provider == "memory") {
        provider_ = CreateInMemoryProvider();
    } else {
        if (settings_.directory.isEmpty()) {
            error = "A durable gameplay provider requires a storage directory.";
            return false;
        }
        const auto path = std::filesystem::u8path(settings_.directory.toStdString());
        if (settings_.provider == "sqlite") {
            auto store = CreateSQLiteProvider({path});
            if (!store) return Error(error, store.Error());
            provider_ = std::move(store).Value();
        } else if (settings_.provider == "rocksdb") {
            auto store = CreateRocksDBProvider({path});
            if (!store) return Error(error, store.Error());
            provider_ = std::move(store).Value();
        } else {
            error = "Unknown gameplay persistence provider.";
            return false;
        }
    }
    OpenSessionRequest request{{settings_.worldId.toStdString()}, Schema(), {Capability::AtomicTransactions}};
    request.policy.journalRevisions = 64;
    request.policy.openSnapshots = 32;
    request.policy.snapshotLifetime = std::chrono::seconds(30);
    if (settings_.provider != "memory") request.requiredCapabilities.insert(Capability::DurablePersistence);
    auto session = provider_->OpenSession(request);
    if (!session) return Error(error, session.Error());
    session_ = std::move(session).Value();
    auto snapshot = session_->OpenSnapshot();
    if (!snapshot) return Error(error, snapshot.Error());
    revision_ = snapshot.Value()->Revision().value;
    return true;
}

bool GameplayStore::ReadScene(const juce::String& scene, juce::ValueTree& state, juce::String& error) const
{
    state = {};
    auto record = session_->Get(Key(scenes, scene.isEmpty() ? "untitled" : scene));
    if (!record) return Error(error, record.Error());
    if (!record.Value()) return true;
    const auto& payload = record.Value()->data.binary;
    if (payload && !payload->empty() && payload->size() <= 4 * 1024 * 1024) {
        const auto xml = juce::String::fromUTF8(reinterpret_cast<const char*>(payload->data()),
                                               static_cast<int>(payload->size()));
        state = juce::ValueTree::fromXml(xml);
    }
    if (state.hasType("CreationEngineScene")) return true;
    error = "The persisted gameplay scene is invalid.";
    return false;
}

bool GameplayStore::Restore(Checkpoint& checkpoint, juce::String& error)
{
    checkpoint = {};
    auto record = session_->Get(Key(worlds, settings_.worldId));
    if (!record) return Error(error, record.Error());
    if (!record.Value()) return true;
    const auto& head = *record.Value();
    const auto* game = Field<std::string>(head, "game");
    const auto* scene = Field<std::string>(head, "scene");
    const auto* tick = Field<std::int64_t>(head, "tick");
    if (game == nullptr || scene == nullptr || tick == nullptr || *tick < 0) {
        error = "The persisted world identity is invalid.";
        return false;
    }
    checkpoint.game = *game;
    checkpoint.scene = *scene;
    checkpoint.tick = static_cast<std::uint64_t>(*tick);
    if (!ReadScene(checkpoint.scene, checkpoint.state, error) || !checkpoint.state.isValid()) {
        if (error.isEmpty()) error = "The checkpoint's scene record is missing.";
        return false;
    }
    return InspectCollection(*session_, players, error, [&](const auto& player) {
        const auto* entity = Field<std::string>(player, "entity");
        const auto* slot = Field<std::string>(player, "slot");
        const juce::String id(player.id.value);
        if (entity == nullptr || slot == nullptr || !Identifier(id)) return false;
        return checkpoint.players.try_emplace(id, PlayerState{*entity, *slot}).second;
    }) && InspectCollection(*session_, application, error, [&](const auto& entry) {
        const auto* value = Field<std::int64_t>(entry, "value");
        const juce::String key(entry.id.value);
        if (value == nullptr || !Identifier(key, 160)) return false;
        return checkpoint.application.try_emplace(key, *value).second;
    });
}

bool GameplayStore::Commit(const Checkpoint& checkpoint, juce::String& error)
{
    const auto xml = checkpoint.state.toXmlString().toStdString();
    if (xml.empty() || xml.size() > 4 * 1024 * 1024 || checkpoint.players.size() > maximumPlayers
        || checkpoint.application.size() > maximumPlayers
        || checkpoint.tick > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error = "The gameplay checkpoint exceeds its storage bounds.";
        return false;
    }

    std::vector<RecordKey> keys;
    std::vector<RecordData> data;
    auto add = [&](RecordKey key, RecordData value) {
        keys.push_back(std::move(key));
        data.push_back(std::move(value));
    };
    add(Key(worlds, settings_.worldId), {{1}, {
        {{"game"}, checkpoint.game.toStdString()}, {{"scene"}, checkpoint.scene.toStdString()},
        {{"tick"}, static_cast<std::int64_t>(checkpoint.tick)}}, {}});
    add(Key(scenes, checkpoint.scene.isEmpty() ? "untitled" : checkpoint.scene),
        {{1}, {}, Binary(xml.begin(), xml.end())});
    for (const auto& [id, player] : checkpoint.players)
        add(Key(players, id), {{1}, {{{"entity"}, player.entityId.toStdString()}, {{"slot"}, player.slotId.toStdString()}}, {}});
    for (const auto& [key, value] : checkpoint.application)
        add(Key(application, key), {{1}, {{{"value"}, value}}, {}});

    auto current = session_->GetMany(keys);
    if (!current) return Error(error, current.Error());
    Transaction transaction;
    transaction.expectedRevision = CommitRevision{revision_};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto& prior = current.Value()[i];
        if (prior && prior->data == data[i]) continue;
        transaction.mutations.push_back({prior ? MutationKind::Replace : MutationKind::Create,
            std::move(keys[i]), std::move(data[i]), prior ? std::optional(prior->version) : std::nullopt});
    }
    if (transaction.mutations.empty()) return true;
    auto committed = session_->Commit(transaction);
    if (!committed) return Error(error, committed.Error());
    revision_ = committed.Value().value;
    return true;
}

developer::PersistenceMount GameplayStore::InspectableMount() const
{
    return {settings_.provider, {settings_.worldId.toStdString()}, provider_, session_};
}
} // namespace ce::gameplay
