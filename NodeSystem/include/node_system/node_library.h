#pragma once

#include <map>
#include <string>
#include <vector>

#include "node_system/type_registry.h"

namespace ce::node_system {

// A plugin-owned, discoverable node catalog. The editor consumes this
// metadata for its palette and documentation; the compiler uses the FRust
// entry point and declared capabilities when it emits a behavior pod.
struct NodeLibraryDescriptor {
    std::string id;
    std::string displayName;
    std::string description;
    GraphTarget target = GraphTarget::Behavior;
    // Compiler-provided FRust modules a generated behavior root imports.
    std::vector<std::string> frustSourceModules;
    std::vector<NodeTypeDescriptor> nodeTypes;
};

class NodeLibraryRegistry {
public:
    // Rejects duplicate library ids and node type names. A node type has one
    // owning plugin, so removing a plugin cannot leave ambiguous graph nodes.
    bool Register(NodeLibraryDescriptor descriptor, std::string* errorOut = nullptr);

    const NodeLibraryDescriptor* FindLibrary(const std::string& id) const;
    const NodeTypeDescriptor* FindNodeType(const std::string& typeName) const;
    Node* AddNode(Graph& graph, const std::string& typeName, std::string* errorOut = nullptr) const;

    const NodeTypeRegistry& TypeRegistry() const { return nodeTypes_; }
    const std::map<std::string, NodeLibraryDescriptor, std::less<>>& Libraries() const { return libraries_; }

private:
    std::map<std::string, NodeLibraryDescriptor, std::less<>> libraries_;
    std::map<std::string, std::string, std::less<>> typeOwners_;
    NodeTypeRegistry nodeTypes_;
};

} // namespace ce::node_system
