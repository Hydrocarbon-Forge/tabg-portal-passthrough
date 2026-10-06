#!/usr/bin/env python3
"""pose_orbit.py — drive link.py with a synthetic 60 Hz orbiting camera and
measure round-trip latency (slice 2 acceptance).

Connects to the hub as a **tool** and sends a `cam` every 1/60 s with a
microsecond timestamp, like the reference ``host/fakehost.py`` camera flight. A
small embedded **guest** (``--no-guest`` to disable) connects to the same hub
and echoes each `cam` straight back as an `echo`; this tool times
``now_us - ts`` per echo and reports p50/p99, exercising the full
tool→hub→guest→hub→tool path (both relay directions).

    python3 tools/pose_orbit.py [--seconds 20] [--hz 60] [--url ws://127.0.0.1:25600]
                                [--out tests/link_results.json] [--p99-ms 16.0]

Exit code 0 if p99 < --p99-ms, else 1 (so run_link_test.sh can gate on it).
"""
from __future__ import annotations

import argparse
import asyncio
import json
import math
import statistics
import sys
import time
from pathlib import Path

from websockets.asyncio.client import connect

DEFAULT_URL = "ws://127.0.0.1:25600"
CENTER = (0.0, 2.0, 0.0)


def _now_us() -> int:
    return time.time_ns() // 1000


async def _echo_guest(url: str, ready: asyncio.Event, stop: asyncio.Event) -> None:
    """Minimal guest: echo every `cam` back as an `echo` as fast as possible."""
    async with connect(url, max_size=None) as ws:
        await ws.send(json.dumps({"t": "hello", "role": "guest",
                                  "name": "pose_orbit-echo", "ver": 1,
                                  "caps": ["echo"]}))
        await ws.recv()  # hub hello
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
            if msg.get("t") == "cam":
                await ws.send(json.dumps({"t": "echo", "f": msg.get("f"),
                                          "ts": msg.get("ts")}))


async def run(url: str, seconds: float, hz: float, out: str, p99_ms: float,
              spawn_guest: bool) -> int:
    stop = asyncio.Event()
    guest_ready = asyncio.Event()
    guest_task = None
    if spawn_guest:
        guest_task = asyncio.create_task(_echo_guest(url, guest_ready, stop))
        try:
            await asyncio.wait_for(guest_ready.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            print("ERROR: embedded guest did not connect", file=sys.stderr)
            return 2

    rtts_us: list[float] = []
    sent = 0
    received = 0
    period = 1.0 / hz

    async with connect(url, max_size=None) as ws:
        await ws.send(json.dumps({"t": "hello", "role": "tool",
                                  "name": "pose_orbit", "ver": 1,
                                  "caps": ["cam"]}))
        hub_hello = json.loads(await ws.recv())

        async def reader():
            nonlocal received
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
                if msg.get("t") == "echo" and msg.get("ts") is not None:
                    rtts_us.append(_now_us() - int(msg["ts"]))
                    received += 1

        rtask = asyncio.create_task(reader())
        t0 = time.perf_counter()
        f = 0
        next_t = t0
        while (now := time.perf_counter() - t0) < seconds:
            f += 1
            a = now * 0.5
            eye = (CENTER[0] + 7 * math.sin(a),
                   CENTER[1] + 2.2 + math.sin(a * 0.7),
                   CENTER[2] + 7 * math.cos(a))
            # forward points at the center; build an orthonormal basis.
            fwd = _norm((CENTER[0] - eye[0], CENTER[1] - eye[1], CENTER[2] - eye[2]))
            right = _norm(_cross(fwd, (0.0, 1.0, 0.0)))
            up = _cross(right, fwd)
            await ws.send(json.dumps({
                "t": "cam", "f": f, "ts": _now_us(),
                "p": list(eye),
                "basis": {"fwd": list(fwd), "right": list(right), "up": list(up)},
                "fov": 60.0, "aspect": 16 / 9, "fp": True,
            }))
            sent += 1
            next_t += period
            sleep = next_t - time.perf_counter()
            if sleep > 0:
                await asyncio.sleep(sleep)
            else:
                next_t = time.perf_counter()  # fell behind; don't spiral

        # let the last echoes drain
        await asyncio.sleep(0.2)
        stop.set()
        rtask.cancel()

    if guest_task is not None:
        try:
            await asyncio.wait_for(guest_task, timeout=2.0)
        except (asyncio.TimeoutError, asyncio.CancelledError):
            pass

    return _report(rtts_us, sent, received, hz, seconds, out, p99_ms, hub_hello)


def _report(rtts_us, sent, received, hz, seconds, out, p99_ms, hub_hello) -> int:
    if not rtts_us:
        print("ERROR: no echoes received", file=sys.stderr)
        result = {"ok": False, "error": "no echoes", "sent": sent}
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_text(json.dumps(result, indent=2))
        return 1

    rtts_ms = sorted(r / 1000.0 for r in rtts_us)
    p50 = _pct(rtts_ms, 50)
    p99 = _pct(rtts_ms, 99)
    achieved_hz = sent / seconds
    ok = p99 < p99_ms
    result = {
        "ok": ok,
        "url": DEFAULT_URL,
        "target_hz": hz,
        "achieved_hz": round(achieved_hz, 2),
        "seconds": seconds,
        "sent": sent,
        "received": received,
        "lost": sent - received,
        "rtt_ms": {
            "p50": round(p50, 3),
            "p90": round(_pct(rtts_ms, 90), 3),
            "p99": round(p99, 3),
            "min": round(rtts_ms[0], 3),
            "max": round(rtts_ms[-1], 3),
            "mean": round(statistics.fmean(rtts_ms), 3),
        },
        "p99_budget_ms": p99_ms,
        "hub_hello": hub_hello,
    }
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["rtt_ms"], indent=2))
    print(f"sent={sent} received={received} achieved_hz={achieved_hz:.1f} "
          f"p99={p99:.3f}ms budget={p99_ms}ms -> {'PASS' if ok else 'FAIL'}")
    print(f"wrote {out}")
    return 0 if ok else 1


def _pct(sorted_vals, pct):
    if not sorted_vals:
        return float("nan")
    k = (len(sorted_vals) - 1) * pct / 100.0
    lo = math.floor(k)
    hi = math.ceil(k)
    if lo == hi:
        return sorted_vals[int(k)]
    return sorted_vals[lo] * (hi - k) + sorted_vals[hi] * (k - lo)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _norm(v):
    n = math.sqrt(sum(c * c for c in v)) or 1.0
    return (v[0] / n, v[1] / n, v[2] / n)


def main() -> int:
    ap = argparse.ArgumentParser(description="60 Hz pose driver + latency probe")
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--hz", type=float, default=60.0)
    ap.add_argument("--out", default="tests/link_results.json")
    ap.add_argument("--p99-ms", type=float, default=16.0)
    ap.add_argument("--no-guest", action="store_true",
                    help="do not spawn the embedded echo guest (use a real guest)")
    args = ap.parse_args()
    return asyncio.run(run(args.url, args.seconds, args.hz, args.out,
                           args.p99_ms, spawn_guest=not args.no_guest))


if __name__ == "__main__":
    raise SystemExit(main())
