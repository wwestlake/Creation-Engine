#include "node_system/frgraph_serialization.h"
#include "node_system/graph_analysis.h"
#include "node_system/node_library.h"

#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace ce::node_system;

PinTypeDesc streamFloat() {
    return { PinKind::Stream, DataType::Float };
}

bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}

} // namespace

int main() {
    NodeLibraryRegistry libraries;
    NodeLibraryDescriptor movementLibrary;
    movementLibrary.id = "engine.movement";
    movementLibrary.displayName = "Movement";
    movementLibrary.description = "Input and movement behavior nodes.";
    // Named-field construction, not positional brace-init: NodeTypeDescriptor's
    // member order (type_registry.h) has been reordered by later phases
    // (controlFlow/monadOperation/isHostExtern insertions) since this test
    // was first written -- positional init here was silently binding
    // fields to the wrong slots (a stale-test bug found and fixed while
    // touching this area for the Animation Control plan, unrelated to that
    // plan's own changes).
    NodeTypeDescriptor inputAxis;
    inputAxis.typeName = "engine.input_axis";
    inputAxis.domain = Domain::Event;
    inputAxis.outputs = { { "value", streamFloat(), {} } };
    inputAxis.displayName = "Input Axis";
    inputAxis.category = "Input";
    inputAxis.description = "Emits a mapped input stream.";
    inputAxis.frustEntryPoint = "input_axis_value";
    inputAxis.requiredCapabilities = { "engine_input_axis" };
    movementLibrary.nodeTypes.push_back(std::move(inputAxis));

    NodeTypeDescriptor move;
    move.typeName = "engine.move";
    move.domain = Domain::Core;
    move.inputs = { { "value", streamFloat(), {} } };
    move.displayName = "Move";
    move.category = "Movement";
    move.description = "Consumes a movement input stream.";
    move.frustEntryPoint = "move_from_input";
    move.requiredCapabilities = { "engine_move" };
    movementLibrary.nodeTypes.push_back(std::move(move));
    std::string libraryError;
    if (!libraries.Register(std::move(movementLibrary), &libraryError)) {
        std::cerr << "Could not register node library: " << libraryError << '\n';
        return 1;
    }

    Graph graph("movement", GraphTarget::Behavior);
    Node* source = libraries.AddNode(graph, "engine.input_axis", &libraryError);
    if (source == nullptr || source->Outputs().size() != 1 || source->Outputs().front().type.kind != PinKind::Stream) {
        std::cerr << "Plugin node library did not create its declared node: " << libraryError << '\n';
        return 1;
    }
    const PinId sourceOut = source->Outputs().front().id;
    Node* consumer = libraries.AddNode(graph, "engine.move", &libraryError);
    if (consumer == nullptr || consumer->Inputs().size() != 1) {
        std::cerr << "Plugin node library did not create a stream consumer: " << libraryError << '\n';
        return 1;
    }
    const PinId consumerIn = consumer->Inputs().front().id;

    if (!graph.Connect(source->Id(), sourceOut, consumer->Id(), consumerIn).has_value()) {
        std::cerr << "Could not create a compatible stream connection.\n";
        return 1;
    }

    const auto streamOrder = TopologicalDataOrder(graph);
    if (!streamOrder || streamOrder->size() != 2 || streamOrder->front() != source->Id()) {
        std::cerr << "Stream graph did not produce deterministic order.\n";
        return 1;
    }

    const std::string serialized = SerializeGraph(graph);
    if (!contains(serialized, "frgraph 1\n") || !contains(serialized, "target behavior\n") ||
        !contains(serialized, "stream float")) {
        std::cerr << "FRust graph serialization missed canonical metadata.\n";
        return 1;
    }

    std::string error;
    const auto restored = DeserializeGraph(serialized, error);
    if (!restored || restored->Target() != GraphTarget::Behavior || restored->Connections().size() != 1 ||
        restored->FindNode(source->Id())->FindPin(sourceOut)->type.kind != PinKind::Stream) {
        std::cerr << "FRust graph round trip failed: " << error << '\n';
        return 1;
    }

    const std::string legacy =
        "frgraph 1\n"
        "graph legacy\n"
        "node 1 Constant core 0 0\n"
        "pin 1 out 1 value data float\n";
    const auto migrated = DeserializeGraph(legacy, error);
    if (!migrated || migrated->Target() != GraphTarget::Behavior || !contains(SerializeGraph(*migrated), "frgraph 1\n")) {
        std::cerr << "FRust graph validation failed: " << error << '\n';
        return 1;
    }

    if (!ValidateGraph(graph, &libraries.TypeRegistry()).ok) {
        std::cerr << "Valid stream graph was rejected.\n";
        return 1;
    }

    NodeLibraryDescriptor conflictingLibrary;
    conflictingLibrary.id = "engine.conflict";
    conflictingLibrary.nodeTypes.push_back({ "engine.input_axis" });
    if (libraries.Register(std::move(conflictingLibrary), &libraryError) ||
        !contains(libraryError, "already owned by library 'engine.movement'")) {
        std::cerr << "Plugin node library type ownership was not enforced.\n";
        return 1;
    }

    Graph cyclic("cyclic", GraphTarget::Dataflow);
    Node& left = cyclic.AddNode("Left", Domain::Core);
    const PinId leftIn = left.AddInput("in", streamFloat());
    const PinId leftOut = left.AddOutput("out", streamFloat());
    Node& right = cyclic.AddNode("Right", Domain::Core);
    const PinId rightIn = right.AddInput("in", streamFloat());
    const PinId rightOut = right.AddOutput("out", streamFloat());
    cyclic.Connect(left.Id(), leftOut, right.Id(), rightIn);
    cyclic.Connect(right.Id(), rightOut, left.Id(), leftIn);

    // ValidateGraph reports this as "data dependency cycle detected"
    // regardless of whether the cycle runs through Data or Stream pins --
    // per IsConnectionCompatible's own reasoning (pin.h), a Stream isn't a
    // different type system, so it isn't given a differently-worded error
    // either (graph_analysis.cpp's BuildAdjacency now includes Stream-kind
    // connections in this check at all -- previously it silently didn't,
    // a real bug found and fixed here: this exact cyclic-stream graph used
    // to pass validation).
    const ValidationResult validation = ValidateGraph(cyclic);
    if (validation.ok || validation.errors.empty() || validation.errors.front() != "data dependency cycle detected") {
        std::cerr << "Stream cycle was not rejected.\n";
        return 1;
    }

    std::cout << "FRust graph asset foundation passed.\n";
    return 0;
}
