#pragma once

#include <memory>
#include <string>

#include "node_system/graph.h"

namespace ce::node_system {

// FRust Graph: a hand-written, line-oriented, whitespace-tokenized text format
// for a Graph -- deliberately NOT JSON (NodeSystem links nothing today,
// and pulling in a JSON library just for this would be the first
// dependency it's ever had), and deliberately NOT binary: every line is
// git-diffable, so two designers' graph edits show up as a normal text
// diff/merge-conflict instead of an opaque blob. Roughly:
//
//   frgraph 1
//   target behavior|material|dataflow
//   graph <name>
//   node <id> <typeName> <domain> <editorX> <editorY>
//   pin <nodeId> in|out <pinId> <name> exec|data [<dataType>] [default <kind> <value...>]
//   connection <id> <fromNode> <fromPin> <toNode> <toPin>
//
// `#`-prefixed and blank lines are ignored (comments / spacing, for
// hand-editing). Every id (node/pin/connection) round-trips EXACTLY --
// SerializeGraph(*DeserializeGraph(SerializeGraph(g))) == SerializeGraph(g)
// byte-for-byte -- via Node::AddInputWithId/AddOutputWithId and
// Graph::AddNodeWithId/ConnectWithId, not freshly renumbered ids. This
// is what the format-stability regression test depends on. The reader also

std::string SerializeGraph(const Graph& graph);

// Parses .frgraph text into a fresh Graph. Returns nullptr and fills
// `errorOut` with a human-readable message (including the 1-based line
// number) on any malformed input -- an unknown domain/dataType/pin-kind
// keyword, a duplicate id, a `pin`/`connection` line referencing a node
// that doesn't exist yet, or a structurally invalid line.
std::unique_ptr<Graph> DeserializeGraph(const std::string& text, std::string& errorOut);

} // namespace ce::node_system
