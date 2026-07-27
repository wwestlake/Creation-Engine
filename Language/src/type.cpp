#include "lang/type.h"

namespace ce::lang {

const char* ToString(Type type) {
    switch (type) {
        case Type::Void: return "void";
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Bool: return "bool";
        case Type::Vec3: return "vec3";
        case Type::Entity: return "entity";
        case Type::String: return "string";
        case Type::Unknown: return "<error>";
    }
    return "<error>";
}

Type ParseTypeName(const std::string& name) {
    if (name == "int") return Type::Int;
    if (name == "float") return Type::Float;
    if (name == "bool") return Type::Bool;
    if (name == "vec3") return Type::Vec3;
    if (name == "entity") return Type::Entity;
    return Type::Unknown;
}

} // namespace ce::lang
