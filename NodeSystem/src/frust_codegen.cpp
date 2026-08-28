#include "node_system/frust_codegen.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "node_system/graph_analysis.h"

namespace ce::node_system {
namespace {

std::string FrustType(DataType type) {
    switch (type) {
    case DataType::Float: return "f64";
    case DataType::Bool: return "bool";
    case DataType::Int: return "i64";
    case DataType::String: return "String";
    default: return {};
    }
}

std::string EscapeString(const std::string& value) {
    std::string result;
    for (const char c : value) {
        if (c == '\\' || c == '\"') result += '\\';
        result += c;
    }
    return result;
}

std::string DefaultExpression(const Pin& pin) {
    if (const auto* value = std::get_if<std::int64_t>(&pin.defaultValue)) return std::to_string(*value);
    if (const auto* value = std::get_if<float>(&pin.defaultValue)) return std::to_string(*value);
    if (const auto* value = std::get_if<bool>(&pin.defaultValue)) return *value ? "true" : "false";
    if (const auto* value = std::get_if<std::string>(&pin.defaultValue)) return "\"" + EscapeString(*value) + "\"";
    return {};
}

const Connection* InputConnection(const Graph& graph, NodeId node, PinId pin) {
    for (const Connection& connection : graph.Connections())
        if (connection.toNode == node && connection.toPin == pin) return &connection;
    return nullptr;
}

const Connection* ExecThenConnection(const Graph& graph, const Node& node) {
    const auto output = std::find_if(node.Outputs().begin(), node.Outputs().end(), [](const Pin& pin) {
        return pin.type.kind == PinKind::Exec && pin.name == "then";
    });
    if (output == node.Outputs().end()) return nullptr;
    for (const Connection& connection : graph.Connections())
        if (connection.fromNode == node.Id() && connection.fromPin == output->id) return &connection;
    return nullptr;
}

} // namespace

FrustGraphCompileResult CompileBehaviorGraphToFrust(const Graph& graph,
                                                     const NodeLibraryRegistry& libraries,
                                                     const FrustGraphCompileOptions& options) {
    FrustGraphCompileResult result;
    if (graph.Target() != GraphTarget::Behavior) {
        result.error = "only behavior graphs compile to FRust";
        return result;
    }
    if (options.functionName.empty() || options.resultNode == 0 || options.resultPin == 0) {
        result.error = "a function name and result node/pin are required";
        return result;
    }
    const ValidationResult validation = ValidateGraph(graph, &libraries.TypeRegistry());
    if (!validation.ok) {
        result.error = validation.errors.empty() ? "graph validation failed" : validation.errors.front();
        return result;
    }
    const auto order = TopologicalDataOrder(graph);
    if (!order) {
        result.error = "data dependency cycle detected";
        return result;
    }

    std::map<std::pair<NodeId, PinId>, std::string> parameterBindings;
    std::map<std::string, DataType> parameterTypes;
    for (const auto& parameter : options.parameters) {
        if (parameter.name.empty() || FrustType(parameter.type).empty() || !parameterTypes.emplace(parameter.name, parameter.type).second) {
            result.error = "graph parameters must have unique names and FRust-supported types";
            return result;
        }
    }
    for (const auto& binding : options.inputBindings) {
        if (!parameterTypes.contains(binding.parameterName) ||
            !parameterBindings.emplace(std::pair { binding.node, binding.pin }, binding.parameterName).second) {
            result.error = "each graph input binding must name one declared parameter";
            return result;
        }
    }

    std::ostringstream body;
    std::set<std::string, std::less<>> importedModules(options.sourceModules.begin(), options.sourceModules.end());
    for (const NodeId id : *order) {
        const Node* node = graph.FindNode(id);
        const NodeTypeDescriptor* type = node ? libraries.FindNodeType(node->TypeName()) : nullptr;
        if (type != nullptr && type->domain == Domain::Event) continue;
        if (!node || !type || type->domain != Domain::Core || type->outputs.size() != 1 ||
            type->outputs.front().type.kind != PinKind::Data || type->frustEntryPoint.empty()) {
            result.error = "node " + std::to_string(id) + " is not a pure single-value FRust node";
            return result;
        }
        for (const auto& [libraryId, library] : libraries.Libraries()) {
            (void) libraryId;
            if (std::any_of(library.nodeTypes.begin(), library.nodeTypes.end(), [&](const NodeTypeDescriptor& candidate) {
                    return candidate.typeName == type->typeName;
                })) {
                importedModules.insert(library.frustSourceModules.begin(), library.frustSourceModules.end());
                break;
            }
        }
        const std::string outputType = FrustType(type->outputs.front().type.dataType);
        if (outputType.empty()) {
            result.error = "node " + std::to_string(id) + " has an unsupported FRust output type";
            return result;
        }
        body << "    let n" << id << ": " << outputType << " = " << type->frustEntryPoint << "(";
        for (size_t index = 0; index < node->Inputs().size(); ++index) {
            const Pin& input = node->Inputs()[index];
            if (input.type.kind != PinKind::Data || FrustType(input.type.dataType).empty()) {
                result.error = "node " + std::to_string(id) + " has non-data or unsupported inputs";
                return result;
            }
            if (index != 0) body << ", ";
            if (const Connection* connection = InputConnection(graph, id, input.id)) {
                const Node* source = graph.FindNode(connection->fromNode);
                const Pin* output = source ? source->FindPin(connection->fromPin) : nullptr;
                if (!output || output->type.kind != PinKind::Data || output->type.dataType != input.type.dataType) {
                    result.error = "node " + std::to_string(id) + " has an invalid data input";
                    return result;
                }
                body << "n" << connection->fromNode;
            } else if (const auto bound = parameterBindings.find({ id, input.id }); bound != parameterBindings.end()) {
                if (parameterTypes.at(bound->second) != input.type.dataType) {
                    result.error = "parameter '" + bound->second + "' does not match its input pin type";
                    return result;
                }
                body << bound->second;
            } else if (const std::string fallback = DefaultExpression(input); !fallback.empty()) {
                body << fallback;
            } else {
                result.error = "node " + std::to_string(id) + " input '" + input.name + "' is unconnected and has no default";
                return result;
            }
        }
        body << ");\n";
    }

    // Exec wiring is lowered to ordinary sequential calls. Branches and loop
    // exits are intentionally rejected here: they need their own structured
    // control-flow lowering, never an interpreter or an arbitrary traversal.
    if (options.entryNode != 0) {
        std::set<NodeId> visited;
        NodeId current = options.entryNode;
        while (current != 0) {
            if (!visited.insert(current).second) {
                result.error = "callable execution cycle detected";
                return result;
            }
            const Node* node = graph.FindNode(current);
            const NodeTypeDescriptor* type = node ? libraries.FindNodeType(node->TypeName()) : nullptr;
            if (!node || !type || type->domain != Domain::Event || type->frustEntryPoint.empty()) {
                result.error = "execution entry reaches a non-callable FRust node";
                return result;
            }
            const auto execInputs = std::count_if(node->Inputs().begin(), node->Inputs().end(), [](const Pin& pin) {
                return pin.type.kind == PinKind::Exec;
            });
            if (execInputs != 1 || std::count_if(node->Outputs().begin(), node->Outputs().end(), [](const Pin& pin) {
                    return pin.type.kind == PinKind::Exec;
                }) != 1) {
                result.error = "node " + std::to_string(current) + " requires structured branch or loop lowering";
                return result;
            }
            for (const auto& [libraryId, library] : libraries.Libraries()) {
                (void) libraryId;
                if (std::any_of(library.nodeTypes.begin(), library.nodeTypes.end(), [&](const NodeTypeDescriptor& candidate) {
                        return candidate.typeName == type->typeName;
                    })) {
                    importedModules.insert(library.frustSourceModules.begin(), library.frustSourceModules.end());
                    break;
                }
            }
            body << "    " << type->frustEntryPoint << "(";
            bool first = true;
            for (const Pin& input : node->Inputs()) {
                if (input.type.kind == PinKind::Exec) continue;
                if (!first) body << ", ";
                first = false;
                if (const Connection* connection = InputConnection(graph, current, input.id)) {
                    body << "n" << connection->fromNode;
                } else if (const auto bound = parameterBindings.find({ current, input.id }); bound != parameterBindings.end()) {
                    body << bound->second;
                } else if (const std::string fallback = DefaultExpression(input); !fallback.empty()) {
                    body << fallback;
                } else {
                    result.error = "callable node " + std::to_string(current) + " input '" + input.name + "' is unconnected";
                    return result;
                }
            }
            body << ");\n";
            const Connection* next = ExecThenConnection(graph, *node);
            current = next ? next->toNode : 0;
        }
    }

    const Node* outputNode = graph.FindNode(options.resultNode);
    const Pin* outputPin = outputNode ? outputNode->FindPin(options.resultPin) : nullptr;
    if (!outputPin || outputPin->isInput || outputPin->type.kind != PinKind::Data || FrustType(outputPin->type.dataType).empty()) {
        result.error = "the requested graph result must be a supported data output";
        return result;
    }

    std::ostringstream source;
    if (!options.manifestJson.empty()) source << "manifest \"" << EscapeString(options.manifestJson) << "\";\n\n";
    for (const auto& module : importedModules) {
        if (module.empty()) { result.error = "source module names cannot be empty"; return result; }
        source << "use self::" << module << ";\n";
    }
    if (!importedModules.empty()) source << '\n';
    source << "pub fn " << options.functionName << "(";
    for (size_t index = 0; index < options.parameters.size(); ++index) {
        if (index != 0) source << ", ";
        source << options.parameters[index].name << ": " << FrustType(options.parameters[index].type);
    }
    source << ") -> " << FrustType(outputPin->type.dataType) << " = {\n" << body.str()
           << "    n" << options.resultNode << "\n}\n";
    result.ok = true;
    result.source = source.str();
    return result;
}

} // namespace ce::node_system
