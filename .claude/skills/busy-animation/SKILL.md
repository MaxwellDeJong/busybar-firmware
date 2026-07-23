---
name: busy-animation
description: Build and use custom Busy Bar active-screen theme animations (the 72x16 looping strip shown while an activity runs). Use when authoring a new <name>_72x16.zip frame sequence, wiring it via a theme.json, validating it compiles to .anim, or recoloring an existing animation. Covers the strict zip layout, the seq2anim validator, and procedural/gradient-map authoring techniques.
---

# Custom Busy Bar theme animations

The animation on the active screen is a **72×16 px, RGB888, 60 fps** looping frame
sequence, compiled from a zip to the `bicycle0` `.anim` format and selected by an
activity's theme.

## Canvas facts

- **72×16** (≈4.5:1) on a **black** background. Horizontal motion reads best;
  everything must be bright/self-illuminated (same black-screen rule as the
  **busy-profile-image** skill — always preview on black).
- Loops seamlessly — design frame N-1 to flow back into frame 0 (e.g. a fade-out
  reset over the last few frames).

## Strict zip layout (seq2anim is picky)

Source lives at `assets/shared/animations/<name>_72x16.zip` and **must** contain a
folder named **exactly `<name>_72x16/`** (= the zip stem) holding:

- **`meta.json`**: `{"fps": 60, "color_mode": "rgb888", "sections": []}`
- **frames**: `<prefix>_00000.png` … `<prefix>_NNNNN.png`, 72×16, RGB (not RGBA).

Frame numbering rules that will silently corrupt the anim if violated:

- Indices must be **contiguous from 0** (0..N-1, no gaps).
- The filename may contain **digits only in the frame index** — use a digit-free
  prefix like `chores_00000.png`. **NOT** `chores_72x16_00000.png`: the `72`/`16`
  poison `number_in_str()` and scramble frame order.

A plain `zip -r <name>_72x16.zip <name>_72x16/` produces the correct archive — this
matches the stock `exercise_72x16.zip` etc. **There is no frames→zip stitcher in the
repo**; `seq2anim.py` goes the *other* way (zip → .anim).

## ALWAYS validate before flashing

`scripts/seq2anim.py` is the build-time compiler (wired as `AnimationConverter` in
`scripts/fbt_tools/bsb_assets.py`). Run it by hand to catch bad meta/numbering/sizes
before a full build. It needs **Python 3.11+** (`typing.Self`) — the system python
may be 3.10, so use the **toolchain python** (it has PIL):

```bash
PYTHONPATH=scripts toolchain/x86_64-linux/bin/python3.11 \
  scripts/seq2anim.py -o /tmp/test.anim assets/shared/animations/<name>_72x16.zip --debug
```

Success prints `ConversionInfo(display_frame_cnt=…)`. A validated 72-frame strip
comes out a few tens of KB after RLE + dedup.

## Wire it to a theme

`applications/main/busy/resources/apps_assets/busy/themes/<name>/theme.json`:

```json
{ "bg_path": "/ext/apps_assets/shared/animations/<name>_72x16.anim", "order": 205 }
```

`order` sets the Theme-picker position; the fork's per-activity seed themes use
**200+** (200 reading_red … 204 exercise, 205 chores). The `.activity`'s
`busy_bar_settings.theme` references the theme dir by name — no registry;
`busy_theme_read(name)` resolves it and the Setup→Theme picker auto-enumerates.

## Authoring techniques that worked well

- **Procedural authoring** (from-scratch, e.g. the Chores checklist tick-off, the
  exercise rising-plusses, the reading page-turn): draw each frame in PIL at 72×16.
  For legibility, dump a few frames as ASCII (brightness-thresholded) and eyeball
  the motion before zipping. Structure the loop as phases (build-up → hold → reset)
  across ~72 frames (72 frames @ 60fps = 1.2 s).
- **Gradient-map recolor** (make color-coded twins from one animation, e.g. coding
  green/purple, reading red/blue): per pixel, map luminance L through a
  black→target→light ramp. Preserves motion/vignette/glow while unifying hue. Also
  colorizes neutral-grey-drawn frames into any accent.

Keep generator scripts (`build_anims.py` style) in the scratchpad; only the built
`.zip` is committed as source.

## Ship + retire

Deploy via **build-firmware** + **flash-firmware**. Retiring an animation needs the
THREE-delete rule (see **flash-firmware**) or the orphan `.anim` reappears.
