# Portable persistence indexes and queries

ENG-005 adds a planner and bounded executor over the existing native persistence interface. Applications declare fields and indexes in their collection schemas. The same query selects the same logical plan and returns the same records in the same order in the in-memory, SQLite, and RocksDB providers.

## Catalog and maintenance

An index is identified within its collection. Its definition declares an ordered list of field IDs, a uniqueness constraint, an index version, and an optional sort direction for each component. Key types come from the collection schema. An empty direction list means ascending order on every component. Index versions are positive and default to 1. The existing representation of default ascending version-1 definitions is preserved on disk.

Use Session::MaintainIndex from native application code to create, remove, rebuild, or replace one index. Create takes a definition. Rebuild with the same definition repairs its entries. Rebuild with changed fields, directions, or uniqueness requires a higher version. Remove and rebuild can take an expected version for a conditional operation. An index with a missing component has no entry for that record. A present null is an index key. Unique indexes enforce uniqueness on complete keys, including null.

A successful operation publishes the catalog and its entries together. New snapshots see both, and open snapshots keep their previous catalog and data view. Catalog maintenance leaves record versions, commit revisions, and the record change journal intact. A snapshot-bound query cursor identifies the exact view, including when two snapshots have the same record revision. Reopen a session with its current catalog after changing an index. The application's updated collection declaration must match the stored definition.

Maintenance uses one bounded operation. The record budget limits create and rebuild, and a stop token can cancel before publication. Concurrent record commits serialize with maintenance. Snapshot readers continue on their pinned view. Published definitions have status ready. Metadata and incomplete entries from a failed build are absent from the published catalog.

- **In memory** uses a copy of the affected collection state and publishes it under the same writer mutex as record commits.
- **SQLite** replaces the index rows and stored definition in one WAL transaction. A reader's WAL snapshot pins the old definition and rows. A session using a stale catalog fails its next write or snapshot acquisition so it can reopen with the current declaration. FULL and NORMAL retain the existing durability guarantees.
- **RocksDB** stages index entries, unique keys, and the stored definition in one write batch. Removal uses range tombstones for that index. An older RocksDB snapshot keeps its entries through replacement. The provider's configured sync policy governs publication durability.

## Query contract

Include engine/persistence_query.h for PortableQuery, ExplainQuery, and ExecuteQuery. Explain and execute use the same planner on an open ReadSnapshot. The existing QueryDescription prefix and range interface remains supported.

A query can compare typed values using equality, inequality, and ordered comparisons. It can combine predicates using AND, OR, and NOT. A comparison with an absent field has an unknown result, and an unknown predicate does not select a record. Null is a present value that sorts before other values in natural ascending order. The explicit isNull, isMissing, and isPresent predicates distinguish the three cases. A leaf without a field addresses the opaque record ID as a byte string.

The planner combines conjunctive equalities into a leading key prefix and bounds on the next key component into a range. It applies the complete predicate to candidates, including any residual conditions. A sparse index is eligible only when schema requirements and predicates guarantee the presence of every key component. If a caller explicitly selects a sparse index without that guarantee, the planner reports invalid input. The plan distinguishes a primary seek, secondary index seek, primary scan, secondary index scan, and an empty result. Explain can show a scan without executing it. Execution requires explicit allowScan for either scan plan.

Records follow the selected index's component orders, with record ID as the final tie-breaker. Descending traversal reverses the complete selected order. Explicit primary order uses record ID. Applications that need a particular field order can select an index that declares that order. When no order source is specified, the planner deterministically chooses a suitable seek or falls back to primary ID order. Unindexed arbitrary sorting is outside the native query contract.

Execution bounds returned rows, evaluated candidates, predicate size, provider page requests, and cursor lifetime. An optional deadline and a stop token are checked at safe boundaries during execution. A bounded page can return fewer rows than requested or zero matches and still carry a continuation. Its cursor resumes after the last candidate examined and requires the original snapshot and query. A deadline or cancellation during a provider read is observed when that read returns. Statistics count candidates delivered to the executor. A provider can internally probe an additional row to determine whether a continuation is available.

## Developer API

The three ENG-005 read commands are available over HTTP, CLI, and MCP on a mounted session. Existing persistence commands and schemas still apply. Index creation, removal, and rebuilding are native application operations. [Developer API reference](DEVELOPER_API.md)

## Verification and measurements

The native conformance suite uses identical operations against all three providers. The recovery runner restarts both durable providers in separate processes after committed creation, a deliberately bounded rebuild that aborts partway through staging, and committed replacement. SQLite recovery also injects a failure after replacement keys are staged and before catalog publication. RocksDB recovery tears the final WAL frame of a process-durability index rebuild and checks the recovered catalog and entries against the prior state.

The Windows x64 Debug benchmark ran on an Intel Core i5-6500. Each of the six fixtures used 64 groups and increasing integer ranks. The same predicate selected one group and a rank range that matched eight records. The indexed plan evaluated eight candidates. The forced primary scan evaluated every record. Both returned the same record IDs. Timings cover one complete native explain-and-execute call on a pinned snapshot, with two warm-up calls followed by 21 measured calls per plan. Values are local observations in milliseconds.

| Provider | Records | Index seek p50 | Index seek p95 | Primary scan p50 | Primary scan p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Memory | 4,096 | 0.530 | 1.901 | 124.960 | 143.515 |
| SQLite | 4,096 | 1.036 | 1.853 | 244.731 | 277.311 |
| RocksDB | 4,096 | 1.212 | 1.978 | 285.676 | 303.111 |
| Memory | 16,384 | 0.503 | 1.450 | 490.936 | 531.321 |
| SQLite | 16,384 | 0.928 | 1.281 | 1,051.130 | 1,135.220 |
| RocksDB | 16,384 | 2.750 | 3.813 | 1,483.730 | 1,700.100 |

Build the CreationEnginePortablePersistenceQuery target and run benchmark with a provider name, a fresh scratch directory, and a record count. The program prints candidate counts and latency in microseconds.

~~~powershell
ctest --test-dir build -C Debug -R '^creation_engine_portable_persistence_query$' --output-on-failure
& build/Debug/CreationEnginePortablePersistenceQuery.exe benchmark sqlite build/query-benchmark-sqlite-16384 16384
~~~
