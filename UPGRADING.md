# Fork Maintenance & Upstream Upgrades

This fork adds **on-device activity selection** (Setup → Activity picker) plus a
few owner-preference tweaks on top of `busy-app/busybar-firmware`. This document
is the checklist for pulling in upstream changes without breaking that feature.

The design rationale and full detail live in
`../firmware-activity-selection-plan.md`. This file is the *maintenance* view.

---

## 1. What this fork changes (the surface to protect)

Kept deliberately small and mostly **additive**, so upgrades stay tractable.

### New files (cannot textually conflict with upstream)

| File | Purpose |
|------|---------|
| `applications/main/busy/helpers/activity_list.{c,h}` | Loads `*.activity` files into a sorted list. |
| `applications/main/busy/scenes/busy_scene_setup_activity.c` | The Activity picker scene. |
| `applications/main/busy/resources/apps_assets/busy/activities/*.activity` | Seed activities (data, not code). |

### Small edits to shared upstream files (possible merge conflicts)

| File | Edit |
|------|------|
| `applications/main/busy/storage_macros.h` | `+ #define BUSY_ACTIVITIES_DIR` |
| `applications/main/busy/scenes/busy_scenes.h` | `+ BusyAppSceneIdSetupActivity` enum member |
| `applications/main/busy/scenes/busy_scenes.c` | `+ extern` + array registration entry |
| `applications/main/busy/scenes/busy_scene_setup.c` | `+ "Activity"` menu item (front+back) and its route |
| `applications/main/busy/scenes/busy_scene_timer.c` | audio cue gated to `timer_state == BusyTimerStateRest` |
| `applications/main/busy/scenes/busy_scene_finish.c` | removed `session_completed.snd` play |

Nothing under `targets/`, the bootloader, USB/network bring-up, the protobuf
transport, or provisioning/OTP is touched — keep it that way (that boundary is
what makes every change USB-recoverable).

---

## 2. The real risk is semantic, not textual

Textual merge conflicts on the six edited files are small and easy. The dangerous
failure is **silent semantic drift**: the feature depends on internal behaviors
of the `busy_timer` service that are **not a stable public contract**. If
upstream refactors them, your code may still compile and be wrong.

The load-bearing assumptions to re-verify on every upgrade:

1. **`busy_timer_set_profile()` carries `metadata.card_id`** all the way to the
   streamed snapshot — and `set_preset()` does **not** (it drops metadata). The
   accept handler in `busy_scene_setup_activity.c` relies on using `set_profile`.
2. **The staleness gate**: `busy_timer_set_profile_internal()` rejects a profile
   whose `timestamp_ms <=` the stored one. The accept handler restamps with
   `furi_hal_rtc_get_timestamp_ms()` to defeat it. If the gate changes,
   re-selection may silently no-op or misbehave.
3. **The snapshot exposes `card_id`** (`busy_timer_snapshot.c`) — this is the join
   key the host recorder maps to an activity. If the field name or shape changes,
   `activity_card_id_map.json` and `activity_recorder.py` need matching updates.
4. **The interval work→rest boundary waits, paused, when autostart is off**
   (`busy_timer.c` next-state logic) — the `--flow-overtime` recorder behavior and
   the audio gating both assume this.
5. **The `Menu` module API** (`menu_add_item` requires a non-NULL icon;
   `menu_set_selected_item_index` for pre-focus) — used by the Activity scene.

Files whose upstream history you should read on each upgrade:

```
git log <old>..<new> -- \
  applications/services/busy_timer/ \
  applications/main/busy/scenes/busy_scenes.h \
  applications/main/busy/scenes/busy_scenes.c \
  applications/main/busy/scenes/busy_scene_setup.c \
  applications/main/busy/scenes/busy_scene_timer.c \
  applications/main/busy/scenes/busy_scene_finish.c \
  applications/services/gui/modules/menu.h
```

---

## 3. Git setup (once)

```bash
# origin = your fork; upstream = the public repo
git remote add upstream https://github.com/busy-app/busybar-firmware.git
git fetch upstream --tags
```

Keep the feature as a **small number of focused commits** (ideally one per
concern: loader, scene, audio) so they rebase cleanly. Avoid interleaving feature
work with unrelated formatting.

**Pin to release tags, not `dev` HEAD.** `dev` is unreleased and churns; a release
tag is a known-good, and its commit hash is also what you need for the intercom
pin (§5).

---

## 4. Upgrade procedure

