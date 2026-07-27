#pragma once

#include <cstdint>
#include <string>
#include <variant>

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
//
// GS9: Entity added for the node catalog's OnStart/OnTick "self" output,
// Spawn's result, and Get/SetPosition's entity input -- CEL's
// ce::lang::Type::Entity (an opaque i64 handle, no arithmetic, no
// literal syntax) had no DataType counterpart until a real node needed
// to carry one across a wire.
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
    Entity,
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

// A plain 3-float literal for a Vec3-typed pin's default -- deliberately
// its own type here rather than a reuse of e.g. ce::engine::Vec3:
// NodeSystem must stay backend-agnostic (no EngineCore/Language
// dependency at all -- see this module's own CMakeLists.txt, which
// links nothing), the same reason codegen itself lives in
// Language/src/nodegen, not here.
struct Vec3Default {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    bool operator==(const Vec3Default&) const = default;
};

// GS8: the value an unconnected Data input pin evaluates to -- needed
// for codegen (a future consumer in Language/src/nodegen, not built
// yet) to emit a literal instead of failing when an input has no wire.
// std::monostate means "no default" -- legitimate for an Exec pin (a
// default value is meaningless there) or a Data input a node type
// requires always be wired. Not meant to be used on output pins (an
// output's value is always computed), though nothing enforces that here
// -- Pin is a single shared struct for both directions, same as before
// this field existed.
using PinDefaultValue = std::variant<std::monostate, float, std::int64_t, bool, std::string, Vec3Default>;

struct Pin {
    PinId id = 0;
    std::string name;
    PinTypeDesc type;
    bool isInput = true;
    PinDefaultValue defaultValue;

    bool operator==(const Pin&) const = default;
};

} // namespace ce::node_system
