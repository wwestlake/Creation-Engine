#include "WorldSession.h"

#include "Frust/EngineFrustHost.h"
#include "Physics/PhysicsComponents.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ce::gameplay
{
namespace
{
constexpr auto admissionTimeout = std::chrono::seconds(4);
constexpr auto idleInputTimeout = std::chrono::milliseconds(350);
constexpr std::int64_t maximumSequence = std::numeric_limits<std::int32_t>::max();

std::int64_t Handle(entt::entity entity)
{
    return entity == entt::null ? -1 : static_cast<std::int64_t>(entt::to_integral(entity));
}

entt::entity Find(const entt::registry& registry, const juce::String& id)
{
    if (id.isEmpty()) return entt::null;
    const auto identities = registry.view<const scene::InstanceId>();
    for (const auto entity : identities)
        if (identities.get<const scene::InstanceId>(entity).value == id) return entity;
    return entt::null;
}

using Slots = std::map<juce::String, juce::String>;

Slots FindSlots(engine::World& world)
{
    std::lock_guard lock(world.RegistryMutex());
    const auto& registry = world.Registry();
    Slots slots;
    const auto starts = registry.view<const scene::PossessionSpawn, const scene::InstanceId, const engine::Transform>();
    for (const auto entity : starts) {
        const auto& slot = starts.get<const scene::PossessionSpawn>(entity).playerSlotId;
        if (slot.isEmpty()) continue;
        const auto* flags = registry.try_get<scene::SceneFlags>(entity);
        if (flags == nullptr || !flags->editorOnly || registry.all_of<scene::CharacterInstanceRef>(entity))
            slots.try_emplace(slot, starts.get<const scene::InstanceId>(entity).value);
    }
    const auto characters = registry.view<const scene::CharacterInstanceRef, const scene::InstanceId, const engine::Transform>();
    for (const auto entity : characters) {
        const auto& character = characters.get<const scene::CharacterInstanceRef>(entity);
        const auto slot = character.state.getWithDefault("playerSlotId", "").toString();
        if (slot.isNotEmpty())
            slots.insert_or_assign(slot, characters.get<const scene::InstanceId>(entity).value);
    }
    return slots;
}
} // namespace

Authenticate DevelopmentIdentities(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile() || file.getSize() > static_cast<juce::int64>(maximumPacketBytes)) {
        error = "The development credentials file is missing or oversized.";
        return {};
    }
    const auto document = Decode(file.loadFileAsString().toStdString());
    if (!document) { error = "Invalid development credentials document."; return {}; }
    struct Account { juce::String secret; Identity identity; };
    std::map<juce::String, Account> accounts;
    for (const auto& entry : document->getDynamicObject()->getProperties()) {
        const auto name = entry.name.toString();
        const auto secret = Text(Get(entry.value, "secret"), 128);
        const auto move = Get(entry.value, "move"), request = Get(entry.value, "request");
        if (!Identifier(name) || !ExactFields(entry.value, {"secret", "move", "request"})
            || !secret || secret->getNumBytesAsUTF8() < 24 || !move.isBool() || !request.isBool()
            || accounts.size() >= maximumPlayers) {
            error = "Invalid development player configuration.";
            return {};
        }
        accounts.emplace(name, Account{*secret, {name, static_cast<bool>(move), static_cast<bool>(request)}});
    }
    if (accounts.empty()) { error = "No development players were configured."; return {}; }
    return [accounts = std::move(accounts)](const auto& challenge, const auto& world,
                                           const auto& claim, const auto& response) -> std::optional<Identity> {
        const auto account = accounts.find(claim);
        const auto secret = account == accounts.end() ? juce::String("unused-identity-verification-key") : account->second.secret;
        const bool verified = VerifyProof(MakeProof(secret, challenge, world, claim), response);
        if (verified && account != accounts.end()) return account->second.identity;
        return {};
    };
}

