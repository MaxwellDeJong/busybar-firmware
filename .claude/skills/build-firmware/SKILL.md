---
name: build-firmware
description: Build the Busy Bar (f22) firmware and its resources with fbt. Use when compiling after code/asset changes, producing the flashable bundle, or running any repo python script (which needs the bundled toolchain interpreter, not system python). Covers the build target, output locations, and the toolchain-python gotcha.
---

# Build the Busy Bar firmware

The build system is **fbt** (`./fbt`), a SCons wrapper. The active hardware target
is **f22** (`f22-firmware-D`).

## Common commands

```bash
./fbt                      # build firmware + resources → dist/ and build/f22-firmware-D/
./fbt flash_usb            # build the update bundle AND flash over USB (see flash-firmware skill)
./fbt resources_upload     # build + upload ONLY /ext resources (use after a debugger flash)
./fbt vscode_dist          # generate .vscode/ project config
./fbt doxy                 # render developer docs
```

- Interactive flags aren't supported in this environment; fbt targets are
  non-interactive.
- A plain **`./fbt` is the cheapest way to catch upstream/API breaks** — a moved or
  re-signatured internal function shows up as a compile error. Run it first after
  syncing upstream or editing firmware C.

## Output locations

- `build/f22-firmware-D/firmware.elf` / `.bin` / `.dfu` — the U5 image.
- `build/f22-firmware-D/flash_usb_f22.tgz` — the update bundle (U5 firmware +
  `resources.tar` of `/ext/apps_assets`). This is what `flash_usb` uploads; you can
  also POST it to `/api/update` manually.
- Compiled resources: `fbt_layers/fbtng/build/f22-firmware-D/resources/apps_assets/…`
  (PNG→`.image`, animation zip→`.anim`, etc.). **Note:** SCons does NOT delete a
  compiled artifact here when its source is removed — relevant to asset retirement
  (see **flash-firmware**).

## Toolchain python (important)

Repo scripts under `scripts/*.py` need the **bundled interpreter**, not system
python:

```bash
toolchain/x86_64-linux/bin/python3.11
```

- System `python3` may be **3.10**, which fails on `typing.Self`
  (`scripts/seq2anim.py`) and lacks `colorlog` (`scripts/update_over_http.py`).
- The toolchain python has **PIL**; `pngquant` (needed by `image.py`) is alongside
  it at `toolchain/x86_64-linux/bin/pngquant`.
- For scripts that shell out oddly, prefix `PYTHONPATH=scripts` (seq2anim) or
  `PYTHONPATH=` (update_over_http restore), as shown in the respective skills.

## Asset build pipeline (what a build compiles)

Under `scripts/fbt_env_modules/fwenv/bsb_assets.scons`:
- **Icons**: `assets/images/external/busy/*.png` → `…/busy/images/*.image` via
  `scripts/image.py` (see **busy-profile-image**).
- **Animations**: `assets/shared/animations/<name>_72x16.zip` →
  `…/shared/animations/<name>_72x16.anim` via `scripts/seq2anim.py` (see
  **busy-animation**).
- **Activities/themes**: JSON under
  `applications/main/busy/resources/apps_assets/busy/` copied into resources;
  globbed at runtime (no registry).

After building, deploy with the **flash-firmware** skill.
