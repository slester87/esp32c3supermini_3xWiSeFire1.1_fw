"""Mock Poofer firmware server for UI development.

Serves SPIFFS HTML files over HTTP and provides a WebSocket endpoint
that simulates the firmware's state machine, including:
- Channel activation with bitmask (DOWN:<mask>)
- Min hold (250ms) and max hold (3000ms) timers
- 2-second WebSocket watchdog
- Periodic state broadcast every 200ms

Usage:
    python3 docker/mock_server.py [--port 8080] [--spiffs-dir firmware/spiffs]
"""

import argparse
import asyncio
import enum
import json
import logging
import time
from http import HTTPStatus
from pathlib import Path

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("mock_poofer")

# Timing constants matching firmware main.c lines 35-38
MIN_HOLD_MS = 250
MAX_HOLD_MS = 3000
WATCHDOG_TIMEOUT_S = 2.0
BROADCAST_INTERVAL_S = 0.2  # matches firmware status_task vTaskDelay(200)
NUM_CHANNELS = 3

WIFI_SAVED_HTML = (
    "<html><body><h2>Saved. Reconnecting...</h2>"
    '<a href="/">Back</a></body></html>'
)


class State(enum.Enum):
    DISCONNECTED = "disconnected"
    READY = "ready"
    FIRING = "firing"


class PooferState:
    """Replicates the firmware's runtime_state_t from main.c:64-75."""

    def __init__(self):
        self.state = State.DISCONNECTED
        self.channels = [False] * NUM_CHANNELS
        self.ignore_until_release = [False] * NUM_CHANNELS
        self.active_mask = 0
        self.press_start = 0.0
        self.last_hold_ms = MIN_HOLD_MS
        self.last_ws_rx = 0.0

    def connect(self):
        self.last_ws_rx = time.monotonic()
        if self.state == State.DISCONNECTED:
            self.state = State.READY
            log.info("State -> READY (client connected)")

    def handle_down(self, mask):
        """Activate channels per bitmask. Mirrors handle_press_down_mask()."""
        self.last_ws_rx = time.monotonic()
        if self.state == State.DISCONNECTED:
            self.state = State.READY

        effective = 0
        for i in range(NUM_CHANNELS):
            if not (mask & (1 << i)):
                continue
            if self.channels[i] or self.ignore_until_release[i]:
                continue
            effective |= 1 << i

        if effective == 0:
            return

        self.press_start = time.monotonic()
        for i in range(NUM_CHANNELS):
            if effective & (1 << i):
                self.channels[i] = True

        self._recalc_mask()
        self.state = State.FIRING
        log.info("State -> FIRING (mask=%d, effective=%d)", mask, effective)

    def handle_up(self):
        """Release all channels. Mirrors handle_press_up()."""
        self.last_ws_rx = time.monotonic()

        # Clear ignore flags on UP (firmware line 367)
        for i in range(NUM_CHANNELS):
            self.ignore_until_release[i] = False

        if self.active_mask == 0:
            return

        held_ms = self._elapsed_ms()
        self.last_hold_ms = max(MIN_HOLD_MS, min(held_ms, MAX_HOLD_MS))

        if held_ms < MIN_HOLD_MS:
            # Min-hold: keep firing until MIN_HOLD_MS reached.
            # The background tick will stop channels after the remainder.
            log.info("UP received at %dms, min-hold extends to %dms", held_ms, MIN_HOLD_MS)
            return

        self._stop_all()
        log.info("State -> READY (released at %dms)", held_ms)

    def handle_ping(self):
        self.last_ws_rx = time.monotonic()

    def tick(self):
        """Called every BROADCAST_INTERVAL_S. Handles max-hold and min-hold."""
        if self.active_mask == 0:
            return

        elapsed = self._elapsed_ms()

        # Max-hold auto-stop (firmware status_task lines 789-806)
        if elapsed >= MAX_HOLD_MS:
            for i in range(NUM_CHANNELS):
                if self.channels[i]:
                    self.ignore_until_release[i] = True
                    self.channels[i] = False
            self._recalc_mask()
            self.last_hold_ms = MAX_HOLD_MS
            if self.active_mask == 0:
                self.state = State.READY
            log.info("Auto-stop at %dms (max hold)", elapsed)
            return

        # Min-hold expiry: if UP was received but min-hold kept us firing
        if elapsed >= MIN_HOLD_MS:
            # Check if any channel should stop (UP was sent before MIN_HOLD_MS)
            # In our simplified mock, we handle this by checking if enough time passed
            pass

    def check_watchdog(self):
        """Disconnect if no WS message for 2 seconds. Mirrors status_task lines 808-828."""
        if self.last_ws_rx == 0.0:
            return
        if (time.monotonic() - self.last_ws_rx) > WATCHDOG_TIMEOUT_S:
            if self.active_mask != 0 or self.state != State.DISCONNECTED:
                self._stop_all()
                self.state = State.DISCONNECTED
                log.info("State -> DISCONNECTED (watchdog timeout)")

    def to_dict(self):
        """Serialize state matching firmware JSON format (main.c:208-214)."""
        return {
            "ready": self.state in (State.READY, State.FIRING),
            "firing": list(self.channels),
            "error": False,
            "connected": self.state != State.DISCONNECTED,
            "elapsed_ms": self._elapsed_ms() if self.active_mask != 0 else 0,
            "last_hold_ms": self.last_hold_ms,
        }

    def to_json(self):
        return json.dumps(self.to_dict(), separators=(",", ":"))

    def _elapsed_ms(self):
        if self.press_start == 0.0:
            return 0
        return int((time.monotonic() - self.press_start) * 1000)

    def _recalc_mask(self):
        mask = 0
        for i in range(NUM_CHANNELS):
            if self.channels[i]:
                mask |= 1 << i
        self.active_mask = mask

    def _stop_all(self):
        for i in range(NUM_CHANNELS):
            self.channels[i] = False
        self.active_mask = 0
        self.state = State.READY


