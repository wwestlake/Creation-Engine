#include "node_system/frust_codegen.h"
#include "creation/frust/PluginRuntime.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace ce::node_system;

namespace {

// Registers the real core_select_value<T> node (BuiltInPlugins/
// CoreNodesLibrary.frust) as a NodeTypeDescriptor matching its actual
// FRust signature -- generic over one parameter appearing on two inputs
// and the output, the strongest available proof that per-instance
// binding resolution and turbofish emission are both correct (not a
// single hardcoded case), per LANGUAGE_GAPS.md's own established
// discipline for verifying generics.
NodeLibraryDescriptor BuildSelectValueLibrary() {
    NodeLibraryDescriptor core;
    core.id = "core.nodes";
    core.target = GraphTarget::Behavior;
    core.frustSourceModules = { "CoreNodesLibrary" };

    NodeTypeDescriptor selectValue;
    selectValue.typeName = "core.select_value";
    selectValue.domain = Domain::Core;
    selectValue.frustEntryPoint = "core_select_value";
    selectValue.genericParams = { "T" };

    PinSignature condition;
    condition.name = "condition";
    condition.type = { PinKind::Data, DataType::Bool };

    PinSignature ifTrue;
    ifTrue.name = "if_true";
    ifTrue.type = { PinKind::Data, DataType::Any };
    ifTrue.genericParam = "T";

    PinSignature ifFalse;
    ifFalse.name = "if_false";
    ifFalse.type = { PinKind::Data, DataType::Any };
    ifFalse.genericParam = "T";

    selectValue.inputs = { condition, ifTrue, ifFalse };

    PinSignature value;
    value.name = "value";
    value.type = { PinKind::Data, DataType::Any };
    value.genericParam = "T";
    selectValue.outputs = { value };

    selectValue.displayName = "Select Value<T>";
    selectValue.category = "Generics";

    core.nodeTypes = { std::move(selectValue) };
    return core;
}

// Builds a graph with one core.select_value instance bound to `boundType`,
// wired to graph parameters "condition"/"if_true"/"if_false" of that same
// type, and compiles it as `functionName`. `first` controls whether the
// manifest/import header is emitted -- exactly the documented convention
// for compiling multiple functions into one .frust file
// (FrustGraphCompileOptions::emitManifestAndImports's own comment).
FrustGraphCompileResult CompileOneInstance(const NodeLibraryRegistry& libraries, const std::string& functionName,
                                            DataType boundType, bool first, std::string& error) {
    Graph graph(functionName);
    Node* node = libraries.AddNode(graph, "core.select_value", &error);
    if (!node) return {};
    node->SetGenericBinding("T", boundType);

    FrustGraphCompileOptions options;
    options.functionName = functionName;
    options.parameters = { { "condition", DataType::Bool }, { "if_true", boundType }, { "if_false", boundType } };
    options.inputBindings = {
        { node->Id(), node->Inputs()[0].id, "condition" },
        { node->Id(), node->Inputs()[1].id, "if_true" },
        { node->Id(), node->Inputs()[2].id, "if_false" },
    };
    options.resultNode = node->Id();
    options.resultPin = node->Outputs()[0].id;
    options.manifestJson = "{\"name\":\"generic_node_codegen\",\"version\":\"0.1.0\"}";
    options.emitManifestAndImports = first;
    return CompileBehaviorGraphToFrust(graph, libraries, options);
}

} // namespace

int main() {
    NodeLibraryRegistry libraries;
    std::string error;
    if (!libraries.Register(BuildSelectValueLibrary(), &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    // Two DIFFERENT instantiations of the SAME generic node type, in the
    // SAME compiled program -- proves genuine per-instance monomorphized
    // dispatch, not one hardcoded case (same discipline
    // LANGUAGE_GAPS.md's own generics verification already established).
    const auto f64Result = CompileOneInstance(libraries, "select_f64", DataType::Float, /*first=*/true, error);
    if (!f64Result.ok) {
        std::cerr << "could not compile the f64-bound instance: " << f64Result.error << '\n';
        return 1;
    }
    const auto i64Result = CompileOneInstance(libraries, "select_i64", DataType::Int, /*first=*/false, error);
    if (!i64Result.ok) {
        std::cerr << "could not compile the i64-bound instance: " << i64Result.error << '\n';
        return 1;
    }
    if (f64Result.source.find("core_select_value::<f64>(condition, if_true, if_false)") == std::string::npos ||
        i64Result.source.find("core_select_value::<i64>(condition, if_true, if_false)") == std::string::npos) {
        std::cerr << "generated source did not contain the expected per-instance turbofish calls:\n"
                   << f64Result.source << '\n' << i64Result.source << '\n';
        return 1;
    }

    const std::string combinedSource = f64Result.source + "\n" + i64Result.source;
    const auto generatedDirectory = std::filesystem::temp_directory_path() / "creation_engine_generic_node_codegen_smoke";
    std::filesystem::create_directories(generatedDirectory);
    std::filesystem::copy_file(CE_CORE_NODE_LIBRARY, generatedDirectory / "CoreNodesLibrary.frust",
                               std::filesystem::copy_options::overwrite_existing);
    const auto generatedPath = generatedDirectory / "GeneratedGenericNodes.frust";
    std::ofstream generatedFile(generatedPath);
    generatedFile << combinedSource;
    generatedFile.close();

    creation::frust::PluginRuntime runtime("creation-engine");
    if (!runtime.load(generatedPath.string(), error)) {
        std::cerr << "generated generic-node FRust source did not load: " << error << '\n';
        return 1;
    }

    // Assert the ACTUAL returned values, both branches, both bound types --
    // not merely that the module JIT'd successfully. A passing JIT-load
    // with no value check would not catch, for example, a turbofish
    // argument silently binding the wrong type and returning a
    // coincidentally-plausible value.
    using SelectF64 = double (*)(bool, double, double);
    const auto selectF64 = reinterpret_cast<SelectF64>(runtime.getFunction("select_f64"));
    if (!selectF64) {
        std::cerr << "could not resolve select_f64 in the loaded plugin\n";
        return 1;
    }
    if (selectF64(true, 1.5, 2.5) != 1.5) {
        std::cerr << "select_f64(true, 1.5, 2.5) did not return 1.5\n";
        return 1;
    }
    if (selectF64(false, 1.5, 2.5) != 2.5) {
        std::cerr << "select_f64(false, 1.5, 2.5) did not return 2.5\n";
        return 1;
    }

    using SelectI64 = std::int64_t (*)(bool, std::int64_t, std::int64_t);
    const auto selectI64 = reinterpret_cast<SelectI64>(runtime.getFunction("select_i64"));
    if (!selectI64) {
        std::cerr << "could not resolve select_i64 in the loaded plugin\n";
        return 1;
    }
    if (selectI64(true, 10, 20) != 10) {
        std::cerr << "select_i64(true, 10, 20) did not return 10\n";
        return 1;
    }
    if (selectI64(false, 10, 20) != 20) {
        std::cerr << "select_i64(false, 10, 20) did not return 20\n";
        return 1;
    }

    std::cout << "Generic node codegen (per-instance monomorphized dispatch) passed.\n";
    return 0;
}
