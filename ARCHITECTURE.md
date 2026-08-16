# Architecture — `drotiesel.fw12-tablet`

Design derived from measured Phase 0 results. See `FINDINGS.md` for the
evidence behind every claim here.

**Revised 2026-08-15** after discovering Hyprland's Lua config can do the
hardware work itself. The earlier draft proposed a C daemon for tablet
detection and rotation; that has been removed. Nothing is kept here because it
was already written.

---

## The one-paragraph version

One piece. **Tablet detection and auto-rotation are ~200 lines of Lua** loaded
into Hyprland's own config: the compositor already receives the
`SW_TABLET_MODE` switch and can read the accelerometer from sysfs, so no
separate process, socket, or systemd unit is involved. Plus a one-time root
fix for a firmware probe race that otherwise costs the tablet switch on some
boots.

**The on-screen keyboard was built and then dropped** in favour of
`plasma-keyboard` from the Arch `extra` repository. See below.

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

## The keyboard: dropped

There is no keyboard here any more. It was built -- a C daemon acting as
fcitx5's virtual-keyboard backend, injecting through
`zwp_virtual_keyboard_v1`, plus a Quickshell panel -- and it worked in the
sense that every individual piece could be demonstrated: auto-show on focus,
uppercase, AltGr, dead keys, live layout following.

It was still bad to type on, and that is the only test that counts. Keys were
missed, the space bar worst of all; each fix found a real defect and the thing
underneath was still unpleasant. Replaced by `plasma-keyboard` from the Arch
`extra` repository, which is maintained by people who do this full time.

The code is in git history (removed at `HEAD` of the keyboard work) if it is
ever wanted. `FINDINGS.md` 3.x keeps the measurements, because what was learned
about fcitx5's virtual-keyboard protocol is worth more than the code was.

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

- **The bar is hard to hit by touch** (§5.4) — 6.9 mm targets, and a missed tap
  reaching the wallpaper opens the picker on double-click. Reported upstream;
  out of scope here by decision.
- **One boot in N has no switch** until the initramfs fix is applied. Now
  applied on this machine, after it cost a boot.

---

## Build order

1. ~~`fw12d` skeleton~~ — replaced by Component A.
2. Component A: verify live on hardware (fold, four orientations, SUPER+R lock,
   `hyprctl reload` idempotency, suspend/resume).
3. System units + initramfs fix; reboot several times to confirm the race is
   closed. **Done** — installed after the race cost a real boot.
4. ~~On-screen keyboard~~ — built, rejected, removed. `plasma-keyboard` instead.
5. Packaging: PKGBUILD, README, marketplace submission.
