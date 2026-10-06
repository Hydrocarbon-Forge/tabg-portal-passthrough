#!/usr/bin/env python3
"""framechannel.py — writer + reader for the passthrough shared-memory frame
channel (slice 3, host-agnostic half). Layout: see ``framechannel.md``.

The guest (Portal) writes colour + depth into 3 rotating slots with a seqlock;
the host (TABG) reads the latest completed slot. On Windows this is a named file
mapping; on Linux (tests here) a POSIX shared-memory object. Same byte layout.

As a CLI this is the ``mcframe.py`` equivalent: attach, dump the latest slot to
a colour PNG + a linearised depth heatmap PNG, and print/verify the pose snapshot.

    python3 src/framechannel/framechannel.py [--name TABGPortalFrame]
            [--out out_dir] [--near 1] [--far 5000] [--expect-pose x,y,z]
"""
from __future__ import annotations

import argparse
import mmap
import os
import struct
import sys
import tempfile
import time
from dataclasses import dataclass

import numpy as np

MAGIC = 0x31435054  # "TPC1"
VERSION = 1
HEADER_BYTES = 4096
DESC_TABLE_OFF = 256
DESC_STRIDE = 128
DEFAULT_NAME = "TABGPortalFrame"
DEFAULT_SLOTS = 3
ALIGN = 64

# header: magic,version,header_size,slot_count,slot_stride,max_w,max_h,
#         depth_conv,published,latest_slot,flags
_HEADER_FMT = "<iiiiqiiiqii"
# descriptor after the leading seq(i64): frame,ts,w,h, pos(3f),rot(4f),fov,
#         depth_conv,payload_size, capture_ns, publish_ns
_DESC_FMT = "<qqii3f4ffiiqq"

# flags
ROWS_TOP_DOWN = 1

# depth conventions
LINEAR01 = 0
REVERSED_Z_01 = 1
ZBUF_01 = 2
GL_NDC = 3
EYE_METRES = 4


_IS_WIN = sys.platform == "win32"


def _posix_path(name: str) -> str:
    """Backing file for the mapping on POSIX (prefer /dev/shm, like shm_open)."""
    base = "/dev/shm" if os.path.isdir("/dev/shm") else tempfile.gettempdir()
    return os.path.join(base, name)


class _Shm:
    """Minimal cross-platform shared mapping with an explicit owner.

    Windows: a named file mapping (``mmap`` with ``tagname``) — no backing file,
    no resource tracker. Linux/macOS: a file in ``/dev/shm`` mmap'd in place
    (the POSIX analog used for tests). Only the creating ``FrameWriter`` owns the
    lifetime; readers attach and never unlink.
    """

    def __init__(self, name: str, size: int, *, create: bool):
        self.name = name
        self.size = size
        self.create = create
        self._fd = -1
        self.path = None
        if _IS_WIN:
            # tagname maps the OS named section directly; -1 fd = pagefile-backed
            self.mm = mmap.mmap(-1, size, tagname=name)
        else:
            self.path = _posix_path(name)
            if create:
                self._fd = os.open(self.path, os.O_CREAT | os.O_RDWR, 0o600)
                os.ftruncate(self._fd, size)
            else:
                self._fd = os.open(self.path, os.O_RDWR)
            self.mm = mmap.mmap(self._fd, size)

    @property
    def buf(self):
        return self.mm

    def close(self) -> None:
        try:
            self.mm.close()
        except Exception:
            pass
        if self._fd >= 0:
            try:
                os.close(self._fd)
            except Exception:
                pass
            self._fd = -1

    def unlink(self) -> None:
        # Windows named sections vanish when the last handle closes; POSIX needs
        # the backing file removed by the owner.
        if self.path and os.path.exists(self.path):
            try:
                os.unlink(self.path)
            except Exception:
                pass


def _probe_size(name: str) -> int:
    """Read slot_count/stride from a mapping's header to learn its full size."""
    if _IS_WIN:
        probe = mmap.mmap(-1, HEADER_BYTES, tagname=name)
        try:
            (magic, _v, _hs, slots, stride, *_rest) = struct.unpack_from(
                _HEADER_FMT, probe, 0)
        finally:
            probe.close()
        if magic != MAGIC:
            raise RuntimeError("not a TABGPortalFrame mapping (bad magic)")
        return HEADER_BYTES + stride * slots
    path = _posix_path(name)
    return os.path.getsize(path)