WorldSession::WorldSession(engine::World& world, physics::PhysicsWorld& physics, frust::EngineFrustHost& frust,
                           runtime::RuntimeWorldRunner& runner, GameplayStore& store, SessionPolicy policy)
    : world_(world), physics_(physics), frust_(frust), runner_(runner), store_(store), policy_(std::move(policy))
{
    policy_.capacity = std::clamp(policy_.capacity, 1, maximumConnections);
    frust_.setGameplayBindings({
        [this] { return store_.Settings().worldId.toStdString(); },
        [this] { return currentPlayer_.toStdString(); },
        [this] { return currentRequest_.toStdString(); },
        [this] { return currentTarget_.toStdString(); },
        [this](const auto& key) { return GetApplication(juce::String(key)); },
        [this](const auto& key, auto value, auto publish) { return SetApplication(juce::String(key), value, publish); },
        [this](const auto& name, const auto& target) {
            for (auto& [id, client] : clients_) {
                (void)id;
                if (currentPlayer_.isEmpty() || currentPlayer_ != client.identity.playerId) continue;
                if (!client.identity.mayRequest || !Identifier(juce::String(name)) || !Identifier(juce::String(target), 128)
                    || client.requests.size() >= maximumRequestsPerTick) return false;
                client.requests.push_back({juce::String(name), juce::String(target), 0});
                return true;
            }
            return false;
        }
    });
}

WorldSession::~WorldSession()
{
    (void)Stop();
    frust_.setGameplayBindings({});
}

bool WorldSession::Start(runtime::RuntimeGameSource source, project::SceneDocumentInfo scene, juce::ValueTree state,
                         const std::function<bool(juce::String&)>& prepareHost)
{
    if (runner_.IsPlaying()) return false;
    error_.clear();
    Checkpoint restored;
    if (!store_.Open(error_) || !store_.Restore(restored, error_)) return false;
    if (restored.state.isValid()) {
        if (restored.game != source.game.id) {
            error_ = "The checkpoint belongs to a different authored game.";
            return false;
        }
        if (restored.scene != scene.id) {
            const auto found = std::find_if(source.game.scenes.begin(), source.game.scenes.end(),
                [&](const auto& candidate) { return candidate.id == restored.scene; });
            if (found == source.game.scenes.end()) {
                error_ = "The checkpoint scene is missing from the authored game.";
                return false;
            }
            scene = *found;
        }
        state = restored.state;
    }

    players_ = std::move(restored.players);
    persistent_ = std::move(restored.application);
    published_.clear();
    auto readAuthored = std::move(source.readScene);
    source.readScene = [this, readAuthored](const auto& target, auto& document, auto& error) {
        if (!store_.ReadScene(target.id, document, error)) return false;
        if (!document.isValid() && readAuthored && !readAuthored(target, document, error)) return false;
        if (!document.isValid()) document = juce::ValueTree("CreationEngineScene");
        fetchedScene_ = document;
        return true;
    };
    const auto loadState = state;
    if (!runner_.Start(std::move(source), std::move(scene), std::move(state), [&](juce::String& error) {
        AssignIdentities();
        RestoreBodyMotion(loadState.isValid() ? loadState : fetchedScene_);
        return !prepareHost || prepareHost(error);
    }, restored.tick)) {
        error_ = runner_.LastError();
        return false;
    }
    lastCheckpointTick_ = world_.CurrentTick();
    BindPlayers();
    Dispatch("on_world_started", {}, {});
    return true;
}

bool WorldSession::Stop()
{
    if (!runner_.IsPlaying()) return true;
    Dispatch("on_world_stopping", {}, {});
    for (const auto& [id, client] : clients_) {
        (void)id;
        if (client.identity.playerId.isNotEmpty())
            Dispatch("on_player_left", client.identity.playerId, players_.at(client.identity.playerId).entityId);
    }
    if (!CheckpointNow()) return false;
    for (auto& [id, client] : clients_) {
        (void)client;
        if (id != 0) {
            Send(id, Lane::control, Object({{"type", "closing"}}));
            transport_.Disconnect(id);
        }
    }
    transport_.Flush();
    clients_.clear();
    runner_.Stop();
    return true;
}

