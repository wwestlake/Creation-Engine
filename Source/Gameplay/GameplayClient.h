#pragma once

#include "WorldSession.h"

namespace ce::gameplay
{
struct ClientSettings {
    juce::String address = "127.0.0.1";
    int port = 0;
    juce::String world, player, secret;
};

class GameplayClient final
{
public:
    enum class State { disconnected, connecting, synchronizing, active, rejected };

    explicit GameplayClient(ClientSettings settings);
    ~GameplayClient();
    bool Connect(juce::String& error);
    void Disconnect();
    void Pump();
    bool Submit(Input input);
    bool Request(const juce::String& name, const juce::String& target);
    bool RetryLastRequest();
    void Resynchronize() { RequestSynchronization(); }
    [[nodiscard]] State ConnectionState() const noexcept { return state_; }
    [[nodiscard]] const juce::String& Reason() const noexcept { return reason_; }
    [[nodiscard]] const juce::String& Entity() const noexcept { return entity_; }
    [[nodiscard]] const juce::String& Scene() const noexcept { return scene_; }
    [[nodiscard]] const juce::ValueTree& Document() const noexcept { return document_; }
    [[nodiscard]] std::int64_t Baseline() const noexcept { return baseline_; }
    [[nodiscard]] std::int64_t Tick() const noexcept { return tick_; }
    [[nodiscard]] const Poses& AuthoritativePoses() const noexcept { return poses_; }
    [[nodiscard]] Poses InterpolatedPoses() const;
    [[nodiscard]] Message Inspect() const;

private:
    using Clock = std::chrono::steady_clock;
    void Event(const Transport::Event& event);
    void Receive(Lane lane, const Message& message);
    bool Synchronize(const Message& message);
    bool Update(const Message& message);
    bool ReadApplication(const Message& message);
    void RequestSynchronization();
    bool Send(Lane lane, const Message& message);
    void Reject(const juce::String& reason);

    ClientSettings settings_;
    std::unique_ptr<Transport> transport_;
    State state_ = State::disconnected;
    juce::String reason_, entity_, scene_;
    juce::ValueTree document_;
    ConnectionId connection_ = 0;
    std::int64_t baseline_ = 0, tick_ = 0, sequence_ = 0, requestSequence_ = 0;
    std::int64_t resultSequence_ = 0;
    bool requestAccepted_ = false;
    Clock::time_point lastFrame_{}, lastResync_{}, connectDeadline_{};
    Poses baselinePoses_, poses_, previous_;
    Message application_, lastRequest_;
};
} // namespace ce::gameplay
