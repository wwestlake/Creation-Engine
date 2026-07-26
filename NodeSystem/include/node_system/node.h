#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "node_system/pin.h"

namespace ce::node_system {

using NodeId = std::uint64_t;

// The authoring domain a node belongs to. This is metadata for the editor
// (palette grouping, icon, preview behavior) — the graph/connection/type
// rules themselves are identical across domains, per section 4.1: one
// editor, one language, every domain.
enum class Domain {
    Core,
    Animation,
    Material,
    Event,
    Audio,
};

class Node {
public:
    Node(NodeId id, std::string typeName, Domain domain);

    NodeId Id() const { return id_; }
    const std::string& TypeName() const { return typeName_; }
    Domain NodeDomain() const { return domain_; }

    PinId AddInput(const std::string& name, PinTypeDesc type);
    PinId AddOutput(const std::string& name, PinTypeDesc type);

    const std::vector<Pin>& Inputs() const { return inputs_; }
    const std::vector<Pin>& Outputs() const { return outputs_; }

    const Pin* FindPin(PinId pinId) const;

private:
    NodeId id_;
    std::string typeName_;
    Domain domain_;
    std::vector<Pin> inputs_;
    std::vector<Pin> outputs_;
    PinId nextPinId_ = 1;
};

} // namespace ce::node_system