bool WorldSession::CheckpointNow()
{
    if (!runner_.IsPlaying()) return false;
    AssignIdentities();
    ce::gameplay::Checkpoint checkpoint{runner_.Game().id, runner_.Scene().id, world_.CurrentTick(),
        Snapshot(), players_, persistent_};
    error_.clear();
    if (!store_.Commit(checkpoint, error_)) return false;
    lastCheckpointTick_ = checkpoint.tick;
    ++checkpoints_;
    return true;
}

bool WorldSession::Listen(const juce::String& address, int port, juce::String& error)
{
    if (!policy_.authenticate) { error = "A remote session requires gameplay authentication."; return false; }
    return transport_.Listen(address, port, std::min(maximumConnections, policy_.capacity + 4), error);
}

void WorldSession::Pump()
{
    transport_.Pump([this](const auto& event) { Event(event); });
    const auto now = Clock::now();
    for (auto& [id, client] : clients_)
        if (!client.closeRequested && client.identity.playerId.isEmpty() && now >= client.admissionDeadline)
            Deny(id, client, "admission_timeout");
    if (runner_.IsPlaying()) Publish(true);
    transport_.Flush();
}

void WorldSession::Event(const Transport::Event& event)
{
    if (event.kind == Transport::Event::Kind::connected) {
        auto& client = clients_[event.connection];
        if (!runner_.IsPlaying()) { Deny(event.connection, client, "world_unavailable"); return; }
        client.challenge = juce::Uuid().toString();
        client.admissionDeadline = Clock::now() + admissionTimeout;
        Send(event.connection, Lane::control, Object({{"type", "challenge"}, {"version", protocolVersion},
            {"world", store_.Settings().worldId}, {"challenge", client.challenge}}));
        return;
    }
    const auto found = clients_.find(event.connection);
    if (found == clients_.end()) return;
    auto& client = found->second;
    if (event.kind == Transport::Event::Kind::disconnected) {
        Leave(client);
        clients_.erase(found);
        return;
    }
    if (client.closeRequested) return;

    const auto now = Clock::now();
    if (now - client.lastBudgetReset >= std::chrono::seconds(1)) {
        client.lastBudgetReset = now;
        client.budget = 0;
    }
    if (++client.budget > 100 || event.bytes.size() > 2048) {
        Deny(event.connection, client, "message_budget");
        return;
    }
    const auto message = Decode(event.bytes);
    if (!message) { Deny(event.connection, client, "malformed"); return; }
    Receive(event.connection, client, event.lane, *message);
}

