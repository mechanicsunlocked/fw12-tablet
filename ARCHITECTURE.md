# Architecture — `drotiesel.fw12-tablet`

Proposed design, derived from measured Phase 0 results. See `FINDINGS.md` for
the evidence behind every claim here.

**Status: awaiting approval. No implementation code written yet.**

---

## The one-paragraph version

Two pieces. A small C daemon (`fw12d`) owns the hardware and the input-method
conversation: it watches the `SW_TABLET_MODE` switch, reads the display
accelerometer, drives Hyprland's rotation, and registers as **fcitx5's virtual
keyboard backend** over DBus. A Quickshell plugin owns everything visible: a
bottom-anchored keyboard surface, a bar widget, and the Omarchy theming. They
talk over a Unix socket carrying JSON lines. Notably, **no Wayland protocol
client code is needed anywhere** — Quickshell already owns the layer surface
and fcitx5 already owns the input method.

---

## What changed from the brief, and why

| Brief said | We do instead | Because |
|---|---|---|
| fw12d becomes the `zwp_input_method_v2` client | fw12d becomes fcitx5's virtual-keyboard backend | fcitx5 holds the seat's only IM slot and Omarchy ships it for `~/.XCompose`. Evicting it breaks compose keys. §3 |
| Inject via `commit_string` + `zwp_virtual_keyboard_v1` | Call fcitx5's `ProcessKeyEvent` | fcitx5 already does keymap, dead keys, AltGr, compose, candidates. §3.1 |
| Qt Virtual Keyboard `InputPanel` for the UI | Our own QML keys, legends derived from xkb | Qt VK loads in Quickshell but is inert: `availableLocales = []`, wants to *be* the Qt IM, conflicts with `QT_IM_MODULE=fcitx`, and has no `lb`. §5.6 |
| Subscribe to `iio-sensor-proxy` over DBus | Read `accel-display` from sysfs by label | Proxy picks its accelerometer **by index**, and indices are not stable across boots. Direct read is less code and ~1 s faster. §2.1, §2.2c |
| Detect tablet mode from the switch (assumed present) | Same, but with an explicit absent/hotplug state machine | The switch is bound by a **non-deterministic** boot race. §1.3 |
| Scrape Hyprland `socket2` for `activelayout` | Ask fcitx5 / read the xkb keymap | fcitx5 is the authority once it is the input method. |
| squeekboard as Phase 0.5 baseline | Skipped | It is an input-method-v2 client; it cannot coexist with fcitx5. §3.3 |
| Decide `lb` layout handling | Follow the live xkb layout; no custom layout | There is no `lb` in xkb *or* Qt VK. User is moving to `us(intl)`, whose dead keys cover the accent set. §5.5 |

---

## Component A — `fw12d` (C)

A single-threaded `poll()` loop over four file descriptors. No threads, no
timers beyond `timerfd`, no subprocesses.

```
                    ┌───────────────────────────────┐
   evdev  ────────► │                               │ ──► Hyprland .socket.sock
   inotify ───────► │            fw12d              │      (monitor + touch +
   sysfs (accel) ─► │                               │       tablet transform)
   timerfd ───────► │                               │
                    │                               │ ◄─► fcitx5 (sd-bus)
                    └───────────────┬───────────────┘
                                    │ JSON lines
                                    ▼
                        $XDG_RUNTIME_DIR/fw12d.sock
                                    │
                                    ▼
                       Quickshell plugin (Component B)
```

### A1. Tablet-mode detection

Open the switch by its stable path,
`/dev/input/by-path/platform-gpio-keys.1.auto-event`. Read the initial level
with `EVIOCGSW` (not just wait for an edge — we may start mid-fold), then read
`EV_SW`/`SW_TABLET_MODE` events.

Because the device may be **absent at startup** and may **vanish on
unbind/resume** (§1.3), this is a state machine, not an `open()`:

- absent → watch `/dev/input` with **inotify** for `IN_CREATE`, rescan on each
  event, attach when a device advertising `SW_TABLET_MODE` appears
- attached → on `read()` returning `ENODEV`/EOF, close and fall back to absent
- while absent, fall back to the **hinge angle** with the measured thresholds
  (enter >230°, exit <150°) and `>360 ⇒ hold current state` for the sentinel

