#include "Frust/EngineNodeLibraryLoader.h"

#include <iostream>
#include <set>
#include <string>

int main()
{
    using creation::frust::PluginRuntime;
    using namespace ce::node_system;

    const std::set<std::string, std::less<>> noCapabilities;

    const PluginRuntime::NodeLibraryManifest manifest {
        "core.flow",
        R"json({
            "id": "core.flow",
            "displayName": "Core Flow",
            "description": "General-purpose control-flow nodes supplied by a FRust plugin.",
            "target": "behavior",
            "nodes": [{
                "typeName": "core.select",
                "displayName": "Select",
                "category": "Flow",
                "description": "Selects between two values from a condition.",
                "frustEntryPoint": "core_select",
                "requiredCapabilities": [],
                "domain": "event",
                "inputs": [{ "name": "condition", "kind": "data", "dataType": "bool" }],
                "outputs": [{ "name": "then", "kind": "exec" }, { "name": "else", "kind": "exec" }]
            }]
        })json"
    };

    NodeLibraryRegistry registry;
    std::string error;
    if (!ce::frust::RegisterPluginNodeLibraries({ manifest }, registry, noCapabilities, error)) {
        std::cerr << "Could not load plugin node library: " << error << '\n';
        return 1;
    }
    const auto* library = registry.FindLibrary("core.flow");
    const auto* node = registry.FindNodeType("core.select");
    if (library == nullptr || node == nullptr || node->frustEntryPoint != "core_select" ||
        !node->requiredCapabilities.empty() || node->inputs.size() != 1 ||
        node->inputs.front().type.kind != PinKind::Data || node->inputs.front().type.dataType != DataType::Bool ||
        node->outputs.size() != 2 || node->outputs.front().type.kind != PinKind::Exec) {
        std::cerr << "Loaded node library did not preserve the FRust node contract.\n";
        return 1;
    }

    Graph graph("plugin-flow", GraphTarget::Behavior);
    if (registry.AddNode(graph, "core.select", &error) == nullptr || graph.Nodes().size() != 1) {
        std::cerr << "Loaded node type could not create a graph node: " << error << '\n';
        return 1;
    }

    const PluginRuntime::NodeLibraryManifest malformed { "engine.bad", R"json({"id":"engine.bad","target":"behavior","nodes":[{}]})json" };
    if (ce::frust::RegisterPluginNodeLibraries({ malformed }, registry, noCapabilities, error) ||
        error.find("needs typeName") == std::string::npos) {
        std::cerr << "Malformed node-library metadata was accepted.\n";
        return 1;
    }

    // "any" dataType (Codegen.h's nodeDataType fix): a generic/unrecognized
    // FRust type now reflects honestly as DataType::Any instead of the old
    // silent, false "int" default -- confirm the host actually accepts and
    // preserves it, not just that it no longer errors.
    const PluginRuntime::NodeLibraryManifest genericManifest {
        "core.generic",
        R"json({
            "id": "core.generic",
            "target": "dataflow",
            "nodes": [{
                "typeName": "core.identity",
                "frustEntryPoint": "core_identity",
                "domain": "core",
                "inputs": [{ "name": "value", "kind": "data", "dataType": "any" }],
                "outputs": [{ "name": "result", "kind": "data", "dataType": "any" }]
            }]
        })json"
    };
    if (!ce::frust::RegisterPluginNodeLibraries({ genericManifest }, registry, noCapabilities, error)) {
        std::cerr << "Could not load node library with \"any\"-typed pins: " << error << '\n';
        return 1;
    }
    const auto* genericNode = registry.FindNodeType("core.identity");
    if (genericNode == nullptr || genericNode->inputs.front().type.dataType != DataType::Any ||
        genericNode->outputs.front().type.dataType != DataType::Any) {
        std::cerr << "\"any\" dataType did not round-trip as DataType::Any.\n";
        return 1;
    }

    std::cout << "Engine plugin node-library loader passed.\n";
    return 0;
}
