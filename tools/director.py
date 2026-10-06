#!/usr/bin/env python3
"""director.py — shot-list driver for the TABG×Portal passthrough (slice 8).

Connects to the link hub (``ws://127.0.0.1:25600``) as a **tool** and runs a
scripted shot list, sending `director` ops (relayed to both engines) and input
(`key`/`slot`) over the protocol in ``src/link/protocol.md``. Host-side ops drive
TABG (teleport/look/time/weather); guest-side ops drive Portal (portal-pair
placement, turret fire, cube launch, cake state). Collects an event log (guest
`event`/`explosion` messages, timestamped) for the title cards in ``cut.py``.

    python3 tools/director.py run shots/demo.json [--url ws://127.0.0.1:25600]
    python3 tools/director.py op portal_pair '{"a":{...},"b":{...}}'
    python3 tools/director.py op teleport '{"pos":[0,0,0],"yaw":0}'

A shot list is JSON: {"origin":[x,y,z], "h_in":0.0, "steps":[[op, args...], ...]}.
Positions in steps are [right, forward, up] metres from ``origin`` in the frame
of heading ``h_in`` (so the whole sequence moves/turns by changing two numbers);
with no origin they are absolute host coordinates. See ``shots/demo.json``.

Step kinds (host): ["time", h, m], ["weather", w], ["teleport", [r,f,u], dh],
  ["look", yaw, pitch, secs], ["wait", s], ["mark", name], ["slot", n],
  ["key", name, down].
Step kinds (guest Portal): ["portal_pair", aRFU, aNormal, bRFU, bNormal],
  ["turret_fire", id], ["cube_launch", [vx,vy,vz]], ["cake", state],
  ["event_wait", kind, timeout] (block until the guest reports that event).
Raw passthrough: ["director", op, {json}], ["send", {json}].
"""
from __future__ import annotations

import argparse
import asyncio
import json
import math
import sys
import time
from pathlib import Path

from websockets.asyncio.client import connect

DEFAULT_URL = "ws://127.0.0.1:25600"


def smooth(t: float) -> float:
    return t * t * (3 - 2 * t)


class Director:
    def __init__(self, ws):
        self.ws = ws
        self.events: list[tuple[float, str]] = []
        self.t0 = time.time()
        self._waiters: dict[str, asyncio.Event] = {}
        self._last_event: dict[str, dict] = {}
        self._recv_counts: dict[str, int] = {}     # events received per kind
        self._consumed: dict[str, int] = {}        # events consumed by wait_event
        self.reader = asyncio.create_task(self._read())

    async def _read(self):
        try:
            async for raw in self.ws:
                try:
                    msg = json.loads(raw)
                except (ValueError, TypeError):
                    continue
                t = msg.get("t")
                if t == "explosion":
                    self._record("explosion", msg)
                elif t == "event":
                    self._record(str(msg.get("kind", "event")), msg)
        except asyncio.CancelledError:
            pass

    def mark(self, name: str):
        self.events.append((round(time.time() - self.t0, 3), name))

    def _record(self, kind: str, msg: dict):
        self.mark(kind)
        self._last_event[kind] = msg
        self._recv_counts[kind] = self._recv_counts.get(kind, 0) + 1
        w = self._waiters.get(kind)
        if w is not None:
            w.set()

    async def send(self, msg: dict):
        await self.ws.send(json.dumps(msg, separators=(",", ":")))

    async def director(self, op: str, **kw):
        await self.send({"t": "director", "op": op, **kw})

    async def look(self, yaw: float, pitch: float, duration: float = 0.0,
                   start: tuple[float, float] | None = None):
        if duration <= 0 or start is None:
            await self.director("look", yaw=yaw, pitch=pitch)
            return
        y0, p0 = start
        n = max(1, int(duration * 60))
        for i in range(1, n + 1):
            k = smooth(i / n)
            await self.director("look", yaw=y0 + (yaw - y0) * k,
                                pitch=p0 + (pitch - p0) * k)
            await asyncio.sleep(1 / 60)

    async def wait_event(self, kind: str, timeout: float) -> bool:
        # Consume an already-arrived event first (it may have come back before
        # this wait step ran — e.g. a warp fires right after portal placement).
        if self._recv_counts.get(kind, 0) > self._consumed.get(kind, 0):
            self._consumed[kind] = self._consumed.get(kind, 0) + 1
            return True
        ev = self._waiters.setdefault(kind, asyncio.Event())
        ev.clear()
        try:
            await asyncio.wait_for(ev.wait(), timeout)
        except asyncio.TimeoutError:
            return False
        self._consumed[kind] = self._consumed.get(kind, 0) + 1
        return True