void WorldSession::Receive(ConnectionId id, Client& client, Lane lane, const Message& message)
{
    const auto type = Get(message, "type").toString();
    if (client.identity.playerId.isEmpty()) {
        if (lane != Lane::control || type != "hello" || !ExactFields(message, {"type", "version", "player", "proof"})) {
            Deny(id, client, "admission_required");
            return;
        }
        if (!Integer(Get(message, "version"), protocolVersion, protocolVersion)) {
            Deny(id, client, "incompatible_version");
            return;
        }
        const auto claim = Text(Get(message, "player"), 64), proof = Text(Get(message, "proof"), 64);
        if (!claim || !proof || !Identifier(*claim) || Clock::now() >= client.admissionDeadline) {
            Deny(id, client, "invalid_identity");
            return;
        }
        const auto identity = policy_.authenticate(std::exchange(client.challenge, {}), store_.Settings().worldId, *claim, *proof);
        if (!identity || !Identifier(identity->playerId)) { Deny(id, client, "invalid_identity"); return; }
        if (!Admit(client, *identity)) { Deny(id, client, "admission_rejected"); return; }
        Send(id, Lane::control, Object({{"type", "welcome"}, {"version", protocolVersion},
            {"player", client.identity.playerId}, {"world", store_.Settings().worldId}}));
        return;
    }

    if (type == "input" && lane == Lane::state && ExactFields(message, {"type", "seq", "x", "z", "jump"})) {
        const auto seq = Integer(Get(message, "seq"), 1, maximumSequence);
        const auto x = Integer(Get(message, "x"), -1000, 1000), z = Integer(Get(message, "z"), -1000, 1000);
        const auto jump = Get(message, "jump");
        if (!seq || !x || !z || !jump.isBool() || !client.identity.mayMove) { ++rejected_; return; }
        if (*seq <= client.inputSequence) return;
        client.inputSequence = *seq;
        client.lastInput = Clock::now();
        client.input = {static_cast<int>(*x), static_cast<int>(*z), static_cast<bool>(jump)};
        return;
    }
    if (type == "request" && lane == Lane::control && ExactFields(message, {"type", "seq", "name", "target"})) {
        const auto seq = Integer(Get(message, "seq"), 1, maximumSequence);
        const auto name = Text(Get(message, "name"), 64), target = Text(Get(message, "target"), 128);
        if (seq && *seq <= client.requestSequence) return;
        if (seq) client.requestSequence = *seq;
        if (!seq || !name || !target || !Identifier(*name) || !Identifier(*target, 128)
            || !client.identity.mayRequest || client.requests.size() >= maximumRequestsPerTick) {
            ++rejected_;
            if (seq) Send(id, Lane::control, Object({{"type", "result"}, {"seq", juce::var(static_cast<juce::int64>(*seq))}, {"ok", false}}));
            return;
        }
        client.requests.push_back({*name, *target, *seq});
        return;
    }
    if (type == "resync" && lane == Lane::control && ExactFields(message, {"type"})) {
        const auto now = Clock::now();
        if (now - client.lastResync >= std::chrono::seconds(1)) {
            client.lastResync = now;
            client.synchronize = true;
        } else {
            ++rejected_;
        }
        return;
    }
    if (type == "leave" && lane == Lane::control && ExactFields(message, {"type"})) {
        Leave(client);
        client.closeRequested = true;
        transport_.Disconnect(id);
        return;
    }
    Deny(id, client, "invalid_message");
}

void WorldSession::Deny(ConnectionId id, Client& client, const char* reason)
{
    ++rejected_;
    Leave(client);
    client.closeRequested = true;
    Send(id, Lane::control, Object({{"type", "rejected"}, {"reason", reason}}));
    transport_.Disconnect(id);
}

bool WorldSession::Admit(Client& client, Identity identity)
{
    int active = 0;
    for (const auto& [id, other] : clients_) {
        (void)id;
        if (other.identity.playerId.isEmpty()) continue;
        if (other.identity.playerId == identity.playerId) return false;
        ++active;
    }
    if (active >= policy_.capacity) return false;
    const auto retained = players_.find(identity.playerId);
    if (retained == players_.end()) {
        if (players_.size() >= maximumPlayers) return false;
        const auto slots = FindSlots(world_);
        PlayerState player;
        for (const auto& [slot, entity] : slots) {
            if (std::none_of(players_.begin(), players_.end(), [&](const auto& entry) { return entry.second.slotId == slot; })) {
                player = {entity, slot};
                break;
            }
        }
        if (player.slotId.isEmpty() && !slots.empty()) return false;
        players_.emplace(identity.playerId, std::move(player));
        if (!CheckpointNow()) {
            players_.erase(identity.playerId);
            return false;
        }
    }
    client.identity = std::move(identity);
    client.lastInput = Clock::now();
    CreateController(client);
    Dispatch("on_player_joined", client.identity.playerId, players_.at(client.identity.playerId).entityId);
    return true;
}

void WorldSession::Leave(Client& client)
{
    if (client.identity.playerId.isEmpty()) return;
    const auto player = client.identity.playerId;
    Dispatch("on_player_left", player, players_.at(player).entityId);
    if (client.character >= 0) {
        std::lock_guard lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        const auto entity = static_cast<entt::entity>(client.character);
        if (registry.valid(entity)) registry.remove<physics::CharacterControllerTag>(entity);
    }
    client.character = -1;
    client.jumping = false;
    client.identity = {};
    client.input = {};
    client.requests.clear();
}