The angle fallback is explicitly a degraded mode, logged as such — not the
primary path (§1.5).

**We do not disable the physical keyboard or touchpad.** libinput already pairs
them with the tablet-mode switch and suspends them automatically (§1.6). Doing
it ourselves would duplicate that and risk leaving devices suspended if the
daemon dies.

**`fw12d` runs as an unprivileged user process.** Membership in group `input`
is sufficient to open the switch device and issue `EVIOCGSW` — verified with
`tools/swstate.c` running as a plain user. Only the boot-time bind helper and
the initramfs change need root, and those are one-shot system units, not the
daemon.

### A2. Rotation

Resolve the accelerometer **by label** at open time (`accel-display`), never by
index (§2.1). Re-resolve if the read fails.

Sample at 4 Hz while in tablet mode, and not at all otherwise — rotation is
gated on tablet mode exactly as GNOME and Windows do it. Classify with the
measured convention (§2.2b):

```
|z| dominant                → flat: hold previous orientation
|y| dominant, y > 0         → normal      transform 0
|x| dominant, x > 0         → right-up    transform 3
|y| dominant, y < 0         → bottom-up   transform 2
|x| dominant, x < 0         → left-up     transform 1
```

Require a candidate orientation to persist **500 ms** before applying, and
require the dominant axis to exceed ~40% of 1 g (≈6500 raw) to count as
dominant at all. Apply by writing directly to Hyprland's IPC socket
`$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock` — no
`hyprctl` fork, no shell:

```
/keyword monitor <name>,preferred,auto,<scale>,transform,<n>
/keyword input:touchdevice:transform <n>
/keyword input:tablet:transform <n>
```

All three, always together — screen, touch and pen must rotate as one. On
leaving tablet mode, reset all three to 0.

### A3. fcitx5 virtual-keyboard backend

Using **sd-bus** (`libsystemd`, always present on Arch — no new dependency):

- own the bus name `org.fcitx.Fcitx5.VirtualKeyboard`
- export `/org/fcitx/virtualkeyboard/impanel` implementing
  `org.fcitx.Fcitx5.VirtualKeyboard1`; fcitx5 calls into this to show/hide the
  keyboard and to push candidate updates — **this is the auto-show source, and
  it is protocol truth, not a heuristic**
- call `ProcessKeyEvent(keysym, keycode, state, isRelease, time)` on
  `org.fcitx.Fcitx5` `/virtualkeyboard` for every key the UI reports
- call `ShowVirtualKeyboard` / `HideVirtualKeyboard` for the manual toggle
- **on exit, restore the previous UI** — the Phase 0 probe left `CurrentUI`
  empty when it released the name, and that must not happen in production

Watch `NameOwnerChanged` for `org.fcitx.Fcitx5` so a fcitx5 restart is
recovered from cleanly rather than papered over with a retry loop.

### A4. Keymap and legends

`libxkbcommon` compiles the active layout and derives, for every key and every
shift level, the glyph it produces — reusing the approach already proven in
`fw12tab/lib/oskbd.c`, including its dead-key glyph table (§5.5). The daemon
computes the whole key table in C and pushes it to the UI as JSON. QML stays
dumb: it renders labels and reports taps.

Body shape (ISO vs ANSI) is chosen by whether the active keymap binds
`KEY_102ND`, so `de` (ISO) and `us(intl)` (ANSI) both render correctly.

### A5. Socket protocol

`$XDG_RUNTIME_DIR/fw12d.sock`, JSON lines, newline-delimited.

Daemon → plugin:
```json
{"t":"tablet","on":true}
{"t":"osk","show":true}
{"t":"orientation","v":"left-up"}
{"t":"keymap","layout":"us","variant":"intl","iso":false,"keys":[...]}
```

Plugin → daemon:
```json
{"t":"key","keysym":100,"code":32,"release":false}
{"t":"toggle-osk"}
{"t":"rotation-lock","on":true}
```

---

## Component B — Quickshell plugin

`~/.config/omarchy/plugins/drotiesel.fw12-tablet/`

```json
{
  "schemaVersion": 1,
  "id": "drotiesel.fw12-tablet",
  "kinds": ["service", "panel", "bar-widget"],
  "keepLoaded": true,
  "entryPoints": {
    "service":   "Service.qml",
    "panel":     "Keyboard.qml",
    "barWidget": "BarWidget.qml"
  }
}
```