```bash
git fetch upstream --tags
git checkout -b sync/<version> <your-feature-branch>

# Rebase your feature commits onto the new release (preferred — linear history):
git rebase --onto <new-release-tag> <old-release-tag>
#   resolve conflicts in the six edited files (they are small),
#   then `git rebase --continue`.

# --recursive submodules move with releases; refresh them:
git submodule update --init --recursive
```

If a rebase gets messy, a `git merge <new-release-tag>` is an acceptable fallback;
you lose linear history but the conflict set is the same six files.

---

## 5. Post-upgrade verification checklist (~15 min)

Run **in order**; each step gates the next.

1. **Build.** `./fbt` — a clean build catches API-signature breaks as compile
   errors (the cheapest way to find that an internal function moved/changed).
2. **Recorder self-test.** From the project root:
   `python3 activity_recorder.py --self-test` — confirms host-side session logic
   (unaffected by firmware, but free insurance before you rely on the log).
3. **Flash with the intercom pin** (see §5.1):
   `./fbt flash_usb INTERCOM_FORCE_VERSION=07e850ec`. The pin does not change on an
   upstream sync (the stock Si917 is left in place); never flash without it.
4. **Linchpin check** — the one that catches silent semantic drift. On the device,
   Setup → Activity → select one, then:
   `curl -s http://<addr>/api/busy/snapshot` and confirm `snapshot.card_id`
   equals that activity's UUID. If it doesn't, assumption #1 or #3 in §2 broke.
5. **Re-selection check** — select the same activity twice; the second run must
   start (not silently no-op). Confirms the restamp still defeats the gate (#2).
6. **Audio check** — start Work (interval): a **work** phase ending is silent;
   a **break** ending chimes. Confirms `timer_state` gating (`busy_scene_timer.c`)
   survived.

If any of 4–6 fail, diff the relevant `busy_timer` source against the version you
built against and adjust the accept handler / recorder accordingly.

### 5.1 The intercom pin

`INTERCOM_FORCE_VERSION` must equal the **Si917's** handshake string, which is its
firmware git hash. Because we flash U5-only and leave the stock Si917 in place, that
value is **`07e850ec`** (stock 1.0.2's commit hash) and does **not** change when you
sync upstream — the Si917 stays stock. So the normal flow is simply:

```bash
./fbt flash_usb INTERCOM_FORCE_VERSION=07e850ec
```

**Reading the pin from the device — mind which field.** `/api/status/firmware`
exposes two version fields that mean different things:

- `commit_hash` — the **U5's own** git hash. It equals the pin **only when the device
  is on stock firmware**; on an already-custom U5 it is the U5's hash (e.g.
  `ff197daa`), **not** the pin.
- `intercom_version` — the U5's **forced control string** (from
  `intercom_get_version_string()`, NOT read from the Si917). On a custom U5 this
  echoes the last pin used.

So: on stock → read `commit_hash`; on a custom U5 → read `intercom_version`, or just
reuse `07e850ec`.

```bash
curl -s http://<addr>/api/status/firmware   # → "commit_hash":"…","intercom_version":"07e850ec"
```

`flash_usb` runs a pre-flight (`update_over_http.py --intercom-version`) that aborts
before upload if the device's current `intercom_version` ≠ the pin, so a *wrong* pin
is refused. The genuine failure mode is omitting the pin entirely: no guard runs and
the U5 boots with a mismatched string → "System error, restart device" screen.
Verify after: `intercom_version` still `07e850ec` and `nwp_version`/`matter_version`
are populated (proof the intercom link handshook). Full procedure — including the
first-flash-from-stock manual-upload case and recovery — is in the **flash-firmware**
skill (`.claude/skills/flash-firmware/`); background in plan §9.3.

---

## 6. Recovery

The verified stock **1.0.2** recovery bundle lives in `../recovery/`. If an
upgrade ever bricks the UI or wedges the intercom, restore it (plan §9.2):

```bash
PYTHONPATH= ./toolchain/x86_64-linux/bin/python3.11 \
  scripts/update_over_http.py --file ../recovery/busybar-f22-update_signed-1.0.2.tgz
```

Keep a matching signed recovery bundle for whatever release you standardize on
(download + SHA-256 verify from `https://update.busy.app/busybar-firmware/directory.json`).

---

## 7. Low/zero-maintenance pieces

- `resources/.../activities/*.activity` and `../activity_card_id_map.json` are
  **pure data** — they carry across upgrades untouched (unless the profile JSON
  schema changes, which step 4 would catch).
- `../activity_recorder.py` lives outside this repo and couples only to the
  snapshot JSON schema, not to firmware code — it needs attention only if step 2
  or step 4 reveals a schema change.
