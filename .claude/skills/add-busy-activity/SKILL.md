---
name: add-busy-activity
description: Add a new activity to the Busy Bar on-device activity picker (e.g. "Chores", "Exercise", "Reading"). Use when creating a new .activity, choosing INFINITE vs INTERVAL timing, or wiring an activity to its menu icon and theme animation. Covers the exact file set, the id/sort_order scheme, and the fact that activities/themes/icons are globbed (no central registry to edit).
---

# Add a Busy Bar activity

A "seed" activity in the Setup → Activity picker is defined entirely by files under
`applications/main/busy/resources/apps_assets/busy/` plus assets under `assets/`.
**There is no registry, enum, or manifest to edit** — the loader globs
`activities/*.activity`, `busy_theme_read(name)` resolves theme dirs, and the icon
picker globs `images/`. Adding files is sufficient; the Setup→Theme picker
auto-enumerates too.

## The 5 files a fully-custom activity needs

1. **`activities/<name>.activity`** — the definition (JSON). See schema below.
2. **`assets/images/external/busy/<icon>_8x8.png`** + **`<icon>_11x11.png`** — the
   menu icon, **paired** (both sizes must exist or the back-display menu row
   breaks). Author with the **busy-profile-image** skill.
3. **`themes/<theme>/theme.json`** — points the active-screen animation at a
   `.anim`. See the **busy-animation** skill.
4. **`assets/shared/animations/<name>_72x16.zip`** — the animation frames. Built
   and validated per the **busy-animation** skill.

(Reusing an existing icon/theme drops the count; e.g. an activity that reuses the
stock `flow` theme needs no new animation.)

## .activity schema

```json
{
  "sort_order": 70,
  "title": "Chores",
  "icon": "house",
  "id": "b1a70000-0000-4000-8000-000000000007",
  "profile_timestamp_ms": 0,
  "timer_settings": { "type": "INFINITE" },
  "busy_bar_settings": {
    "theme": "chores",
    "show_work_phase_only": false,
    "trigger_smart_home": false
  }
}
```

- **`sort_order`** — picker ordering. Existing seeds: Rec Reading 10, Tech Reading
  20, Work 30, Development 40, Exercise 50, Perfect Form 60, Chores 70. Use the next
  free slot.
- **`title`** — display name in the picker.
- **`icon`** — a size-agnostic **base name**; the picker appends `_8x8.image`
  (front) / `_11x11.image` (back) and falls back to `hourglass` if omitted. Must
  match the icon PNG base names from file 2.
- **`id`** — a UUID that is **also the `snapshot.card_id`** streamed when the
  activity runs (host recorder keys on it). Follow the sequence
  `b1a70000-0000-4000-8000-0000000000NN` (Chores = `…07`); pick the next NN.
- **`timer_settings.type`**:
  - **`INFINITE`** — no fixed duration; the screen shows a **count-up elapsed
    timer** (added for unbounded activities). Use for open-ended work: Reading,
    Exercise, Chores. (INFINITE runs no poll timer — elapsed is computed on demand
    from the wall clock — so it's cheap.)
  - **`INTERVAL`** — pomodoro-style; add `interval_work_ms`, `interval_rest_ms`,
    `interval_work_cycles_count`, `is_autostart_enabled` (Work = 25/5×4;
    Development/Perfect Form = 25/5×2, i.e. 1500000/300000 ms).
  - Prefer `INFINITE` over a hard-stop `SIMPLE` for "log true time" activities —
    SIMPLE would stop at the target instead of tracking overtime.
- **`busy_bar_settings.theme`** — the theme dir name from file 3.

## Audio note

Firmware audio is customized in this fork: the end-of-phase sound plays **only at
the end of a break (rest)** — silent at work/activity/session end
(`busy_scene_timer.c` gated to `BusyTimerStateRest`; `session_completed.snd`
removed from `busy_scene_finish.c`). New activities inherit this.

## Ship it

Build + flash deploy the new files as resources: **build-firmware** then
**flash-firmware** (the pinned `flash_usb` flow). Verify on device:

```bash
curl -s "http://10.0.4.20/api/storage/list?path=/ext/apps_assets/busy/activities"
# → your <name>.activity should be listed
```

Then Setup → Activity → select it, and confirm
`curl -s http://10.0.4.20/api/busy/snapshot` reports `card_id` == your UUID.

> **Renaming/retiring an activity or asset later needs THREE deletes** — see the
> retirement warning in the **flash-firmware** skill, or the device orphan
> reappears on the next flash.
