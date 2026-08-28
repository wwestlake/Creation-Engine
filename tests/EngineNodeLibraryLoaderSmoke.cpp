#include "Frust/EngineNodeLibraryLoader.h"

#include <iostream>
#include <string>

int main()
{
    using creation::frust::PluginRuntime;
    using namespace ce::node_system;

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
    if (!ce::frust::RegisterPluginNodeLibraries({ manifest }, registry, error)) {
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
    if (ce::frust::RegisterPluginNodeLibraries({ malformed }, registry, error) ||
        error.find("needs typeName") == std::string::npos) {
        std::cerr << "Malformed node-library metadata was accepted.\n";
        return 1;
    }

    std::cout << "Engine plugin node-library loader passed.\n";
    return 0;
}
