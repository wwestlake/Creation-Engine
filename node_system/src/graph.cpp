#include "node_system/graph.h"

#include <algorithm>

namespace ce::node_system {

Graph::Graph(std::string name) : name_(std::move(name)) {}

Node& Graph::AddNode(std::string typeName, Domain domain) {
    NodeId id = nextNodeId_++;
    auto node = std::make_unique<Node>(id, std::move(typeName), domain);
    Node& ref = *node;
    nodes_.emplace(id, std::move(node));
    return ref;
}

bool Graph::RemoveNode(NodeId id) {
    if (nodes_.erase(id) == 0) {
        return false;
    }
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                        [id](const Connection& c) { return c.fromNode == id || c.toNode == id; }),
        connections_.end());
    return true;
}

Node* Graph::FindNode(NodeId id) {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}

const Node* Graph::FindNode(NodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}

std::optional<ConnectionId> Graph::Connect(NodeId fromNode, PinId fromPin, NodeId toNode, PinId toPin,
                                            ConnectError* outError) {
    auto setError = [&](ConnectError err) {
        if (outError) *outError = err;
        return std::nullopt;
    };

    Node* from = FindNode(fromNode);
    Node* to = FindNode(toNode);
    if (!from || !to) {
        return setError(ConnectError::UnknownNode);
    }

    const Pin* outPin = from->FindPin(fromPin);
    const Pin* inPin = to->FindPin(toPin);
    if (!outPin || !inPin) {
        return setError(ConnectError::UnknownPin);
    }

    if (outPin->isInput || !inPin->isInput) {
        return setError(ConnectError::WrongPinDirection);
    }

    if (!IsConnectionCompatible(outPin->type, inPin->type)) {
        return setError(ConnectError::IncompatibleTypes);
    }

    ConnectionId id = nextConnectionId_++;
    connections_.push_back(Connection{id, fromNode, fromPin, toNode, toPin});
    return id;
}

bool Graph::Disconnect(ConnectionId id) {
    auto it = std::find_if(connections_.begin(), connections_.end(),
                            [id](const Connection& c) { return c.id == id; });
    if (it == connections_.end()) {
        return false;
    }
    connections_.erase(it);
    return true;
}

} // namespace ce::node_system
