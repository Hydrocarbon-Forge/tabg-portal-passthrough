#!/usr/bin/env python3
"""link.py — the passthrough WebSocket hub (slice 2).

A minimal local message bus on ``ws://127.0.0.1:25600`` that relays the
passthrough protocol (see ``protocol.md``) between a **host** (TABG), a **guest**
(Portal) and **tool** clients (pose_orbit / director / tests).

Routing:
    host/tool -> all guests
    guest     -> all hosts and tools
    hello/bye -> handled by the hub, not relayed.

Per-peer bounded outgoing queue with ring-buffer drop-oldest back-pressure, so
one slow reader never stalls the 60 Hz bus (cam/echo are latest-wins).

Pure stdlib + the ``websockets`` package (already required by the reference
host tools). 127.0.0.1 only — single-machine dev tool, no auth by design.

    python3 src/link/link.py [--host 127.0.0.1] [--port 25600] [--queue 240] [-v]
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import signal
from collections import deque
from itertools import count

from websockets.asyncio.server import serve, ServerConnection

VERSION = 1
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 25600
DEFAULT_QUEUE = 240  # ~4 s at 60 Hz
HUB_CAPS = ["relay", "ring", "backpressure"]
ROLES = ("host", "guest", "tool")
# Who receives a message from a peer of a given role. A `tool` (director /
# pose_orbit / tests) is a controller: it drives both engines, so its messages
# reach host and guest. The guest's upstream reaches host and tools.
_TARGETS = {
    "host": ("guest", "tool"),
    "tool": ("host", "guest"),
    "guest": ("host", "tool"),
}

log = logging.getLogger("link")


class Peer:
    """A connected client with a bounded, drop-oldest outgoing queue."""

    _ids = count(1)

    def __init__(self, ws: ServerConnection, maxlen: int):
        self.id = next(self._ids)
        self.ws = ws
        self.role = "tool"
        self.name = "?"
        self.ver = 0
        self.caps: list[str] = []
        self._q: deque[str] = deque()
        self._maxlen = maxlen
        self._event = asyncio.Event()
        self.dropped = 0
        self.sent = 0
        self.recv = 0

    def enqueue(self, data: str, *, priority: bool = False) -> None:
        """Queue a frame for this peer. Drops the oldest on overflow (ring buffer).

        ``priority`` frames (hello/bye) are never dropped to make room and are
        not themselves dropped by overflow of normal frames.
        """
        if not priority and len(self._q) >= self._maxlen:
            self._q.popleft()
            self.dropped += 1
        self._q.append(data)
        self._event.set()

    async def pump(self) -> None:
        """Drain the queue to the socket, waiting when empty."""
        while True:
            if not self._q:
                self._event.clear()
                await self._event.wait()
                continue
            data = self._q.popleft()
            await self.ws.send(data)
            self.sent += 1

    def __repr__(self) -> str:
        return f"Peer#{self.id}({self.role}:{self.name})"


class Hub:
    def __init__(self, maxlen: int = DEFAULT_QUEUE):
        self.peers: dict[int, Peer] = {}
        self.maxlen = maxlen

    def counts(self) -> dict[str, int]:
        c = {r: 0 for r in ROLES}
        for p in self.peers.values():
            c[p.role] = c.get(p.role, 0) + 1
        return c

    async def handler(self, ws: ServerConnection) -> None:
        peer = Peer(ws, self.maxlen)
        self.peers[peer.id] = peer
        pump_task = asyncio.create_task(peer.pump())
        log.info("connect %s (path=%s) peers=%s", peer, _path(ws), self.counts())
        try:
            # First frame must be hello.
            try:
                first = await ws.recv()
            except Exception:
                return
            if not self._do_hello(peer, first):
                return
            async for raw in ws:
                peer.recv += 1
                self._route(peer, raw)
        except Exception as exc:  # pragma: no cover - connection teardown noise
            log.debug("peer %s error: %r", peer, exc)
        finally:
            pump_task.cancel()
            self.peers.pop(peer.id, None)
            log.info("disconnect %s sent=%d recv=%d dropped=%d peers=%s",
                     peer, peer.sent, peer.recv, peer.dropped, self.counts())

    def _do_hello(self, peer: Peer, raw) -> bool:
        try:
            msg = json.loads(raw)
        except (ValueError, TypeError):
            log.warning("%s: first frame not JSON; dropping", peer)
            return False
        if not isinstance(msg, dict) or msg.get("t") != "hello":
            log.warning("%s: first frame was %r, expected hello", peer, _t(msg))
            return False
        role = msg.get("role", "tool")
        peer.role = role if role in ROLES else "tool"
        peer.name = str(msg.get("name", "?"))[:64]
        peer.ver = int(msg.get("ver", 0) or 0)
        caps = msg.get("caps", [])
        peer.caps = [str(c) for c in caps] if isinstance(caps, list) else []
        reply = json.dumps({
            "t": "hello", "role": "hub", "ver": VERSION,
            "peers": self.counts(), "caps": HUB_CAPS,
        })
        peer.enqueue(reply, priority=True)
        log.info("hello %s ver=%d caps=%s", peer, peer.ver, peer.caps)
        return True

    def _route(self, sender: Peer, raw) -> None:
        # Validate just enough to route; relay verbatim otherwise.
        try:
            msg = json.loads(raw)
            t = msg.get("t") if isinstance(msg, dict) else None
        except (ValueError, TypeError):
            log.debug("%s: non-JSON frame dropped", sender)
            return
        if t == "hello":
            return  # ignore duplicate handshakes
        if t == "bye":
            # advisory; forward so peers can react, then let the socket close.
            pass
        targets = _TARGETS.get(sender.role, ())
        data = raw if isinstance(raw, str) else raw.decode("utf-8", "replace")
        priority = t == "bye"
        for p in self.peers.values():
            if p.id == sender.id or p.role not in targets:
                continue
            p.enqueue(data, priority=priority)


def _path(ws: ServerConnection) -> str:
    try:
        return ws.request.path  # type: ignore[union-attr]
    except Exception:
        return "/"


def _t(msg) -> object:
    return msg.get("t") if isinstance(msg, dict) else type(msg).__name__


async def serve_forever(host: str, port: int, maxlen: int) -> None:
    hub = Hub(maxlen)
    stop = asyncio.get_running_loop().create_future()

    def _sig():
        if not stop.done():
            stop.set_result(None)
    for s in (signal.SIGINT, signal.SIGTERM):
        try:
            asyncio.get_running_loop().add_signal_handler(s, _sig)
        except (NotImplementedError, ValueError):  # e.g. non-main thread / Windows
            pass

    async with serve(hub.handler, host, port, max_size=None, ping_interval=20):
        log.info("link hub listening on ws://%s:%d (queue=%d)", host, port, maxlen)
        await stop
    log.info("link hub stopped")


def main() -> None:
    ap = argparse.ArgumentParser(description="passthrough WebSocket hub")
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--queue", type=int, default=DEFAULT_QUEUE,
                    help="per-peer outgoing queue depth (ring buffer)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    try:
        asyncio.run(serve_forever(args.host, args.port, args.queue))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
