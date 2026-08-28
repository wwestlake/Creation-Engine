#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace ce::node_system {

// A pin carries data, imperative execution, or a typed stream. All three
// coexist so one graph system can express gameplay, tools, and materials.
enum class PinKind {
    Data,
    Exec,
    Stream,
};

// Data pin value types. Kept intentionally small; grows only when a domain
// actually needs a new type, not speculatively.
//
// Entity supports graph connections that refer to an Engine object.
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

// Exec pins connect only to Exec pins. Data and Stream pins connect only to
// the same kind carrying the same payload type.
inline bool IsConnectionCompatible(const PinTypeDesc& output, const PinTypeDesc& input) {
    if (output.kind != input.kind) {
        return false;
    }
    if (output.kind == PinKind::Data || output.kind == PinKind::Stream) {
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
