"""Command-line client and shared loopback transport for Djehuti Engine."""

import argparse
import http.client
import json
import os
import re
import sys
import time
import uuid


_TERMINAL = {"succeeded", "failed", "cancelled", "timed_out"}


class ApiError(Exception):
    def __init__(self, error):
        super().__init__(error.get("message", "Developer API error"))
        self.error = error


class Client:
    def __init__(self, endpoint, token, request_timeout=2.5):
        match = re.fullmatch(r"http://127\.0\.0\.1:([0-9]{1,5})/?", endpoint)
        if not match or not 1 <= int(match[1]) <= 65535:
            raise ValueError("Use a Developer API URL of the form http://127.0.0.1:PORT.")
        if not token:
            raise ValueError("Set DJEHUTI_DEV_TOKEN to an Engine-issued local credential.")
        self.port = int(match[1])
        self._token = token
        self.request_timeout = request_timeout

    @classmethod
    def from_environment(cls):
        return cls(os.environ.get("DJEHUTI_DEV_URL", ""), os.environ.get("DJEHUTI_DEV_TOKEN", ""))

    def request(self, method, path, body=None):
        headers = {"Authorization": "Bearer " + self._token}
        if body is not None:
            headers["Content-Type"] = "application/json"
            body = json.dumps(body, ensure_ascii=True)
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=self.request_timeout)
        try:
            connection.request(method, path, body, headers)
            response = connection.getresponse()
            raw = response.read(1048577)
            if len(raw) > 1048576:
                raise ApiError({"code": "resource_limit", "message": "HTTP response exceeds the API wire limit."})
            value = json.loads(raw)
            if response.status >= 400:
                raise ApiError(value.get("error", {"code": "http_error", "message": "Developer API request failed."}))
            return value
        finally:
            connection.close()

    def discovery(self):
        return self.request("GET", "/v1/discovery")

    def job(self, request_id, cancel=False):
        if not re.fullmatch(r"[A-Za-z0-9_-]{16,64}", request_id):
            raise ValueError("Invalid job identifier.")
        return self.request("DELETE" if cancel else "GET", "/v1/jobs/" + request_id)

    def submit(self, command, arguments, request_id):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,80}", command):
            raise ValueError("Invalid command identifier.")
        return self.request("POST", "/v1/commands/" + command, {"requestId": request_id, "arguments": arguments})

    def run(self, command, arguments, request_id=None, wait=10):
        request_id = request_id or uuid.uuid4().hex
        deadline = time.monotonic() + wait
        try:
            job = self.submit(command, arguments, request_id)
            while job["state"] not in _TERMINAL:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError()
                time.sleep(min(0.03, remaining))
                job = self.job(request_id)
            return job
        except (OSError, http.client.HTTPException, json.JSONDecodeError) as error:
            raise ApiError({
                "code": "outcome_unknown",
                "message": "Read the job on the same Engine with the same credential or retry this exact request.",
                "details": {"requestId": request_id},
            }) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("discover")
    commands.add_parser("openapi")
    run = commands.add_parser("run")
    run.add_argument("command")
    run.add_argument("arguments", nargs="?", default="{}")
    run.add_argument("--request-id")
    run.add_argument("--wait", default=10.0, type=float)
    for action in ("job", "cancel"):
        commands.add_parser(action).add_argument("id")
    args = parser.parse_args()
    try:
        client = Client.from_environment()
        if args.action == "discover":
            result = client.discovery()
        elif args.action == "openapi":
            result = client.request("GET", "/v1/openapi")
        elif args.action == "run":
            result = client.run(args.command, json.loads(args.arguments), args.request_id, args.wait)
            if result["state"] != "succeeded":
                print(json.dumps(result, indent=2))
                return 1
        else:
            result = client.job(args.id, args.action == "cancel")
        print(json.dumps(result, indent=2))
        return 0
    except ApiError as error:
        print(json.dumps({"error": error.error}), file=sys.stderr)
    except (ValueError, OSError, http.client.HTTPException) as error:
        print(str(error), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