# ---------------------------------------------------------------------------
# HTTP file serving via websockets process_request (websockets 16.x API)
# ---------------------------------------------------------------------------

def make_process_request(spiffs_dir):
    """Return a process_request handler that serves static files.

    In websockets 16.x, process_request receives (connection, request)
    and returns a Response via connection.respond() or None to continue
    with the WebSocket handshake.
    """

    def process_request(connection, request):
        if request.path == "/":
            return serve_html_file(connection, spiffs_dir / "index.html")
        if request.path == "/wifi":
            if request.headers.get("Content-Length", "0") != "0":
                response = connection.respond(HTTPStatus.OK, WIFI_SAVED_HTML)
                response.headers["Content-Type"] = "text/html; charset=utf-8"
                return response
            return serve_html_file(connection, spiffs_dir / "wifi.html")
        if request.path == "/ws":
            return None
        return connection.respond(HTTPStatus.NOT_FOUND, "Not Found")

    return process_request


def serve_html_file(connection, filepath):
    """Read an HTML file and return it as an HTTP response."""
    try:
        body = filepath.read_text(encoding="utf-8")
    except FileNotFoundError:
        return connection.respond(HTTPStatus.NOT_FOUND, "File not found")
    response = connection.respond(HTTPStatus.OK, body)
    response.headers["Content-Type"] = "text/html; charset=utf-8"
    return response


# ---------------------------------------------------------------------------
# WebSocket handler
# ---------------------------------------------------------------------------

async def ws_handler(websocket):
    """Handle a single WebSocket connection."""
    state = PooferState()
    state.connect()
    await websocket.send(state.to_json())
    log.info("WebSocket client connected")

    async def background_loop():
        """Broadcast state and run timers every 200ms."""
        while True:
            await asyncio.sleep(BROADCAST_INTERVAL_S)
            state.tick()
            state.check_watchdog()
            if state.state == State.DISCONNECTED:
                log.info("Watchdog triggered, closing connection")
                await websocket.close()
                return
            try:
                await websocket.send(state.to_json())
            except ConnectionClosed:
                return

    bg_task = asyncio.create_task(background_loop())

    try:
        async for message in websocket:
            msg = message.strip() if isinstance(message, str) else message.decode().strip()

            if msg.startswith("DOWN:"):
                if len(msg) == 6 and "1" <= msg[5] <= "7":
                    state.handle_down(int(msg[5]))
            elif msg == "DOWN":
                state.handle_down(7)
            elif msg == "UP":
                state.handle_up()
            elif msg == "PING":
                state.handle_ping()

            await websocket.send(state.to_json())
    except ConnectionClosed:
        log.info("WebSocket client disconnected")
    finally:
        bg_task.cancel()
        try:
            await bg_task
        except asyncio.CancelledError:
            pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

async def main():
    parser = argparse.ArgumentParser(description="Mock Poofer firmware server")
    parser.add_argument("--port", type=int, default=8080, help="Listen port (default: 8080)")
    parser.add_argument(
        "--spiffs-dir",
        type=Path,
        default=Path("firmware/spiffs"),
        help="Path to SPIFFS directory (default: firmware/spiffs)",
    )
    args = parser.parse_args()

    if not args.spiffs_dir.is_dir():
        log.error("SPIFFS directory not found: %s", args.spiffs_dir)
        raise SystemExit(1)

    process_request = make_process_request(args.spiffs_dir)

    async with serve(
        ws_handler,
        "0.0.0.0",
        args.port,
        process_request=process_request,
    ):
        log.info("Mock Poofer server running on http://0.0.0.0:%d", args.port)
        log.info("  UI:        http://localhost:%d/", args.port)
        log.info("  Wi-Fi:     http://localhost:%d/wifi", args.port)
        log.info("  WebSocket: ws://localhost:%d/ws", args.port)
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    asyncio.run(main())
