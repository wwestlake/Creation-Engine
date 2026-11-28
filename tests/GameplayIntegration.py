"""Exercise native gameplay in independent Engine processes."""

import argparse
from contextlib import contextmanager, ExitStack
import json
import os
from pathlib import Path
import secrets
import socket
import statistics
import subprocess
import tempfile
import time

from DeveloperApiIntegration import Client, b64, result, unused_port

FIXTURE = Path(__file__).resolve().parent / "GameplayFixture"
FLAGS = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def poll(check, duration=35, process=None):
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        try:
            value = check()
        except (OSError, ValueError, IndexError, KeyError):
            value = None
        if value:
            return value
        if process is not None and process.poll() is not None:
            raise AssertionError(f"Process exited ({process.returncode}) while waiting for {check.__name__}")
        time.sleep(0.04)
    raise AssertionError(f"Timed out waiting for {check.__name__}")


def set_commands(path, **values):
    temporary = path.with_suffix(".new")
    temporary.write_text(json.dumps(values), encoding="utf-8")
    temporary.replace(path)


def load(path):
    for _ in range(4):
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            time.sleep(0.015)
    return {}


def pose(state, entity):
    return next((row[1:] for row in state.get("poses", []) if row[0] == entity), [])


def total(state):
    return (state.get("application") or {}).get("total")


def run(executable, args, environment=None):
    done = subprocess.run([str(executable), *map(str, args)], cwd=executable.parent, env=environment,
                          capture_output=True, text=True, timeout=75, creationflags=FLAGS)
    assert done.returncode == 0, (done.returncode, done.stdout, done.stderr)
    return done.stdout


def base_args(root, provider):
    return ["--world", "integration-world", "--provider", provider, "--store", root / "database",
            "--gameplay-fixture", FIXTURE]


class Server:
    def __init__(self, executable, root, name, provider, accounts, checkpoint):
        self.executable = executable
        self.stop_file = root / f"{name}.stop"
        self.log = (root / f"{name}.log").open("w+b")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as binding:
            binding.bind(("127.0.0.1", 0))
            self.udp = binding.getsockname()[1]
        tcp = unused_port()
        self.read_token, self.control_token = secrets.token_hex(32), secrets.token_hex(32)
        self.reader = Client(f"http://127.0.0.1:{tcp}", self.read_token)
        env = dict(os.environ, DJEHUTI_DEV_ENABLE="1", DJEHUTI_DEV_PORT=str(tcp), DJEHUTI_DEV_FIXTURE="0",
                   DJEHUTI_DEV_READ_TOKEN=self.read_token, DJEHUTI_DEV_CONTROL_TOKEN=self.control_token)
        args = [*base_args(root, provider), "--gameplay-port", self.udp, "--credentials", accounts,
                "--max-players", "2", "--checkpoint-ticks", checkpoint, "--stop-file", self.stop_file]
        self.process = subprocess.Popen([str(executable), *map(str, args)], env=env, cwd=executable.parent,
                                        stdout=self.log, stderr=subprocess.STDOUT, creationflags=FLAGS)

    def diagnostics(self):
        return result(self.reader, "runtime.inspect")["diagnostics"]["gameplay"]

    def record(self, collection, key):
        snapshot = result(self.reader, "persistence.snapshot.open")["snapshot"]
        try:
            return result(self.reader, "persistence.record.get",
                          {"snapshot": snapshot, "namespace": b64(b"gameplay"), "collection": b64(collection.encode()),
                           "id": b64(key.encode())})["record"]
        finally:
            result(self.reader, "persistence.snapshot.release", {"snapshot": snapshot})

    def stop(self, crash=False):
        if self.process.poll() is None:
            if crash:
                self.process.kill()
            else:
                self.stop_file.touch()
            self.process.wait(timeout=20)
        return self.process.returncode

    def finish(self, report=False):
        if self.process.poll() is None:
            self.stop(crash=True)
        self.log.seek(0)
        captured = self.log.read()
        self.log.close()
        assert self.read_token.encode() not in captured and self.control_token.encode() not in captured
        if report:
            print(captured.decode(errors="replace")[-4000:], flush=True)


@contextmanager
def server(executable, root, name, provider, accounts, ticks):
    active = Server(executable, root, name, provider, accounts, ticks)
    try:
        poll(lambda: active.reader.discovery(), duration=60, process=active.process)
        assert active.diagnostics()["port"] == active.udp
        yield active
    except BaseException:
        active.finish(True)
        raise
    else:
        active.finish()


def client_args(server, player):
    return ["--join", "127.0.0.1", "--gameplay-port", server.udp, "--world", "integration-world", "--player", player]