- **`service`** — mounts at shell startup, connects to the daemon socket
  (`Quickshell.Io.Socket`), holds state, supervises reconnection.
- **`panel`** (`keepLoaded: true`) — the keyboard, a `PanelWindow` anchored
  left/right/bottom with `WlrLayershell.keyboardFocus: WlrKeyboardFocus.None`.
  That property is the first-party-proven answer to the "must not steal focus"
  requirement (it is what the OSD uses). An `exclusiveZone` equal to the
  keyboard height so tiled windows shrink above it rather than hide behind it.
- **`bar-widget`** — tablet-mode indicator, OSK toggle, rotation lock.

Theming via the `qs.Commons` `Color` singleton and `Style.space()` — the same
tokens every first-party panel uses, so it follows theme switches
automatically.

---

## System integration

Three pieces, all addressing the non-deterministic probe race (§1.3):

1. `/etc/mkinitcpio.conf.d/fw12-tablet.conf` → `MODULES+=(pinctrl_tigerlake)`
   so the pin controller is up before `soc_button_array` probes. **This is the
   actual fix.**
2. A systemd system unit that binds `INT33D3:*` at boot if it is still unbound.
   Belt and braces.
3. `/usr/lib/systemd/system-sleep/` hook that re-binds after resume.

Plus a systemd **user** unit for `fw12d` itself, `PartOf=graphical-session.target`,
modelled on `omarchy-fcitx5.service` (including its `ConditionEnvironment=WAYLAND_DISPLAY`
guard, which exists for a real reason — an `omarchy update` over SSH otherwise
starts a broken instance).

---

## Why C rather than Rust

The brief prefers Rust unless C is clearly simpler. Under *this* architecture C
is clearly simpler, and the deciding factor is what the daemon no longer has to
do.

**There is no Wayland protocol client work left.** Quickshell owns the layer
surface; fcitx5 owns the input method. The strongest argument for Rust —
`smithay-client-toolkit` for hand-rolled protocol state machines — applies to
zero lines of this design.

What remains is evdev ioctls, inotify, sysfs reads, `poll()`, sd-bus, and a
Unix socket. Every one of those is a C-native API with a thin or absent Rust
wrapper. Against that:

- ~750 lines of working, MIT-licensed C for **this exact hardware** already
  exist in `fw12tab`, by the same author, with correct protocol handling
- `libxkbcommon` and `sd-bus` are C libraries already on the system; the C
  build needs no new packages at all
- no Rust toolchain is installed on this machine
- estimated size is 600–900 lines — below where Rust's safety guarantees
  typically start paying for their integration cost

Honest counterpoint: the fcitx5 DBus state machine plus the switch hotplug
logic is the fiddliest part, and is exactly where Rust's error handling would
help. If the daemon grows past ~1500 lines or gains real concurrency, that
tradeoff flips. It does not appear likely here.

---

## Known limitations, stated up front

- **Ghostty.** If it never activates a text input context, fcitx5 never sees
  focus and the keyboard never auto-shows. This is app-side and would affect
  *any* design, including the brief's original one. The manual toggle covers
  it. To be measured in Phase 1 rather than assumed.
- **fcitx5 is required.** Omarchy ships and runs it by default, so this is
  reasonable — but if a user disables it, the keyboard loses auto-show. Detect
  and report clearly rather than failing silently.
- **The bar remains touch-hostile** (§5.4) — 200 ms press-and-hold, 6.9 mm
  targets. Being reported upstream; out of scope here by decision.
- **One boot in N has no switch** until the initramfs fix is applied and the
  machine rebooted. The angle fallback covers the gap, degraded.

---

## Build order

1. `fw12d` skeleton: switch state machine + socket + logging. Verify against
   real folds.
2. Rotation. Verify all four orientations, touch and pen tracking.
3. System units + initramfs fix. Reboot several times to confirm the race is
   closed.
4. fcitx5 backend registration; prove auto-show in a GTK app.
5. Quickshell plugin: service + bar widget.
6. Keyboard UI and the keymap/legend pipeline.
7. Test matrix: GTK, Qt, Firefox, Ghostty; suspend/resume; shell restart;
   layout switch `de` → `us(intl)`.
8. Packaging: PKGBUILD, README, marketplace submission.
