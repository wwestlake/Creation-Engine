#pragma once

#include "GameplayStore.h"
#include "GameplayTransport.h"
#include "Runtime/RuntimeWorldRunner.h"
#include "engine/math.h"

#include <chrono>
#include <map>
#include <set>

namespace ce::gameplay
{
struct Identity {
    juce::String playerId;
    bool mayMove = true;
    bool mayRequest = true;
};

// An authority consumes verified identities. The development provider below
// verifies a challenge response using credentials provisioned by the operator.
using Authenticate = std::function<std::optional<Identity>(const juce::String& challenge,
    const juce::String& world, const juce::String& claim, const juce::String& response)>;
Authenticate DevelopmentIdentities(const juce::File& file, juce::String& error);

struct SessionPolicy {
    int capacity = 8;
    float movementSpeed = 4.0f;
    float requestDistance = 8.0f;
    Authenticate authenticate;
    std::function<bool(const juce::String& player, const juce::String& entity)> relevant;
    std::function<bool(const juce::String& key, std::int64_t value)> persistentValue;
};

struct Input {
    int x = 0;
    int z = 0;
    bool jump = false;
};

struct Pose {
    engine::Vec3 position, rotation, velocity;
    bool operator==(const Pose& other) const noexcept;
};
using Poses = std::map<juce::String, Pose>;

Message EncodePoses(const Poses& poses);
bool DecodePoses(const Message& message, Poses& poses);

class WorldSession final
{
public:
    WorldSession(engine::World& world, physics::PhysicsWorld& physics, frust::EngineFrustHost& frust,
                 runtime::RuntimeWorldRunner& runner, GameplayStore& store, SessionPolicy policy = {});
    ~WorldSession();

    bool Start(runtime::RuntimeGameSource source, project::SceneDocumentInfo scene, juce::ValueTree state = {},
               const std::function<bool(juce::String&)>& prepareHost = {});
    bool Stop();
    bool CheckpointNow();

    bool Listen(const juce::String& address, int port, juce::String& error);
    void Pump();
    void Tick(const runtime::RuntimeWorldRunner::Hooks& hooks = {});
    bool AdmitLocal();
    bool SubmitLocal(Input input);
    bool SubmitLocalRequest(const juce::String& name, const juce::String& entity);
    juce::String LocalEntity() const;
    std::int64_t LocalHandle() const;
    [[nodiscard]] int Port() const { return transport_.Port(); }
    [[nodiscard]] const juce::String& Error() const noexcept { return error_; }
    [[nodiscard]] Message Diagnostics() const;
    [[nodiscard]] Message InspectLocal() const;

private:
    using Clock = std::chrono::steady_clock;
    struct Request { juce::String name, target; std::int64_t sequence; };
    struct CharacterInput { std::int64_t handle; float x, z, jump; };
    struct Client {
        juce::String challenge;
        Clock::time_point admissionDeadline{}, lastInput{}, lastBudgetReset{}, lastResync{};
        Identity identity;
        Input input;
        std::int64_t inputSequence = 0, requestSequence = 0, baseline = 0;
        std::uint64_t baselineTick = 0;
        int budget = 0;
        std::int64_t character = -1;
        bool jumping = false;
        bool closeRequested = false, synchronize = true;
        Poses baselinePoses;
        std::vector<Request> requests;
    };

    void Event(const Transport::Event& event);
    void Receive(ConnectionId id, Client& client, Lane lane, const Message& message);
    void Deny(ConnectionId id, Client& client, const char* reason);
    bool Admit(Client& client, Identity identity);
    void Leave(Client& client);
    void ApplyInputs();
    void ApplyRequests();
    void CreateController(Client& client);
    bool ApplyRequest(const Client& client, const Request& request);
    void Publish(bool initialOnly = false);
    bool Send(ConnectionId id, Lane lane, const Message& message);
    void Dispatch(const char* event, const juce::String& player, const juce::String& entity,
                  const juce::String& request = {}, const juce::String& target = {});
    bool SetApplication(const juce::String& key, std::int64_t value, bool replicate);
    std::int64_t GetApplication(const juce::String& key) const;
    juce::String ApplicationKey(const juce::String& key) const;
    void BindPlayers();
    void AssignIdentities();
    juce::ValueTree Snapshot() const;
    void RestoreBodyMotion(const juce::ValueTree& document);
    Poses CapturePoses() const;

    engine::World& world_;
    physics::PhysicsWorld& physics_;
    frust::EngineFrustHost& frust_;
    runtime::RuntimeWorldRunner& runner_;
    GameplayStore& store_;
    SessionPolicy policy_;
    Transport transport_;
    std::map<ConnectionId, Client> clients_;
    std::vector<CharacterInput> characterInputs_;
    std::map<juce::String, PlayerState> players_;
    std::map<juce::String, std::int64_t> persistent_, published_;
    juce::String currentPlayer_, currentRequest_, currentTarget_, error_;
    juce::ValueTree fetchedScene_;
    std::int64_t nextBaseline_ = 1;
    std::uint64_t lastCheckpointTick_ = 0, rejected_ = 0, syncFailures_ = 0, checkpoints_ = 0;
    std::uint64_t maxTickMicros_ = 0, tickCount_ = 0, totalTickMicros_ = 0;
};
} // namespace ce::gameplay