bool WorldSession::AdmitLocal()
{
    if (!runner_.IsPlaying() || clients_.contains(0)) return false;
    auto [local, inserted] = clients_.try_emplace(0);
    (void)inserted;
    if (Admit(local->second, {"local", true, true})) return true;
    clients_.erase(local);
    return false;
}

bool WorldSession::SubmitLocal(Input input)
{
    const auto found = clients_.find(0);
    if (found == clients_.end() || std::abs(static_cast<long long>(input.x)) > 1000
        || std::abs(static_cast<long long>(input.z)) > 1000) return false;
    found->second.input = input;
    found->second.lastInput = Clock::now();
    return true;
}

bool WorldSession::SubmitLocalRequest(const juce::String& name, const juce::String& entity)
{
    const auto found = clients_.find(0);
    if (found == clients_.end() || !Identifier(name) || !Identifier(entity, 128)
        || found->second.requests.size() >= maximumRequestsPerTick) return false;
    found->second.requests.push_back({name, entity, 0});
    return true;
}

juce::String WorldSession::LocalEntity() const
{
    const auto local = players_.find("local");
    return local == players_.end() ? juce::String{} : local->second.entityId;
}

std::int64_t WorldSession::LocalHandle() const
{
    const auto id = LocalEntity();
    std::lock_guard lock(world_.RegistryMutex());
    return Handle(Find(world_.Registry(), id));
}

void WorldSession::CreateController(Client& client)
{
    client.character = -1;
    const auto& bound = players_.at(client.identity.playerId).entityId;
    entt::entity handle = entt::null;
    float radius = 0.3f, halfHeight = 0.9f;
    {
        std::lock_guard lock(world_.RegistryMutex());
        const auto& registry = world_.Registry();
        handle = Find(registry, bound);
        if (handle == entt::null) return;
        const auto* character = registry.try_get<scene::CharacterInstanceRef>(handle);
        if (character == nullptr || !registry.all_of<engine::Transform>(handle)) return;
        const auto* body = registry.try_get<physics::RigidBodyComponent>(handle);
        if (body != nullptr && body->motionType == physics::MotionType::Dynamic) return;
        const auto size = [&](const char* key, float fallback) {
            const auto value = static_cast<float>(character->state.getWithDefault(key, fallback));
            return std::isfinite(value) ? std::clamp(value, 0.05f, 5.0f) : fallback;
        };
        radius = size("capsuleRadiusMeters", radius);
        halfHeight = size("capsuleHalfHeightMeters", halfHeight);
    }
    if (physics_.CreateCharacter(world_, Handle(handle), radius, halfHeight))
        client.character = Handle(handle);
}

void WorldSession::Dispatch(const char* event, const juce::String& player, const juce::String& entity,
                            const juce::String& name, const juce::String& target)
{
    entt::entity handle = entt::null;
    {
        std::lock_guard lock(world_.RegistryMutex());
        handle = Find(world_.Registry(), entity);
    }
    if (entity.isNotEmpty() && handle == entt::null) return;
    const juce::ScopedValueSetter playerContext(currentPlayer_, player);
    const juce::ScopedValueSetter requestContext(currentRequest_, name);
    const juce::ScopedValueSetter targetContext(currentTarget_, target);
    frust_.gameplayEvent(Handle(handle), event, static_cast<std::int64_t>(world_.CurrentTick()));
}

void WorldSession::BindPlayers()
{
    const auto slots = FindSlots(world_);
    for (auto& [id, player] : players_) {
        (void)id;
        const auto found = slots.find(player.slotId);
        player.entityId = found == slots.end() ? juce::String{} : found->second;
    }
}

