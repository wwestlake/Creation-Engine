#include "node_system/frust_codegen.h"
#include "creation/frust/PluginRuntime.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace ce::node_system;

int main() {
    NodeLibraryRegistry libraries;
    NodeLibraryDescriptor core;
    core.id = "core.nodes";
    core.target = GraphTarget::Behavior;
    core.frustSourceModules = { "CoreNodesLibrary" };
    core.nodeTypes = {
        { "core_add", "Add", "Math", "", "core_add", {}, Domain::Core,
          { { "left", { PinKind::Data, DataType::Int }, {} }, { "right", { PinKind::Data, DataType::Int }, {} } },
          { { "value", { PinKind::Data, DataType::Int }, {} } } },
        { "core_multiply", "Multiply", "Math", "", "core_multiply", {}, Domain::Core,
          { { "left", { PinKind::Data, DataType::Int }, {} }, { "right", { PinKind::Data, DataType::Int }, {} } },
          { { "value", { PinKind::Data, DataType::Int }, {} } } },
        { "core_trigger", "Trigger", "Flow", "", "core_trigger", {}, Domain::Event,
          { { "execute", { PinKind::Exec, DataType::Int }, {} } },
          { { "then", { PinKind::Exec, DataType::Int }, {} } } },
    };
    std::string error;
    if (!libraries.Register(std::move(core), &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    Graph graph("math");
    Node* add = libraries.AddNode(graph, "core_add", &error);
    Node* multiply = libraries.AddNode(graph, "core_multiply", &error);
    Node* firstTrigger = libraries.AddNode(graph, "core_trigger", &error);
    Node* secondTrigger = libraries.AddNode(graph, "core_trigger", &error);
    if (!add || !multiply || !firstTrigger || !secondTrigger ||
        !graph.Connect(add->Id(), add->Outputs()[0].id, multiply->Id(), multiply->Inputs()[0].id) ||
        !graph.Connect(firstTrigger->Id(), firstTrigger->Outputs()[0].id, secondTrigger->Id(), secondTrigger->Inputs()[0].id)) {
        std::cerr << "could not construct reflected graph: " << error << '\n';
        return 1;
    }

    FrustGraphCompileOptions options;
    options.functionName = "compute";
    options.parameters = { { "x", DataType::Int }, { "scale", DataType::Int } };
    options.inputBindings = {
        { add->Id(), add->Inputs()[0].id, "x" },
        { add->Id(), add->Inputs()[1].id, "scale" },
        { multiply->Id(), multiply->Inputs()[1].id, "scale" },
    };
    options.resultNode = multiply->Id();
    options.resultPin = multiply->Outputs()[0].id;
    options.entryNode = firstTrigger->Id();
    options.manifestJson = "{\"name\":\"generated_math\",\"version\":\"0.1.0\"}";
    const auto compiled = CompileBehaviorGraphToFrust(graph, libraries, options);
    if (!compiled.ok || compiled.source.find("use self::CoreNodesLibrary;") == std::string::npos ||
        compiled.source.find("core_add(x, scale)") == std::string::npos ||
        compiled.source.find("core_multiply(n1, scale)") == std::string::npos ||
        compiled.source.find("core_trigger();\n    core_trigger();") == std::string::npos) {
        std::cerr << "reflected graph did not compile to FRust calls: " << compiled.error << '\n';
        return 1;
    }

    // Execute the exact source produced above. The node module is placed
    // beside the generated root because `use self` is intentional source
    // composition, not a call across separately isolated plugin JITs.
    const auto generatedDirectory = std::filesystem::temp_directory_path() / "creation_engine_frust_codegen_smoke";
    std::filesystem::create_directories(generatedDirectory);
    std::filesystem::copy_file(CE_CORE_NODE_LIBRARY, generatedDirectory / "CoreNodesLibrary.frust",
                               std::filesystem::copy_options::overwrite_existing);
    const auto generatedPath = generatedDirectory / "GeneratedMath.frust";
    std::ofstream generatedFile(generatedPath);
    generatedFile << compiled.source;
    generatedFile.close();

    creation::frust::PluginRuntime runtime("creation-engine");
    if (!runtime.load(generatedPath.string(), error)) {
        std::cerr << "generated FRust graph did not load: " << error << '\n';
        return 1;
    }
    const auto function = reinterpret_cast<std::int64_t (*)(std::int64_t, std::int64_t)>(runtime.getFunction("compute"));
    if (!function || function(2, 3) != 15) {
        std::cerr << "generated FRust graph returned the wrong value" << '\n';
        return 1;
    }
    std::cout << "Creation Engine reflected FRust graph codegen passed.\n";
    return 0;
}
