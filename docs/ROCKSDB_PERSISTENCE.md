# RocksDB persistence provider

ENG-003 adds the native `engine_rocksdb_persistence` target. The Engine retains the in-memory reference and SQLite providers. A host supplies a storage directory, block-cache size, write-buffer size, compression and durability. The neutral interface exposes typed records, indexed queries, transactions, snapshots and journals. RocksDB keys, database handles and statistics stay in the adapter.

## Build and license

CMake builds the RocksDB 11.13.2 source archive and verifies its SHA-256 digest. Djehuti uses the Apache License 2.0 option of RocksDB’s dual license. The build uses the same zlib package already required by the Engine and disables the optional upstream utilities and language bindings. Windows requires an x64 toolchain with C++20 support, such as the Engine’s Visual Studio 2022 configuration. The adapter uses portable C++ and the upstream CMake build for Linux. Windows x64 is the validation platform for this order. [Pinned upstream source](https://github.com/facebook/rocksdb/tree/v11.13.2) · [Apache 2.0 license](https://github.com/facebook/rocksdb/blob/v11.13.2/LICENSE.Apache)

The standard build fetches the dependency into the build tree. For an offline build, set `FETCHCONTENT_SOURCE_DIR_DJEHUTI_ROCKSDB_SOURCE` to an extracted copy of the verified release. The provider ships no separate RocksDB DLL. Windows programs that link it need the Engine’s zlib runtime DLL, which the smoke and benchmark targets stage beside their executables.

## Format and durability

Each configured directory contains a native RocksDB store under `rocks`. The store identifies its format as `rocksdb/1`. Session metadata, records, index entries and journal ranges use separate key prefixes. Signed numeric values, binary data, text, nulls and record IDs sort using portable encodings. Each atomic RocksDB write batch publishes records, unique and secondary indexes, the new revision, journal entries and pruning together.

| Mode | Behavior |
| --- | --- |
| Sync, default | RocksDB writes the WAL and synchronizes it before acknowledging the commit. Power-loss durability depends on the filesystem and device honoring synchronization. |
| Process | RocksDB writes the WAL without synchronous flush per commit. The operating system can recover the buffered WAL after application process termination. OS failure or power loss can lose recent commits. |

Compression is off by default and can use zlib for SST data. One RocksDB primary opens each database directory. The adapter serializes commits, while independent read snapshots observe their pinned versions. RocksDB can compact and remove obsolete data after pinned snapshots are released. [RocksDB basic operations](https://github.com/facebook/rocksdb/wiki/Basic-Operations) · [RocksDB checkpoints](https://github.com/facebook/rocksdb/wiki/Checkpoints)

Checkpoint creation uses the RocksDB API and publishes a marker after opening the checkpoint and checking its format. Restore accepts a completed checkpoint and copies it into a new directory before publishing. Keep the checkpoint immutable while restoring. Local filesystem support is required for native RocksDB locks.

## Common provider policy

Every opened session accepts `PersistencePolicy`. The defaults retain the latest 4,096 journal revisions, allow 64 pinned snapshots per open handle, expire them after five minutes and limit a transaction or returned page to 4,096 entries. A smaller retention setting takes effect at the next successful commit. Zero retains revision metadata and removes all change batches. `ChangePage.historyFloor` gives the earliest valid exclusive cursor. Older cursors return `HistoryUnavailable`.

A new read on an expired snapshot returns `HistoryUnavailable`. Pages carry a process-local expiration deadline as well as a session and revision. The service releases expired pins on subsequent operations and lets reads that already started finish. A closed session’s existing snapshot can still be read until its expiry. Count limits return `ResourceLimit`. The limits apply per open handle. A provider used by a long-lived service should also set deployment-wide byte and request budgets.

## Logical portability

Use `Session::Export` to capture an in-memory logical image at one revision. It includes the schema, records, opaque IDs, record versions, current revision and retained journal. `StorageProvider::Import` validates definitions, record identity and version chains, retained journal bounds and final record state. It builds indexes and publishes the new session atomically. Both durable providers preserve the same logical image. Import refuses an existing ID. A host can validate an imported session before selecting it for use. The native image keeps the logical state in memory, so big deployments will need a streamed format when transport is introduced.

The existing EngineCore session entry points remain the same for callers. Provider implementations now supply snapshot, mutation and journal methods through shared policy wrappers. Logical export and import are additive capabilities. The common maximum revision is the positive signed 64-bit range used by SQLite.
