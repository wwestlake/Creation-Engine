#pragma once

#include <algorithm>
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

    PinId AddInput(const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue = {});
    PinId AddOutput(const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue = {});

    // GS8: for reconstructing a node from a .celg file (see
    // celg_serialization.h) with its pins' ORIGINAL ids intact, so a
    // save/load/save round trip is byte-identical rather than
    // renumbering everything. Also advances the internal
    // next-auto-assigned-id counter past `id` if needed, so a later
    // plain AddInput/AddOutput call on this node can never collide with
    // an id restored this way. Callers are expected to have already
    // validated `id` isn't already in use on this node (see
    // DeserializeGraph's own validation) -- this does not check.
    PinId AddInputWithId(PinId id, const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue = {});
    PinId AddOutputWithId(PinId id, const std::string& name, PinTypeDesc type, PinDefaultValue defaultValue = {});

    const std::vector<Pin>& Inputs() const { return inputs_; }
    const std::vector<Pin>& Outputs() const { return outputs_; }

    const Pin* FindPin(PinId pinId) const;
    Pin* FindPin(PinId pinId);

    // GS8: canvas position for the node editor (GS10) -- purely
    // presentational, never consulted by graph structure/validation/
    // codegen. Persisted through .celg so a saved layout survives a
    // reload.
    float EditorX() const { return editorX_; }
    float EditorY() const { return editorY_; }
    void SetEditorPosition(float x, float y) {
        editorX_ = x;
        editorY_ = y;
    }

private:
    NodeId id_;
    std::string typeName_;
    Domain domain_;
    std::vector<Pin> inputs_;
    std::vector<Pin> outputs_;
    PinId nextPinId_ = 1;
    float editorX_ = 0.0f;
    float editorY_ = 0.0f;
};

} // namespace ce::node_system
