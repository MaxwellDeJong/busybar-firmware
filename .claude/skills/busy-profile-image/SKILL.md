---
name: busy-profile-image
description: Build and use custom Busy Bar activity/profile menu icons (the per-activity glyph, e.g. a house for "Chores"). Use when authoring the paired 8x8/11x11 PNGs an activity references via its "icon" key, or debugging an icon that renders muddy/edgeless on the device. Covers the black-screen design constraint and the PNG→.image build pipeline.
---

# Custom Busy Bar profile / activity icons

An activity's menu icon (its "profile image") is two RGBA PNGs in
`assets/images/external/busy/`, referenced by base name from the `.activity`
`"icon"` key. The picker (`busy_scene_setup_activity.c`) appends `_8x8.image`
(front display) and `_11x11.image` (back display).

## Hard requirements

- **Paired sizes**: both `<base>_8x8.png` AND `<base>_11x11.png` must exist, or the
  back-display menu row breaks. `8x8` and `11x11` are exact pixel dimensions.
- **RGBA PNG**, bright color on a **transparent** background.
- **Base name** in the `.activity` must match the PNG base (e.g. `"icon":"house"` →
  `house_8x8.png` + `house_11x11.png`).

## The one design constraint that matters (learned the hard way)

**The display background is black.** Near-black / dark outline pixels vanish into
it, so an icon drawn with a dark border looks edgeless and muddy on device.

- Author from **bright colors only** — the black screen is your negative space, not
  an outline. No dark outline pixels.
- A **filled** silhouette glows and reads far better than a thin hollow outline at
  8–11 px. (For the house icon: filled roof + walls + a solid door, all one bright
  lime, beat a windowed outline version.)
- **Always preview on a solid black background**, never a checkerboard — the
  checkerboard hides exactly the low-contrast problem you're checking for.
- Accent color carries identity across variants (Reading red vs blue; Coding green
  vs purple). Pick an unused hue for a new activity (as of writing: red, blue,
  green, purple taken; amber/teal/yellow-green free).

## Authoring recipe (PIL)

Draw both sizes from an ASCII layout so you can eyeball the silhouette before
saving, then dump the alpha map to verify. Example (yellow-green house):

```python
from PIL import Image
LIME = (160, 230, 80)
def save(size, rows):
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0)); px = im.load()
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == '#': px[x, y] = (*LIME, 255)
    im.save(f'house_{size}x{size}.png')
# rows = list of 8 strings of 8 chars ('#'/'.') for 8x8, 11 of 11 for 11x11
```

Verify by printing `'#' if alpha>0 else '.'` per pixel — confirm the shape reads and
nothing dark leaked in.

## Build pipeline (PNG → .image)

`scripts/fbt_env_modules/fwenv/bsb_assets.scons` converts
`assets/images/external/busy/*.png` → `/ext/apps_assets/busy/images/*.image` using
`scripts/image.py` (LVGL, indexed I4). It needs **`pngquant`**, which ships at
`toolchain/x86_64-linux/bin/pngquant` — add the toolchain bin to PATH for a manual
test-convert. Normally you don't invoke this directly; **build-firmware** +
**flash-firmware** compile and deploy the icons as resources.

## Retiring / renaming an icon

Removing the source PNG is **not** enough — see the THREE-delete rule in the
**flash-firmware** skill, or the old `.image` re-bundles and the device orphan
reappears on the next flash.
