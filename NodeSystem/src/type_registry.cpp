#include "node_system/type_registry.h"

#include <sstream>

namespace ce::node_system {

void NodeTypeRegistry::Register(NodeTypeDescriptor descriptor) {
    const std::string key = descriptor.typeName;
    types_[key] = std::move(descriptor);
}

const NodeTypeDescriptor* NodeTypeRegistry::Find(const std::string& typeName) const {
    const auto it = types_.find(typeName);
    return it == types_.end() ? nullptr : &it->second;
}

Node* AddRegisteredNode(Graph& graph, const NodeTypeRegistry& registry, const std::string& typeName,
                         std::string* errorOut) {
    const NodeTypeDescriptor* descriptor = registry.Find(typeName);
    if (descriptor == nullptr) {
        if (errorOut) {
            *errorOut = "unregistered node type '" + typeName + "'";
        }
        return nullptr;
    }

    Node& node = graph.AddNode(typeName, descriptor->domain);
    for (const PinSignature& sig : descriptor->inputs) {
        node.AddInput(sig.name, sig.type, sig.defaultValue);
    }
    for (const PinSignature& sig : descriptor->outputs) {
        node.AddOutput(sig.name, sig.type, sig.defaultValue);
    }
    return &node;
}

namespace {

bool PinsMatchSignature(const std::vector<Pin>& pins, const std::vector<PinSignature>& signatures,
                         const std::string& nodeTypeName, const char* direction, std::vector<std::string>* errorsOut) {
    bool ok = true;
    if (pins.size() != signatures.size()) {
        ok = false;
        if (errorsOut) {
            std::ostringstream msg;
            msg << "node type '" << nodeTypeName << "': expected " << signatures.size() << " " << direction
                << " pin(s), found " << pins.size();
            errorsOut->push_back(msg.str());
        }
    }
    const std::size_t count = std::min(pins.size(), signatures.size());
    for (std::size_t i = 0; i < count; ++i) {
        // Name and type only -- NOT defaultValue, which a node instance
        // is free to override (see PinSignature/ValidateAgainstRegistry's
        // own comments on why).
        if (pins[i].name != signatures[i].name || !(pins[i].type == signatures[i].type)) {
            ok = false;
            if (errorsOut) {
                std::ostringstream msg;
                msg << "node type '" << nodeTypeName << "': " << direction << " pin " << i << " ('" << pins[i].name
                    << "') doesn't match its registered signature ('" << signatures[i].name << "')";
                errorsOut->push_back(msg.str());
            }
        }
    }
    return ok;
}

} // namespace

bool ValidateAgainstRegistry(const Graph& graph, const NodeTypeRegistry& registry,
                              std::vector<std::string>* errorsOut) {
    bool ok = true;
    for (const auto& [id, node] : graph.Nodes()) {
        const NodeTypeDescriptor* descriptor = registry.Find(node->TypeName());
        if (descriptor == nullptr) {
            ok = false;
            if (errorsOut) {
                errorsOut->push_back("node " + std::to_string(id) + ": unregistered type '" + node->TypeName() + "'");
            }
            continue;
        }
        ok = PinsMatchSignature(node->Inputs(), descriptor->inputs, node->TypeName(), "input", errorsOut) && ok;
        ok = PinsMatchSignature(node->Outputs(), descriptor->outputs, node->TypeName(), "output", errorsOut) && ok;
    }
    return ok;
}

} // namespace ce::node_system
