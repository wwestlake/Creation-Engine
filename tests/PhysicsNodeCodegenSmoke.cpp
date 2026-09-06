#include "node_system/core_control_flow.h"
#include "node_system/frust_codegen.h"
#include "creation/frust/PluginRuntime.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace ce::node_system;

// Phase 5 of the Jolt vendoring plan -- the one real risk the plan itself
// names: physics nodes are registered under Domain::Core specifically
// because that's the only domain proven to compile through
// frust_codegen.cpp's pure-single-value-node path (Animation/Input nodes
// were registered under their own domains with no such proof). This test
// proves it, rather than assuming it: build the real RegisterCorePhysicsNodes
// library, compile a real graph using one of its nodes through
// CompileBehaviorGraphToFrust, then actually load and run the generated
// source through PluginRuntime with a fake host function standing in for
// the real engine-side one -- bit-exact f64 in, bit-exact f64 out.

namespace {
void Fail(const std::string& message) {
    std::cerr << "PhysicsNodeCodegenSmoke failure: " << message << '\n';
    std::exit(1);
}

constexpr double kExpectedVelocityX = 4.25;

extern "C" double FakeGetLinearVelocityX(std::int64_t entityId) {
    // Bit-exact check, not a numeric-closeness one -- mirrors
    // F64FfiCheckSmoke's own methodology.
    return entityId == 42 ? kExpectedVelocityX : -1.0;
}
} // namespace

int main() {
    NodeTypeRegistry registry;
    RegisterCorePhysicsNodes(registry);

    NodeLibraryRegistry libraries;
    NodeLibraryDescriptor library;
    library.id = "core-physics-test";
    library.target = GraphTarget::Behavior;
    for (const auto& [name, descriptor] : registry.Types())
        library.nodeTypes.push_back(descriptor);
    std::string error;
    if (!libraries.Register(std::move(library), &error))
        Fail("could not register physics node library: " + error);

    Graph graph("physics_query");
    Node* getVelocityX = libraries.AddNode(graph, "core.physics.getLinearVelocityX", &error);
    if (!getVelocityX)
        Fail("could not construct core.physics.getLinearVelocityX node: " + error);

    FrustGraphCompileOptions options;
    options.functionName = "query_velocity_x";
    options.parameters = { { "entity", DataType::Entity } };
    options.inputBindings = { { getVelocityX->Id(), getVelocityX->Inputs()[0].id, "entity" } };
    options.resultNode = getVelocityX->Id();
    options.resultPin = getVelocityX->Outputs()[0].id;
    options.manifestJson = "{\"name\":\"generated_physics_query\",\"version\":\"0.1.0\"}";

    const auto compiled = CompileBehaviorGraphToFrust(graph, libraries, options);
    if (!compiled.ok)
        Fail("Domain::Core physics node did not compile through frust_codegen: " + compiled.error);
    // Extern declarations are returned separately (result.externDeclarations),
    // not folded into result.source -- the caller assembles them (matches
    // how FrustCodegenSmoke's own host-extern-free test never needed this,
    // and how a real engine build's plugin-loading path already handles it).
    if (compiled.externDeclarations.size() != 1 ||
        compiled.externDeclarations.front() != "extern fn engine_physics_get_linear_velocity_x(entity: i64) -> f64;\n")
        Fail("did not get the expected single auto-generated extern fn declaration");
    if (compiled.source.find("engine_physics_get_linear_velocity_x(entity)") == std::string::npos)
        Fail("compiled source did not call the physics host-extern function:\n" + compiled.source);

    const auto generatedDirectory = std::filesystem::temp_directory_path() / "creation_engine_physics_node_codegen_smoke";
    std::filesystem::create_directories(generatedDirectory);
    const auto generatedPath = generatedDirectory / "GeneratedPhysicsQuery.frust";
    std::ofstream generatedFile(generatedPath);
    for (const auto& decl : compiled.externDeclarations)
        generatedFile << decl;
    generatedFile << '\n' << compiled.source;
    generatedFile.close();

    creation::frust::PluginRuntime runtime("creation-engine");
    runtime.registerHostFunction("engine_physics_get_linear_velocity_x", reinterpret_cast<void*>(&FakeGetLinearVelocityX));
    if (!runtime.load(generatedPath.string(), error))
        Fail("generated physics-node FRust graph did not load: " + error);

    using QueryFn = double (*)(std::int64_t);
    const auto function = reinterpret_cast<QueryFn>(runtime.getFunction("query_velocity_x"));
    if (!function)
        Fail("could not resolve query_velocity_x from the loaded plugin");

    const double returned = function(42);
    if (std::memcmp(&returned, &kExpectedVelocityX, sizeof(double)) != 0) {
        Fail("real Domain::Core f64 physics node did not round-trip bit-exact (got " +
             std::to_string(returned) + ", expected " + std::to_string(kExpectedVelocityX) + ")");
    }

    std::cout << "PhysicsNodeCodegenSmoke passed: a real Domain::Core physics node "
                 "(RegisterCorePhysicsNodes) compiled through frust_codegen and ran, bit-exact f64.\n";
    return 0;
}