class Player:
    def __init__(self, server, root, name, password):
        root.mkdir()
        self.name = name
        self.command, self.output = root / "commands.json", root / "client.json"
        self.log = (root / "client.log").open("w+b")
        args = [*client_args(server, name), "--commands", self.command, "--client-output", self.output]
        self.process = subprocess.Popen([str(server.executable), *map(str, args)],
                                        env=dict(os.environ, DJEHUTI_GAMEPLAY_SECRET=password),
                                        cwd=server.executable.parent, stdout=self.log, stderr=subprocess.STDOUT,
                                        creationflags=FLAGS)

    def state(self):
        return load(self.output)

    def active(self):
        def joined():
            state = self.state()
            return state if state.get("state") == "active" else None
        state = poll(joined, process=self.process)
        assert state["player"] == self.name and state["entity"], state
        return state

    def commands(self, **values):
        set_commands(self.command, **values)

    def stop(self, hard=False):
        if self.process.poll() is None:
            if hard:
                self.process.kill()
            else:
                self.commands(exit=True)
            self.process.wait(timeout=15)
        if not self.log.closed:
            self.log.close()


def new_player(stack, server, root, passwords, name, suffix):
    player = Player(server, root / f"{name}-{suffix}", name, passwords[name])
    stack.callback(player.stop)
    return player


def verify_local(executable, root, provider):
    root.mkdir()
    command, output = root / "command.json", root / "state.json"
    set_commands(command, x=500, name="push", target="shared")
    args = [*base_args(root, provider), "--local", "--ticks", "16", "--commands", command, "--client-output", output]
    env = dict(os.environ, DJEHUTI_DEV_ENABLE="0")
    run(executable, args, env)
    first = load(output)
    assert total(first) == 1 and pose(first, first["entity"])[0] > -1.5, first
    set_commands(command)
    run(executable, args, env)
    second = load(output)
    if provider == "memory":
        assert int(second["tick"]) == 16 and total(second) == 0, second
    else:
        assert first["entity"] == second["entity"] and int(second["tick"]) == 32, second
        assert total(second) == 1 and abs(pose(second, second["entity"])[0] - pose(first, first["entity"])[0]) < 0.0001


def move_observed(mover, watcher):
    identity = poll(lambda: mover.state().get("entity"))
    before = poll(lambda: pose(watcher.state(), identity))[0]
    started = time.monotonic()
    mover.commands(x=1000)
    poll(lambda: pose(watcher.state(), identity)[0] - before > 0.2)
    mover.commands(x=0)
    return (time.monotonic() - started) * 1000.0