def _aligned(n: int, a: int = ALIGN) -> int:
    return (n + a - 1) // a * a


def slot_stride_for(max_w: int, max_h: int) -> int:
    return _aligned(max_w * max_h * (4 + 4))


@dataclass
class Frame:
    slot: int
    frame_index: int
    timestamp_us: int
    width: int
    height: int
    pos: tuple
    rot: tuple
    fov: float
    depth_convention: int
    capture_ns: int
    publish_ns: int
    rgba: np.ndarray    # (h, w, 4) uint8, top row first (normalised by reader)
    depth: np.ndarray   # (h, w) float32 raw, top row first

    def depth_metres(self, near: float, far: float) -> np.ndarray:
        """Linearise the raw depth plane to eye-space metres; inf where empty."""
        d = self.depth.astype(np.float64)
        c = self.depth_convention
        if c == EYE_METRES:
            z = d.copy()
            z[~np.isfinite(z)] = np.inf
            return z
        if c == REVERSED_Z_01:
            with np.errstate(divide="ignore", invalid="ignore"):
                z = near * far / (near + d * (far - near))
            z[d <= 0.0] = np.inf  # 0 = far
            return z
        if c == ZBUF_01:
            with np.errstate(divide="ignore", invalid="ignore"):
                z = near * far / (far - d * (far - near))
            z[d >= 1.0] = np.inf
            return z
        if c == GL_NDC:
            ndc = d  # already [-1,1]
            with np.errstate(divide="ignore", invalid="ignore"):
                z = 2.0 * near * far / ((far + near) - ndc * (far - near))
            z[d >= 1.0] = np.inf
            return z
        # LINEAR01
        z = near + d * (far - near)
        z[d >= 1.0] = np.inf
        return z


class _Base:
    def __init__(self, name: str):
        self.name = name
        self.shm: _Shm | None = None
        self.buf = None
        self.slot_count = 0
        self.slot_stride = 0

    def close(self) -> None:
        if self.shm is not None:
            self.buf = None
            self.shm.close()
            self.shm = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class FrameWriter(_Base):
    """Single-writer side. Creates (or re-creates) the mapping."""

    def __init__(self, max_width: int, max_height: int, *,
                 name: str = DEFAULT_NAME, slot_count: int = DEFAULT_SLOTS,
                 depth_convention: int = REVERSED_Z_01, rows_top_down: bool = True):
        super().__init__(name)
        self.max_width = max_width
        self.max_height = max_height
        self.slot_count = slot_count
        self.slot_stride = slot_stride_for(max_width, max_height)
        self.depth_convention = depth_convention
        self.flags = ROWS_TOP_DOWN if rows_top_down else 0
        size = HEADER_BYTES + self.slot_stride * slot_count
        # fresh mapping: remove a stale POSIX backing file of the same name first
        stale_path = None if _IS_WIN else _posix_path(name)
        if stale_path and os.path.exists(stale_path):
            try:
                os.unlink(stale_path)
            except Exception:
                pass
        self.shm = _Shm(name, size, create=True)
        self.buf = self.shm.buf
        self._published = 0
        self._write_header(latest_slot=-1)
        # init every descriptor seq to 0 (even, "no write in progress, empty")
        for i in range(slot_count):
            struct.pack_into("<q", self.buf, self._desc_off(i), 0)

    def _desc_off(self, slot: int) -> int:
        return DESC_TABLE_OFF + slot * DESC_STRIDE

    def _payload_off(self, slot: int) -> int:
        return HEADER_BYTES + slot * self.slot_stride

    def _write_header(self, latest_slot: int) -> None:
        struct.pack_into(_HEADER_FMT, self.buf, 0,
                         MAGIC, VERSION, HEADER_BYTES, self.slot_count,
                         self.slot_stride, self.max_width, self.max_height,
                         self.depth_convention, self._published, latest_slot,
                         self.flags)

    def unlink(self) -> None:
        # Works regardless of close() order: remove the POSIX backing file by
        # path (Windows sections vanish on last-handle-close, nothing to do).
        if self.shm is not None:
            self.shm.unlink()
        elif not _IS_WIN:
            p = _posix_path(self.name)
            if os.path.exists(p):
                try:
                    os.unlink(p)
                except Exception:
                    pass

    def write(self, rgba: np.ndarray, depth: np.ndarray, *,
              frame_index: int, pos=(0, 0, 0), rot=(0, 0, 0, 1), fov=60.0,
              timestamp_us: int | None = None, capture_ns: int = 0) -> int:
        """Publish one frame. Returns the slot index used."""
        h, w = rgba.shape[0], rgba.shape[1]
        if w > self.max_width or h > self.max_height:
            raise ValueError(f"frame {w}x{h} exceeds capacity "
                             f"{self.max_width}x{self.max_height}")
        rgba = np.ascontiguousarray(rgba, dtype=np.uint8)
        depth = np.ascontiguousarray(depth, dtype=np.float32)
        if rgba.shape[2] != 4:
            raise ValueError("rgba must be (h, w, 4)")
        ts = timestamp_us if timestamp_us is not None else time.time_ns() // 1000

        slot = self._published % self.slot_count
        doff = self._desc_off(slot)

        # seqlock: make odd (write in progress)
        seq = struct.unpack_from("<q", self.buf, doff)[0]
        seq += 1  # -> odd
        struct.pack_into("<q", self.buf, doff, seq)

        rgba_bytes = w * h * 4
        depth_bytes = w * h * 4
        payload_size = rgba_bytes + depth_bytes
        struct.pack_into(_DESC_FMT, self.buf, doff + 8,
                         frame_index, ts, w, h,
                         float(pos[0]), float(pos[1]), float(pos[2]),
                         float(rot[0]), float(rot[1]), float(rot[2]), float(rot[3]),
                         float(fov), self.depth_convention, payload_size,
                         int(capture_ns), time.perf_counter_ns())

        poff = self._payload_off(slot)
        self.buf[poff:poff + rgba_bytes] = rgba.reshape(-1).tobytes()
        self.buf[poff + rgba_bytes:poff + payload_size] = depth.reshape(-1).tobytes()

        # seqlock: make even (complete)
        seq += 1
        struct.pack_into("<q", self.buf, doff, seq)

        # publish
        self._published += 1
        self._write_header(latest_slot=slot)
        return slot


