#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "node_system/graph.h"
#include "node_system/node.h"
#include "node_system/pin.h"

namespace ce::node_system {

// One input or output pin's fixed shape, as promised by a registered
// node type -- name, type, and (for inputs) the default value an
// unconnected wire evaluates to.
struct PinSignature {
    std::string name;
    PinTypeDesc type;
    PinDefaultValue defaultValue;
};

// GS8: the fix for Node::TypeName() being a completely free-form,
// unvalidated std::string today (nothing currently stops
// graph.AddNode("totally_made_up", Domain::Core) from producing a node
// with whatever pins the caller happens to add by hand, with no
// guarantee two nodes of the "same" type actually have the same
// shape). A NodeTypeDescriptor is the authoritative pin signature for
// one type name; AddRegisteredNode (below) is what actually constructs
// a node guaranteed to match it. The real v1 Core/Event node catalog
// (OnStart, OnTick, Branch, ...) is GS9 scope, not built here --
// GS8 is just this registry mechanism itself.
struct NodeTypeDescriptor {
    std::string typeName;
    Domain domain = Domain::Core;
    std::vector<PinSignature> inputs;
    std::vector<PinSignature> outputs;
};

class NodeTypeRegistry {
public:
    // Overwrites any existing registration under the same type name --
    // deliberately permissive (no "already registered" error) so a
    // future hot-reload of the node catalog itself can simply
    // re-register.
    void Register(NodeTypeDescriptor descriptor);

    const NodeTypeDescriptor* Find(const std::string& typeName) const;
    const std::unordered_map<std::string, NodeTypeDescriptor>& Types() const { return types_; }

private:
    std::unordered_map<std::string, NodeTypeDescriptor> types_;
};

// Constructs a node in `graph` from `registry`'s descriptor for
// `typeName`, auto-populating every pin exactly as the descriptor
// specifies -- the actual mechanism that makes a registered type name
// mean something, instead of AddNode's existing free-form
// "typeName plus whatever pins the caller separately adds by hand."
// Returns nullptr (and fills `errorOut`, if given) if `typeName` isn't
// registered. Graph::AddNode itself is untouched and still available
// for ad hoc/dynamically-shaped nodes (e.g. a future CallFunction node
// whose pins depend on the target function's signature, not a fixed
// registry entry) -- this is an additive convenience, not a
// replacement.
Node* AddRegisteredNode(Graph& graph, const NodeTypeRegistry& registry, const std::string& typeName,
                         std::string* errorOut = nullptr);

// Checks every node in `graph` against `registry`: the type name must
// be registered, and the node's actual pins (name, type, and for inputs
// the default value, in order) must exactly match the descriptor.
// Nodes built via AddRegisteredNode always pass trivially; this exists
// to catch drift -- a node hand-built via the raw AddNode/AddInput/
// AddOutput API, or loaded from a .celg file saved before a type's
// registered signature changed. Returns true (with `errorsOut` left
// empty, if given) iff every node passes.
bool ValidateAgainstRegistry(const Graph& graph, const NodeTypeRegistry& registry,
                              std::vector<std::string>* errorsOut = nullptr);

} // namespace ce::node_system
