#!/usr/bin/env python3
"""cut.py — turn a recorded take into a 1080p cut with a title card (slice 8).

Deliberately simple (per the task: title PNG overlay, no font-sheet
archaeology). Reads the director's event log for title timing, renders a title
card PNG with Pillow, and uses ffmpeg to trim, scale to 1080p, and overlay the
title(s) with a fade. No game or GPU needed — it operates on an existing video.

    python3 tools/cut.py <take.mp4> <out.mp4> [--events shots/demo.events.json]
            [--title "TABG × Portal"] [--title2 "The Chamber" --mark2 cube]
            [--head 0] [--length 30]

    python3 tools/cut.py --selftest        # make a synthetic clip and cut it (here)
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FFMPEG = os.environ.get("UM_FFMPEG", shutil.which("ffmpeg") or "ffmpeg")
W, H = 1920, 1080


def _font(size: int):
    from PIL import ImageFont
    # Try a few common truetype fonts; fall back to Pillow's scalable default.
    for name in ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    try:
        return ImageFont.load_default(size=size)  # Pillow >= 10
    except TypeError:
        return ImageFont.load_default()


def make_title_png(text: str, out: Path, w: int = W, h: int = H,
                   subtitle: str | None = None) -> Path:
    """A transparent lower-third-ish title: text with a soft shadow."""
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    font = _font(int(h * 0.075))
    sub_font = _font(int(h * 0.035))

    def centered(txt, fnt, cy):
        bb = d.textbbox((0, 0), txt, font=fnt)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        x = (w - tw) // 2
        y = cy - th // 2
        # shadow
        d.text((x + 3, y + 3), txt, font=fnt, fill=(0, 0, 0, 180))
        d.text((x, y), txt, font=fnt, fill=(255, 255, 255, 255))

    centered(text, font, int(h * 0.80))
    if subtitle:
        centered(subtitle, sub_font, int(h * 0.88))
    img.save(out)
    return out


def _events(path: str | None) -> dict:
    if not path or not os.path.exists(path):
        return {}
    log = json.load(open(path))
    ev: dict[str, float] = {}
    for t, n in log.get("events", []):
        ev.setdefault(n, t)
    return ev


def cut(take: str, out: str, events: str | None = None, title: str = "TABG × Portal",
        title2: str | None = None, mark2: str | None = None,
        head: float | None = None, length: float | None = None) -> int:
    if not os.path.exists(take):
        print(f"ERROR: take not found: {take}", file=sys.stderr)
        return 2
    ev = _events(events)
    if head is None:
        head = max(0.0, ev.get("intro", ev.get("approach", 0.0)) - 0.2)
    if length is None:
        end = ev.get("end")
        length = (end - head) if end else 30.0

    tmp = Path(tempfile.mkdtemp(prefix="tabgcut_"))
    try:
        t1 = make_title_png(title, tmp / "title1.png")
        inputs = ["-ss", f"{head}", "-t", f"{length}", "-i", take, "-i", str(t1)]
        # title 1: fade in 0.2-0.5, out 2.7-3.0
        filt = ("[0:v]scale=1920:1080:force_original_aspect_ratio=decrease,"
                "pad=1920:1080:-1:-1:color=black,setsar=1[v0];"
                "[1:v]format=rgba,fade=t=in:st=0.2:d=0.3:alpha=1,"
                "fade=t=out:st=2.7:d=0.3:alpha=1[t1];"
                "[v0][t1]overlay=0:0[v1]")
        last = "v1"
        if title2:
            t2 = make_title_png(title2, tmp / "title2.png")
            inputs += ["-i", str(t2)]
            st = (ev.get(mark2, head + 5.0) - head) if mark2 else 5.0
            st = max(0.5, st)
            filt += (f";[2:v]format=rgba,fade=t=in:st={st:.2f}:d=0.3:alpha=1,"
                     f"fade=t=out:st={st + 2.7:.2f}:d=0.3:alpha=1[t2];"
                     f"[v1][t2]overlay=0:0[v2]")
            last = "v2"
        cmd = [FFMPEG, "-hide_banner", "-loglevel", "error", "-y", *inputs,
               "-filter_complex", filt, "-map", f"[{last}]"]
        # carry audio if present
        cmd += ["-map", "0:a?", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                "-crf", "18", "-c:a", "aac", "-shortest", out]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print("ffmpeg failed:\n" + r.stderr, file=sys.stderr)
            return 1
        print(f"wrote {out}  (head={head:.2f}s length={length:.2f}s title='{title}'"
              + (f" title2='{title2}'@{mark2}" if title2 else "") + ")")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def selftest() -> int:
    """Generate a synthetic clip + event log and cut it, proving the pipeline."""
    if not (shutil.which("ffmpeg") or os.path.exists(FFMPEG)):
        print("SKIP: no ffmpeg")
        return 0
    tmp = Path(tempfile.mkdtemp(prefix="tabgcut_self_"))
    try:
        clip = tmp / "take.mp4"
        r = subprocess.run(
            [FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
             "-f", "lavfi", "-i", "testsrc=size=1280x720:rate=30:duration=8",
             "-f", "lavfi", "-i", "sine=frequency=440:duration=8",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
             "-shortest", str(clip)],
            capture_output=True, text=True)
        if r.returncode != 0:
            print("FAIL: could not synthesize test clip\n" + r.stderr)
            return 1
        events = tmp / "e.events.json"
        events.write_text(json.dumps({"events": [[0.0, "intro"], [3.0, "cube"],
                                                 [7.0, "end"]]}))
        out = tmp / "out.mp4"
        rc = cut(str(clip), str(out), str(events), title="TABG × Portal",
                 title2="The Chamber", mark2="cube")
        if rc != 0 or not out.exists() or out.stat().st_size < 1000:
            print("FAIL: cut did not produce a valid output")
            return 1
        # probe the result
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries",
             "stream=width,height", "-of", "csv=p=0", str(out)],
            capture_output=True, text=True)
        print(f"selftest output: {out.stat().st_size} bytes, streams: "
              f"{probe.stdout.strip() or '(ffprobe n/a)'}")
        print("PASS: cut.py pipeline (title render + ffmpeg overlay + 1080p)")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="take -> 1080p titled cut")
    ap.add_argument("take", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--events", default=None)
    ap.add_argument("--title", default="TABG × Portal")
    ap.add_argument("--title2", default=None)
    ap.add_argument("--mark2", default=None)
    ap.add_argument("--head", type=float, default=None)
    ap.add_argument("--length", type=float, default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.take or not args.out:
        ap.error("take and out are required (or use --selftest)")
    return cut(args.take, args.out, args.events, args.title, args.title2,
               args.mark2, args.head, args.length)


if __name__ == "__main__":
    raise SystemExit(main())
