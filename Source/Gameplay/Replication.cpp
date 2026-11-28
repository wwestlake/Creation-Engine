#include "WorldSession.h"

#include "Physics/PhysicsComponents.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"
#include "Scene/EngineSceneSerializer.h"

#include <algorithm>
#include <cmath>

namespace ce::gameplay
{
namespace
{
constexpr engine::Tick sendEveryTicks = 2;
constexpr engine::Tick keyframeEveryTicks = 120;

bool Equal(const engine::Vec3& a, const engine::Vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void StripPrivateState(juce::ValueTree entity)
{
    for (int i = entity.getNumChildren() - 1; i >= 0; --i) {
        const auto type = entity.getChild(i).getType();
        if (type == juce::Identifier("ObjectState") || type == juce::Identifier("Behaviors")
            || type == juce::Identifier("GameplayMotion") || type == juce::Identifier("CharacterInstance"))
            entity.removeChild(i, nullptr);
    }
}
} // namespace

bool Pose::operator==(const Pose& other) const noexcept
{
    return Equal(position, other.position) && Equal(rotation, other.rotation) && Equal(velocity, other.velocity);
}

Message EncodePoses(const Poses& poses)
{
    juce::Array<Message> result;
    for (const auto& [id, pose] : poses) {
        juce::Array<Message> row{id, pose.position.x, pose.position.y, pose.position.z,
            pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.velocity.x, pose.velocity.y, pose.velocity.z};
        result.add(Message(std::move(row)));
    }
    return Message(std::move(result));
}

bool DecodePoses(const Message& message, Poses& poses)
{
    const auto* rows = message.getArray();
    if (rows == nullptr || rows->size() > maximumEntities) return false;
    Poses result;
    for (const auto& entry : *rows) {
        const auto* row = entry.getArray();
        if (row == nullptr || row->size() != 10) return false;
        const auto id = Text(row->getReference(0), 128);
        if (!id || !Identifier(*id, 128)) return false;
        std::array<float, 9> numbers{};
        for (std::size_t i = 0; i < numbers.size(); ++i) {
            const auto value = Number(row->getReference(static_cast<int>(i + 1)), 10000000.0);
            if (!value) return false;
            numbers[i] = static_cast<float>(*value);
        }
        if (!result.emplace(*id, Pose{{numbers[0], numbers[1], numbers[2]},
            {numbers[3], numbers[4], numbers[5]}, {numbers[6], numbers[7], numbers[8]}}).second) return false;
    }
    poses = std::move(result);
    return true;
}

void WorldSession::AssignIdentities()
{
    std::lock_guard lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    std::set<juce::String> seen;
    for (const auto entity : registry.storage<entt::entity>()) {
        if (registry.all_of<scene::SceneRoot>(entity)) continue;
        auto& identity = registry.get_or_emplace<scene::InstanceId>(entity);
        if (!Identifier(identity.value, 128) || !seen.insert(identity.value).second) {
            identity.value = juce::Uuid().toString();
            seen.insert(identity.value);
        }
    }
}

juce::ValueTree WorldSession::Snapshot() const
{
    auto document = scene::EngineSceneSerializer::serializeScene(world_);
    std::vector<std::pair<std::int64_t, juce::String>> bodies;
    {
        std::lock_guard lock(world_.RegistryMutex());
        const auto view = world_.Registry().view<const scene::InstanceId, const physics::RigidBodyComponent>();
        for (const auto entity : view)
            if (view.get<const physics::RigidBodyComponent>(entity).motionType == physics::MotionType::Dynamic)
                bodies.emplace_back(static_cast<std::int64_t>(entt::to_integral(entity)),
                    view.get<const scene::InstanceId>(entity).value);
    }
    std::map<juce::String, physics::RestoredBodyMotion> motion;
    for (const auto& [entity, id] : bodies)
        motion.emplace(id, physics_.CaptureBodyMotion(world_, entity));

    for (auto entity : document.getChildWithName("Entities")) {
        const auto found = motion.find(entity.getProperty("instanceId").toString());
        if (found == motion.end()) continue;
        const auto& state = found->second;
        juce::ValueTree values("GameplayMotion");
        values.setProperty("vx", state.linearX, nullptr);
        values.setProperty("vy", state.linearY, nullptr);
        values.setProperty("vz", state.linearZ, nullptr);
        values.setProperty("wx", state.angularX, nullptr);
        values.setProperty("wy", state.angularY, nullptr);
        values.setProperty("wz", state.angularZ, nullptr);
        entity.appendChild(values, nullptr);
    }
    return document;
}

void WorldSession::RestoreBodyMotion(const juce::ValueTree& document)
{
    std::map<juce::String, physics::RestoredBodyMotion> motion;
    for (const auto entity : document.getChildWithName("Entities")) {
        const auto values = entity.getChildWithName("GameplayMotion");
        if (!values.isValid()) continue;
        auto scalar = [&](const char* key) {
            const auto value = values.getProperty(key, 0.0).toString().getDoubleValue();
            return std::isfinite(value) && std::abs(value) <= 10000000.0 ? static_cast<float>(value) : 0.0f;
        };
        motion.emplace(entity.getProperty("instanceId").toString(),
            physics::RestoredBodyMotion{scalar("vx"), scalar("vy"), scalar("vz"), scalar("wx"), scalar("wy"), scalar("wz")});
    }

    std::lock_guard lock(world_.RegistryMutex());
    auto& registry = world_.Registry();
    const auto view = registry.view<const scene::InstanceId, const physics::RigidBodyComponent>();
    for (const auto entity : view) {
        const auto found = motion.find(view.get<const scene::InstanceId>(entity).value);
        if (found != motion.end()) registry.emplace_or_replace<physics::RestoredBodyMotion>(entity, found->second);
    }
}

Poses WorldSession::CapturePoses() const
{
    Poses result;
    std::vector<std::pair<std::int64_t, juce::String>> motion;
    {
        std::lock_guard lock(world_.RegistryMutex());
        const auto& registry = world_.Registry();
        const auto view = registry.view<const scene::InstanceId, const engine::Transform>();
        for (const auto entity : view) {
            if (result.size() > maximumEntities) break;
            const auto* flags = registry.try_get<scene::SceneFlags>(entity);
            if (flags != nullptr && flags->editorOnly) continue;
            const auto& id = view.get<const scene::InstanceId>(entity).value;
            const auto& transform = view.get<const engine::Transform>(entity);
            result.emplace(id, Pose{transform.position, transform.eulerRotationRadians, {}});
            if (registry.all_of<physics::RigidBodyComponent>(entity))
                motion.emplace_back(static_cast<std::int64_t>(entt::to_integral(entity)), id);
        }
    }
    for (const auto& [entity, id] : motion) {
        const auto body = physics_.CaptureBodyMotion(world_, entity);
        result.at(id).velocity = {body.linearX, body.linearY, body.linearZ};
    }
    return result;
}

void WorldSession::Publish(bool initialOnly)
{
    const auto tick = world_.CurrentTick();
    if ((!initialOnly && tick % sendEveryTicks != 0) ||
        std::none_of(clients_.begin(), clients_.end(), [&](const auto& entry) {
            return entry.first != 0 && !entry.second.identity.playerId.isEmpty() && (!initialOnly || entry.second.synchronize);
        })) return;
    AssignIdentities();
    const auto all = CapturePoses();
    juce::ValueTree document;
    const auto wireTick = Message(static_cast<juce::int64>(tick));
    auto app = Object();
    for (const auto& [key, value] : published_)
        app.getDynamicObject()->setProperty(key, juce::var(static_cast<juce::int64>(value)));

    for (auto& [id, client] : clients_) {
        if (id == 0 || client.identity.playerId.isEmpty() || (initialOnly && !client.synchronize)) continue;
        if (all.size() > maximumEntities) {
            ++syncFailures_;
            Deny(id, client, "world_too_large");
            continue;
        }
        const auto& player = players_.at(client.identity.playerId);
        Poses relevant;
        for (const auto& [entity, pose] : all) {
            if (!policy_.relevant || player.entityId == entity || policy_.relevant(client.identity.playerId, entity))
                relevant.emplace(entity, pose);
        }
        const bool structural = client.baselinePoses.size() != relevant.size() ||
            !std::equal(client.baselinePoses.begin(), client.baselinePoses.end(), relevant.begin(),
                [](const auto& a, const auto& b) { return a.first == b.first; });
        if (client.synchronize || client.baseline == 0 || structural || tick - client.baselineTick >= keyframeEveryTicks) {
            if (!document.isValid()) document = Snapshot();
            auto visible = document.createCopy();
            const auto entities = visible.getChildWithName("Entities");
            for (int i = entities.getNumChildren() - 1; i >= 0; --i) {
                auto entry = entities.getChild(i);
                const auto entity = entry.getProperty("instanceId").toString();
                if ((!relevant.contains(entity) && entry.getChildWithName("Transform").isValid())
                    || (policy_.relevant && player.entityId != entity && !policy_.relevant(client.identity.playerId, entity))) {
                    auto mutableEntities = entities;
                    mutableEntities.removeChild(i, nullptr);
                } else {
                    StripPrivateState(entry);
                }
            }
            const auto baseline = nextBaseline_++;
            const auto message = Object({{"type", "keyframe"}, {"base", Message(static_cast<juce::int64>(baseline))},
                {"tick", wireTick}, {"scene", runner_.Scene().id},
                {"entity", relevant.contains(player.entityId) ? player.entityId : juce::String{}},
                {"document", visible.toXmlString()}, {"poses", EncodePoses(relevant)}, {"application", app}});
            if (!Send(id, Lane::control, message)) {
                ++syncFailures_;
                Deny(id, client, "synchronization_failed");
                continue;
            }
            client.baseline = baseline;
            client.baselineTick = tick;
            client.baselinePoses = std::move(relevant);
            client.synchronize = false;
            continue;
        }

        Poses changed;
        for (const auto& [entity, pose] : relevant)
            if (!(pose == client.baselinePoses.at(entity))) changed.emplace(entity, pose);
        Send(id, Lane::state, Object({{"type", "frame"}, {"base", Message(static_cast<juce::int64>(client.baseline))},
            {"tick", wireTick}, {"poses", EncodePoses(changed)}, {"application", app}}));
    }
}
} // namespace ce::gameplay
