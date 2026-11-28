#include "GameplayTransport.h"

#include <enet/enet.h>
#include <map>

namespace ce::gameplay
{
namespace
{
bool Initialize()
{
    struct Library {
        bool ok = enet_initialize() == 0;
        ~Library() { if (ok) enet_deinitialize(); }
    };
    static Library library;
    return library.ok;
}

bool Resolve(ENetAddress& address, const juce::String& host, int port)
{
    if (port < 0 || port > 65535 || host.isEmpty()) return false;
    address.port = static_cast<enet_uint16>(port);
    if (host == "*") {
        address.host = ENET_HOST_ANY;
        return true;
    }
    return enet_address_set_host(&address, host.toRawUTF8()) == 0;
}
} // namespace

struct Transport::State {
    ~State() { if (host != nullptr) enet_host_destroy(host); }

    ENetHost* host = nullptr;
    std::map<ENetPeer*, ConnectionId> connections;
    ConnectionId next = 1;
    Metrics metrics;

    void Configure()
    {
        host->maximumPacketSize = maximumPacketBytes;
        host->maximumWaitingData = maximumPendingBytes;
        host->checksum = enet_crc32;
    }
};

Transport::Transport() : state_(std::make_unique<State>()) {}
Transport::~Transport() = default;

bool Transport::Listen(const juce::String& address, int port, int peers, juce::String& error)
{
    ENetAddress endpoint{};
    if (state_->host != nullptr || !Initialize() || peers < 1 || peers > maximumConnections
        || !Resolve(endpoint, address, port)) {
        error = "Invalid gameplay listener settings.";
        return false;
    }
    state_->host = enet_host_create(&endpoint, static_cast<std::size_t>(peers), 2, 0, 0);
    if (state_->host == nullptr) {
        error = "Could not bind the gameplay UDP listener.";
        return false;
    }
    state_->Configure();
    return true;
}

bool Transport::Connect(const juce::String& address, int port, juce::String& error)
{
    ENetAddress endpoint{};
    if (state_->host != nullptr || !Initialize() || port == 0 || address == "*" || !Resolve(endpoint, address, port)) {
        error = "Invalid gameplay destination.";
        return false;
    }
    state_->host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (state_->host != nullptr) {
        state_->Configure();
        if (auto* peer = enet_host_connect(state_->host, &endpoint, 2, 0); peer != nullptr) {
            enet_peer_timeout(peer, 8, 1000, 6000);
            return true;
        }
    }
    error = "Could not create a gameplay connection.";
    return false;
}

void Transport::Pump(const Handler& handler)
{
    if (state_->host == nullptr) return;
    for (int i = 0; i < maximumEventsPerPump; ++i) {
        ENetEvent event{};
        if (enet_host_service(state_->host, &event, 0) <= 0) return;
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                const auto id = state_->next++;
                state_->connections.insert_or_assign(event.peer, id);
                enet_peer_ping_interval(event.peer, 500);
                enet_peer_timeout(event.peer, 8, 1000, 6000);
                handler({Event::Kind::connected, id, Lane::control, {}});
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                const auto found = state_->connections.find(event.peer);
                if (found != state_->connections.end()) {
                    const auto id = found->second;
                    state_->connections.erase(found);
                    handler({Event::Kind::disconnected, id, Lane::control, {}});
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> packet(event.packet, enet_packet_destroy);
                const auto found = state_->connections.find(event.peer);
                if (found == state_->connections.end() || packet->dataLength > maximumPacketBytes || event.channelID > 1) {
                    ++state_->metrics.droppedPackets;
                    break;
                }
                ++state_->metrics.receivedPackets;
                state_->metrics.receivedBytes += packet->dataLength;
                handler({Event::Kind::message, found->second, event.channelID == 0 ? Lane::control : Lane::state,
                         {reinterpret_cast<const char*>(packet->data), packet->dataLength}});
                break;
            }
            default: break;
        }
    }
    ++state_->metrics.cappedPumps;
}

bool Transport::Send(ConnectionId connection, Lane lane, std::string_view wire)
{
    if (wire.empty() || wire.size() > maximumPacketBytes) return false;
    for (const auto& [peer, id] : state_->connections) {
        if (id != connection) continue;
        if (peer->reliableDataInTransit > maximumPendingBytes) break;
        const auto flags = lane == Lane::control ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
        auto* packet = enet_packet_create(wire.data(), wire.size(), static_cast<enet_uint32>(flags));
        if (packet == nullptr) break;
        if (enet_peer_send(peer, lane == Lane::control ? 0 : 1, packet) != 0) {
            enet_packet_destroy(packet);
            break;
        }
        ++state_->metrics.sentPackets;
        state_->metrics.sentBytes += wire.size();
        return true;
    }
    ++state_->metrics.droppedPackets;
    return false;
}

void Transport::Disconnect(ConnectionId connection, bool drain)
{
    for (auto iter = state_->connections.begin(); iter != state_->connections.end(); ++iter) {
        if (iter->second != connection) continue;
        if (drain) {
            enet_peer_disconnect_later(iter->first, 0);
        } else {
            enet_peer_disconnect_now(iter->first, 0);
            state_->connections.erase(iter);
        }
        return;
    }
}

void Transport::Flush() { if (state_->host != nullptr) enet_host_flush(state_->host); }
int Transport::Port() const { return state_->host == nullptr ? 0 : state_->host->address.port; }
Transport::Metrics Transport::GetMetrics() const { return state_->metrics; }
} // namespace ce::gameplay
