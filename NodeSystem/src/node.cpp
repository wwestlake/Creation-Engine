#include "node_system/node.h"

namespace ce::node_system {

Node::Node(NodeId id, std::string typeName, Domain domain)
    : id_(id), typeName_(std::move(typeName)), domain_(domain) {}

PinId Node::AddInput(const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue) {
    PinId id = nextPinId_++;
    inputs_.push_back(Pin{ id, name, type, /*isInput=*/true, std::move(defaultValue) });
    return id;
}

PinId Node::AddOutput(const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue) {
    PinId id = nextPinId_++;
    outputs_.push_back(Pin{ id, name, type, /*isInput=*/false, std::move(defaultValue) });
    return id;
}

PinId Node::AddInputWithId(PinId id, const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue) {
    inputs_.push_back(Pin{ id, name, type, /*isInput=*/true, std::move(defaultValue) });
    nextPinId_ = std::max(nextPinId_, id + 1);
    return id;
}

PinId Node::AddOutputWithId(PinId id, const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue) {
    outputs_.push_back(Pin{ id, name, type, /*isInput=*/false, std::move(defaultValue) });
    nextPinId_ = std::max(nextPinId_, id + 1);
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

Pin* Node::FindPin(PinId pinId) {
    for (auto& pin : inputs_) {
        if (pin.id == pinId) return &pin;
    }
    for (auto& pin : outputs_) {
        if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

} // namespace ce::node_system
