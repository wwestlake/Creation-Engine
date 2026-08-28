#include "node_system/frgraph_serialization.h"
#include "node_system/graph_analysis.h"

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
    Graph graph("movement", GraphTarget::Behavior);
    Node& source = graph.AddNode("InputAxis", Domain::Event);
    const PinId sourceOut = source.AddOutput("value", streamFloat());
    Node& consumer = graph.AddNode("Move", Domain::Core);
    const PinId consumerIn = consumer.AddInput("value", streamFloat());

    if (!graph.Connect(source.Id(), sourceOut, consumer.Id(), consumerIn).has_value()) {
        std::cerr << "Could not create a compatible stream connection.\n";
        return 1;
    }

    const auto streamOrder = TopologicalStreamOrder(graph);
    if (!streamOrder || streamOrder->size() != 2 || streamOrder->front() != source.Id()) {
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
        restored->FindNode(source.Id())->FindPin(sourceOut)->type.kind != PinKind::Stream) {
        std::cerr << "FRust graph round trip failed: " << error << '\n';
        return 1;
    }

    const std::string legacy =
        "celg 1\n"
        "graph legacy\n"
        "node 1 Constant core 0 0\n"
        "pin 1 out 1 value data float\n";
    const auto migrated = DeserializeGraph(legacy, error);
    if (!migrated || migrated->Target() != GraphTarget::Behavior || !contains(SerializeGraph(*migrated), "frgraph 1\n")) {
        std::cerr << "Legacy CEL graph migration failed: " << error << '\n';
        return 1;
    }

    if (!ValidateGraph(graph).ok) {
        std::cerr << "Valid stream graph was rejected.\n";
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
