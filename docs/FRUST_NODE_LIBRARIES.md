# FRust Node Libraries

A node library is a plugin-owned catalog of graph nodes. It is the bridge
between a visual graph and the FRust code that implements its behavior.

## Library Contract

Every library declares:

- a stable library id, display name, description, and graph target;
- each node's stable type name, palette category, display name, and help text;
- typed input and output pins, including data, execution, or stream wiring;
- the FRust entry point used when graph compilation emits that node; and
- the host capabilities the entry point requires.

Type names are globally unique. `NodeLibraryRegistry` rejects a library that
tries to claim an existing node type, which keeps saved graph assets
unambiguous and makes plugin unload/reload ownership explicit.

## Authoring Rule

A graph stores node type names and pin instances. It does not store a second
copy of the node catalog. When a graph is opened, its plugin libraries provide
the authoritative signatures and metadata; validation detects a missing plugin
or a saved node whose pins no longer match its declared type.

The current registry is backend-neutral. The Engine-side plugin loader will
populate it from the loaded FRust plugin manifest, while the graph compiler
will use `frustEntryPoint` and `requiredCapabilities` to emit a behavior pod.
Material and dataflow libraries use the same contract with their respective
graph targets.
