"""MCP stdio adapter for the authenticated Djehuti Engine Developer API."""

import http.client
import json
import sys
import uuid

sys.dont_write_bytecode = True
from developer_api import ApiError, Client  # noqa: E402


PROTOCOL = "2025-11-25"
JOB_TOOL = "developer.job"
MAX_MESSAGE = 1048576


class RpcError(Exception):
    def __init__(self, code, message):
        self.code = code
        self.message = message


def send(message):
    sys.stdout.buffer.write(json.dumps(message, ensure_ascii=True).encode("ascii") + b"\n")
    sys.stdout.buffer.flush()


class Adapter:
    def __init__(self, client):
        self.client = client
        self.initialized = False
        self.ready = False

    def tools(self):
        discovery = self.client.discovery()
        tools = [
            {
                "name": command["id"],
                "description": command["id"] + " on the Engine " + discovery["processMode"],
                "inputSchema": command["parameters"],
                "outputSchema": command["result"],
            }
            for command in discovery["commands"]
            if command["available"] and command["allowed"]
        ]
        tools.append({
            "name": JOB_TOOL,
            "description": "Recover the status and result of an Engine Developer API request.",
            "inputSchema": {
                "type": "object",
                "properties": {"id": {"type": "string", "pattern": "^[A-Za-z0-9_-]{16,64}$"}},
                "required": ["id"],
                "additionalProperties": False,
            },
        })
        return tools

    def call_tool(self, params):
        name = params.get("name")
        arguments = params.get("arguments", {})
        metadata = params.get("_meta", {})
        if not isinstance(name, str) or not isinstance(arguments, dict) or not isinstance(metadata, dict):
            raise RpcError(-32602, "Expected a tool name, an arguments object, and optional metadata.")
        request_id = metadata.get("org.djehuti/requestId", uuid.uuid4().hex)
        try:
            if name == JOB_TOOL:
                content = self.client.job(arguments.get("id", ""))
                failed = False
            else:
                job = self.client.run(name, arguments, request_id)
                failed = job["state"] != "succeeded"
                content = job if failed else job["result"]
        except ApiError as error:
            failed, content = True, {"error": error.error}
        except (ValueError, TypeError):
            raise RpcError(-32602, "Invalid Developer API job or request identifier.") from None
        return {"content": [{"type": "text", "text": json.dumps(content)}], "structuredContent": content, "isError": failed}

    def dispatch(self, method, params):
        if method == "initialize":
            if self.initialized or not isinstance(params.get("protocolVersion"), str):
                raise RpcError(-32602, "Invalid initialization.")
            self.initialized = True
            requested = params["protocolVersion"]
            return {
                "protocolVersion": requested if requested in {PROTOCOL, "2025-06-18", "2025-03-26"} else PROTOCOL,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "djehuti-engine-developer", "version": "1.0.0"},
            }
        if method == "notifications/initialized" and self.initialized:
            self.ready = True
            return {}
        if method == "ping":
            return {}
        if not self.ready:
            raise RpcError(-32002, "MCP initialization is required.")
        if method == "tools/list":
            return {"tools": self.tools()}
        if method == "tools/call":
            return self.call_tool(params)
        raise RpcError(-32601, "Unknown method.")

    def handle(self, message):
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid request."}})
            return
        response = {"jsonrpc": "2.0", "id": message.get("id")}
        try:
            params = message.get("params", {})
            if not isinstance(params, dict):
                raise RpcError(-32602, "Expected an object for params.")
            response["result"] = self.dispatch(message["method"], params)
        except RpcError as error:
            response["error"] = {"code": error.code, "message": error.message}
        except ApiError as error:
            response["error"] = {"code": -32000, "message": str(error), "data": error.error}
        except (OSError, http.client.HTTPException, ValueError):
            response["error"] = {"code": -32000, "message": "Developer API connection failed."}
        if "id" in message:
            send(response)


def main():
    try:
        adapter = Adapter(Client.from_environment())
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    while line := sys.stdin.buffer.readline(MAX_MESSAGE + 1):
        if len(line) > MAX_MESSAGE:
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Message exceeds the size limit."}})
            return 1
        try:
            message = json.loads(line)
        except (UnicodeError, ValueError, RecursionError):
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Invalid JSON."}})
            continue
        adapter.handle(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