void WorldSession::ApplyInputs()
{
    characterInputs_.clear();
    const auto now = Clock::now();
    for (auto& [id, client] : clients_) {
        (void)id;
        if (client.identity.playerId.isEmpty()) continue;
        auto& player = players_.at(client.identity.playerId);
        auto input = now - client.lastInput < idleInputTimeout ? client.input : Input{};
        if (!client.identity.mayMove) input = {};
        const float length = std::max(1000.0f, std::hypot(static_cast<float>(input.x), static_cast<float>(input.z)));
        const float vx = policy_.movementSpeed * static_cast<float>(input.x) / length;
        const float vz = policy_.movementSpeed * static_cast<float>(input.z) / length;
        if (client.character >= 0) {
            characterInputs_.push_back({client.character, vx, vz, input.jump && !client.jumping ? 5.0f : 0.0f});
            client.jumping = input.jump;
            continue;
        }
        entt::entity entity = entt::null;
        bool dynamic = false;
        {
            std::lock_guard lock(world_.RegistryMutex());
            auto& registry = world_.Registry();
            entity = Find(registry, player.entityId);
            if (entity != entt::null) {
                const auto* rigid = registry.try_get<physics::RigidBodyComponent>(entity);
                dynamic = rigid != nullptr && rigid->motionType == physics::MotionType::Dynamic;
                if (!dynamic && (rigid == nullptr || rigid->motionType == physics::MotionType::Kinematic)) {
                    if (auto* transform = registry.try_get<engine::Transform>(entity)) {
                        transform->position.x += vx * runner_.StepSeconds();
                        transform->position.z += vz * runner_.StepSeconds();
                    }
                }
            }
        }
        if (dynamic) {
            const auto motion = physics_.CaptureBodyMotion(world_, Handle(entity));
            physics_.SetLinearVelocity(world_, Handle(entity), vx, motion.linearY, vz);
        }
    }
}

void WorldSession::ApplyRequests()
{
    for (auto& [id, client] : clients_) {
        if (client.identity.playerId.isEmpty()) continue;
        auto requests = std::exchange(client.requests, {});
        for (const auto& request : requests) {
            const bool ok = ApplyRequest(client, request);
            if (!ok) ++rejected_;
            if (id != 0 && request.sequence > 0)
                Send(id, Lane::control, Object({{"type", "result"}, {"seq", juce::var(static_cast<juce::int64>(request.sequence))}, {"ok", ok}}));
        }
    }
}

bool WorldSession::ApplyRequest(const Client& client, const Request& request)
{
    if (!client.identity.mayRequest
        || (policy_.relevant && !policy_.relevant(client.identity.playerId, request.target))) return false;
    const auto& player = players_.at(client.identity.playerId);
    {
        std::lock_guard lock(world_.RegistryMutex());
        const auto& registry = world_.Registry();
        const auto from = Find(registry, player.entityId), target = Find(registry, request.target);
        if (from == entt::null || target == entt::null) return false;
        const auto* origin = registry.try_get<engine::Transform>(from);
        const auto* destination = registry.try_get<engine::Transform>(target);
        if (origin == nullptr || destination == nullptr) return false;
        const auto dx = origin->position.x - destination->position.x;
        const auto dy = origin->position.y - destination->position.y;
        const auto dz = origin->position.z - destination->position.z;
        if (dx * dx + dy * dy + dz * dz > policy_.requestDistance * policy_.requestDistance) return false;
    }
    Dispatch("on_gameplay_request", client.identity.playerId, request.target, request.name, request.target);
    return true;
}

juce::String WorldSession::ApplicationKey(const juce::String& key) const
{
    if (!Identifier(key, 64)) return {};
    return currentPlayer_.isEmpty() ? "world." + key
        : "player." + juce::String(currentPlayer_.length()) + "." + currentPlayer_ + "." + key;
}

bool WorldSession::SetApplication(const juce::String& key, std::int64_t value, bool replicate)
{
    if (!Identifier(key, 64)) return false;
    if (replicate) {
        if (published_.size() >= 64 && !published_.contains(key)) return false;
        published_[key] = value;
        return true;
    }
    const auto scoped = ApplicationKey(key);
    if ((policy_.persistentValue && !policy_.persistentValue(key, value))
        || (persistent_.size() >= maximumPlayers && !persistent_.contains(scoped))) return false;
    persistent_[scoped] = value;
    return true;
}

