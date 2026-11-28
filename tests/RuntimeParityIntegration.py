"""Exercise one authored fixture through the real editor, local client and server."""

import argparse
import http.client
import math
from pathlib import Path
import tempfile

from DeveloperApiIntegration import Client, host, result, unused_port, wait_for


POSITION_TOLERANCE_METERS = 0.0001


def inspect(client):
    return result(client, "runtime.inspect")


def paused_session(client):
    active = result(client, "runtime.start", {"source": "fixture"})
    paused = result(client, "runtime.pause")
    diagnostics = paused["diagnostics"]
    assert paused["state"] == "paused" and paused["session"] == active["session"], paused
    assert diagnostics["contextId"] and diagnostics["sceneId"] == "entry" and not diagnostics["runtimeError"], paused
    return paused


def at_tick(client, target):
    current = inspect(client)
    ticks = target - int(current["tick"])
    assert ticks >= 0, current
    if ticks:
        current = result(client, "runtime.step", {"ticks": ticks})
        assert current["advanced"] == ticks, current
    assert current["state"] == "paused" and int(current["tick"]) == target, current
    return current["diagnostics"]


def assert_probe(diagnostics, scene, tick, begin, count):
    assert diagnostics["sceneId"] == scene and diagnostics["gameId"] == "runtime-fixture", diagnostics
    assert math.isclose(diagnostics["stepSeconds"], 1 / 30, abs_tol=1e-7), diagnostics
    sample = diagnostics["fixtureProbe"]
    assert sample["runtime-probe.game"] == "runtime-fixture", sample
    assert sample["runtime-probe.scene"] == scene, sample
    assert sample["runtime-probe.spawnTick"] == sample["runtime-probe.beginTick"] == begin, sample
    assert sample["runtime-probe.lastTick"] == tick and sample["runtime-probe.count"] == count, sample
    return sample


def same_pose(left, right):
    for axis in ("x", "y", "z"):
        assert math.isclose(left[axis], right[axis], abs_tol=POSITION_TOLERANCE_METERS), (axis, left, right)


def check_entry(client):
    diagnostics = at_tick(client, 8)
    probe = assert_probe(diagnostics, "entry", 8, 0, 8)
    assert not diagnostics["runtimeError"], diagnostics
    assert probe["runtime-probe.initTick"] == 0 and 2.5 < probe["y"] < 2.8 and probe["z"] == 1, probe
    return probe


def check_next(client):
    switched = at_tick(client, 12)
    assert switched["sceneId"] == "next" and not switched["runtimeError"], switched
    assert switched["fixtureProbe"]["y"] == 5 and switched["fixtureProbe"]["runtime-probe.beginTick"] == 12, switched
    diagnostics = at_tick(client, 20)
    probe = assert_probe(diagnostics, "next", 20, 12, 8)
    assert "missing-scene" in diagnostics["runtimeError"], diagnostics
    assert 4.5 < probe["y"] < 4.8 and probe["z"] == 2, probe
    return probe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--editor", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).resolve().parent / "RuntimeParityFixture"
    extra = {"DJEHUTI_DEV_RUNTIME_FIXTURE": str(fixture)}
    with tempfile.TemporaryDirectory(dir=args.work) as work:
        directory = Path(work)
        with host(args.server, directory, "server", env_overrides=extra) as (server, *_):
            paused_session(server)
            server_entry = check_entry(server)
            server_next = check_next(server)
            assert result(server, "runtime.stop")["tick"] == "0"
            assert result(server, "runtime.start", {"source": "fixture"})["diagnostics"]["sceneId"] == "entry"
            assert result(server, "runtime.stop")["state"] == "stopped"

        if not args.editor:
            print("Dedicated server advanced Jolt, ran authored FRust and transitioned scenes.")
            return

        client_port = unused_port()
        with host(args.editor, directory, "editor",
                  env_overrides=dict(extra, DJEHUTI_DEV_RUNTIME_CLIENT_PORT=str(client_port))) as (editor, _, env, _):
            local = Client(f"http://127.0.0.1:{client_port}", env["DJEHUTI_DEV_CONTROL_TOKEN"])

            def local_ready():
                try:
                    return local.discovery()
                except (OSError, http.client.HTTPException):
                    return None

            wait_for(local_ready, 15)
            editor_initial = paused_session(editor)
            assert inspect(local)["state"] == "stopped"
            local_initial = paused_session(local)
            assert local_initial["session"] != editor_initial["session"]
            assert local_initial["diagnostics"]["contextId"] != editor_initial["diagnostics"]["contextId"]
            assert inspect(editor)["tick"] == editor_initial["tick"]

            same_pose(server_entry, check_entry(editor))
            assert inspect(local)["tick"] == local_initial["tick"]
            same_pose(server_entry, check_entry(local))
            same_pose(server_next, check_next(local))
            assert inspect(editor)["tick"] == "8" and inspect(editor)["diagnostics"]["sceneId"] == "entry"
            same_pose(server_next, check_next(editor))

            editor_before = inspect(editor)
            assert result(local, "runtime.resume")["state"] == "running"
            assert result(local, "runtime.stop")["tick"] == "0"
            assert inspect(editor) == editor_before
            restarted = paused_session(local)
            assert restarted["session"] != local_initial["session"]
            assert restarted["diagnostics"]["contextId"] == local_initial["diagnostics"]["contextId"]
            same_pose(server_entry, check_entry(local))
            assert result(editor, "runtime.stop")["tick"] == "0"
            assert inspect(local)["tick"] == "8"
            same_pose(server_next, check_next(local))
            assert result(local, "runtime.stop")["state"] == "stopped"
    print("Editor, local client and server matched within 0.0001 m. Same-process commands and callbacks were isolated.")


if __name__ == "__main__":
    main()
