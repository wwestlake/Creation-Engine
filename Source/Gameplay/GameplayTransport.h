#pragma once

#include "GameplayProtocol.h"
#include <functional>
#include <memory>

namespace ce::gameplay
{
using ConnectionId = std::uint64_t;
enum class Lane { control, state };

class Transport final
{
public:
    struct Event {
        enum class Kind { connected, disconnected, message };
        Kind kind;
        ConnectionId connection;
        Lane lane = Lane::control;
        std::string_view bytes;
    };
    struct Metrics {
        std::uint64_t receivedPackets = 0, sentPackets = 0, receivedBytes = 0, sentBytes = 0;
        std::uint64_t droppedPackets = 0, cappedPumps = 0;
    };
    using Handler = std::function<void(const Event&)>;

    Transport();
    ~Transport();
    bool Listen(const juce::String& address, int port, int peers, juce::String& error);
    bool Connect(const juce::String& address, int port, juce::String& error);
    void Pump(const Handler& handler);
    bool Send(ConnectionId connection, Lane lane, std::string_view wire);
    void Disconnect(ConnectionId connection, bool drain = true);
    void Flush();
    [[nodiscard]] int Port() const;
    [[nodiscard]] Metrics GetMetrics() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace ce::gameplay
