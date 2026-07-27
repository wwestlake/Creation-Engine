#include "node_system/celg_serialization.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace ce::node_system {

namespace {

std::string DomainToString(Domain d) {
    switch (d) {
        case Domain::Core: return "core";
        case Domain::Animation: return "animation";
        case Domain::Material: return "material";
        case Domain::Event: return "event";
        case Domain::Audio: return "audio";
    }
    return "core";
}

std::optional<Domain> DomainFromString(const std::string& s) {
    if (s == "core") return Domain::Core;
    if (s == "animation") return Domain::Animation;
    if (s == "material") return Domain::Material;
    if (s == "event") return Domain::Event;
    if (s == "audio") return Domain::Audio;
    return std::nullopt;
}

std::string DataTypeToString(DataType t) {
    switch (t) {
        case DataType::Float: return "float";
        case DataType::Vec2: return "vec2";
        case DataType::Vec3: return "vec3";
        case DataType::Vec4: return "vec4";
        case DataType::Color: return "color";
        case DataType::Bool: return "bool";
        case DataType::Int: return "int";
        case DataType::String: return "string";
        case DataType::Transform: return "transform";
        case DataType::BoneTransform: return "bonetransform";
        case DataType::Texture: return "texture";
        case DataType::AudioSignal: return "audiosignal";
        case DataType::Entity: return "entity";
    }
    return "float";
}

std::optional<DataType> DataTypeFromString(const std::string& s) {
    static const std::unordered_map<std::string, DataType> table = {
        { "float", DataType::Float },     { "vec2", DataType::Vec2 },       { "vec3", DataType::Vec3 },
        { "vec4", DataType::Vec4 },       { "color", DataType::Color },     { "bool", DataType::Bool },
        { "int", DataType::Int },         { "string", DataType::String },   { "transform", DataType::Transform },
        { "bonetransform", DataType::BoneTransform }, { "texture", DataType::Texture },
        { "audiosignal", DataType::AudioSignal }, { "entity", DataType::Entity },
    };
    const auto it = table.find(s);
    return it == table.end() ? std::nullopt : std::optional<DataType>(it->second);
}

std::string SerializeDefaultValue(const PinDefaultValue& value) {
    return std::visit(
        [](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "";
            } else if constexpr (std::is_same_v<T, float>) {
                std::ostringstream os;
                os << " default float " << v;
                return os.str();
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return " default int " + std::to_string(v);
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::string(" default bool ") + (v ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::string>) {
                // Rest-of-line -- must be the LAST field on its pin line (enforced by
                // being the only default kind that isn't followed by anything else).
                return " default string " + v;
            } else if constexpr (std::is_same_v<T, Vec3Default>) {
                std::ostringstream os;
                os << " default vec3 " << v.x << " " << v.y << " " << v.z;
                return os.str();
            }
        },
        value);
}

std::string SerializePinLine(NodeId nodeId, const Pin& pin) {
    std::ostringstream os;
    os << std::setprecision(9);
    os << "pin " << nodeId << " " << (pin.isInput ? "in" : "out") << " " << pin.id << " " << pin.name << " ";
    if (pin.type.kind == PinKind::Exec) {
        os << "exec";
    } else {
        os << "data " << DataTypeToString(pin.type.dataType);
    }
    os << SerializeDefaultValue(pin.defaultValue);
    return os.str();
}

// Strips a trailing '\r' (files saved/edited with CRLF line endings)
// and returns the leading-whitespace-trimmed start index, or
// std::string::npos for a blank line.
std::size_t FirstNonBlank(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line.find_first_not_of(" \t");
}

} // namespace

std::string SerializeGraph(const Graph& graph) {
    std::ostringstream os;
    os << std::setprecision(9);
    os << "celg 1\n";
    os << "graph " << graph.Name() << "\n";

    std::vector<NodeId> nodeIds;
    nodeIds.reserve(graph.Nodes().size());
    for (const auto& [id, node] : graph.Nodes()) {
        nodeIds.push_back(id);
    }
    std::sort(nodeIds.begin(), nodeIds.end());

    for (NodeId id : nodeIds) {
        const Node* node = graph.FindNode(id);
        os << "node " << id << " " << node->TypeName() << " " << DomainToString(node->NodeDomain()) << " "
           << node->EditorX() << " " << node->EditorY() << "\n";
        for (const Pin& pin : node->Inputs()) {
            os << SerializePinLine(id, pin) << "\n";
        }
        for (const Pin& pin : node->Outputs()) {
            os << SerializePinLine(id, pin) << "\n";
        }
    }

    std::vector<Connection> connections = graph.Connections();
    std::sort(connections.begin(), connections.end(), [](const Connection& a, const Connection& b) { return a.id < b.id; });
    for (const Connection& c : connections) {
        os << "connection " << c.id << " " << c.fromNode << " " << c.fromPin << " " << c.toNode << " " << c.toPin
           << "\n";
    }

    return os.str();
}

