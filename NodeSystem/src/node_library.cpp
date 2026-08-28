#include "node_system/node_library.h"

namespace ce::node_system {

bool NodeLibraryRegistry::Register(NodeLibraryDescriptor descriptor, std::string* errorOut) {
    if (descriptor.id.empty()) {
        if (errorOut) *errorOut = "node library id cannot be empty";
        return false;
    }
    if (libraries_.contains(descriptor.id)) {
        if (errorOut) *errorOut = "node library '" + descriptor.id + "' is already registered";
        return false;
    }

    for (const NodeTypeDescriptor& node : descriptor.nodeTypes) {
        if (node.typeName.empty()) {
            if (errorOut) *errorOut = "node library '" + descriptor.id + "' declares a node without a type name";
            return false;
        }
        if (typeOwners_.contains(node.typeName)) {
            if (errorOut) {
                *errorOut = "node type '" + node.typeName + "' is already owned by library '" +
                            typeOwners_.at(node.typeName) + "'";
            }
            return false;
        }
    }

    for (const NodeTypeDescriptor& node : descriptor.nodeTypes) {
        typeOwners_.emplace(node.typeName, descriptor.id);
        nodeTypes_.Register(node);
    }
    libraries_.emplace(descriptor.id, std::move(descriptor));
    return true;
}

const NodeLibraryDescriptor* NodeLibraryRegistry::FindLibrary(const std::string& id) const {
    const auto found = libraries_.find(id);
    return found == libraries_.end() ? nullptr : &found->second;
}

const NodeTypeDescriptor* NodeLibraryRegistry::FindNodeType(const std::string& typeName) const {
    return nodeTypes_.Find(typeName);
}

Node* NodeLibraryRegistry::AddNode(Graph& graph, const std::string& typeName, std::string* errorOut) const {
    return AddRegisteredNode(graph, nodeTypes_, typeName, errorOut);
}

} // namespace ce::node_system