std::int64_t WorldSession::GetApplication(const juce::String& key) const
{
    const auto found = persistent_.find(ApplicationKey(key));
    return found == persistent_.end() ? 0 : found->second;
}

void WorldSession::Tick(const runtime::RuntimeWorldRunner::Hooks& hooks)
{
    if (!runner_.IsPlaying()) return;
    const auto started = Clock::now();
    ApplyInputs();
    runner_.Tick({[&](float dt) {
        for (const auto& input : characterInputs_)
            physics_.UpdateCharacter(world_, input.handle, input.x, input.z, input.jump, dt);
        ApplyRequests();
        if (hooks.afterPhysics) hooks.afterPhysics(dt);
    }, [&] {
        if (hooks.beforeSceneChange) hooks.beforeSceneChange();
    }, [&] {
        AssignIdentities();
        RestoreBodyMotion(fetchedScene_);
        BindPlayers();
        for (auto& [id, client] : clients_) {
            (void)id;
            client.synchronize = true;
            if (client.identity.playerId.isNotEmpty()) {
                CreateController(client);
                Dispatch("on_player_joined", client.identity.playerId, players_.at(client.identity.playerId).entityId);
            }
        }
        CheckpointNow();
        if (hooks.afterSceneChange) hooks.afterSceneChange();
    }, [&] {
        return (!hooks.canChangeScene || hooks.canChangeScene()) && CheckpointNow();
    }});
    if (world_.CurrentTick() - lastCheckpointTick_ >= static_cast<std::uint64_t>(store_.Settings().checkpointTicks))
        CheckpointNow();
    Publish();
    transport_.Flush();
    const auto micros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    ++tickCount_;
    totalTickMicros_ += micros;
    maxTickMicros_ = std::max(maxTickMicros_, micros);
}

bool WorldSession::Send(ConnectionId id, Lane lane, const Message& message)
{
    return transport_.Send(id, lane, Encode(message));
}

Message WorldSession::InspectLocal() const
{
    auto result = Diagnostics();
    Put(result, "entity", LocalEntity());
    Put(result, "tick", juce::String(static_cast<juce::uint64>(world_.CurrentTick())));
    Put(result, "poses", EncodePoses(CapturePoses()));
    auto app = Object();
    for (const auto& [key, value] : published_)
        app.getDynamicObject()->setProperty(key, juce::var(static_cast<juce::int64>(value)));
    Put(result, "application", app);
    return result;
}

Message WorldSession::Diagnostics() const
{
    const auto count = [](std::uint64_t value) { return juce::String(static_cast<juce::uint64>(value)); };
    const auto transport = transport_.GetMetrics();
    juce::Array<Message> clients;
    for (const auto& [id, client] : clients_) {
        const auto player = players_.find(client.identity.playerId);
        clients.add(Object({{"connection", count(id)}, {"player", client.identity.playerId},
            {"entity", player == players_.end() ? juce::String{} : player->second.entityId},
            {"state", client.closeRequested ? "closing" : client.identity.playerId.isEmpty() ? "admission" : "active"}}));
    }
    return Object({{"worldId", store_.Settings().worldId}, {"port", Port()}, {"clients", Message(std::move(clients))},
        {"provider", store_.Settings().provider}, {"revision", count(store_.Revision())}, {"checkpoints", count(checkpoints_)},
        {"rejected", count(rejected_)}, {"syncFailures", count(syncFailures_)}, {"error", error_},
        {"sentPackets", count(transport.sentPackets)}, {"receivedPackets", count(transport.receivedPackets)},
        {"sentBytes", count(transport.sentBytes)}, {"receivedBytes", count(transport.receivedBytes)},
        {"droppedPackets", count(transport.droppedPackets)}, {"cappedPumps", count(transport.cappedPumps)},
        {"maxTickMicros", count(maxTickMicros_)}, {"meanTickMicros", count(tickCount_ == 0 ? 0 : totalTickMicros_ / tickCount_)}});
}
} // namespace ce::gameplay