std::unique_ptr<Graph> DeserializeGraph(const std::string& text, std::string& errorOut) {
    std::istringstream lines(text);
    std::string line;
    int lineNo = 0;
    bool sawHeader = false;
    std::unique_ptr<Graph> graph;

    auto fail = [&](const std::string& msg) -> std::unique_ptr<Graph> {
        errorOut = "line " + std::to_string(lineNo) + ": " + msg;
        return nullptr;
    };

    while (std::getline(lines, line)) {
        ++lineNo;
        const std::size_t contentStart = FirstNonBlank(line);
        if (contentStart == std::string::npos || line[contentStart] == '#') {
            continue; // blank line or comment.
        }

        std::istringstream tok(line);
        std::string keyword;
        tok >> keyword;

        if (keyword == "celg") {
            int version = 0;
            if (!(tok >> version) || version != 1) {
                return fail("expected 'celg 1' header");
            }
            sawHeader = true;
        } else if (keyword == "graph") {
            if (!sawHeader) {
                return fail("'graph' line before 'celg' header");
            }
            std::string name;
            std::getline(tok, name);
            if (!name.empty() && name.front() == ' ') {
                name.erase(0, 1);
            }
            graph = std::make_unique<Graph>(name);
        } else if (keyword == "node") {
            if (!graph) {
                return fail("'node' line before 'graph' line");
            }
            NodeId id = 0;
            std::string typeName, domainStr;
            float x = 0.0f, y = 0.0f;
            if (!(tok >> id >> typeName >> domainStr >> x >> y)) {
                return fail("malformed 'node' line (expected: node <id> <typeName> <domain> <editorX> <editorY>)");
            }
            const auto domain = DomainFromString(domainStr);
            if (!domain) {
                return fail("unknown domain '" + domainStr + "'");
            }
            Node* node = graph->AddNodeWithId(id, typeName, *domain);
            if (node == nullptr) {
                return fail("duplicate node id " + std::to_string(id));
            }
            node->SetEditorPosition(x, y);
        } else if (keyword == "pin") {
            if (!graph) {
                return fail("'pin' line before 'graph' line");
            }
            NodeId nodeId = 0;
            std::string direction;
            PinId pinId = 0;
            std::string name, kindStr;
            if (!(tok >> nodeId >> direction >> pinId >> name >> kindStr)) {
                return fail("malformed 'pin' line (expected: pin <nodeId> in|out <pinId> <name> exec|data ...)");
            }
            Node* node = graph->FindNode(nodeId);
            if (node == nullptr) {
                return fail("'pin' references unknown node " + std::to_string(nodeId));
            }
            if (node->FindPin(pinId) != nullptr) {
                return fail("duplicate pin id " + std::to_string(pinId) + " on node " + std::to_string(nodeId));
            }

            bool isInput;
            if (direction == "in") {
                isInput = true;
            } else if (direction == "out") {
                isInput = false;
            } else {
                return fail("unknown pin direction '" + direction + "' (expected 'in' or 'out')");
            }

            PinTypeDesc type;
            if (kindStr == "exec") {
                type.kind = PinKind::Exec;
            } else if (kindStr == "data") {
                type.kind = PinKind::Data;
                std::string dataTypeStr;
                if (!(tok >> dataTypeStr)) {
                    return fail("'pin ... data' line missing its data type");
                }
                const auto dataType = DataTypeFromString(dataTypeStr);
                if (!dataType) {
                    return fail("unknown data type '" + dataTypeStr + "'");
                }
                type.dataType = *dataType;
            } else {
                return fail("unknown pin kind '" + kindStr + "' (expected 'exec' or 'data')");
            }

            PinDefaultValue defaultValue;
            std::string maybeDefaultKeyword;
            if (tok >> maybeDefaultKeyword) {
                if (maybeDefaultKeyword != "default") {
                    return fail("unexpected trailing token '" + maybeDefaultKeyword + "' on 'pin' line");
                }
                std::string valueKind;
                if (!(tok >> valueKind)) {
                    return fail("'default' missing its value kind");
                }
                if (valueKind == "float") {
                    float v = 0.0f;
                    if (!(tok >> v)) return fail("malformed float default value");
                    defaultValue = v;
                } else if (valueKind == "int") {
                    std::int64_t v = 0;
                    if (!(tok >> v)) return fail("malformed int default value");
                    defaultValue = v;
                } else if (valueKind == "bool") {
                    std::string b;
                    if (!(tok >> b)) return fail("malformed bool default value");
                    if (b != "true" && b != "false") return fail("bool default must be 'true' or 'false'");
                    defaultValue = (b == "true");
                } else if (valueKind == "vec3") {
                    Vec3Default v;
                    if (!(tok >> v.x >> v.y >> v.z)) return fail("malformed vec3 default value");
                    defaultValue = v;
                } else if (valueKind == "string") {
                    std::string rest;
                    std::getline(tok, rest);
                    if (!rest.empty() && rest.front() == ' ') {
                        rest.erase(0, 1);
                    }
                    defaultValue = rest;
                } else {
                    return fail("unknown default value kind '" + valueKind + "'");
                }
            }

            if (isInput) {
                node->AddInputWithId(pinId, name, type, defaultValue);
            } else {
                node->AddOutputWithId(pinId, name, type, defaultValue);
            }
        } else if (keyword == "connection") {
            if (!graph) {
                return fail("'connection' line before 'graph' line");
            }
            ConnectionId id = 0;
            NodeId fromNode = 0, toNode = 0;
            PinId fromPin = 0, toPin = 0;
            if (!(tok >> id >> fromNode >> fromPin >> toNode >> toPin)) {
                return fail("malformed 'connection' line (expected: connection <id> <fromNode> <fromPin> <toNode> <toPin>)");
            }
            ConnectError connectError = ConnectError::UnknownNode;
            const auto result = graph->ConnectWithId(id, fromNode, fromPin, toNode, toPin, &connectError);
            if (!result) {
                return fail("connection " + std::to_string(id) + " is invalid (duplicate id, unknown node/pin, "
                             "wrong pin direction, or incompatible types)");
            }
        } else {
            return fail("unknown line keyword '" + keyword + "'");
        }
    }

    if (!sawHeader) {
        return fail("missing 'celg 1' header");
    }
    if (!graph) {
        return fail("missing 'graph' line");
    }
    return graph;
}

} // namespace ce::node_system