def verify_remote(executable, root, provider, wire_checks):
    root.mkdir()
    passwords = {name: secrets.token_hex(24) for name in ("alice", "bob")}
    accounts = root / "accounts.json"
    accounts.write_text(json.dumps({name: {"secret": secret, "move": True, "request": name == "alice"}
                                    for name, secret in passwords.items()}), encoding="utf-8")
    with server(executable, root, "first", provider, accounts, 6) as active:
        assert result(active.reader, "persistence.info")["provider"] == provider
        with ExitStack() as stack:
            alice = new_player(stack, active, root, passwords, "alice", "first")
            bob = new_player(stack, active, root, passwords, "bob", "first")
            initial_a, initial_b = alice.active(), bob.active()
            assert initial_a["entity"] != initial_b["entity"]
            latencies = [move_observed(alice, bob), move_observed(bob, alice)]
            if wire_checks:
                for case in ("version", "malformed", "budget"):
                    run(executable, [*client_args(active, "alice"), "--protocol-check", case])
                for suffix, password, expected in (
                    ("duplicate", passwords["alice"], "admission_rejected"),
                    ("bad-secret", secrets.token_hex(24), "invalid_identity"),
                ):
                    candidate = Player(active, root / suffix, "alice", password)
                    try:
                        poll(lambda: candidate.state().get("state") == "rejected")
                        assert candidate.state()["reason"] == expected, candidate.state()
                    finally:
                        candidate.stop()

            bob.commands(request=1, name="push", target="shared")
            rejected = poll(lambda: (state if (state := bob.state()).get("resultSeq") == 1 else None))
            assert not rejected["resultOk"], rejected
            alice.commands(request=1, name="push", target="shared", duplicate=True)
            poll(lambda: alice.state().get("resultSeq") == 1 and alice.state().get("resultOk"))
            poll(lambda: total(bob.state()) == 1 and pose(bob.state(), "shared")[1] > 0.65)
            poll(lambda: total(alice.state()) == 1)
            def comparable_poses():
                left, right = alice.state(), bob.state()
                left_pose, right_pose = pose(left, "shared"), pose(right, "shared")
                return (left_pose, right_pose) if left_pose and right_pose and left.get("tick") == right.get("tick") else None

            left, right = poll(comparable_poses)
            assert all(abs(a - b) < 0.0001 for a, b in zip(left, right)), (left, right)
            alice.commands(request=2, name="push", target="nonexistent", resync=1)
            rejected = poll(lambda: (state if (state := alice.state()).get("resultSeq") == 2 else None))
            assert not rejected["resultOk"], rejected
            poll(lambda: alice.state().get("base", 0) > initial_a["base"])
            assert total(bob.state()) == 1

            previous = poll(lambda: bob.state().get("tick"))
            alice.stop(hard=True)
            poll(lambda: bob.state()["tick"] > previous + 2)
            poll(lambda: not any(client["player"] == "alice" for client in active.diagnostics()["clients"]), duration=20)
            returning = new_player(stack, active, root, passwords, "alice", "rejoin")
            rejoined = returning.active()
            assert rejoined["entity"] == initial_a["entity"] and total(rejoined) == 1
            assert pose(rejoined, initial_a["entity"])[0] > pose(initial_a, initial_a["entity"])[0] + 0.15
            metrics = active.diagnostics()
            assert int(metrics["rejected"]) >= 2 and int(metrics["sentBytes"]) > 0 and int(metrics["syncFailures"]) == 0
        assert active.stop() == 0

    if provider != "memory":
        with server(executable, root, "graceful-restart", provider, accounts, 3600) as active, ExitStack() as stack:
            alice = new_player(stack, active, root, passwords, "alice", "graceful")
            bob = new_player(stack, active, root, passwords, "bob", "graceful")
            state_a, state_b = alice.active(), bob.active()
            assert state_a["entity"] == rejoined["entity"] and state_a["tick"] > rejoined["tick"]
            assert total(state_a) == total(state_b) == 1
            assert abs(pose(state_a, state_a["entity"])[0] - pose(rejoined, rejoined["entity"])[0]) < 0.0001
            assert active.record("application", "player.5.alice.requests")
            committed = active.diagnostics()["revision"]
            alice.commands(request=1, name="push", target="shared")
            poll(lambda: total(bob.state()) == 2 and alice.state()["application"]["personal"] == 2)
            assert active.diagnostics()["revision"] == committed
            active.stop(crash=True)

        with server(executable, root, "crash-restart", provider, accounts, 6) as active:
            with ExitStack() as stack:
                alice = new_player(stack, active, root, passwords, "alice", "recovered")
                bob = new_player(stack, active, root, passwords, "bob", "recovered")
                state_a, state_b = alice.active(), bob.active()
                assert state_a["entity"] == rejoined["entity"] and state_b["entity"] == initial_b["entity"]
                assert total(state_a) == total(state_b) == 1
                alice.commands(request=1, name="push", target="shared")
                poll(lambda: total(bob.state()) == 2 and (alice.state().get("application") or {}).get("personal") == 2)
            assert active.stop() == 0

    print(f"{provider}: gameplay and configured recovery passed. Movement observation median "
          f"{statistics.median(latencies):.0f} ms, server mean tick {metrics['meanTickMicros']} us, "
          f"maximum tick {metrics['maxTickMicros']} us.", flush=True)


def verify_desktop(executable, editor, root):
    root.mkdir()
    passwords = {name: secrets.token_hex(24) for name in ("alice", "bob")}
    accounts = root / "accounts.json"
    accounts.write_text(json.dumps({name: {"secret": secret, "move": True, "request": True}
                                    for name, secret in passwords.items()}), encoding="utf-8")
    with server(executable, root, "desktop", "memory", accounts, 30) as active, ExitStack() as stack:
        processes = []
        for name, secret in passwords.items():
            process = subprocess.Popen([str(editor), *map(str, client_args(active, name))],
                                       env=dict(os.environ, DJEHUTI_GAMEPLAY_SECRET=secret), cwd=editor.parent,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(process)

            def close(proc=process):
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=15)
            stack.callback(close)

        def joined():
            if any(process.poll() is not None for process in processes):
                raise AssertionError(f"Desktop client exited early: {[process.poll() for process in processes]}")
            active_players = {client["player"] for client in active.diagnostics()["clients"] if client["state"] == "active"}
            return active_players == set(passwords)

        poll(joined, duration=45)
        metrics = active.diagnostics()
        assert int(metrics["sentPackets"]) >= 6 and int(metrics["syncFailures"]) == 0, metrics
        assert active.stop() == 0
    print("Two independent desktop game windows connected and received authoritative gameplay state.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--editor", type=Path)
    parser.add_argument("--provider", choices=("memory", "sqlite", "rocksdb"), action="append")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work) as work:
        root = Path(work).resolve()
        if args.editor:
            verify_desktop(args.server.resolve(), args.editor.resolve(), root / "desktop")
            return
        for provider in args.provider or ("memory", "sqlite", "rocksdb"):
            verify_local(args.server.resolve(), root / f"local-{provider}", provider)
            verify_remote(args.server.resolve(), root / f"remote-{provider}", provider, provider == "memory")


if __name__ == "__main__":
    main()
