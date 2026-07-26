#include "node_system/node.h"

namespace ce::node_system {

Node::Node(NodeId id, std::string typeName, Domain domain)
    : id_(id), typeName_(std::move(typeName)), domain_(domain) {}

PinId Node::AddInput(const std::string& name, PinTypeDesc type) {
    PinId id = nextPinId_++;
    inputs_.push_back(Pin{id, name, type, /*isInput=*/true});
    return id;
}

PinId Node::AddOutput(const std::string& name, PinTypeDesc type) {
    PinId id = nextPinId_++;
    outputs_.push_back(Pin{id, name, type, /*isInput=*/false});
    return id;
}

const Pin* Node::FindPin(PinId pinId) const {
    for (const auto& pin : inputs_) {
        if (pin.id == pinId) return &pin;
    }
    for (const auto& pin : outputs_) {
        if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

} // namespace ce::node_system
