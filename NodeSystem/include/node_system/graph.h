#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "node_system/node.h"

namespace ce::node_system {

using ConnectionId = std::uint64_t;

// The compiler backend selected for a graph asset. Behavior graphs compile
// to FRust, material graphs compile to shader code, and dataflow graphs can
// target non-rendering evaluators without changing their authored structure.
enum class GraphTarget {
    Behavior,
    Material,
    Dataflow,
};

struct Connection {
    ConnectionId id = 0;
    NodeId fromNode = 0;
    PinId fromPin = 0;
    NodeId toNode = 0;
    PinId toPin = 0;

    bool operator==(const Connection&) const = default;
};

// Why a connection was rejected, surfaced to the editor so a bad wire is
// flagged at author time rather than failing silently or at runtime
// (spec section 4.1).
enum class ConnectError {
    UnknownNode,
    UnknownPin,
    WrongPinDirection,
    IncompatibleTypes,
};

// A single node graph: the unit a designer edits, tests in-editor, and
// eventually compiles. Domain-agnostic — an animation blend tree, a
// material graph, and an event/rule graph are all a Graph, distinguished
// by the Domain of the nodes inside it.
class Graph {
public:
    explicit Graph(std::string name, GraphTarget target = GraphTarget::Behavior);

    Node& AddNode(std::string typeName, Domain domain);
    bool RemoveNode(NodeId id);
    Node* FindNode(NodeId id);
    const Node* FindNode(NodeId id) const;
    const std::unordered_map<NodeId, std::unique_ptr<Node>>& Nodes() const { return nodes_; }

    // Restores a graph from an .frgraph file with its original node id
    // intact, so a
    // save/load/save round trip is byte-identical. Also advances the
    // internal next-auto-assigned-id counter past `id`, mirroring
    // Node::AddInputWithId/AddOutputWithId's contract exactly. Rejects
    // (returns nullptr) if `id` is already in use.
    Node* AddNodeWithId(NodeId id, std::string typeName, Domain domain);

    // Attempts to wire fromNode:fromPin (must be an output) to
    // toNode:toPin (must be an input). Returns the new connection's id on
    // success, or the reason for rejection.
    std::optional<ConnectionId> Connect(NodeId fromNode, PinId fromPin, NodeId toNode, PinId toPin,
                                         ConnectError* outError = nullptr);

    // Same validation as Connect, but for reconstructing a graph from a
    // .frgraph file with the connection's original id intact -- same
    // reasoning as AddNodeWithId. Rejects (returns nullopt with
    // *outError left untouched) if `id` is already in use, in addition
    // to every rejection reason Connect itself checks.
    std::optional<ConnectionId> ConnectWithId(ConnectionId id, NodeId fromNode, PinId fromPin, NodeId toNode,
                                               PinId toPin, ConnectError* outError = nullptr);

    bool Disconnect(ConnectionId id);

    const std::vector<Connection>& Connections() const { return connections_; }
    const std::string& Name() const { return name_; }
    GraphTarget Target() const { return target_; }
    void SetTarget(GraphTarget target) { target_ = target; }

private:
    std::optional<ConnectionId> ConnectInternal(std::optional<ConnectionId> explicitId, NodeId fromNode,
                                                 PinId fromPin, NodeId toNode, PinId toPin, ConnectError* outError);

    std::string name_;
    GraphTarget target_ = GraphTarget::Behavior;
    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes_;
    std::vector<Connection> connections_;
    NodeId nextNodeId_ = 1;
    ConnectionId nextConnectionId_ = 1;
};

} // namespace ce::node_system
