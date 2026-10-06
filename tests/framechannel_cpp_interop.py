#!/usr/bin/env python3
"""framechannel_cpp_interop.py — prove the C++ writer and the Python reader
agree on the byte layout (slice 3). Compiles src/framechannel/cpp/fc_selftest.cpp,
runs it in "write" mode (POSIX backend), and validates the frames with the Python
FrameReader. Linux-only (uses the POSIX shm path); skips cleanly if g++ is absent.

    python3 tests/framechannel_cpp_interop.py
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
CPP = REPO / "src" / "framechannel" / "cpp"
sys.path.insert(0, str(REPO / "src" / "framechannel"))
import framechannel as fc  # noqa: E402

NAME = "TABGPortalFrameCppInterop"


def main() -> int:
    gpp = shutil.which("g++") or shutil.which("c++")
    if not gpp:
        print("SKIP: no g++/c++ on this box")
        return 0
    if sys.platform == "win32":
        print("SKIP: interop harness uses the POSIX backend")
        return 0

    exe = Path("/tmp/fc_selftest_interop")
    build = subprocess.run(
        [gpp, "-std=c++11", "-O2", str(CPP / "fc_selftest.cpp"), "-o", str(exe)],
        capture_output=True, text=True)
    if build.returncode != 0:
        print("FAIL: C++ build error\n" + build.stderr)
        return 1

    # clean any stale backing file
    stale = Path("/dev/shm") / NAME
    if stale.exists():
        stale.unlink()

    # launch the C++ writer (publishes ~2 s of frames at 60 Hz)
    proc = subprocess.Popen([str(exe), "write", NAME, "2"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        # wait for the mapping to appear
        reader = None
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < 5.0:
            try:
                reader = fc.FrameReader(NAME)
                break
            except (FileNotFoundError, RuntimeError, OSError):
                time.sleep(0.05)
        if reader is None:
            print("FAIL: C++ mapping never appeared")
            return 1

        checks = 0
        errors = []
        seen_max = 0
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < 3.0:
            fr = reader.latest()
            if fr is None:
                time.sleep(1 / 120)
                continue
            checks += 1
            seen_max = max(seen_max, fr.frame_index)

            # C++ wrote pos.x = 1.5 + f*0.01, fov 72, rot (0,0,0,1), reversed-Z
            exp_x = 1.5 + fr.frame_index * 0.01
            if abs(fr.pos[0] - exp_x) > 1e-3:
                errors.append(f"pos.x frame {fr.frame_index}: {fr.pos[0]} != {exp_x}")
            if abs(fr.fov - 72.0) > 1e-3:
                errors.append(f"fov frame {fr.frame_index}: {fr.fov}")
            if fr.depth_convention != fc.REVERSED_Z_01:
                errors.append(f"depth_convention {fr.depth_convention}")
            # pixels: R = x ramp, G = y ramp, B = frame & 0xFF
            if fr.rgba[0, 0, 2] != (fr.frame_index & 0xFF):
                errors.append(f"blue channel frame {fr.frame_index}: {fr.rgba[0,0,2]}")
            if fr.rgba[0, -1, 0] != 255 or fr.rgba[-1, 0, 1] != 255:
                errors.append("gradient corners wrong")
            # depth linearises: reversed-Z 1.0 at x=0 => near; increasing across row
            dm = fr.depth_metres(near=1.0, far=5000.0)
            row = dm[fr.height // 2]
            if not (row[0] < row[-1]):
                errors.append("depth not increasing near->far")
            if errors:
                break
            time.sleep(1 / 120)

        reader.close()
        if errors:
            print("FAIL: C++/Python interop mismatches:")
            for e in errors[:8]:
                print("  -", e)
            return 1
        if checks < 5 or seen_max < 10:
            print(f"FAIL: too few frames (checks={checks}, max={seen_max})")
            return 1
        print(f"PASS: C++ writer / Python reader interop "
              f"(checks={checks}, max_frame={seen_max})")
        return 0
    finally:
        proc.wait(timeout=5)
        if stale.exists():
            try:
                stale.unlink()
            except OSError:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
