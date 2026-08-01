#!/usr/bin/env python3.11
"""Render a Busy Bar activity's assets as large "LED dot-matrix" images.

The device's screen is a matrix of round LED dots, so the raw 8x8 / 11x11 icon
PNGs and 72x16 animation frames look nothing like what a viewer actually sees on
the physical bar. This tool inflates each *logical* pixel into a round, softly
blooming LED dot on black, faithfully approximating the on-device appearance —
handy for documenting activities on a website or in a README.

Given only an activity NAME it resolves, via the activity/theme JSON, every asset
the activity references and renders each one:

  * the profile icon      -> <activity>_icon_8x8.png  and  <activity>_icon_11x11.png
  * the theme animation   -> <activity>_animation.mp4  (native fps, H.264)

Usage (must run under the bundled toolchain interpreter, which has PIL):

    toolchain/x86_64-linux/bin/python3.11 utils/render_activity_assets.py development
    toolchain/x86_64-linux/bin/python3.11 utils/render_activity_assets.py chores -o ~/site/assets

Only the activity name is required (the .activity file stem, e.g. "development",
or its title, e.g. "Development"). Output directory is optional and defaults to
./<activity>_led/.  Encoding the animation needs `ffmpeg` on PATH.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
except ImportError:
    sys.exit(
        "PIL not found. Run this with the bundled toolchain interpreter:\n"
        "  toolchain/x86_64-linux/bin/python3.11 utils/render_activity_assets.py ..."
    )

# --- repo layout (paths are resolved relative to the repo root) -------------
REPO = Path(__file__).resolve().parent.parent
ACTIVITIES = REPO / "applications/main/busy/resources/apps_assets/busy/activities"
THEMES = REPO / "applications/main/busy/resources/apps_assets/busy/themes"
IMAGES = REPO / "assets/images/external/busy"
ANIMATIONS = REPO / "assets/shared/animations"

# --- LED-dot rendering knobs ------------------------------------------------
SS = 4              # supersample factor for smooth dot edges
DOT = 0.86         # dot diameter as a fraction of the cell pitch
OFF = (9, 9, 12)   # faint "unlit LED" so the matrix grid stays visible on black
ICON_PITCH = 34    # output px per logical pixel for icons
ANIM_PITCH = 13    # output px per logical pixel for the animation


def render_led(src: Image.Image, pitch: int) -> Image.Image:
    """Render one logical image as an LED dot-matrix panel at `pitch` px/pixel."""
    src = src.convert("RGBA")
    w, h = src.size
    s = pitch * SS
    big = Image.new("RGB", (w * s, h * s), (0, 0, 0))
    draw = ImageDraw.Draw(big)
    d = DOT * s
    px = src.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                col = OFF
            else:
                f = a / 255.0  # composite over black, then floor to the unlit dot
                col = (max(int(r * f), OFF[0]),
                       max(int(g * f), OFF[1]),
                       max(int(b * f), OFF[2]))
            cx, cy = x * s + s / 2, y * s + s / 2
            draw.ellipse([cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2], fill=col)
    # LED bloom: add a blurred, dimmed copy of the panel back onto itself.
    glow = big.filter(ImageFilter.GaussianBlur(radius=s * 0.32)).point(lambda v: int(v * 0.55))
    big = ImageChops.add(big, glow)
    return big.resize((w * pitch, h * pitch), Image.LANCZOS)


def find_activity(name: str) -> Path:
    """Resolve an activity by file stem or by title (case-insensitive)."""
    direct = ACTIVITIES / f"{name}.activity"
    if direct.exists():
        return direct
    for path in sorted(ACTIVITIES.glob("*.activity")):
        data = json.loads(path.read_text())
        if name.lower() in (path.stem.lower(), str(data.get("title", "")).lower()):
            return path
    available = ", ".join(p.stem for p in sorted(ACTIVITIES.glob("*.activity")))
    sys.exit(f"No activity matching {name!r}.\nAvailable: {available}")


def render_icons(icon_base: str, out: Path, stem: str) -> list[Path]:
    written = []
    for size in (8, 11):
        srcp = IMAGES / f"{icon_base}_{size}x{size}.png"
        if not srcp.exists():
            print(f"  ! icon {srcp.name} missing, skipping", file=sys.stderr)
            continue
        dst = out / f"{stem}_icon_{size}x{size}.png"
        render_led(Image.open(srcp), ICON_PITCH).save(dst)
        written.append(dst)
        print(f"  icon  {srcp.name:24} -> {dst.name}")
    return written


def render_animation(theme: str, out: Path, stem: str) -> Path | None:
    theme_json = THEMES / theme / "theme.json"
    if not theme_json.exists():
        print(f"  ! theme {theme!r} has no theme.json, skipping animation", file=sys.stderr)
        return None
    anim_stem = Path(json.loads(theme_json.read_text())["bg_path"]).stem  # e.g. coding_green_72x16
    zip_path = ANIMATIONS / f"{anim_stem}.zip"
    if not zip_path.exists():
        print(f"  ! animation source {zip_path.name} not found, skipping", file=sys.stderr)
        return None
    if not shutil.which("ffmpeg"):
        print("  ! ffmpeg not on PATH; cannot encode the animation MP4", file=sys.stderr)
        return None

    with zipfile.ZipFile(zip_path) as zf:
        frame_names = sorted(n for n in zf.namelist() if n.lower().endswith(".png"))
        fps = 60
        for n in zf.namelist():
            if n.endswith("meta.json"):
                fps = int(json.loads(zf.read(n)).get("fps", 60))
        with tempfile.TemporaryDirectory() as tmp:
            for i, n in enumerate(frame_names):
                frame = Image.open(io.BytesIO(zf.read(n)))
                render_led(frame, ANIM_PITCH).save(Path(tmp) / f"f_{i:05d}.png")
            dst = out / f"{stem}_animation.mp4"
            subprocess.run(
                ["ffmpeg", "-y", "-framerate", str(fps), "-i", f"{tmp}/f_%05d.png",
                 "-c:v", "libx264", "-crf", "18", "-preset", "slow",
                 "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                 # force even dimensions so yuv420p is always valid
                 "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", str(dst)],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
    print(f"  anim  {zip_path.name:24} -> {dst.name} ({len(frame_names)} frames @ {fps}fps)")
    return dst


def main() -> None:
    ap = argparse.ArgumentParser(description="Render a Busy Bar activity's assets as LED dot-matrix images.")
    ap.add_argument("activity", help="activity file stem (e.g. development) or title (e.g. Development)")
    ap.add_argument("-o", "--output", help="output directory (default ./<activity>_led/)")
    args = ap.parse_args()

    act_path = find_activity(args.activity)
    act = json.loads(act_path.read_text())
    stem = act_path.stem
    out = Path(args.output) if args.output else Path.cwd() / f"{stem}_led"
    out.mkdir(parents=True, exist_ok=True)

    print(f"Activity: {act.get('title', stem)}  ({act_path.name})")
    print(f"Output:   {out}")
    render_icons(act.get("icon", ""), out, stem)
    render_animation(act.get("busy_bar_settings", {}).get("theme", ""), out, stem)
    print("Done.")


if __name__ == "__main__":
    main()