class FrameReader(_Base):
    """Attach to an existing mapping and read the latest completed frame."""

    def __init__(self, name: str = DEFAULT_NAME, *, max_retries: int = 8):
        super().__init__(name)
        # A reader attaches and never unlinks: _Shm.unlink is only called by the
        # writer. Learn the full size from the header, then map it.
        size = _probe_size(name)
        self.shm = _Shm(name, size, create=False)
        self.buf = self.shm.buf
        (magic, version, header_size, slot_count, slot_stride, max_w, max_h,
         depth_conv, published, latest_slot, flags) = struct.unpack_from(
            _HEADER_FMT, self.buf, 0)
        if magic != MAGIC:
            self.close()
            raise RuntimeError("not a TABGPortalFrame mapping (bad magic)")
        self.version = version
        self.slot_count = slot_count
        self.slot_stride = slot_stride
        self.max_width = max_w
        self.max_height = max_h
        self.header_flags = flags
        self.max_retries = max_retries
        self._last_frame_index = -1

    def published(self) -> int:
        return struct.unpack_from("<q", self.buf, 36)[0]

    def latest_slot(self) -> int:
        return struct.unpack_from("<i", self.buf, 44)[0]

    def latest(self, copy: bool = True) -> Frame | None:
        for _ in range(self.max_retries):
            slot = self.latest_slot()
            if slot < 0:
                return None
            doff = DESC_TABLE_OFF + slot * DESC_STRIDE
            seq0 = struct.unpack_from("<q", self.buf, doff)[0]
            if seq0 & 1:
                continue  # write in progress
            (frame_index, ts, w, h, px, py, pz, rx, ry, rz, rw, fov,
             depth_conv, payload_size, cap, pub) = struct.unpack_from(
                _DESC_FMT, self.buf, doff + 8)
            if w <= 0 or h <= 0 or w > self.max_width or h > self.max_height:
                continue
            rgba_bytes = w * h * 4
            poff = HEADER_BYTES + slot * self.slot_stride
            raw = bytes(self.buf[poff:poff + rgba_bytes + w * h * 4])
            # re-check seqlock
            seq1 = struct.unpack_from("<q", self.buf, doff)[0]
            if seq1 != seq0:
                continue  # torn read
            rgba = np.frombuffer(raw, np.uint8, w * h * 4, 0).reshape(h, w, 4)
            depth = np.frombuffer(raw, np.float32, w * h, rgba_bytes).reshape(h, w)
            if copy:
                rgba, depth = rgba.copy(), depth.copy()
            if not (self.header_flags & ROWS_TOP_DOWN):
                rgba, depth = rgba[::-1], depth[::-1]
            self._last_frame_index = frame_index
            return Frame(slot, frame_index, ts, w, h, (px, py, pz),
                         (rx, ry, rz, rw), fov, depth_conv, cap, pub, rgba, depth)
        return None

    def is_stale(self, max_age_us: int) -> bool:
        slot = self.latest_slot()
        if slot < 0:
            return True
        doff = DESC_TABLE_OFF + slot * DESC_STRIDE
        ts = struct.unpack_from("<q", self.buf, doff + 16)[0]
        return (time.time_ns() // 1000 - ts) > max_age_us


def _depth_heatmap(depth_m: np.ndarray) -> np.ndarray:
    """Map linearised depth to an 8-bit RGB heatmap (near=warm, far=cool)."""
    finite = np.isfinite(depth_m)
    out = np.zeros((*depth_m.shape, 3), np.uint8)
    if not finite.any():
        return out
    vals = depth_m[finite]
    lo, hi = np.percentile(vals, 2), np.percentile(vals, 98)
    hi = max(hi, lo + 1e-6)
    t = np.clip((depth_m - lo) / (hi - lo), 0, 1)
    # simple warm->cool ramp
    r = np.clip(1.5 - 2 * t, 0, 1)
    g = np.clip(1.5 - np.abs(2 * t - 1) * 1.5, 0, 1)
    b = np.clip(2 * t - 0.5, 0, 1)
    for i, ch in enumerate((r, g, b)):
        c = (ch * 255).astype(np.uint8)
        c[~finite] = 0
        out[..., i] = c
    return out


def _cli(argv=None) -> int:
    from pathlib import Path
    ap = argparse.ArgumentParser(description="dump latest frame-channel slot")
    ap.add_argument("--name", default=DEFAULT_NAME)
    ap.add_argument("--out", default="framechannel_out")
    ap.add_argument("--near", type=float, default=1.0)
    ap.add_argument("--far", type=float, default=5000.0)
    ap.add_argument("--expect-pose", default=None,
                    help="x,y,z the reader asserts the pose snapshot matches")
    ap.add_argument("--wait", type=float, default=5.0,
                    help="seconds to wait for a frame to appear")
    args = ap.parse_args(argv)

    try:
        from PIL import Image
    except ImportError:
        print("Pillow required for the CLI dumper", file=sys.stderr)
        return 2

    t0 = time.perf_counter()
    reader = None
    while time.perf_counter() - t0 < args.wait:
        try:
            reader = FrameReader(args.name)
            break
        except (FileNotFoundError, RuntimeError):
            time.sleep(0.1)
    if reader is None:
        print(f"no mapping '{args.name}' (is the guest exporting?)", file=sys.stderr)
        return 1

    fr = None
    while time.perf_counter() - t0 < args.wait:
        fr = reader.latest()
        if fr is not None:
            break
        time.sleep(1 / 60)
    if fr is None:
        print("no frame published yet", file=sys.stderr)
        return 1

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    Image.fromarray(fr.rgba[..., :3]).save(out / "colour.png")
    dm = fr.depth_metres(args.near, args.far)
    Image.fromarray(_depth_heatmap(dm)).save(out / "depth.png")
    print(f"frame {fr.frame_index}  {fr.width}x{fr.height}  slot {fr.slot}")
    print(f"pose pos={tuple(round(v,3) for v in fr.pos)} "
          f"rot={tuple(round(v,3) for v in fr.rot)} fov={fr.fov}")
    print(f"depth_convention={fr.depth_convention} "
          f"metres {np.nanmin(dm[np.isfinite(dm)]) if np.isfinite(dm).any() else float('nan'):.2f}.."
          f"{np.nanmax(dm[np.isfinite(dm)]) if np.isfinite(dm).any() else float('nan'):.2f}")
    print(f"wrote {out/'colour.png'} and {out/'depth.png'}")

    if args.expect_pose:
        want = tuple(float(x) for x in args.expect_pose.split(","))
        got = tuple(fr.pos)
        err = max(abs(a - b) for a, b in zip(want, got))
        print(f"pose check: want={want} got={got} max_err={err:.4f} "
              f"-> {'OK' if err < 1e-2 else 'MISMATCH'}")
        return 0 if err < 1e-2 else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli())
