# Djehuti Engine Developer API

See [common runtime inspection](RUNTIME_PARITY.md#inspection-and-verification) for execution context diagnostics. Dedicated gameplay hosts also expose [read-only world session diagnostics and persistence](AUTHORITATIVE_GAMEPLAY.md#frust-and-inspection).
ENG-004 adds a local development interface for the graphical editor and the dedicated server. A typed command dispatcher owns validation, permissions and job state. Runtime commands execute on the Engine owner thread and use the editor’s Play transport or the dedicated server’s simulation loop. Persistence inspection uses the native persistence contract on a separate serial worker. HTTP, the CLI and the MCP adapter use the same command identifiers and contracts. Commands execute directly without model invocation.

## Enable and connect

The listener is disabled by default. Set the following environment variables in the Engine process’s launch environment. Generate each credential independently with a cryptographic random generator. For example, Python’s secrets.token_hex(32) returns a suitable value. Keep the two values in your local secret store or process environment.

| Variable | Effect |
| --- | --- |
| DJEHUTI_DEV_ENABLE | Set to 1 to enable loopback access. |
| DJEHUTI_DEV_PORT | Set a fixed TCP port or use 0 for an OS-assigned port. Defaults to 0. |
| DJEHUTI_DEV_READ_TOKEN | 32–64 characters from the URL-safe ASCII credential alphabet. Grants inspection. |
| DJEHUTI_DEV_CONTROL_TOKEN | Separate credential using the same format. Grants inspection and runtime control. |
| DJEHUTI_DEV_FIXTURE | Set to 1 to enable synthetic runtime and mount synthetic persistence data. |
| DJEHUTI_DEV_FIXTURE_ONLY | In the editor, set to 1 with fixture mode to bypass project setup for automation. |
| DJEHUTI_DEV_PROVIDER | Synthetic persistence provider. Choose memory (default), sqlite or rocksdb. |
| DJEHUTI_DEV_STORE | Explicit data directory for a durable fixture. Use a dedicated fixture directory. |
| DJEHUTI_DEV_LIMITS | Optional JSON object overriding the limits reported by discovery. |

The server prints its endpoint to stdout. With the editor in project mode, the Engine publishes the URL, API version, process mode and instance ID through Suite VFS at agents/engine-developer.json. The editor also logs the URL. Fixture-only editor runs use a configured port. Discovery can survive an abnormal process exit, so contact the endpoint and verify its process mode before use.

HTTP binds to IPv4 loopback. Clients pass Authorization: Bearer TOKEN on each request. The permitted Host values are 127.0.0.1:PORT and localhost:PORT. Browser Origin and HTTP upgrade requests are rejected. The two credentials are distinct ownership scopes for jobs, snapshots and query tokens. Clients sharing a credential share that scope.

The existing Suite AgentApi associates its listener with Virtual Engineer and accepts a single bearer permission. ENG-004 uses the Suite VFS JSON discovery convention and bearer scheme. Virtual Engineer can consume the authenticated HTTP contract just as a conventional client can. Engine-side callers can also use DeveloperApi::Submit with an already established access scope. Shared Suite components require no changes.

## Operations by process

| Capability | Editor | Dedicated server |
| --- | --- | --- |
| Inspect, pause, step, resume and stop | Current play-in-viewport runtime | Dedicated runtime |
| Start an authored scene | Current scene with the normal Play prerequisites | Suite project passed via --project with optional --game and --scene |
| Start a synthetic session | Explicit fixture opt-in | Explicit fixture opt-in |
| Persistence inspection | Mounted native session | Mounted native session |
| HTTP, OpenAPI, jobs, CLI and MCP | Available when enabled | Available when enabled |

The headless process begins stopped when launched with the Developer API enabled. Its existing --ticks mode remains the finite server smoke path. The editor uses its active scene for an authored start. An editor synthetic start temporarily saves the current scene, runs an empty scene in the actual viewport runtime, then restores the saved scene when stopped. The synthetic store is the current host-provided persistence mount. A native host can pass its own provider and session to DeveloperApi.

| Command | Permission | Arguments and result |
| --- | --- | --- |
| runtime.inspect | Read | Empty arguments. Reports session, execution mode, source, phase, tick and diagnostics. |
| runtime.start | Control | source is authored or fixture, subject to discovery and launch configuration. |
| runtime.pause | Control | Empty arguments. Running to paused. |
| runtime.step | Control | ticks from 1 through 120. Advances the simulation while paused and returns advanced. |
| runtime.resume | Control | Empty arguments. Paused to running. |
| runtime.stop | Control | Empty arguments. Running or paused to stopped. |
| persistence.info | Read | Empty arguments. Provider, capabilities, mounted session, latest revision and history floor. |
| persistence.snapshot.open | Read | Empty arguments. Opens a pinned snapshot and returns its token and revision. |
| persistence.snapshot.inspect | Read | snapshot. Reports the pinned revision. |
| persistence.snapshot.release | Read | snapshot. Releases its pin and query tokens. |
| persistence.record.get | Read | snapshot, namespace, collection and id. Returns a nullable record. |
| persistence.query | Read | snapshot, namespace, collection, limit, and optional index, prefix, lower, upper and cursor. |
| persistence.changes | Read | after and limit. Returns subsequent journal batches in commit order. |
| persistence.indexes | Read | snapshot, namespace, collection, and optional index. Reports published definitions and status. |
| persistence.query.explain | Read | Portable query arguments. Reports the selected index, scan or seek, bounds and residual filtering. |
| persistence.query.execute | Read | Portable query arguments with optional cursor. Returns records, plan, statistics and a continuation. |

Runtime start requires a stopped host. Step requires a paused host and stays paused. It uses the host’s simulation code without firing session start and stop callbacks. Inspection does not advance the simulation. A running session can advance between requests. Session identity identifies the most recent run until the next successful start. Stop resets the simulation tick. The editor shares status and playback state updates between the toolbar and API.

## HTTP, discovery and jobs

Read GET /v1/discovery for version, process mode, command availability, permissions, result and parameter schemas, and configured limits. GET /v1/openapi serves the generated OpenAPI 3.1.1 document. The command schemas in its x-djehuti-commands extension come from the same definitions used by discovery and input validation.

POST /v1/commands/runtime.step accepts an application/json body such as

~~~json
{"requestId":"748ab91755a848e5a0fd41759c78527ba","arguments":{"ticks":8}}
~~~

A successful submission responds with HTTP 202 and a job representation. Read GET /v1/jobs/748ab91755a848e5a0fd41759c78527ba until the job reaches a terminal state. A completed step can look like this

~~~json
{
  "id": "748ab91755a848e5a0fd41759c78527ba",
  "command": "runtime.step",
  "state": "succeeded",
  "cancellable": false,
  "deadlineExceeded": false,
  "result": {
    "session": "d942e1ba853a483d9c88bbe3a8de461e",
    "state": "paused",
    "source": "fixture",
    "tick": "8",
    "executionMode": "server",
    "lastError": "",
    "diagnostics": {"tickRateHz": 30, "authoredConfigured": false},
    "advanced": 8
  }
}
~~~

A job starts queued, may become running, and settles as succeeded, failed, cancelled or timed_out. DELETE /v1/jobs/{id} cancels a queued job and is safe to repeat for a cancelled job. Discovery reports which commands can be cancelled once running. Portable query execution observes cancellation and deadlines at safe provider boundaries. Runtime operations that have started finish at their safe boundary and can report deadlineExceeded with the actual outcome.

After losing a response, read the original request ID using the same endpoint and credential. If the job does not yet exist, resubmit exactly the same command and arguments under that ID. Reusing the ID for different work produces conflict. Finished results expire by time or retained-byte budget. The Engine keeps the deduplication entry for its lifetime and returns result_expired on repeat access after eviction. When the configured identity capacity is exhausted, new requests receive resource_limit. IDs and outcomes are scoped to one Engine process lifetime.

Requests are bounded by bytes, concurrent work, queue depth, deadlines and identity history. Responses and completed results have separate byte budgets. An oversized result produces resource_limit. Reduce the page size for a smaller result. Transport errors have an error envelope with code and message. Execution errors appear on the job with the same envelope fields. They include invalid_transition, retention_gap, snapshot_expired, cursor_expired and operation_failed.

## Persistence wire format

Namespace, collection, index, field and record identifiers use canonical RFC 4648 base64. Binary payloads use the same encoding. An absent binary payload is null and an empty payload is an empty string. Revisions, record/schema versions, journal positions and simulation ticks are unsigned decimal strings. An i64 typed field uses a signed decimal string. A text field encodes the native string’s bytes in base64, preserving embedded NUL and non-UTF-8 values. A real field uses a decimal string that round-trips a finite binary64 value. A bool field uses a JSON boolean. A null typed field has no value property.

~~~json
{"snapshot":"a422ab79dae4475e9187129a3c2f9030","namespace":"YXV0b21hdGlvbg==","collection":"cmVjb3Jkcw==","index":"dmFsdWU=","limit":1}
~~~

Preserve the snapshot and original query arguments, including limit, when adding the returned cursor for the next page. The continuation is scoped to the caller credential, mounted session, pinned snapshot revision, original query and expiry. Release snapshots at the end of inspection. Snapshot and continuation expiration are independent, and a continuation never extends its originating lifetime. Journal pagination uses next as the next after revision. An after position below historyFloor produces retention_gap. The journal revision position is a public position within the permitted mounted session. Query continuation tokens are opaque.

### Portable index queries

Start with persistence.snapshot.open. The catalog and query commands view the exact same pinned catalog as the records on that snapshot. Send snapshot, namespace, collection and limit to explain or execute. Optional arguments include where, index, primaryOnly, direction, allowScan and candidates. Direction is asc or desc. The index and primaryOnly options are mutually exclusive when primaryOnly is true. Omit both to let the planner select a deterministic index or primary order.

~~~json
{
  "snapshot": "a422ab79dae4475e9187129a3c2f9030",
  "namespace": "YXV0b21hdGlvbg==",
  "collection": "cmVjb3Jkcw==",
  "where": {
    "op": "and",
    "children": [
      {"op": "ge", "field": "dmFsdWU=", "value": {"type": "i64", "value": "0"}},
      {"op": "isPresent", "field": "bWVhc3VyZW1lbnQ="}
    ]
  },
  "limit": 8,
  "candidates": 64
}
~~~

Field identifiers use base64. Substitute a declared field for every comparison or presence test. Omit field from a leaf to address the record ID with a text operand. An optional compound predicate uses eq, ne, lt, le, gt, ge, isNull, isMissing, isPresent, and, or, and not. Pass allowScan true to execute a plan that requires a scan. The scan_required job error indicates that explicit consent is needed.

Statistics report evaluated candidates, provider pages, returned records, elapsed nanoseconds and whether the candidate budget caused a yield. When the result has a cursor, send it with the original arguments even if records is empty. The configured queryCandidates, queryPredicateNodes and queryPredicateDepth limits appear in discovery. Executing a portable query can be cancelled while running. The job settles at the next safe provider boundary. Index maintenance is available through the native application interface. [Native portable query reference](PORTABLE_PERSISTENCE_QUERIES.md)

## CLI and MCP

Both tools use Python 3.9 or later and the standard library. Set DJEHUTI_DEV_URL and inject one credential as DJEHUTI_DEV_TOKEN in the tool process. The client connects directly to literal IPv4 loopback.

~~~powershell
python -B scripts/developer_api.py discover
python -B scripts/developer_api.py openapi
python -B scripts/developer_api.py run runtime.inspect
python -B scripts/developer_api.py job 748ab91755a848e5a0fd41759c78527ba
~~~

To call from an MCP client, configure a stdio server command of python with arguments -B and the absolute path to scripts/developer_mcp.py. Supply DJEHUTI_DEV_URL and DJEHUTI_DEV_TOKEN through the client’s environment or secret injection. The adapter negotiates the protocol version, lists allowed and available commands with their schemas, and executes tools through the Engine HTTP endpoint. The optional tools/call metadata field org.djehuti/requestId preserves caller-generated retry identity. The developer.job tool retrieves the job after a disconnect. [MCP lifecycle specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle), [MCP tools specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).

## Verification and dependency

The native Developer API conformance test exercises all three providers with a native write between API snapshots. The Python integration test launches real processes, covers HTTP, CLI and MCP, and exercises API permissions and lifecycle. Run it against the editor with the normal editor instance closed.

~~~powershell
ctest --test-dir build -C Debug -R developer_api --output-on-failure
python -B tests/DeveloperApiIntegration.py --editor build/CreationEngineEditor_artefacts/Debug/DjehutiEngineEditor.exe --work build/developer-api-tests
~~~

The HTTP transport uses cpp-httplib v0.67.0 under the MIT license. CMake pins the source archive checksum. The adapter and CLI have no Python package dependencies. [cpp-httplib source and license](https://github.com/yhirose/cpp-httplib/tree/v0.67.0).
