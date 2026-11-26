# SQLite persistence provider

ENG-002 adds the native `engine_sqlite_persistence` target. Link it to use `CreateSQLiteProvider` from `engine/sqlite_persistence.h`. The provider implements the same session, snapshot, query, transaction, and journal interfaces as the in-memory provider. The generic contract is unchanged. A host selects the provider and supplies a local storage directory, per-connection cache in KiB, lock timeout in milliseconds, and durability mode.

## Dependency

CMake builds the SQLite 3.54.1 amalgamation as a static C library. It verifies the upstream archive against the pinned SHA3-256 digest. SQLite’s public-domain dedication covers the distributable source. The Engine leaves the C API private to the SQLite adapter and its SQLite-specific tests. Runtime extension loading is disabled. [SQLite download](https://sqlite.org/download.html) · [SQLite public-domain dedication](https://sqlite.org/copyright.html)

The default CMake build downloads the archive into the build tree. An offline build can set `FETCHCONTENT_SOURCE_DIR_DJEHUTI_SQLITE_SOURCE` to a locally extracted copy of the same verified amalgamation. The standard build requires no SQLite installation or vcpkg manifest change.

## Storage format and transactions

Each configured directory contains `state.sqlite3` and can contain SQLite-managed `-wal` and `-shm` files while open. SQLite stores a Djehuti application ID and format version 1. The provider initializes an empty database and rejects foreign files or unknown versions. A session's schema and index declarations are compared at open. Migration and journal retention are future work.

The database keys records by session, namespace, collection, and opaque ID. Structured fields and binary payloads have a portable encoding. The adapter projects declared index tuples into sortable binary keys and enforces uniqueness inside SQLite. Records, required indexes, the next revision, and the journal batch commit together. Revision numbering uses SQLite's positive signed 64-bit range.

The provider grants one open writer handle per session within an instance. SQLite serializes write transactions between connections on the same host. Independent provider instances can open the same database, so the caller controls deployment ownership. Readers pin a WAL snapshot when they open a view. Long-lived readers can retain WAL pages and delay truncation. Use a local filesystem on one host. [SQLite WAL concurrency and constraints](https://sqlite.org/wal.html)

## Durability settings

| Mode | Guarantee after a successful commit |
| --- | --- |
| FULL, default | SQLite synchronizes the WAL for each commit. Revisions survive a process crash and are designed to survive an operating-system crash or power loss when storage and the operating system honor synchronization. |
| NORMAL | SQLite preserves atomicity and supports recovery after a process crash. Recent commits can be lost after power loss or an operating-system crash because synchronization can wait for a checkpoint. |

Both settings use WAL and the same file format. The recovery tests verify process restart on the tested machine. Power-loss guarantees follow SQLite’s documented synchronization semantics. [SQLite synchronous settings](https://sqlite.org/pragma.html#pragma_synchronous)

## Backup and restore

Call `CreateBackup` on the provider to copy all sessions through the SQLite online backup API. A source read transaction fixes the copied state while writes can continue. The adapter validates the copy's database integrity and foreign-key references, then publishes it in a new directory. Call `RestoreSQLiteBackup` with a backup directory and a new destination directory, then open the destination as a provider when ready to cut over. Both operations require an existing parent and reserve the destination directory before writing. [SQLite online backup](https://sqlite.org/backup.html)

An interrupted copy can leave an incomplete staging file in the reserved destination. A successful copy publishes `state.sqlite3` after closing the destination database. Keep the live database and its WAL together until SQLite completes recovery or an online backup succeeds.

## Verification

The same conformance executable runs the generic scenarios against both providers. The SQLite-specific test launches independent child processes. It verifies that committed WAL state survives abrupt process exit in both modes, then spills an uncommitted transaction to the WAL and exits without closing SQLite. A new process checks records, indexes, revisions, and the journal. The suite also covers concurrent readers, backup and restore, incompatible formats, a competing writer, and an injected journal write failure after record changes. Fixtures live under the CMake test tree.
