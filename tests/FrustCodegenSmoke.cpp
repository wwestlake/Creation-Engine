#include "node_system/frust_codegen.h"
#include "node_system/frgraph_serialization.h"
#include "creation/frust/PluginRuntime.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

using namespace ce::node_system;

int main() {
    NodeLibraryRegistry libraries;
    NodeLibraryDescriptor core;
    core.id = "core.nodes";
    core.target = GraphTarget::Behavior;
    core.frustSourceModules = { "CoreNodesLibrary" };

    // Named-field construction, not positional brace-init:
    // NodeTypeDescriptor's member order (type_registry.h) has been
    // reordered by later phases (controlFlow/monadOperation/isHostExtern
    // insertions) since this test was first written -- positional init
    // here no longer even compiled (e.g. "Add" landing on the `domain`
    // slot, a Domain enum with no string conversion). Stale-test bug found
    // and fixed while touching this area for the Animation Control plan,
    // unrelated to that plan's own changes.
    NodeTypeDescriptor addType;
    addType.typeName = "core_add";
    addType.domain = Domain::Core;
    addType.inputs = { { "left", { PinKind::Data, DataType::Int }, {} }, { "right", { PinKind::Data, DataType::Int }, {} } };
    addType.outputs = { { "value", { PinKind::Data, DataType::Int }, {} } };
    addType.displayName = "Add";
    addType.category = "Math";
    addType.frustEntryPoint = "core_add";

    NodeTypeDescriptor multiplyType;
    multiplyType.typeName = "core_multiply";
    multiplyType.domain = Domain::Core;
    multiplyType.inputs = { { "left", { PinKind::Data, DataType::Int }, {} }, { "right", { PinKind::Data, DataType::Int }, {} } };
    multiplyType.outputs = { { "value", { PinKind::Data, DataType::Int }, {} } };
    multiplyType.displayName = "Multiply";
    multiplyType.category = "Math";
    multiplyType.frustEntryPoint = "core_multiply";

    NodeTypeDescriptor triggerType;
    triggerType.typeName = "core_trigger";
    triggerType.domain = Domain::Event;
    triggerType.inputs = { { "execute", { PinKind::Exec, DataType::Int }, {} } };
    triggerType.outputs = { { "then", { PinKind::Exec, DataType::Int }, {} } };
    triggerType.displayName = "Trigger";
    triggerType.category = "Flow";
    triggerType.frustEntryPoint = "core_trigger";

    // Zero-input Event marker, same shape as the real core.event.tick
    // (core_control_flow.cpp) -- CompileBehaviorGraphToFrust special-cases
    // "the entryNode itself, if Domain::Event" as a pure marker it skips
    // over rather than calls (see its own comment on this). core_trigger
    // above has a real exec INPUT, so it does NOT fit that marker shape --
    // using it directly as entryNode would silently absorb its own call
    // instead of emitting it. This entry node is the actual chain start;
    // both core_trigger instances below become genuine calls.
    NodeTypeDescriptor entryType;
    entryType.typeName = "core_entry";
    entryType.domain = Domain::Event;
    entryType.outputs = { { "then", { PinKind::Exec, DataType::Int }, {} } };
    entryType.displayName = "Entry";
    entryType.category = "Flow";

    // Real monomorphized generics for Schematic nodes plan, Phase 2: a
    // node type generic over one type parameter, matching what FRust
    // reflection now emits for a real `node pure fn identity<T>(x: T) ->
    // T` (see FrustGenericNodeReflectionSmoke) -- both pins are Any at the
    // descriptor level, tagged with genericParam "T"; a per-instance
    // binding is what makes them concrete (below).
    NodeTypeDescriptor identityType;
    identityType.typeName = "core.identity";
    identityType.domain = Domain::Core;
    identityType.frustEntryPoint = "identity";
    identityType.genericParams = { "T" };
    PinSignature identityInput;
    identityInput.name = "x";
    identityInput.type = { PinKind::Data, DataType::Any };
    identityInput.genericParam = "T";
    identityType.inputs = { identityInput };
    PinSignature identityOutput;
    identityOutput.name = "value";
    identityOutput.type = { PinKind::Data, DataType::Any };
    identityOutput.genericParam = "T";
    identityType.outputs = { identityOutput };
    identityType.displayName = "Identity<T>";
    identityType.category = "Generics";

    core.nodeTypes = { std::move(addType), std::move(multiplyType), std::move(triggerType), std::move(entryType),
                       std::move(identityType) };
    std::string error;
    if (!libraries.Register(std::move(core), &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    Graph graph("math");
    Node* add = libraries.AddNode(graph, "core_add", &error);
    Node* multiply = libraries.AddNode(graph, "core_multiply", &error);
    Node* entry = libraries.AddNode(graph, "core_entry", &error);
    Node* firstTrigger = libraries.AddNode(graph, "core_trigger", &error);
    Node* secondTrigger = libraries.AddNode(graph, "core_trigger", &error);
    if (!add || !multiply || !entry || !firstTrigger || !secondTrigger ||
        !graph.Connect(add->Id(), add->Outputs()[0].id, multiply->Id(), multiply->Inputs()[0].id) ||
        !graph.Connect(entry->Id(), entry->Outputs()[0].id, firstTrigger->Id(), firstTrigger->Inputs()[0].id) ||
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
    options.entryNode = entry->Id();
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
    // -- Real monomorphized generics for Schematic nodes (Phase 2) --

    // A bound generic node instance emits the correct turbofish call and
    // type annotation.
    {
        Graph genericGraph("generic");
        Node* identity = libraries.AddNode(genericGraph, "core.identity", &error);
        if (!identity) {
            std::cerr << "could not construct generic node instance: " << error << '\n';
            return 1;
        }
        identity->SetGenericBinding("T", DataType::Float);

        FrustGraphCompileOptions genericOptions;
        genericOptions.functionName = "generic_identity";
        genericOptions.parameters = { { "x", DataType::Float } };
        genericOptions.inputBindings = { { identity->Id(), identity->Inputs()[0].id, "x" } };
        genericOptions.resultNode = identity->Id();
        genericOptions.resultPin = identity->Outputs()[0].id;
        const auto genericCompiled = CompileBehaviorGraphToFrust(genericGraph, libraries, genericOptions);
        const std::string expectedCall = "let n" + std::to_string(identity->Id()) + ": f64 = identity::<f64>(x);";
        if (!genericCompiled.ok || genericCompiled.source.find(expectedCall) == std::string::npos) {
            std::cerr << "bound generic node did not emit the correct turbofish call: " << genericCompiled.error
                       << '\n' << genericCompiled.source << '\n';
            return 1;
        }
    }

    // An unresolved type parameter fails with a clear, specific error.
    {
        Graph unresolvedGraph("generic_unresolved");
        Node* identity = libraries.AddNode(unresolvedGraph, "core.identity", &error);
        if (!identity) {
            std::cerr << "could not construct generic node instance: " << error << '\n';
            return 1;
        }
        // Deliberately no SetGenericBinding call.
        FrustGraphCompileOptions options;
        options.functionName = "generic_unresolved";
        options.resultNode = identity->Id();
        options.resultPin = identity->Outputs()[0].id;
        const auto compiled = CompileBehaviorGraphToFrust(unresolvedGraph, libraries, options);
        if (compiled.ok || compiled.error.find("unresolved type parameter") == std::string::npos) {
            std::cerr << "an unresolved generic binding should have failed clearly: " << compiled.error << '\n';
            return 1;
        }
    }

    // A binding to a DataType FrustType has no mapping for (Vec3) fails
    // with its own clear, specific error -- not a malformed/empty token.
    {
        Graph unsupportedGraph("generic_unsupported");
        Node* identity = libraries.AddNode(unsupportedGraph, "core.identity", &error);
        if (!identity) {
            std::cerr << "could not construct generic node instance: " << error << '\n';
            return 1;
        }
        identity->SetGenericBinding("T", DataType::Vec3);
        FrustGraphCompileOptions options;
        options.functionName = "generic_unsupported";
        options.resultNode = identity->Id();
        options.resultPin = identity->Outputs()[0].id;
        const auto compiled = CompileBehaviorGraphToFrust(unsupportedGraph, libraries, options);
        if (compiled.ok || compiled.error.find("unsupported type for a generic node") == std::string::npos) {
            std::cerr << "an unsupported generic binding type should have failed clearly: " << compiled.error << '\n';
            return 1;
        }
    }

    // A generic node instance's binding survives a save/load round trip --
    // a generic node that works programmatically but silently loses its
    // binding on save/load would be a nasty, delayed failure.
    {
        Graph persistGraph("generic_persist");
        Node* identity = libraries.AddNode(persistGraph, "core.identity", &error);
        if (!identity) {
            std::cerr << "could not construct generic node instance: " << error << '\n';
            return 1;
        }
        identity->SetGenericBinding("T", DataType::Int);

        const std::string serialized = SerializeGraph(persistGraph);
        std::string loadError;
        const auto reloaded = DeserializeGraph(serialized, loadError);
        if (!reloaded) {
            std::cerr << "failed to reload serialized generic-bound graph: " << loadError << '\n';
            return 1;
        }
        const Node* reloadedNode = reloaded->FindNode(identity->Id());
        if (!reloadedNode || reloadedNode->GenericBindings().size() != 1 ||
            reloadedNode->GenericBindings().at("T") != DataType::Int) {
            std::cerr << "generic binding did not survive a save/load round trip.\n";
            return 1;
        }
        if (SerializeGraph(*reloaded) != serialized) {
            std::cerr << "reloaded graph did not re-serialize byte-for-byte identical.\n";
            return 1;
        }
    }

    std::cout << "Creation Engine reflected FRust graph codegen passed.\n";
    return 0;
}
