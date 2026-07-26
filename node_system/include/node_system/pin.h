#pragma once

#include <cstdint>
#include <string>

namespace ce::node_system {

// A pin either carries continuous/discrete data (dataflow graphs: materials,
// animation blending, audio) or is an execution/trigger pin (control-flow
// graphs: event/rule graphs). Both kinds coexist in the same graph so a
// single node system serves every authoring domain (spec section 4.1).
enum class PinKind {
    Data,
    Exec,
};

// Data pin value types. Kept intentionally small; grows only when a domain
// actually needs a new type, not speculatively.
enum class DataType {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Bool,
    Int,
    String,
    Transform,
    BoneTransform,
    Texture,
    AudioSignal,
};

struct PinTypeDesc {
    PinKind kind = PinKind::Data;
    DataType dataType = DataType::Float;

    bool operator==(const PinTypeDesc& other) const {
        return kind == other.kind && dataType == other.dataType;
    }
};

// Whether a connection from `output` to `input` is legal. Exec pins only
// connect to Exec pins; Data pins only connect to Data pins of the same
// DataType. This is the author-time type check referenced in section 4.1 —
// a bone-transform output cannot be wired into a color input, and an
// event-trigger pin cannot be wired into a numeric data pin.
inline bool IsConnectionCompatible(const PinTypeDesc& output, const PinTypeDesc& input) {
    if (output.kind != input.kind) {
        return false;
    }
    if (output.kind == PinKind::Data) {
        return output.dataType == input.dataType;
    }
    return true; // Exec -> Exec is always compatible.
}

using PinId = std::uint64_t;

struct Pin {
    PinId id = 0;
    std::string name;
    PinTypeDesc type;
    bool isInput = true;
};

} // namespace ce::node_system
