#pragma once

#include <string>

namespace ce::lang {

// CEL v1's deliberately closed type system -- every one of these maps to
// exactly one LLVM type with no lowering ambiguity (see GS4). String is
// NOT a first-class value type: it exists only as a literal, usable
// solely as a direct argument to an intrinsic call (enforced in
// sema.cpp) -- there is no way to declare a variable of type String, and
// no operator accepts it. Unknown is an error-recovery sentinel: sema
// assigns it after a type error so downstream checks on the same
// expression don't cascade into a wall of duplicate diagnostics.
enum class Type { Void, Int, Float, Bool, Vec3, Entity, String, Unknown };

const char* ToString(Type type);

// Resolves a type name as written in source (after `:` or `->`) to a
// Type -- Type::Unknown if it isn't one of the known names. String is
// deliberately not resolvable here: it can only ever appear as a
// literal's inferred type, never as a declared one.
Type ParseTypeName(const std::string& name);

} // namespace ce::lang
