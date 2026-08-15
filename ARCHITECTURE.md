# Architecture — `drotiesel.fw12-tablet`

Design derived from measured Phase 0 results. See `FINDINGS.md` for the
evidence behind every claim here.

**Revised 2026-08-15** after discovering Hyprland's Lua config can do the
hardware work itself. The earlier draft proposed a C daemon for tablet
detection and rotation; that has been removed. Nothing is kept here because it
was already written.

---

## The one-paragraph version

Two pieces, and neither is a general-purpose daemon. **Tablet detection and
auto-rotation are ~200 lines of Lua** loaded into Hyprland's own config: the
compositor already receives the `SW_TABLET_MODE` switch and can read the
accelerometer from sysfs, so no separate process, socket, or systemd unit is
involved. **The on-screen keyboard** is the only part needing native code,
because it must own a DBus name to act as fcitx5's virtual-keyboard backend,
and must draw — neither of which Lua can do.

---

## Component A — `lua/fw12-tablet.lua` (no daemon)

Installed to `~/.config/hypr/fw12-tablet.lua`, activated by one line in
`~/.config/hypr/hyprland.lua`:

```lua
require("hypr.fw12-tablet")
```

`package.path` includes `~/.config/?.lua` (set by Omarchy's `bootstrap.lua`),
and Omarchy's own config file invites exactly this: *"Add any other personal
Hyprland configuration below."*

### How each part works

| Concern | Mechanism | Evidence |
|---|---|---|
| Tablet enter/exit | `hl.bind("switch:on\|off:gpio-keys", …, {locked=true})` | binds fire, both directions, verified |
| Initial state on load | hinge angle `>= 200°` from `cros-ec-lid-angle` | binds are edge-triggered and cannot report current position |
| Orientation | `io.open` on `accel-display` `in_accel_{x,y,z}_raw` at 4 Hz | 82 µs mean / 291 µs worst per 3-axis read |
| Apply rotation | `hl.monitor{transform=…}` + `hl.config{input={touchdevice,tablet}}` | applied and reverted live |
| Rotation lock | `hl.bind("SUPER + R", …)` | SUPER+R free; existing R binds are SUPER+CTRL variants |

### The decisions that are not obvious

**Sensors are resolved by `label`/`name`, never by index.** IIO numbering moves
between boots — `cros-ec-accel.11.auto` was `accel-base` on one boot and
`accel-display` on the next. An index would silently rotate to the *keyboard*
half of the laptop and look like flaky hardware. Lua has no directory listing,
so the module probes `iio:device0..15` and matches the attribute.

**The switch is the source of truth, not the hinge angle.** The firmware owns
the hysteresis (enters 220–257°, leaves 106–170°) and emits one clean debounced
event per transition. The angle reads `500` — an "indeterminate" sentinel —
reliably *during* the fold, exactly where detection matters most. The angle is
used only to seed initial state at load, where `> 360` is treated as unknown.

**Flat means hold, not guess.** With the screen horizontal, gravity is on Z and
X/Y carry no orientation information. The classifier returns "no opinion" and
the previous orientation stands, rather than snapping to a default.

**Monitor, touch and stylus rotate together**, always, or the pen and finger
stop landing where the user is pointing.

**Reload safety.** Omarchy's `bootstrap.lua` clears every `hypr.*` module from
`package.loaded` on config reload, so this file re-executes on every `hyprctl
reload`. State lives on a global and prior binds and timers are torn down
first; otherwise each reload stacks another 4 Hz timer and a duplicate bind.

**`hyprctl keyword` does not work here.** Omarchy 4 uses the Lua config
manager, which refuses it outright (`keyword can't work with non-legacy
parsers. Use eval.`) while `hyprctl` still exits 0. Any port from an
Omarchy 3 / hyprlang setup fails silently. Direct `hl.*` calls avoid the issue
entirely.

### The trade-off, stated honestly

The 4 Hz poll runs **inside the compositor process**. A callback that blocks or
throws degrades the whole desktop, which a separate process could not. Measured
cost is 82 µs mean and 291 µs worst per sample — 0.03% of compositor time, and
under 2% of one 60 Hz frame even at worst. That is comfortably safe, but it is
the reason the tick does nothing at all outside tablet mode and never retries
in a loop.

---

## Component B — on-screen keyboard

The only part that needs native code, for two reasons that are not going away:

- **Lua cannot draw.** The entire 1777-line `hl.meta.lua` API is configuration.
  No surface, no widget, no rendering call.
- **Quickshell cannot speak arbitrary DBus.** Its DBus use is internal and
  wrapped into fixed services (Mpris, UPower, Notifications, Polkit…). There is
  no generic client exposed to QML, so a plugin cannot own a bus name or export
  an object.

### B1. `fw12-oskd` — small C helper (sd-bus)

- owns `org.fcitx.Fcitx5.VirtualKeyboard`
- exports `/org/fcitx/virtualkeyboard/impanel` implementing
  `org.fcitx.Fcitx5.VirtualKeyboard1`; fcitx5 calls in to show/hide — **this is
  the auto-show source, protocol truth rather than a focus heuristic**
- calls `ProcessKeyEvent(keysym, keycode, state, isRelease, time)` on
  `org.fcitx.Fcitx5` `/virtualkeyboard` to type
- derives key legends from the live xkb keymap via `libxkbcommon`, so `de` and
  `us(intl)` both render correctly, including dead keys
- **restores the previous fcitx5 UI on exit** — the Phase 0 probe left
  `CurrentUI` empty when it released the name, which must not happen in
  production
- watches `NameOwnerChanged` so a fcitx5 restart is recovered from cleanly

No root, no Wayland protocol client, no evdev.

### B2. Quickshell plugin

`~/.config/omarchy/plugins/drotiesel.fw12-tablet/` — `service` + `panel` +
`bar-widget`. The keyboard is a `PanelWindow` with
`WlrLayershell.keyboardFocus: WlrKeyboardFocus.None`, which is the
first-party-proven answer to "must not steal focus" (the OSD uses it). Themed
through the `qs.Commons` `Color` singleton so it follows theme switches.

**Quickshell is 0.3.0 — pre-1.0, and the biggest version-churn risk in this
design.** That is contained by construction: tablet mode and rotation are in
Component A and do not involve Quickshell at all. A Quickshell update that
breaks the plugin costs the on-screen keyboard and the bar widget; it does not
cost auto-rotation.

---

## What was removed from the earlier draft

`tabletsw.c`, `accel.c`, `hypr.c`, the `poll()` loop, the inotify hotplug
state machine, the JSON-lines Unix socket protocol, and the systemd user unit —
all replaced by Component A. The switch hotplug problem disappeared with them,
because Hyprland owns the device rather than us.

The C→Rust question is now moot for this part: there is no C here to write.
For Component B the same reasoning as before applies — sd-bus and libxkbcommon
are C libraries, there is no Wayland protocol work, and the helper is small.

---

## System integration

Two one-shot root pieces, addressing the non-deterministic probe race (§1.3):

1. `/etc/mkinitcpio.conf.d/fw12-tablet.conf` → `MODULES+=(pinctrl_tigerlake)`
   so the pin controller is up before `soc_button_array` probes. **The actual
   fix.**
2. A systemd system unit binding `INT33D3:*` at boot if still unbound, plus a
   `system-sleep` hook re-binding after resume. Belt and braces.

Neither is needed at runtime by Component A, which degrades gracefully: with no
switch device the binds simply never fire, and the module stays in laptop mode.

---

## Known limitations, stated up front

- **Ghostty.** If it never activates a text input context, fcitx5 never sees
  focus and the keyboard never auto-shows. App-side, and would affect any
  design. The manual toggle covers it. To be measured, not assumed.
- **fcitx5 is required** for the keyboard. Omarchy ships and runs it by
  default; if a user disables it, auto-show is lost. Detect and say so rather
  than failing silently.
- **The bar is hard to hit by touch** (§5.4) — 6.9 mm targets, and a missed tap
  reaching the wallpaper opens the picker on double-click. Reported upstream;
  out of scope here by decision.
- **One boot in N has no switch** until the initramfs fix is applied.

---

## Build order

1. ~~`fw12d` skeleton~~ — replaced by Component A.
2. Component A: verify live on hardware (fold, four orientations, SUPER+R lock,
   `hyprctl reload` idempotency, suspend/resume).
3. System units + initramfs fix; reboot several times to confirm the race is
   closed.
4. `fw12-oskd`: register with fcitx5, prove auto-show in a GTK app.
5. Quickshell plugin: service + bar widget.
6. Keyboard UI and the keymap/legend pipeline.
7. Test matrix: GTK, Qt, Firefox, Ghostty; suspend/resume; shell restart;
   layout switch `de` → `us(intl)`.
8. Packaging: PKGBUILD, README, marketplace submission.
