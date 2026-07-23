# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

BUSY Bar firmware — an embedded C firmware for the **f22** hardware target, built on
a Flipper-derived FURI/SCons stack. **This checkout is a customized fork** (branch
`activity-selection`) that adds an on-device **activity picker** feature on top of
upstream `busy-app/busybar-firmware`. Fork-maintenance and upstream-sync procedure
live in `UPGRADING.md`.

The device has **two chips**: **U5** (main MCU, this firmware) and **Si917**
(wireless co-processor). They handshake on boot — see the flashing rule below, it is
the single most important thing to get right.

## Commands

Build system is **fbt** (`./fbt`, a SCons wrapper). Hardware target: `f22-firmware-D`.

```bash
./fbt                    # build firmware + resources → dist/, build/f22-firmware-D/
./fbt flash_usb INTERCOM_FORCE_VERSION=07e850ec   # build bundle + flash U5 over USB (SEE RULE BELOW)
./fbt resources_upload   # build + upload ONLY /ext resources (after a debugger flash)
./fbt lint               # clang-format + filename check on firmware C
./fbt format             # auto-fix formatting/filenames
./fbt vscode_dist        # generate .vscode/ project config
./fbt doxy               # render developer docs (documentation/*.dox.md)
```

### Running scripts — use the toolchain python, not system python

Repo scripts under `scripts/*.py` require the **bundled interpreter**
`toolchain/x86_64-linux/bin/python3.11`. System `python3` may be 3.10 and will fail
on `typing.Self` (`seq2anim.py`) or lack `colorlog` (`update_over_http.py`). The
toolchain python has PIL; `pngquant` (used by `image.py`) sits alongside it.

### Tests

Integration tests are a **pytest suite that runs against a real device** on the LAN
(there is no host-only unit-test target for the firmware C). From `tests/`:

```bash
cd tests && ./bootstrap_local.sh        # creates .venv (Poetry), probes device, smoke tests
poetry run pytest                       # full suite
poetry run pytest path/to/test_x.py::test_name   # a single test
```

Config lives in `tests/config/.env` (gitignored; copy from `.env.example`);
`BUSYBAR_IP` defaults to `10.0.4.20`. On-device test ops (version, power, unit-tests,
update-fw) are driven via `scripts/testops.py` (`BUSYBAR_IP` env or `--host`).

## Flashing rule (CRITICAL — read before any flash)

`./fbt flash_usb` updates **only the U5**, never the Si917. On boot both chips
compare an intercom control string; a custom U5's default string is its own git hash,
which won't match the stock Si917, so the handshake fails and the screen shows
**"System error, restart device"**. **Always pin the U5 to the stock Si917's hash:**

```bash
./fbt flash_usb INTERCOM_FORCE_VERSION=07e850ec
```

`07e850ec` = stock 1.0.2's `commit_hash` (the Si917 is never reflashed, so it stays
this value). Flashing **without** the pin skips the pre-flight guard and boots a
mismatched U5 — the real brick-the-UI path. Full mechanism, verification, recovery,
and the auto-update-reverts-the-fork hazard are in the **flash-firmware** skill.

## Skills — use these for the common workflows

`.claude/skills/` encodes the hard-won procedure for each recurring task; prefer them
over re-deriving:

- **add-busy-activity** — add an activity to the picker (the file set, `.activity`
  schema, `id`/`sort_order` scheme, INFINITE vs INTERVAL timing).
- **busy-profile-image** — author the paired 8×8/11×11 menu icons (the black-screen
  design constraint).
- **busy-animation** — author the 72×16 theme animations (strict zip layout,
  `seq2anim` validation).
- **build-firmware** / **flash-firmware** — the build and the pinned flash flow.

## Architecture notes that span multiple files

- **FURI service/app model.** `applications/` holds services (`applications/services/`,
  e.g. `intercom`, `busy_timer`, `web_server`) and apps (`applications/main/busy` is
  the activity/timer UI). The firmware module set for f22 is assembled at build time;
  a plain `./fbt` prints the enabled Service/App/Settings/CliCommand lists.
- **The activity feature has NO registry.** Seed activities, themes, and icons are
  **globbed**, not enumerated: `activities/*.activity` are loaded by the picker,
  `busy_theme_read(name)` resolves theme dirs, and the icon picker globs `images/`.
  Adding files is sufficient; there is no C enum/list to update. The picker adds the
  "Activity" menu item unconditionally, so a *missing* menu item means the feature
  CODE is absent (e.g. reverted by an official update), not just missing data.
- **Asset pipeline** (`scripts/fbt_env_modules/fwenv/bsb_assets.scons`): source PNGs
  (`assets/images/external/busy/*.png`) → `.image` via `scripts/image.py`; animation
  zips (`assets/shared/animations/<name>_72x16.zip`) → `.anim` via
  `scripts/seq2anim.py`; activity/theme JSON under
  `applications/main/busy/resources/apps_assets/busy/`. All land under `/ext` on the
  device. `flash_usb` **overlays and never prunes** `/ext`, and SCons does not delete
  a compiled artifact when its source is removed — so retiring an asset takes three
  deletes (device, build output, source); see the flash-firmware skill.
- **Intercom (chip-to-chip).** `applications/services/intercom/` is the U5↔Si917
  serial link. `intercom_get_version_string()` returns the handshake control string
  (default = firmware git hash; overridden by `INTERCOM_FORCE_VERSION`). The
  `/api/status/firmware` fields `nwp_version`/`matter_version` are read *from* the
  Si917 over this link — their presence proves the handshake works.
- **Timer stream is event-driven, not tick-driven.** `busy_timer` publishes on
  state-change events (start/pause/resume/skip/interval-end/stop), NOT on ticks, so a
  running timer's cached `/api/busy/snapshot` goes stale between events. INFINITE
  activities compute elapsed on demand from the wall clock (count-up display). Host
  consumers key on transitions, not polling.

## Device & recovery

Device: `http://10.0.4.20` over USB-ethernet (host iface 10.0.4.21/24). Keep
**auto-update OFF** — an official signed release is a full-image update that
overwrites the custom U5 + resources. SHA-256-verified stock recovery bundle:
`../recovery/busybar-f22-update_signed-1.0.2.tgz` (restore via `update_over_http.py`;
see the flash-firmware skill).
