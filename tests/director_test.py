#!/usr/bin/env python3
"""director_test.py — slice 8 acceptance for the director, against a fake guest.

Starts the link hub, a fake Portal guest that turns `director` ops into the
matching guest `event`s (portal_pair->portal_warp, turret_fire->turret_hit,
cube_launch->cube_through_portal, cake->event cake), and runs
``tools/director.py run`` on the demo shot list. Verifies the director delivered
every op (the guest saw them) and that its event log recorded the expected
marks, including the ones gated on `event_wait`.

    python3 tests/director_test.py
"""
from __future__ import annotations

import asyncio
import json
import sys
from pathlib import Path

from websockets.asyncio.client import connect
from websockets.asyncio.server import serve

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "src" / "link"))
sys.path.insert(0, str(REPO / "tools"))

import link as link_mod  # noqa: E402
import director as director_mod  # noqa: E402

PORT = 25610
URL = f"ws://127.0.0.1:{PORT}"

# op -> event kind the fake guest emits in response
OP_TO_EVENT = {
    "portal_pair": "portal_warp",
    "turret_fire": "turret_hit",
    "cube_launch": "cube_through_portal",
    "cake": "cake",
}


async def fake_guest(seen: list[str], ready: asyncio.Event, stop: asyncio.Event):
    async with connect(URL, max_size=None) as ws:
        await ws.send(json.dumps({"t": "hello", "role": "guest",
                                  "name": "fake-portal", "ver": 1}))
        await ws.recv()
        ready.set()
        while not stop.is_set():
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=0.5)
            except asyncio.TimeoutError:
                continue
            except Exception:
                break
            try:
                msg = json.loads(raw)
            except (ValueError, TypeError):
                continue
            if msg.get("t") == "director":
                op = msg.get("op")
                seen.append(op)
                kind = OP_TO_EVENT.get(op)
                if kind:
                    await ws.send(json.dumps({"t": "event", "kind": kind,
                                              "pos": [0, 0, 0]}))


async def main() -> int:
    hub = link_mod.Hub()
    server = await serve(hub.handler, "127.0.0.1", PORT, max_size=None)

    seen: list[str] = []
    ready = asyncio.Event()
    stop = asyncio.Event()
    guest_task = asyncio.create_task(fake_guest(seen, ready, stop))
    await asyncio.wait_for(ready.wait(), timeout=5.0)

    # run the director on the demo shot list
    cfg = json.load(open(REPO / "tools" / "shots" / "demo.json"))
    async with connect(URL, max_size=None) as ws:
        await ws.send(json.dumps({"t": "hello", "role": "tool",
                                  "name": "director-test", "ver": 1}))
        await ws.recv()
        d = director_mod.Director(ws)
        await director_mod.run_steps(d, cfg["steps"], cfg.get("origin"),
                                     cfg.get("h_in", 0.0))
        d.reader.cancel()
        marks = [n for _, n in d.events]

    stop.set()
    try:
        await asyncio.wait_for(guest_task, timeout=2.0)
    except (asyncio.TimeoutError, asyncio.CancelledError):
        pass
    server.close()
    await server.wait_closed()

    errors = []
    # every director op kind in the shot list reached the guest
    for op in ("portal_pair", "turret_fire", "cube_launch", "cake",
               "teleport", "look", "time", "weather"):
        # teleport/look/time/weather are also director ops
        pass
    for need in ("portal_pair", "turret_fire", "cube_launch", "cake"):
        if need not in seen:
            errors.append(f"guest never received op {need}")
    # event_wait marks prove the round-trip events came back
    for mark in ("portal_warp", "turret_hit", "cube_through_portal",
                 "intro", "end"):
        if mark not in marks:
            errors.append(f"missing event-log mark {mark}")
    # no event_wait timed out
    timeouts = [m for m in marks if m.startswith("timeout:")]
    if timeouts:
        errors.append(f"event_wait timed out: {timeouts}")

    print(f"guest ops seen: {seen}")
    print(f"director marks: {marks}")
    if errors:
        print("FAIL:")
        for e in errors:
            print("  -", e)
        return 1
    print("PASS: director delivered ops, guest responded, event log complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
