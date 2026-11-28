#include "GameplayClient.h"

#include <algorithm>
#include <limits>

namespace ce::gameplay
{
namespace
{
constexpr auto maximumValue = std::numeric_limits<std::int64_t>::max();

Message Wire(std::int64_t value) { return Message(static_cast<juce::int64>(value)); }

engine::Vec3 Blend(const engine::Vec3& before, const engine::Vec3& after, float amount)
{
    return {std::lerp(before.x, after.x, amount), std::lerp(before.y, after.y, amount), std::lerp(before.z, after.z, amount)};
}
}

GameplayClient::GameplayClient(ClientSettings settings) : settings_(std::move(settings)) {}
GameplayClient::~GameplayClient() { Disconnect(); }

bool GameplayClient::Connect(juce::String& error)
{
    if (!Identifier(settings_.world) || !Identifier(settings_.player) || settings_.secret.getNumBytesAsUTF8() < 24
        || settings_.secret.getNumBytesAsUTF8() > 128) {
        error = "A world, player and valid gameplay credential are required.";
        return false;
    }
    Disconnect();
    transport_ = std::make_unique<Transport>();
    if (!transport_->Connect(settings_.address, settings_.port, error)) {
        transport_.reset();
        return false;
    }
    reason_.clear();
    entity_.clear();
    scene_.clear();
    document_ = {};
    application_ = Object();
    lastRequest_ = Message{};
    baselinePoses_.clear();
    poses_.clear();
    previous_.clear();
    baseline_ = tick_ = sequence_ = requestSequence_ = resultSequence_ = 0;
    requestAccepted_ = false;
    state_ = State::connecting;
    connectDeadline_ = Clock::now() + std::chrono::seconds(7);
    return true;
}

void GameplayClient::Disconnect()
{
    if (transport_ && connection_ != 0) {
        if (state_ == State::active || state_ == State::synchronizing)
            Send(Lane::control, Object({{"type", "leave"}}));
        transport_->Flush();
        transport_->Disconnect(connection_);
        transport_->Flush();
    }
    transport_.reset();
    connection_ = 0;
    state_ = State::disconnected;
}

void GameplayClient::Pump()
{
    if (!transport_) return;
    transport_->Pump([this](const auto& event) { Event(event); });
    const auto now = Clock::now();
    if (now >= connectDeadline_ && (state_ == State::connecting || state_ == State::synchronizing))
        Reject("The server did not complete admission and synchronization.");
    if (state_ == State::active && now - lastFrame_ >= std::chrono::seconds(2))
        RequestSynchronization();
    transport_->Flush();
}

void GameplayClient::Event(const Transport::Event& event)
{
    if (event.kind == Transport::Event::Kind::connected) { connection_ = event.connection; return; }
    if (event.kind == Transport::Event::Kind::disconnected) {
        connection_ = 0;
        if (state_ != State::rejected) {
            state_ = State::disconnected;
            if (reason_.isEmpty()) reason_ = "The gameplay connection closed.";
        }
        return;
    }
    if (state_ == State::rejected) return;
    const auto decoded = Decode(event.bytes);
    if (!decoded) { Reject("Invalid server message."); return; }
    Receive(event.lane, *decoded);
}

void GameplayClient::Receive(Lane lane, const Message& message)
{
    const auto type = Get(message, "type").toString();
    if (lane == Lane::control) {
        if (type == "rejected") { Reject(Get(message, "reason").toString()); return; }
        if (type == "closing") { reason_ = "The world shut down."; return; }
        if (type == "challenge" && state_ == State::connecting) {
            const auto challenge = Text(Get(message, "challenge"), 64);
            if (!challenge || !Identifier(*challenge) || Get(message, "world").toString() != settings_.world
                || !Integer(Get(message, "version"), protocolVersion, protocolVersion)) {
                Reject("The server world or protocol did not match.");
                return;
            }
            Send(Lane::control, Object({{"type", "hello"}, {"version", protocolVersion}, {"player", settings_.player},
                {"proof", MakeProof(settings_.secret, *challenge, settings_.world, settings_.player)}}));
            return;
        }
        if (type == "welcome" && state_ == State::connecting) {
            if (Get(message, "player").toString() != settings_.player || Get(message, "world").toString() != settings_.world
                || !Integer(Get(message, "version"), protocolVersion, protocolVersion)) {
                Reject("The server confirmed an unexpected identity.");
                return;
            }
            state_ = State::synchronizing;
            return;
        }
        if (type == "keyframe" && (state_ == State::synchronizing || state_ == State::active)) {
            if (!Synchronize(message)) Reject("Invalid authoritative world snapshot.");
            return;
        }
        if (type == "result" && state_ == State::active) {
            const auto seq = Integer(Get(message, "seq"), 1, requestSequence_);
            const auto ok = Get(message, "ok");
            if (seq && *seq > resultSequence_ && ok.isBool()) {
                resultSequence_ = *seq;
                requestAccepted_ = static_cast<bool>(ok);
            }
            return;
        }
        Reject("Unexpected reliable gameplay message.");
        return;
    }
    if (type == "frame" && state_ == State::active && !Update(message))
        RequestSynchronization();
}

bool GameplayClient::Synchronize(const Message& message)
{
    if (!ExactFields(message, {"type", "base", "tick", "scene", "entity", "document", "poses", "application"}))
        return false;
    const auto base = Integer(Get(message, "base"), 1, maximumValue);
    const auto tick = Integer(Get(message, "tick"), 0, maximumValue);
    const auto entity = Text(Get(message, "entity"), 128, true), scene = Text(Get(message, "scene"), 128, true);
    const auto xml = Text(Get(message, "document"), static_cast<int>(maximumPacketBytes));
    if (!base || !tick || !entity || !scene || !xml || (*entity != "" && !Identifier(*entity, 128))) return false;
    if (*base <= baseline_) return true;
    Poses observed;
    if (!DecodePoses(Get(message, "poses"), observed) || (entity->isNotEmpty() && !observed.contains(*entity))) return false;
    // The gameplay serializer emits shallow XML with bounded object counts.
    // Reject doctypes and excessively large tag counts before parsing it.
    if (xml->containsIgnoreCase("<!DOCTYPE") || xml->containsIgnoreCase("<!ENTITY")
        || xml->retainCharacters("<").length() > 8000) return false;
    const auto document = juce::ValueTree::fromXml(*xml);
    if (!document.hasType("CreationEngineScene") || !ReadApplication(Get(message, "application"))) return false;
    scene_ = *scene;
    entity_ = *entity;
    document_ = document;
    baseline_ = *base;
    tick_ = *tick;
    baselinePoses_ = observed;
    previous_ = observed;
    poses_ = std::move(observed);
    lastFrame_ = Clock::now();
    state_ = State::active;
    return true;
}

bool GameplayClient::Update(const Message& message)
{
    if (!ExactFields(message, {"type", "base", "tick", "poses", "application"})) return false;
    const auto base = Integer(Get(message, "base"), 1, maximumValue);
    const auto tick = Integer(Get(message, "tick"), 0, maximumValue);
    if (!base || !tick) return false;
    if (*base != baseline_ || *tick <= tick_) return *base <= baseline_;
    Poses changed;
    if (!DecodePoses(Get(message, "poses"), changed)) return false;
    auto latest = baselinePoses_;
    for (const auto& [entity, pose] : changed) {
        const auto found = latest.find(entity);
        if (found == latest.end()) return false;
        found->second = pose;
    }
    if (!ReadApplication(Get(message, "application"))) return false;
    previous_ = InterpolatedPoses();
    poses_ = std::move(latest);
    tick_ = *tick;
    lastFrame_ = Clock::now();
    return true;
}

bool GameplayClient::ReadApplication(const Message& value)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || object->getProperties().size() > 64) return false;
    for (const auto& field : object->getProperties())
        if (!Identifier(field.name.toString()) || (!field.value.isInt() && !field.value.isInt64())) return false;
    application_ = value;
    return true;
}

