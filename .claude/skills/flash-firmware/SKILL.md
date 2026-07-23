---
name: flash-firmware
description: Flash a custom Busy Bar firmware build to the physical device over USB, keeping both chips' handshake in sync. Use when deploying to the device at 10.0.4.20 via flash_usb. CRITICAL — a custom flash MUST pin INTERCOM_FORCE_VERSION=07e850ec or the device drops to a "System error" screen. Covers the two-chip handshake, the correct pin, the pre-flight guard, verification, asset-retirement pitfalls, auto-update, and recovery.
---

# Flash the Busy Bar firmware (USB, both chips in sync)

The device is at **http://10.0.4.20** (USB-ethernet; host iface 10.0.4.21/24). It
has **two chips**: **U5** (main) + **Si917** (wireless). `./fbt flash_usb` updates
**only the U5** (plus `/ext` resources) — it never touches the Si917.

## THE non-negotiable rule: pin the intercom version

On every boot the two chips handshake over the "intercom" serial link by comparing
a control string. A custom/dev U5's default string is its **own git hash**, which
won't match the untouched **stock Si917** — the handshake fails and the screen shows
**"System error, restart device"** with input locked
(`SupervisorWarningTypeIntercomError`). The HTTP API stays up, but the UI is
unusable. `INTERCOM_DISABLE_VERSION_CHECK` on the U5 alone does NOT help (no
wildcard; both sides must present the same string).

**Fix:** force the U5's string to the stock Si917's hash, which is **`07e850ec`**
(= stock 1.0.2's `commit_hash`, the value the Si917 shipped with):

```bash
./fbt flash_usb INTERCOM_FORCE_VERSION=07e850ec
```

### Getting the pin (don't read the wrong field)

The device exposes two version fields at `/api/status/firmware` that mean different
things:

- **`commit_hash`** — the **U5's own** git hash. Equals the pin **only when the
  device is on stock firmware**. On an already-custom U5 it is the U5's hash (e.g.
  `ff197daa`) — **not** the pin.
- **`intercom_version`** — the U5's **forced handshake control string** (via
  `intercom_get_version_string()`; it is NOT read from the Si917). On a custom U5
  this echoes the last pin used.

So: on stock → read `commit_hash`. On a custom U5 → read `intercom_version`, or just
**reuse the known `07e850ec`** (the Si917 is never reflashed by `flash_usb`, so it
stays `07e850ec` indefinitely).

## Safety: the pre-flight guard, and the real brick path

`flash_usb` runs `scripts/update_over_http.py --intercom-version <pin>`, which
**checks the device's current `intercom_version` == pin and ABORTS before upload on
mismatch**. Consequences:

- `INTERCOM_FORCE_VERSION=07e850ec` on a custom U5 → guard passes → flashes. ✅
- A *wrong* pin → guard aborts, no flash (safe but wrong). ⚠️
- **`./fbt flash_usb` with NO pin → no `--intercom-version`, so NO guard runs**, and
  the U5 boots with a mismatched control string → **error screen**. ❌ This omission
  is the genuine brick-the-UI path — never flash a custom build without the pin.

**First flash from stock is special:** stock's older API returns no
`intercom_version`, so the guard reads `""`, mismatches, and aborts. In that one
case, upload the bundle manually:

```bash
PYTHONPATH= toolchain/x86_64-linux/bin/python3.11 scripts/update_over_http.py \
  --file build/f22-firmware-D/flash_usb_f22.tgz --intercom-version 07e850ec
```

(Flashing from an already-custom U5, which reports `intercom_version:07e850ec`,
passes the guard and auto-flashes fine.)

## After the flash: verify

The device reboots to apply the update (HTTP drops briefly). Then:

```bash
curl -s http://10.0.4.20/api/status/firmware
```

Confirm:
- `commit_hash` == your new U5 hash (build from an **untracked-only** working tree
  reports a clean hash, e.g. `ff197daa`, since `git describe --dirty` ignores
  untracked files).
- `intercom_version` == **`07e850ec`** (both chips in sync).
- **`nwp_version` and `matter_version` are populated** — these are read *from* the
  Si917 over the intercom link, so their presence is positive proof the handshake
  succeeded (HTTP being up alone is not).
- No "System error" screen on the physical device.
- New resources present, e.g.
  `curl -s "http://10.0.4.20/api/storage/list?path=/ext/apps_assets/busy/activities"`.

## Retiring/renaming an asset needs THREE deletes

`flash_usb` **overlays** `/ext` and never prunes, and SCons never deletes a compiled
artifact whose source is gone — so a removed icon/anim keeps re-bundling and
**reappears on the device after the next flash** unless you delete all three:

1. On device: `DELETE /api/storage/remove?path=/ext/apps_assets/busy/…`
2. In the build output:
   `rm fbt_layers/fbtng/build/f22-firmware-D/resources/apps_assets/busy/…` (or
   clean-build).
3. The source file (PNG / zip / .activity / theme).

Skipping (2) is why a deleted orphan comes back.

## Keep auto-update OFF (it silently reverts the fork)

An **official signed release is a FULL-image update** (both chips + all resources).
If auto-update is on and the bar is on internet WiFi, it will install stock 1.0.2 in
the 02:00–05:00 window and **wipe the custom U5 code and the activity resources**.
Keep it off and verify:

```bash
# POST /api/update/autoupdate {"is_enabled": false}  → then GET to confirm false
curl -s http://10.0.4.20/api/status | grep -o '"auto_update_enabled":[a-z]*'
```

## Recovery (if the UI ever bricks or the intercom wedges)

SHA-256-verified stock bundle lives at
`/home/max/Documents/busy_bar/recovery/busybar-f22-update_signed-1.0.2.tgz`. Restore
BOTH chips (run from `busybar-firmware/`):

```bash
PYTHONPATH= toolchain/x86_64-linux/bin/python3.11 scripts/update_over_http.py \
  --file ../recovery/busybar-f22-update_signed-1.0.2.tgz
```

To rebuild the fork afterward: build (see **build-firmware**), reflash U5 with
`INTERCOM_FORCE_VERSION=07e850ec`, and re-deploy resources (flashing U5 alone leaves
the picker present but the activity list empty).
