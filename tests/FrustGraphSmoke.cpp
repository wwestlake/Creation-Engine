#include "node_system/frgraph_serialization.h"
#include "node_system/graph_analysis.h"
#include "node_system/node_library.h"

#include <iostream>
#include <string>

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
    movementLibrary.nodeTypes.push_back({
        "engine.input_axis", "Input Axis", "Input", "Emits a mapped input stream.", "input_axis_value",
        { "engine_input_axis" }, Domain::Event, {}, { { "value", streamFloat(), {} } }
    });
    movementLibrary.nodeTypes.push_back({
        "engine.move", "Move", "Movement", "Consumes a movement input stream.", "move_from_input",
        { "engine_move" }, Domain::Core, { { "value", streamFloat(), {} } }, {}
    });
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

    const auto streamOrder = TopologicalStreamOrder(graph);
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

    const ValidationResult validation = ValidateGraph(cyclic);
    if (validation.ok || validation.errors.empty() || validation.errors.front() != "stream dependency cycle detected") {
        std::cerr << "Stream cycle was not rejected.\n";
        return 1;
    }

    std::cout << "FRust graph asset foundation passed.\n";
    return 0;
}