void GameplayClient::RequestSynchronization()
{
    const auto now = Clock::now();
    if (now - lastResync_ < std::chrono::seconds(2)) return;
    lastResync_ = now;
    Send(Lane::control, Object({{"type", "resync"}}));
}

bool GameplayClient::Submit(Input input)
{
    if (state_ != State::active || sequence_ >= std::numeric_limits<std::int32_t>::max()) return false;
    return Send(Lane::state, Object({{"type", "input"}, {"seq", Wire(++sequence_)}, {"x", input.x}, {"z", input.z}, {"jump", input.jump}}));
}

bool GameplayClient::Request(const juce::String& name, const juce::String& target)
{
    if (state_ != State::active || requestSequence_ >= std::numeric_limits<std::int32_t>::max()
        || !Identifier(name) || !Identifier(target, 128)) return false;
    lastRequest_ = Object({{"type", "request"}, {"seq", Wire(++requestSequence_)}, {"name", name}, {"target", target}});
    return Send(Lane::control, lastRequest_);
}

bool GameplayClient::RetryLastRequest()
{
    return state_ == State::active && !lastRequest_.isVoid() && Send(Lane::control, lastRequest_);
}

bool GameplayClient::Send(Lane lane, const Message& message)
{
    return transport_ && connection_ != 0 && transport_->Send(connection_, lane, Encode(message));
}

void GameplayClient::Reject(const juce::String& reason)
{
    reason_ = reason;
    state_ = State::rejected;
    if (transport_ && connection_ != 0) transport_->Disconnect(connection_);
}

Poses GameplayClient::InterpolatedPoses() const
{
    const auto elapsed = std::chrono::duration<float>(Clock::now() - lastFrame_).count();
    const auto amount = std::clamp(elapsed * 15.0f, 0.0f, 1.0f);
    auto result = poses_;
    for (auto& [id, pose] : result) {
        const auto before = previous_.find(id);
        if (before == previous_.end()) continue;
        pose.position = Blend(before->second.position, pose.position, amount);
        pose.rotation = Blend(before->second.rotation, pose.rotation, amount);
    }
    return result;
}

Message GameplayClient::Inspect() const
{
    static constexpr const char* states[]{"disconnected", "connecting", "synchronizing", "active", "rejected"};
    return Object({{"state", states[static_cast<int>(state_)]}, {"reason", reason_}, {"player", settings_.player},
        {"world", settings_.world}, {"scene", scene_}, {"entity", entity_}, {"base", Wire(baseline_)},
        {"tick", Wire(tick_)}, {"poses", EncodePoses(poses_)}, {"application", application_}, {"resultSeq", Wire(resultSequence_)},
        {"resultOk", requestAccepted_}});
}
} // namespace ce::gameplay
