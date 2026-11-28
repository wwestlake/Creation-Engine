"""Run the Developer API end-to-end against real Debug Engine processes."""

import argparse
import base64
import contextlib
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from developer_api import ApiError, Client  # noqa: E402


def new_id():
    return uuid.uuid4().hex


def b64(value):
    return base64.b64encode(value).decode("ascii")


def wait_for(predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while True:
        result = predicate()
        if result:
            return result
        if time.monotonic() >= deadline:
            raise AssertionError("Wait timed out")
        time.sleep(0.02)


def raw(port, method, path, token="", body=None, headers=None):
    headers = dict(headers or {})
    if token:
        headers["Authorization"] = "Bearer " + token
    if body is not None:
        headers.setdefault("Content-Type", "application/json")
    with contextlib.closing(http.client.HTTPConnection("127.0.0.1", port, timeout=3)) as connection:
        connection.request(method, path, body, headers)
        response = connection.getresponse()
        return response.status, json.loads(response.read())


def unused_port():
    with socket.socket() as claim:
        claim.bind(("127.0.0.1", 0))
        return claim.getsockname()[1]


@contextlib.contextmanager
def host(executable, directory, mode, provider="memory", limit_overrides=None, arguments=(), fixture=True,
         env_overrides=None):
    port = unused_port()
    endpoint = f"http://127.0.0.1:{port}"
    read, control = secrets.token_hex(32), secrets.token_hex(32)
    env = dict(os.environ, DJEHUTI_DEV_ENABLE="1", DJEHUTI_DEV_PORT=str(port), DJEHUTI_DEV_READ_TOKEN=read,
               DJEHUTI_DEV_CONTROL_TOKEN=control, DJEHUTI_DEV_FIXTURE="1" if fixture else "0", DJEHUTI_DEV_FIXTURE_ONLY="1",
               DJEHUTI_DEV_PROVIDER=provider, DJEHUTI_DEV_STORE=str(directory / provider),
               DJEHUTI_DEV_LIMITS=json.dumps(limit_overrides or {}), DJEHUTI_DEV_URL=endpoint, DJEHUTI_DEV_TOKEN=control,
               PYTHONDONTWRITEBYTECODE="1")
    env.update(env_overrides or {})
    directory.mkdir(parents=True, exist_ok=True)
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    executable = executable.resolve()
    with (directory / f"{mode}-{provider}.log").open("w+b") as log:
        # A Windows editor can bootstrap the shared VFS, which outlives the
        # fixture and can inherit its stdout. Keep that handle off the fixture log.
        output = subprocess.DEVNULL if mode == "editor" and os.name == "nt" else log
        process = subprocess.Popen([str(executable), *arguments], cwd=executable.parent, env=env,
                                   stdout=output, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            reader, controller = Client(endpoint, read), Client(endpoint, control)

            def ready():
                if process.poll() is not None:
                    raise AssertionError(f"{mode} exited ({process.returncode}). See {log.name}")
                try:
                    return controller.discovery()
                except (OSError, http.client.HTTPException):
                    return None

            discovery = wait_for(ready, 40)
            assert discovery["processMode"] == mode, discovery
            yield controller, reader, env, discovery
        except Exception:
            log.seek(0)
            tail = log.read().decode("utf-8", errors="replace").replace(read, "[redacted]").replace(control, "[redacted]")
            if tail:
                print(tail[-4000:], file=sys.stderr, flush=True)
            raise
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            log.seek(0)
            contents = log.read()
            assert read.encode() not in contents and control.encode() not in contents, "Host logged a credential"


def result(client, command, args=None, expected=None, request_id=None):
    job = client.run(command, args or {}, request_id)
    if expected is not None:
        assert job["state"] == "failed" and job["error"]["code"] == expected, job
        return job
    assert job["state"] == "succeeded", job
    return job["result"]


def expect_http(code, status, value, expected_status):
    assert status == expected_status and value["error"]["code"] == code, (status, value)


def lifecycle(client, mode):
    inspect = lambda: result(client, "runtime.inspect")
    assert inspect()["state"] == "stopped"
    active = result(client, "runtime.start", {"source": "fixture"})
    assert active["executionMode"] == mode and active["state"] == "running" and active["session"], active
    assert active["diagnostics"]["contextId"] and not active["diagnostics"]["runtimeError"], active
    if mode == "editor":
        assert active["diagnostics"]["playing"] is True and active["diagnostics"]["editorStatus"] == "Playing", active
    paused = result(client, "runtime.pause")
    before = inspect()
    assert paused["state"] == "paused" and before["tick"] == paused["tick"]
    assert inspect()["tick"] == before["tick"]
    request = new_id()
    advanced = result(client, "runtime.step", {"ticks": 8}, request_id=request)
    assert advanced["advanced"] == 8 and advanced["state"] == "paused"
    assert int(advanced["tick"]) - int(before["tick"]) == 8, advanced
    assert inspect()["tick"] == advanced["tick"] and inspect()["session"] == active["session"]
    assert result(client, "runtime.step", {"ticks": 8}, request_id=request) == advanced
    try:
        client.submit("runtime.step", {"ticks": 9}, request)
        raise AssertionError("A request identifier was reused for a different request")
    except ApiError as error:
        assert error.error["code"] == "conflict", error.error
    assert inspect()["tick"] == advanced["tick"]
    assert result(client, "runtime.resume")["state"] == "running"
    stopped = result(client, "runtime.stop")
    assert stopped["state"] == inspect()["state"] == "stopped"
    assert stopped["diagnostics"]["contextId"] == active["diagnostics"]["contextId"], stopped
    if mode == "editor":
        assert stopped["diagnostics"]["playing"] is False and stopped["diagnostics"]["editorStatus"] == "Stopped", stopped
    result(client, "runtime.resume", expected="invalid_transition")
    print(f"{mode} lifecycle and exact eight ticks passed", flush=True)


def persistence(ctl, reader, provider, discovery):
    info = result(reader, "persistence.info")
    assert info["provider"] == provider
    assert (info["revision"], info["historyFloor"]) == ("4", "2"), info
    assert ("durable" in info["capabilities"]) == (provider != "memory")
    snapshot = result(reader, "persistence.snapshot.open")["snapshot"]
    assert result(reader, "persistence.snapshot.inspect", {"snapshot": snapshot})["revision"] == "4"
    keys = {"snapshot": snapshot, "namespace": b64(b"automation"), "collection": b64(b"records")}
    first_id = b64(b"opaque\0\xff")
    fetched = result(reader, "persistence.record.get", dict(keys, id=first_id))
    assert fetched["revision"] == "4" and fetched["record"]["id"] == first_id
    assert fetched["record"]["binary"] == "AH//"
    fields = {item["name"]: item["field"] for item in fetched["record"]["fields"]}
    assert fields == {
        "bWVhc3VyZW1lbnQ=": {"type": "real", "value": "1.0000000000000002"},
        "dmFsdWU=": {"type": "i64", "value": "-9223372036854775808"},
    }
    assert result(reader, "persistence.record.get", dict(keys, id=b64(b"missing")))["record"] is None
    query = dict(keys, index=b64(b"value"), limit=1)
    page = result(reader, "persistence.query", query)
    assert page["revision"] == fetched["revision"] and page["records"] == [fetched["record"]]
    assert page["cursor"] is not None
    result(ctl, "persistence.query", dict(query, cursor=page["cursor"]), expected="forbidden")
    own_snapshot = result(ctl, "persistence.snapshot.open")["snapshot"]
    result(ctl, "persistence.query", dict(query, snapshot=own_snapshot, cursor=page["cursor"]), expected="forbidden")
    result(ctl, "persistence.snapshot.release", {"snapshot": own_snapshot})
    result(reader, "persistence.query", dict(query, limit=2, cursor=page["cursor"]), expected="conflict")
    next_page = result(reader, "persistence.query", dict(query, cursor=page["cursor"]))
    assert next_page["revision"] == "4" and next_page["records"][0]["id"] != first_id
    bounded = result(reader, "persistence.query", dict(query, prefix=[{"type": "i64", "value": "9223372036854775806"}]))
    values = {item["name"]: item["field"]["value"] for item in bounded["records"][0]["fields"]}
    assert values["dmFsdWU="] == "9223372036854775806"
    journal = result(reader, "persistence.changes", {"after": "2", "limit": 1})
    assert journal["batches"][0]["revision"] == journal["next"] == "3" and journal["more"]
    journal = result(reader, "persistence.changes", {"after": journal["next"], "limit": 1})
    assert journal["revision"] == journal["next"] == "4" and not journal["more"]
    result(reader, "persistence.changes", {"after": "0", "limit": 1}, expected="retention_gap")
    time.sleep(discovery["limits"]["cursorMs"] / 1000)
    result(reader, "persistence.query", dict(query, cursor=page["cursor"]), expected="cursor_expired")
    assert result(reader, "persistence.snapshot.release", {"snapshot": snapshot})["released"]
    result(reader, "persistence.snapshot.inspect", {"snapshot": snapshot}, expected="snapshot_expired")
    expiring = result(reader, "persistence.snapshot.open")["snapshot"]
    time.sleep(discovery["limits"]["snapshotMs"] / 1000)
    result(reader, "persistence.snapshot.inspect", {"snapshot": expiring}, expected="snapshot_expired")
    print(f"{provider} read, indexed pagination, snapshots, journal, expiry and lossless wire format passed", flush=True)


def portable_index_queries(ctl, reader):
    view = result(reader, "persistence.snapshot.open")
    keys = {"snapshot": view["snapshot"], "namespace": b64(b"automation"), "collection": b64(b"records")}
    catalog = result(reader, "persistence.indexes", keys)
    assert catalog == {"revision": "4", "indexes": [{
        "id": b64(b"value"), "version": "1", "unique": False, "status": "ready",
        "fields": [{"field": b64(b"value"), "type": "i64", "direction": "asc", "required": True, "nullable": False}],
    }]}
    field = b64(b"value")
    predicate = {"op": "and", "children": [
        {"op": "ge", "field": field, "value": {"type": "i64", "value": "0"}},
        {"op": "not", "children": [{"op": "eq", "field": field,
                                     "value": {"type": "i64", "value": "9223372036854775804"}}]},
    ]}
    query = dict(keys, direction="desc", where=predicate, limit=1)
    plan = result(reader, "persistence.query.explain", query)
    assert plan["plan"]["access"] == "index_seek" and plan["plan"]["index"] == catalog["indexes"][0]["id"]
    assert plan["plan"]["residualFilter"]
    page = result(reader, "persistence.query.execute", query)
    assert page["plan"] == plan["plan"] and page["records"][0]["id"] == b64(b"1") and page["cursor"]
    assert page["statistics"]["candidates"] == page["statistics"]["returned"] == "1"
    next_page = result(reader, "persistence.query.execute", dict(query, cursor=page["cursor"]))
    assert next_page["records"][0]["id"] == b64(b"2")
    result(ctl, "persistence.query.execute", dict(query, cursor=page["cursor"]), expected="forbidden")
    result(reader, "persistence.query.execute", dict(query, direction="asc", cursor=page["cursor"]), expected="conflict")
    assert "portable_index_queries" in reader.discovery()["capabilities"]
    scan = dict(keys, where={"op": "isMissing", "field": field}, limit=1, primaryOnly=True)
    assert result(reader, "persistence.query.explain", scan)["plan"]["access"] == "primary_scan"
    result(reader, "persistence.query.execute", scan, expected="scan_required")
    partial = result(reader, "persistence.query.execute", dict(scan, allowScan=True, candidates=1))
    assert partial["records"] == [] and partial["cursor"] and partial["statistics"]["candidateBudgetReached"]
    result(reader, "persistence.snapshot.release", {"snapshot": view["snapshot"]})
    print("Portable index catalog, explain plan, statistics and scoped execution passed", flush=True)


def contract_and_security(controller, read, env, discovery):
    port, read_token, control = controller.port, env["DJEHUTI_DEV_READ_TOKEN"], env["DJEHUTI_DEV_CONTROL_TOKEN"]
    expect_http("unauthenticated", *raw(port, "GET", "/v1/discovery"), 401)
    expect_http("unauthenticated", *raw(port, "GET", "/v1/discovery", secrets.token_hex(32)), 401)
    expect_http("forbidden", *raw(port, "GET", "/v1/discovery", read_token, headers={"Origin": "http://127.0.0.1"}), 403)
    expect_http("forbidden", *raw(port, "GET", "/v1/discovery", read_token, headers={"Host": "example.invalid"}), 403)
    expect_http("forbidden", *raw(port, "POST", "/v1/commands/runtime.stop", read_token, "{}"), 403)
    expect_http("unsupported", *raw(port, "POST", "/v1/commands/unknown", control, "{}"), 404)
    expect_http("unsupported", *raw(port, "POST", "/v1/commands/persistence.index.rebuild", control, "{}"), 404)
    expect_http("invalid_input", *raw(port, "POST", "/v1/commands/runtime.inspect", read_token, '{"requestId":'), 400)
    bad = json.dumps({"requestId": new_id(), "arguments": {"ticks": 121}})
    expect_http("invalid_input", *raw(port, "POST", "/v1/commands/runtime.step", control, bad), 400)
    expect_http("resource_limit", *raw(port, "POST", "/v1/commands/runtime.inspect", read_token,
                                      "x" * (discovery["limits"]["requestBytes"] + 1)), 413)
    assert all(not c["allowed"] for c in read.discovery()["commands"] if c["permission"] == "control")

    status, spec = raw(port, "GET", "/v1/openapi", read_token)
    assert status == 200
    wire_contract = json.dumps([spec, discovery])
    assert read_token not in wire_contract and control not in wire_contract
    assert spec["openapi"] == "3.1.1" and spec["info"]["version"] == discovery["apiVersion"]
    assert spec["x-djehuti-commands"] == discovery["commands"]
    assert {path: set(methods) for path, methods in spec["paths"].items()} == {
        "/v1/discovery": {"get"}, "/v1/openapi": {"get"}, "/v1/commands/{command}": {"post"},
        "/v1/jobs/{id}": {"get", "delete"},
    }
    for methods in spec["paths"].values():
        for operation in methods.values():
            assert "application/json" in operation["responses"]["401"]["content"]
    status, job = raw(port, "POST", "/v1/commands/runtime.inspect", read_token,
                      json.dumps({"requestId": new_id(), "arguments": {}}))
    assert status == 202 and job["id"]
    status, settled = raw(port, "GET", "/v1/jobs/" + job["id"], read_token)
    assert status == 200 and settled["command"] == "runtime.inspect"
    print("HTTP routes, OpenAPI contract, authorization and size limits passed", flush=True)


def cli_and_mcp(environment):
    cli = [sys.executable, "-B", str(ROOT / "scripts" / "developer_api.py")]
    completed = subprocess.run([*cli, "run", "runtime.inspect"], env=environment, text=True,
                               capture_output=True, check=True, timeout=10)
    assert json.loads(completed.stdout)["result"]["state"] == "stopped"
    controller = Client(environment["DJEHUTI_DEV_URL"], environment["DJEHUTI_DEV_TOKEN"])
    snapshot = result(controller, "persistence.snapshot.open")["snapshot"]
    scope = {"snapshot": snapshot, "namespace": b64(b"automation"), "collection": b64(b"records")}
    catalog = subprocess.run([*cli, "run", "persistence.indexes", json.dumps(scope)], env=environment, text=True,
                             capture_output=True, check=True, timeout=10)
    assert json.loads(catalog.stdout)["result"]["indexes"][0]["status"] == "ready"
    with subprocess.Popen([sys.executable, "-B", str(ROOT / "scripts" / "developer_mcp.py")],
                          env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True) as process:
        messages = [
            {"id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                                                          "clientInfo": {"name": "integration", "version": "1"}}},
            {"method": "notifications/initialized"}, {"id": 2, "method": "tools/list"},
            {"id": 3, "method": "tools/call", "params": {"name": "runtime.start", "arguments": {"source": "fixture"}}},
            {"id": 4, "method": "tools/call", "params": {"name": "runtime.stop", "arguments": {}}},
            {"id": 5, "method": "tools/call", "params": {"name": "persistence.query.explain",
                "arguments": dict(scope, limit=1)}},
        ]
        wire = "".join(json.dumps(dict(message, jsonrpc="2.0")) + "\n" for message in messages)
        try:
            stdout, stderr = process.communicate(wire, timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
    assert process.returncode == 0 and not stderr, stderr
    replies = {r["id"]: r["result"] for r in map(json.loads, stdout.splitlines())}
    assert replies[1]["protocolVersion"] == "2025-11-25"
    assert {"runtime.start", "persistence.info", "persistence.query.explain", "developer.job"} <= {
        tool["name"] for tool in replies[2]["tools"]
    }
    assert replies[3]["structuredContent"]["state"] == "running"
    assert replies[4]["structuredContent"]["state"] == "stopped"
    assert replies[5]["structuredContent"]["plan"]["access"] == "primary_scan"
    result(controller, "persistence.snapshot.release", {"snapshot": snapshot})
    print("CLI and MCP stdio invoked the same Engine session", flush=True)


def jobs(controller, reader, _env, _discovery):
    def check(job_id, state):
        job = controller.job(job_id)
        return job if job["state"] == state else None

    # The 1 Hz owner pump leaves a deterministic queue window behind each completed job.
    barrier = new_id()
    controller.submit("runtime.inspect", {}, barrier)
    wait_for(lambda: check(barrier, "succeeded"))
    target = new_id()
    submitted = controller.submit("runtime.start", {"source": "fixture"}, target)
    assert submitted["state"] == "queued" and submitted["cancellable"]
    try:
        controller.submit("runtime.inspect", {}, new_id())
        raise AssertionError("Queue overflow was accepted")
    except ApiError as error:
        assert error.error["code"] == "resource_limit", error.error
    assert controller.job(target, True)["state"] == "cancelled"
    assert controller.submit("runtime.start", {"source": "fixture"}, target)["state"] == "cancelled"
    try:
        reader.job(target)
        raise AssertionError("Job visible to another credential")
    except ApiError as error:
        assert error.error["code"] == "not_found"

    interrupted = new_id()
    try:
        controller.run("runtime.start", {"source": "fixture"}, interrupted, wait=0.0)
    except ApiError as error:
        assert error.error["code"] == "outcome_unknown" and error.error["details"]["requestId"] == interrupted
    else:
        raise AssertionError("Expected a client timeout")
    original = wait_for(lambda: check(interrupted, "succeeded"))
    assert not original["cancellable"] and original["result"]["state"] == "running"
    assert controller.submit("runtime.start", {"source": "fixture"}, interrupted) == original
    try:
        controller.job(interrupted, True)
        raise AssertionError("A job crossed its cancellation boundary but accepted cancellation")
    except ApiError as error:
        assert error.error["code"] == "not_cancellable", error.error
    assert result(controller, "runtime.stop")["state"] == "stopped"
    print("Queued cancellation, resource limits, timeout recovery and safe retries passed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path)
    parser.add_argument("--editor", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    if not args.server and not args.editor:
        parser.error("Supply a server and/or editor executable.")
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work) as name:
        directory = Path(name)
        if args.server:
            for provider in ("memory", "sqlite", "rocksdb"):
                with host(args.server, directory / provider, "server", provider,
                          {"cursorMs": 850, "snapshotMs": 3000}) as engine:
                    if provider == "memory":
                        lifecycle(engine[0], "server")
                        contract_and_security(*engine)
                        cli_and_mcp(engine[2])
                    persistence(engine[0], engine[1], provider, engine[3])
                    portable_index_queries(engine[0], engine[1])
            with host(args.server, directory / "jobs", "server", limit_overrides={"queueDepth": 1},
                      arguments=("--tick-rate", "1")) as engine:
                jobs(*engine)
            with host(args.server, directory / "plain", "server", fixture=False) as engine:
                assert all(not c["available"] for c in engine[3]["commands"] if c["id"].startswith("persistence."))
                expect_http("unsupported", *raw(engine[0].port, "POST", "/v1/commands/persistence.info",
                                                  engine[2]["DJEHUTI_DEV_CONTROL_TOKEN"], "{}"), 404)
        if args.editor:
            with host(args.editor, directory / "editor", "editor") as engine:
                lifecycle(engine[0], "editor")
                portable_index_queries(engine[0], engine[1])


if __name__ == "__main__":
    main()
