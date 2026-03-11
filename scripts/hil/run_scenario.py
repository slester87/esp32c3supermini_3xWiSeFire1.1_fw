#!/usr/bin/env python3
"""Drive deterministic HIL websocket scenarios against the device."""

from __future__ import annotations

import argparse
import base64
import contextlib
import hashlib
import json
import os
import secrets
import socket
import struct
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCENARIO_DIR = ROOT / "scripts" / "hil" / "scenarios"


class WsClient:
    def __init__(self, host: str, port: int, path: str, timeout_s: float) -> None:
        self.host = host
        self.port = port
        self.path = path
        self.timeout_s = timeout_s
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        sock = socket.create_connection((self.host, self.port), timeout=self.timeout_s)
        sock.settimeout(self.timeout_s)

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        req = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        sock.sendall(req.encode("ascii"))

        response = self._recv_http_headers(sock)
        if " 101 " not in response.splitlines()[0]:
            raise RuntimeError(f"websocket upgrade failed: {response.splitlines()[0]}")

        accept = self._header_value(response, "sec-websocket-accept")
        expected = base64.b64encode(
            hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
        ).decode("ascii")
        if accept != expected:
            raise RuntimeError("websocket accept key mismatch")

        self.sock = sock

    def close(self) -> None:
        if not self.sock:
            return
        with contextlib.suppress(OSError):
            self._send_frame(0x8, b"")
        self.sock.close()
        self.sock = None

    def send_text(self, message: str) -> None:
        self._send_frame(0x1, message.encode("utf-8"))

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        if not self.sock:
            raise RuntimeError("websocket is not connected")

        fin_opcode = 0x80 | opcode
        mask_bit = 0x80
        payload_len = len(payload)
        mask_key = secrets.token_bytes(4)

        header = bytearray([fin_opcode])
        if payload_len < 126:
            header.append(mask_bit | payload_len)
        elif payload_len <= 0xFFFF:
            header.append(mask_bit | 126)
            header.extend(struct.pack("!H", payload_len))
        else:
            header.append(mask_bit | 127)
            header.extend(struct.pack("!Q", payload_len))

        masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask_key + masked)

    @staticmethod
    def _recv_http_headers(sock: socket.socket) -> str:
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
            if b"\r\n\r\n" in b"".join(chunks):
                break
        return b"".join(chunks).decode("iso-8859-1")

    @staticmethod
    def _header_value(response: str, name: str) -> str:
        prefix = f"{name.lower()}:"
        for line in response.splitlines()[1:]:
            if line.lower().startswith(prefix):
                return line.split(":", 1)[1].strip()
        raise RuntimeError(f"missing response header {name}")


def monotonic_ms() -> int:
    return time.monotonic_ns() // 1_000_000


def load_scenario(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text())
    if "steps" not in data or not isinstance(data["steps"], list):
        raise ValueError(f"scenario is missing steps: {path}")
    return data


def run_step(
    step: dict[str, Any],
    ws: WsClient | None,
    event_log: list[dict[str, Any]],
    hold_interval_ms: int,
) -> WsClient | None:
    action = step["action"]

    if action == "connect":
        if ws is not None:
            raise RuntimeError("connect requested while websocket is already open")
        ws = WsClient(
            host=step["host"],
            port=step.get("port", 80),
            path=step.get("path", "/ws"),
            timeout_s=step.get("timeout_s", 2.0),
        )
        ws.connect()
        event_log.append({"t_ms": monotonic_ms(), "event": "connect"})
        return ws

    if ws is None:
        raise RuntimeError(f"action {action!r} requires an active websocket")

    if action == "send":
        message = step["message"]
        ws.send_text(message)
        event_log.append({"t_ms": monotonic_ms(), "event": "send", "message": message})
        return ws

    if action == "sleep":
        duration_ms = int(step["duration_ms"])
        time.sleep(duration_ms / 1000.0)
        event_log.append({"t_ms": monotonic_ms(), "event": "sleep", "duration_ms": duration_ms})
        return ws

    if action == "hold_for":
        duration_ms = int(step["duration_ms"])
        message = step.get("message", "HOLD")
        deadline = time.monotonic() + (duration_ms / 1000.0)
        while True:
            now = time.monotonic()
            if now >= deadline:
                break
            ws.send_text(message)
            event_log.append({"t_ms": monotonic_ms(), "event": "send", "message": message})
            time.sleep(hold_interval_ms / 1000.0)
        return ws

    if action == "disconnect":
        ws.close()
        event_log.append({"t_ms": monotonic_ms(), "event": "disconnect"})
        return None

    raise RuntimeError(f"unsupported action: {action}")


def materialize_steps(
    scenario: dict[str, Any],
    host: str,
    port: int,
    path: str,
    timeout_s: float,
) -> list[dict[str, Any]]:
    steps: list[dict[str, Any]] = []
    for step in scenario["steps"]:
        if step["action"] == "connect":
            materialized = dict(step)
            materialized["host"] = host
            materialized["port"] = port
            materialized["path"] = path
            materialized["timeout_s"] = timeout_s
            steps.append(materialized)
        else:
            steps.append(step)
    return steps


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a websocket HIL scenario")
    parser.add_argument("scenario", help="Scenario file name or absolute path")
    parser.add_argument("--host", default="192.168.4.1", help="Device host")
    parser.add_argument("--port", type=int, default=80, help="Device port")
    parser.add_argument("--path", default="/ws", help="Websocket path")
    parser.add_argument(
        "--hold-interval-ms",
        type=int,
        default=50,
        help="Interval between HOLD frames",
    )
    parser.add_argument("--timeout-s", type=float, default=2.0, help="Socket timeout")
    parser.add_argument("--repeat", type=int, default=1, help="Scenario iterations")
    parser.add_argument("--log-json", type=Path, help="Write event log JSON")
    args = parser.parse_args()

    scenario_path = Path(args.scenario)
    if not scenario_path.exists():
        scenario_path = DEFAULT_SCENARIO_DIR / args.scenario
    scenario = load_scenario(scenario_path)

    runs: list[dict[str, Any]] = []
    steps = materialize_steps(scenario, args.host, args.port, args.path, args.timeout_s)

    for iteration in range(args.repeat):
        event_log: list[dict[str, Any]] = []
        ws: WsClient | None = None
        event_log.append(
            {
                "t_ms": monotonic_ms(),
                "event": "scenario_start",
                "scenario": scenario.get("name", scenario_path.stem),
                "iteration": iteration + 1,
            }
        )

        try:
            for step in steps:
                ws = run_step(step, ws, event_log, args.hold_interval_ms)
        finally:
            if ws is not None:
                ws.close()
                event_log.append({"t_ms": monotonic_ms(), "event": "disconnect"})

        event_log.append({"t_ms": monotonic_ms(), "event": "scenario_end"})
        runs.append({"iteration": iteration + 1, "events": event_log})

    result = {
        "scenario": scenario.get("name", scenario_path.stem),
        "description": scenario.get("description", ""),
        "hold_interval_ms": args.hold_interval_ms,
        "host": args.host,
        "port": args.port,
        "path": args.path,
        "runs": runs,
    }

    if args.log_json:
        args.log_json.write_text(json.dumps(result, indent=2) + "\n")
    else:
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