def _worlder(origin, h_in):
    absolute = origin is None or origin == "absolute"

    def world(rfu):
        if absolute:
            return list(rfu)
        h = math.radians(h_in)
        fx, fz = -math.sin(h), math.cos(h)     # forward (host Y-up, Z fwd)
        rx, rz = math.cos(h), math.sin(h)      # right
        r, f, u = rfu
        return [origin[0] + rx * r + fx * f, origin[1] + u, origin[2] + rz * r + fz * f]
    return world


async def run_steps(d: Director, steps, origin=None, h_in=0.0):
    world = _worlder(origin, h_in)
    look = (0.0, 0.0)
    for step in steps:
        kind, args = step[0], step[1:]
        if kind == "time":
            await d.director("time", h=args[0], m=args[1] if len(args) > 1 else 0)
        elif kind == "weather":
            await d.director("weather", w=args[0])
        elif kind == "wait":
            await asyncio.sleep(args[0])
        elif kind == "mark":
            d.mark(args[0])
        elif kind == "slot":
            await d.send({"t": "slot", "n": args[0]})
        elif kind == "key":
            await d.send({"t": "key", "code": "key", "name": args[0],
                          "down": bool(args[1]) if len(args) > 1 else True})
        elif kind == "teleport":
            x, y, z = world(args[0])
            await d.director("teleport", pos=[x, y, z], yaw=h_in + (args[1] if len(args) > 1 else 0))
            look = (0.0, 0.0)
        elif kind == "look":
            secs = args[2] if len(args) > 2 else 0.3
            await d.look(args[0], args[1], duration=secs, start=look)
            look = (args[0], args[1])
        elif kind == "portal_pair":
            a_pos, a_n, b_pos, b_n = world(args[0]), args[1], world(args[2]), args[3]
            await d.director("portal_pair",
                             a={"pos": a_pos, "normal": a_n},
                             b={"pos": b_pos, "normal": b_n})
        elif kind == "turret_fire":
            await d.director("turret_fire", id=args[0])
        elif kind == "cube_launch":
            await d.director("cube_launch", vel=args[0])
        elif kind == "cake":
            await d.director("cake", state=args[0])
        elif kind == "event_wait":
            ok = await d.wait_event(args[0], args[1] if len(args) > 1 else 10.0)
            if not ok:
                d.mark(f"timeout:{args[0]}")
        elif kind == "director":
            await d.director(args[0], **(args[1] if len(args) > 1 else {}))
        elif kind == "send":
            await d.send(args[0])
        else:
            print(f"warning: unknown step {kind!r}", file=sys.stderr)


async def main() -> int:
    ap = argparse.ArgumentParser(description="passthrough shot-list driver")
    ap.add_argument("what", choices=["run", "op"])
    ap.add_argument("arg", nargs="?", help="shot-list json file (run) or op name (op)")
    ap.add_argument("opargs", nargs="?", help="op args json (op)")
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--events-out", default=None,
                    help="write the event log here (for cut.py titles)")
    args = ap.parse_args()

    async with connect(args.url, max_size=None) as ws:
        await ws.send(json.dumps({"t": "hello", "role": "tool",
                                  "name": "director", "ver": 1,
                                  "caps": ["director"]}))
        await ws.recv()
        d = Director(ws)
        if args.what == "run":
            cfg = json.load(open(args.arg)) if args.arg else {"steps": []}
            await run_steps(d, cfg.get("steps", []), cfg.get("origin"),
                            cfg.get("h_in", 0.0))
        elif args.what == "op":
            kw = json.loads(args.opargs) if args.opargs else {}
            await d.director(args.arg, **kw)
            await asyncio.sleep(0.3)

        d.reader.cancel()
        log = {"events": d.events}
        out = args.events_out
        if out is None and args.what == "run" and args.arg:
            out = str(Path(args.arg).with_suffix(".events.json"))
        if out:
            Path(out).parent.mkdir(parents=True, exist_ok=True)
            Path(out).write_text(json.dumps(log, indent=2) + "\n")
            print(f"wrote {out}")
        print(json.dumps(log))
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
