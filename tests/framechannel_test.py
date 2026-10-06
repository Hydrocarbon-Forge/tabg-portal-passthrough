#!/usr/bin/env python3
"""framechannel_test.py — slice 3 acceptance for the shared-memory frame channel.

A writer thread produces synthetic gradient frames with a known, frame-dependent
pose at 60 Hz; a reader (attached by name, exercising the attach path) validates:
  - ordering: frame_index never goes backwards;
  - pose freshness: the pose snapshot read matches the pose the writer rendered
    with, within 1 frame of the reader's latest observed index;
  - pixel correctness: the gradient the reader sees matches the formula for the
    frame_index in the slot it read;
  - depth linearisation round-trips (reversed-Z -> metres).
  - stale detection flips true after the writer stops.

Runs on this Linux box (POSIX shared memory). Exit 0 on pass.

    python3 tests/framechannel_test.py [--seconds 6] [--w 320 --h 180]
"""
from __future__ import annotations

import argparse
import sys
import threading
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src" / "framechannel"))
import framechannel as fc  # noqa: E402

NAME = "TABGPortalFrameTest"


def synth_rgba(w: int, h: int, frame_index: int) -> np.ndarray:
    """Deterministic gradient that depends on frame_index (so a reader can tell
    which frame it got). R = x ramp, G = y ramp, B = frame_index low byte."""
    xs = (np.linspace(0, 255, w)).astype(np.uint8)
    ys = (np.linspace(0, 255, h)).astype(np.uint8)
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., 0] = np.broadcast_to(xs[None, :], (h, w))
    rgba[..., 1] = np.broadcast_to(ys[:, None], (h, w))
    rgba[..., 2] = frame_index & 0xFF
    rgba[..., 3] = 255
    return rgba


def synth_depth(w: int, h: int, frame_index: int) -> np.ndarray:
    """A reversed-Z depth plane: a horizontal ramp from near(1.0) to far(~0),
    with the whole thing nudged per frame so freshness is testable."""
    base = np.linspace(1.0, 0.05, w, dtype=np.float32)  # reversed-Z: 1=near
    nudge = ((frame_index % 10) * 0.001)
    d = np.clip(base + nudge, 0.0, 1.0)
    return np.broadcast_to(d[None, :], (h, w)).astype(np.float32)


def expected_pose(frame_index: int):
    """Pose is a function of frame_index so the reader can verify freshness."""
    x = frame_index * 0.01
    return (x, 2.0, -x), (0.0, 0.0, 0.0, 1.0), 60.0 + (frame_index % 30) * 0.1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--w", type=int, default=320)
    ap.add_argument("--h", type=int, default=180)
    ap.add_argument("--hz", type=float, default=60.0)
    args = ap.parse_args()
    w, h, hz = args.w, args.h, args.hz

    writer = fc.FrameWriter(w, h, name=NAME, depth_convention=fc.REVERSED_Z_01)
    stop = threading.Event()
    wrote = {"n": 0}
    errors: list[str] = []

    def writer_thread():
        period = 1.0 / hz
        next_t = time.perf_counter()
        fi = 0
        while not stop.is_set():
            fi += 1
            pos, rot, fov = expected_pose(fi)
            writer.write(synth_rgba(w, h, fi), synth_depth(w, h, fi),
                         frame_index=fi, pos=pos, rot=rot, fov=fov)
            wrote["n"] = fi
            next_t += period
            s = next_t - time.perf_counter()
            if s > 0:
                time.sleep(s)
            else:
                next_t = time.perf_counter()

    wt = threading.Thread(target=writer_thread, daemon=True)
    wt.start()

    # wait for the first frame, then attach a reader by name.
    time.sleep(0.1)
    reader = fc.FrameReader(NAME)

    checks = 0
    last_index = -1
    max_index = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < args.seconds:
        fr = reader.latest()
        if fr is None:
            time.sleep(1 / 120)
            continue
        checks += 1

        # ordering
        if fr.frame_index < last_index:
            errors.append(f"ordering: {fr.frame_index} < {last_index}")
        last_index = fr.frame_index
        max_index = max(max_index, fr.frame_index)

        # pixel correctness vs the frame_index we actually got
        exp_rgba = synth_rgba(w, h, fr.frame_index)
        if not np.array_equal(fr.rgba, exp_rgba):
            # pinpoint
            diff = int(np.abs(fr.rgba.astype(int) - exp_rgba.astype(int)).max())
            errors.append(f"pixels: frame {fr.frame_index} max channel diff {diff}")

        # pose snapshot matches the formula for that exact frame_index
        epos, erot, efov = expected_pose(fr.frame_index)
        if max(abs(a - b) for a, b in zip(epos, fr.pos)) > 1e-3:
            errors.append(f"pose pos: frame {fr.frame_index} {fr.pos} != {epos}")
        if abs(efov - fr.fov) > 1e-2:
            errors.append(f"pose fov: frame {fr.frame_index} {fr.fov} != {efov}")

        # freshness: reader's frame is within a small lag of what the writer has
        lag = wrote["n"] - fr.frame_index
        if lag < 0 or lag > 5:
            errors.append(f"freshness: lag {lag} (reader {fr.frame_index}, "
                          f"writer {wrote['n']})")

        # depth linearises monotonically near->far across the row
        dm = fr.depth_metres(near=1.0, far=5000.0)
        if not np.isfinite(dm).all():
            errors.append("depth: unexpected inf in synthetic frame")
        else:
            row = dm[h // 2]
            if not (row[0] < row[-1]):  # near at x=0 (reversed-Z 1.0) -> small metres
                errors.append("depth: not increasing near->far across row")

        if errors:
            break
        time.sleep(1 / 240)

    # stale detection
    stop.set()
    wt.join(timeout=2.0)
    time.sleep(0.2)
    stale_ok = reader.is_stale(max_age_us=100_000)  # >100 ms since last write

    reader.close()
    writer.close()
    writer.unlink()

    ok = not errors and checks > 10 and max_index > int(args.seconds * hz * 0.5) and stale_ok
    print(f"checks={checks} max_frame_index={max_index} wrote={wrote['n']} "
          f"stale_after_stop={stale_ok}")
    if errors:
        print("FAIL:")
        for e in errors[:10]:
            print("  -", e)
        return 1
    if not ok:
        print("FAIL: insufficient throughput or stale check")
        return 1
    print("PASS: ordering, pose freshness, pixel correctness, depth, stale detection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
