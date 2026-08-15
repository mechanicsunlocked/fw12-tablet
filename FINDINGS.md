# FINDINGS — Phase 0

Framework Laptop 12 tablet-mode support for Omarchy 4 "Quattro".
All facts below were measured on this machine unless marked otherwise.

Date: 2026-08-15. Host: `fw12-omarchy-quattro`.

---

## 0. Platform baseline

| Item | Value |
|---|---|
| Vendor / product | Framework / Laptop 12 (13th Gen Intel Core) |
| Board / BIOS | FRAPMACP05 / 03.07 |
| CPU | 13th Gen Intel Core i5-1334U |
| OS | Omarchy 4.0.0 (`ID=omarchy`, `ID_LIKE=arch`) |
| Kernel | 7.1.8-arch1-3 |
| Hyprland | 0.56.2 (commit efb5099, 2026-08-05) |
| Quickshell | 0.3.0.r20.g28771c7 |
| Session | Wayland, `XDG_CURRENT_DESKTOP=Hyprland` |
| `$OMARCHY_PATH` | `/usr/share/omarchy` |

Not installed: `iio-sensor-proxy`, `squeekboard`, `qt6-virtualkeyboard`,
`wayland-utils`, `evtest`, `rust`/`rustup`. All are in the `extra` repo except
`wvkbd`, which is not in the repos at all.

Toolchain present: `gcc`, `cc`, `pkg-config`, `make`, `wayland-scanner`,
`wayland-client` 1.26.0. **No Rust toolchain is installed.**

---

## 1. Tablet-mode detection — RESOLVED: the switch works, but the race is real

> **Status update after reboot (2026-08-15 14:34).** On the *first* boot the
> switch was absent. After a plain reboot with **no configuration change**, it
> came up bound and fully working. Everything in §1.1–1.2 below describes the
> failing boot and is retained because it is the failure mode we must defend
> against; §1.4 is the working state.

### 1.4 CONFIRMED WORKING — the switch, its stable path, and its thresholds

Second boot, `soc_button_array` won the race:

```
N: Name="gpio-keys"
S: Sysfs=/devices/platform/INT33D3:00/gpio-keys.1.auto/input/input7
H: Handlers=event5
B: EV=21     B: SW=2         <- bit 1 only = SW_TABLET_MODE, nothing else
```

**Stable path** (the brief's step-1 deliverable):

```
/dev/input/by-path/platform-gpio-keys.1.auto-event -> ../event5
```

Live `evtest` capture of a real fold and unfold:

```
Event: time 1786797750.287562, type 5 (EV_SW), code 1 (SW_TABLET_MODE), value 1
Event: time 1786797761.190068, type 5 (EV_SW), code 1 (SW_TABLET_MODE), value 0
```

Clean, debounced, one event per transition. No chatter.

**Measured firmware hysteresis**, from a 1 Hz log of switch state against hinge
angle:

| | angle |
|---|---|
| last `laptop` sample before entering | 220° |
| first `TABLET` sample | 257° |
| last `TABLET` sample before leaving | 170° |
| first `laptop` sample after | 106° |

So enter is somewhere in 220–257° and exit in 106–170°. The firmware owns these
thresholds; they are not tunable. That is *good* — it is real hysteresis, which
is exactly what an angle-based state machine would have had to invent.

### 1.5 The `500` sentinel clusters exactly where tablet detection matters

From the same capture, during the fold through full 360°:

```
14 sw=TABLET angle=346
15 sw=TABLET angle=500     <- sentinel
16 sw=TABLET angle=500
17 sw=TABLET angle=500
18 sw=TABLET angle=360
```

`500` is not a rare motion artifact — it appears **reliably near full fold**,
where the two accelerometers are close to antiparallel and the EC cannot solve
the angle. That is precisely the posture in which we most need to know we are in
tablet mode.

**This settles the detection design: use `SW_TABLET_MODE` as the source of
truth, not the hinge angle.** The switch has firmware hysteresis, fires clean
single events, is immune to the sentinel, and is what GNOME and Windows gate
on. The angle's only remaining role is optional corroboration and diagnostics.

### 1.6 libinput already disables the keyboard and touchpad — we get that free

Strings in `/usr/lib/libinput.so.10`:

```
tablet-mode: paired %s<->%s
tablet-mode: activated for %s<->%s
tablet-mode: suspending device
tablet-mode: suspending touchpad
tablet-mode: resuming device
tablet-mode: resume touchpad
device is an unreliable tablet mode switch, filtering events.
```

libinput **pairs** the tablet-mode switch with the internal keyboard and
touchpad and suspends them while the switch is on, resuming on exit. This is
built-in behaviour, not something a compositor or client arranges.

**Consequence:** the testing-checklist item *"flip to tablet → physical
keyboard off"* is satisfied by the kernel/libinput stack with no code from us.
`fw12d` must **not** try to disable input devices itself — doing so would
double up on libinput and risk leaving devices suspended when our daemon dies.

Note also the last string: libinput carries a quirk for switches it considers
unreliable, in which case it filters their events. If tablet mode ever appears
dead on a machine where the switch is bound and `evtest` shows transitions,
that quirk is the thing to check
(`/usr/share/libinput/*.quirks`, `LIBINPUT_MODEL_*`).

### 1.7 Hyprland can bind to switch transitions

`strings /usr/bin/Hyprland` shows `switch:`, `switch:on:`, `switch:off:` — the
`bindl = ,switch:on:<device>, ...` binding form. This is an alternative trigger
path and a useful escape hatch, but `fw12d` reading evdev directly is preferred:
it gets the *initial* state via `EVIOCGSW` (a binding only ever sees edges), and
it does not depend on the user's Hyprland config being wired up.

### 1.1 On the first boot there was no `SW_TABLET_MODE` device

Dumping the `EV_SW` capability bitmask of every input device:

```
input0   sw=1     Lid Switch                   -> SW_LID only (bit 0)
input21  sw=4     HDA Intel PCH Headphone      -> jack sense
input22  sw=140   HDA Intel PCH HDMI/DP        -> jack sense
input23  sw=140   ...
```

`SW_TABLET_MODE` is bit 1 (`0x2`). **No device sets it.** `evtest` /
`libinput debug-events` would therefore have nothing to report, and step 1 of
the brief as written cannot be performed.

`intel_vbtn` and `intel_hid` are not loaded and have no ACPI device here — the
FW12 does not use that path.

### 1.2 Why: the `soc_button_array` probe race is live on this install

```
/sys/bus/platform/devices/INT33D3:00          EXISTS
/sys/bus/platform/drivers/soc_button_array/   loaded, ZERO devices bound
pinctrl_tigerlake                             loaded (3 users)
```

The ACPI device `INT33D3` — the gpio-keys node that carries `SW_TABLET_MODE` —
is present, and its driver is loaded, but **the two are not bound**. This is
exactly the boot probe race Framework's knowledgebase documents: if
`soc_button_array` probes before `pinctrl_tigerlake` has registered the pin
controller, the probe fails and the device is left unbound with no retry.

### 1.3 The race is NON-DETERMINISTIC — this is the key result

| | boot 1 (13:41) | boot 2 (14:34) |
|---|---|---|
| `INT33D3:00` present | yes | yes |
| bound to `soc_button_array` | **NO** | **YES** |
| `SW_TABLET_MODE` device | none | `gpio-keys`, event5 |
| `pinctrl_tigerlake` users | 3 | 5 |

**Same kernel, same initramfs, same config, no changes in between.** The switch
simply lost the race on one boot and won on the next.

This is the single most important robustness finding in Phase 0. It means:

- Any design that assumes the switch exists at startup will break on roughly
  some fraction of boots, silently, with no tablet mode at all.
- The bind helper is **not** optional hardening — it is required for the
  feature to work reliably.
- Putting `pinctrl_tigerlake` in `MODULES=()` in mkinitcpio is the actual fix
  (load the pin controller from the initramfs so it is always up first); the
  bind service is the belt-and-braces for boots and resumes where it still
  loses.
- fw12tab's `system/` directory was solving a real, reproducible problem, and
  its approach (initramfs module + boot unit + `system-sleep` post hook) is
  correct. Neither is currently applied on this install:
  `MODULES=()` is empty and there is no bind unit.

Binding by hand works and requires root:

```
echo INT33D3:00 > /sys/bus/platform/drivers/soc_button_array/bind
```

### 1.3 Omarchy does nothing with tablet mode

Grepping all of `$OMARCHY_PATH` for tablet / rotate / accel / OSK /
input-method terms returns only unrelated hits (`accel_profile` in an input
config comment, a `transform:` comment for portrait monitors, and CSS/QML
`rotation` properties on widgets).

**There is no physical-keyboard auto-disable on tablet mode in Omarchy.** The
brief's claim that this "already proves the switch works" does not hold — there
is no such feature, and consequently nothing for us to fight or coordinate
with. We own this behaviour entirely.

---

## 2. Sensors — better than the brief assumed

The FW12 does **not** expose a generic `iio-buffer-accel` / HID-sensor
accelerometer. It exposes the Framework EC sensor stack (ChromeOS-EC derived):

```
iio:device0   cros-ec-lid-angle    in_angl_raw
iio:device1   cros-ec-accel        label=accel-display   scale=0.000598550
iio:device2   cros-ec-accel        label=accel-base      scale=0.000598550
```

All three live under `platform/FRMWC004:00/cros-ec-dev.1.auto/cros-ec-sensorhub.2.auto/`.
`/dev/cros_ec` exists (root-only, mode 0600).

Live readings confirm the sensors work: `in_angl_raw` was observed changing
from `142` to `99` as the lid was moved during this session, and
`accel-display` reports plausible three-axis values.

Consequences:

- There are **two** accelerometers. Rotation must follow `accel-display`
  (the lid), not `accel-base`. A naive "first accel" pick is a 50/50 coin flip.
- The **hinge angle is directly readable**, which most convertibles do not
  offer. It is a viable tablet-mode signal on its own and a useful cross-check
  against the switch.
- The brief's note about commenting out an `iio-buffer-accel` udev rule is not
  applicable — that rule targets a different driver family. There is no such
  packaging problem here.

### 2.1 CRITICAL: IIO device numbering is not stable across boots

Comparing the two boots directly:

| identifier | boot 1 | boot 2 |
|---|---|---|
| lid-angle | `iio:device0` | `iio:device1` |
| accel-display | `iio:device1` | `iio:device0` |
| accel-base | `iio:device2` | `iio:device2` |
| cros-ec-dev | `cros-ec-dev.1.auto` | `cros-ec-dev.2.auto` |
| sensorhub | `.2.auto` | `.3.auto` |
| **`cros-ec-accel.11.auto`** | **accel-base** | **accel-display** |
| lid-angle platform | `cros-ec-lid-angle.12.auto` | `cros-ec-lid-angle.13.auto` |

Note the bolded row. The platform path `cros-ec-accel.11.auto` referred to the
**base** accelerometer on one boot and the **display** accelerometer on the
next. Anything that pins an `iio:deviceN` index or a `cros-ec-accel.N.auto`
path will, on some boots, silently read the keyboard half of the laptop instead
of the screen — producing rotation that is wrong in a way that looks
intermittent and hardware-ish rather than like a bug.

**The only stable identifiers are:**

- `name` — `cros-ec-lid-angle` / `cros-ec-accel`
- `label` — `accel-display` / `accel-base`
- `id` — `0` (display) / `1` (base)

Every read must resolve through `label`/`name` at open time. This is also a
plausible alternative explanation for the original "hinge stopped working after
resume" report: not a dead sensor, but a renumbered one.

### 2.2 `iio-sensor-proxy` works, but is the wrong dependency here

Installed and started (`iio-sensor-proxy 3.9-1`). It does report orientation:

```
=== Has accelerometer (orientation: normal, tilt: vertical)
net.hadess.SensorProxy AccelerometerOrientation = "normal"
```

But its own log shows the buffered path failing on this hardware:

```
Could not find trigger name associated with .../cros-ec-accel.11.auto/iio:device0
Buffer '/dev/iio:device0' did not have data within 0.5s
```

The cros-ec accelerometer exposes no IIO trigger, so the proxy's preferred
triggered-buffer read fails and it falls back to polling. Worse, it selected
**`iio:device0` by index** — which happened to be `accel-display` on this boot,
but per §2.1 that is luck, not logic.

Against that, reading the sensor ourselves costs three `open`/`read`/`close`
calls on a label-resolved path, with no DBus, no daemon, and no ambiguity about
which sensor we got.

### 2.2a The proxy only polls while a client holds a claim

An early test appeared to show the proxy frozen at `normal` through a full
four-orientation rotation. **That test was invalid.** `iio-sensor-proxy` does
not read the sensor until a client calls `ClaimAccelerometer`; a bare
`busctl get-property` does not claim it, so the property was simply stale.

Re-run with `monitor-sensor` held open for the whole capture (which does
claim), the proxy tracks correctly:

```
Accelerometer orientation changed: right-up
Accelerometer orientation changed: normal
Accelerometer orientation changed: right-up
Accelerometer orientation changed: bottom-up
Accelerometer orientation changed: left-up
Accelerometer orientation changed: normal
```

**Both paths work.** The proxy is a legitimate option, not a broken one. It
does lag the raw sensor by roughly one sample (~1 s) — at sample 50 the raw
data had already reached `bottom-up` while the proxy still reported `right-up`,
catching up at 51.

### 2.2b Empirically pinned axis convention — my first guess was inverted

The side-by-side comparison disagreed on 40 of 90 samples, and the pattern was
systematic: agreement on the Y axis (`normal` / `bottom-up`), consistent
disagreement on the X axis.

```
sample 30   x=+13600   proxy=right-up   naive=left-up
sample 70   x=-16784   proxy=left-up    naive=right-up
```

My initial classifier had the X sign backwards. The correct convention for
`accel-display` on this unit, matching `iio-sensor-proxy`:

| condition | orientation | Hyprland transform |
|---|---|---|
| \|y\| dominant, y > 0 | `normal` | 0 |
| \|x\| dominant, **x > 0** | `right-up` | 3 |
| \|y\| dominant, y < 0 | `bottom-up` | 2 |
| \|x\| dominant, **x < 0** | `left-up` | 1 |
| \|z\| dominant | flat — hold previous | — |

This is exactly the trap fw12tab documents in its README: *"Rotation comes out
mirrored? Swap the `1` and `3` in `_transform()` (panel mounting differs between
units)."* It is a real hazard, it is easy to get backwards, and it is not
guessable from first principles — it has to be measured. It is now measured on
**this** unit.

### 2.2c Recommendation

**Read `in_accel_{x,y,z}_raw` directly from the `accel-display`-labelled
device; do not depend on `iio-sensor-proxy`.**

The case is now narrower than it first appeared, since the proxy does work:

- no extra runtime dependency, systemd unit, or DBus round trip
- no claim/release lifecycle to manage, and no risk of reading a stale property
  because nothing currently holds a claim
- ~1 s lower latency
- the proxy selects its accelerometer by index (§2.1), which is not stable
  across boots; resolving by `label` ourselves is unambiguous
- it is genuinely less code than a correct DBus client would be

The proxy remains valuable as the **cross-check that established the axis
convention above**, and is worth keeping installed for diagnostics. This
diverges from the brief's proposed architecture deliberately, on measured
grounds.

### 2.3 Flat means "no orientation" — the state machine must hold, not guess

During the tablet-fold capture the screen ended up roughly horizontal and the
readings were dominated by Z:

```
12 sw=TABLET angle=257 disp=(944,-672,16816)     <- z ≈ +1 g, screen face-up
16 sw=TABLET angle=500 disp=(5744,2112,14928)
```

With gravity along Z, X and Y carry no orientation information and
`AccelerometerOrientation` stayed `normal` throughout. This is correct
behaviour, not a fault — but it means the rotation logic must detect the flat
case (|Z| dominant, X and Y both small) and **hold the last known orientation**
rather than snapping to a default. A naive `atan2(x, y)` on near-zero inputs
will jitter wildly.

**VERIFIED.** A second capture with the tablet held upright and rotated through
all four positions produced clean, unambiguous readings on the raw sensor:

```
normal      x=   784  y= 13856  z= 8832
right-up    x= 13600  y=  7424  z=-4224      (|x| dominant, positive)
bottom-up   x= -1056  y=-17024  z=-1040
left-up     x=-16784  y= -4448  z= -992      (|x| dominant, negative)
```

All four orientations are detected reliably, and the flat case correctly falls
through to "hold previous". Magnitudes sit near ±16384 (≈1 g at
`scale=0.000598550`), so a simple dominant-axis test with a dead zone is
sufficient — no filtering or trigonometry needed.

---

## 3. Wayland protocols — the showstopper check passed

`wayland-info` is not installed, so I compiled a small `wayland-client` C
program and enumerated the registry of the **running** compositor directly.
Relevant globals advertised:

```
zwp_input_method_manager_v2        v1
zwp_virtual_keyboard_manager_v1    v1
zwp_text_input_manager_v3          v1
zwp_text_input_manager_v1          v1
zwlr_layer_shell_v1                v5
zwp_tablet_manager_v2              v1
zwlr_output_manager_v1             v4
```

Everything the design needs is present.

### Does the shell own the input-method slot? No — but **fcitx5 does**

`omarchy-shell` is not the problem. The `quickshell` binary contains no
`zwp_input_method_manager_v2`, `zwp_input_method_v2`,
`zwp_virtual_keyboard_manager_v1`, or `zwp_text_input_manager_v3` symbols — the
only one of these it contains is `zwlr_layer_shell_v1`. The shell binds
layer-shell and nothing else.

**The seat's input-method slot is taken by fcitx5, which Omarchy 4 ships and
runs by default.**

```
$ pgrep -a fcitx5
1304 /usr/bin/fcitx5 --disable notificationitem

$ systemctl --user list-units --state=running | grep fcitx
omarchy-fcitx5.service   loaded active running   Fcitx5 input method (XCompose sequences)
```

- `fcitx5`, `fcitx5-gtk`, `fcitx5-qt` are in `install/omarchy-base.packages`.
- `$OMARCHY_PATH/default/systemd/user/omarchy-fcitx5.service` is
  `WantedBy=graphical-session.target`. Its stated purpose: *"fcitx5 turns the
  CapsLock compose sequences in `~/.XCompose` into text for Wayland clients."*
- `$OMARCHY_PATH/default/environment.d/10-omarchy-fcitx.conf` exports
  `INPUT_METHOD=fcitx`, `QT_IM_MODULE=fcitx`, `XMODIFIERS=@im=fcitx`,
  `SDL_IM_MODULE=fcitx`. All four are live in this session.
- `/usr/lib/fcitx5/libwaylandim.so` binds `zwp_input_method_manager_v2`,
  `zwp_input_method_v2`, **and** `zwp_virtual_keyboard_manager_v1`.

And it is demonstrably *connected*, not merely installed — `hyprctl devices`
lists a virtual keyboard fcitx5 created on the seat:

```
hl-virtual-keyboard-fcitx5   |layout: de |active: German
```

So the brief's showstopper scenario is real, but the culprit is fcitx5 rather
than the shell. A daemon that tries to bind `get_input_method` will be told
`unavailable`.

### This is good news, not bad

Displacing fcitx5 would cost the user their `~/.XCompose` CapsLock compose
sequences — which for a Luxembourg user typing `é ë ä à` is likely the main way
they produce accented characters today. That is a regression we should not
ship.

The better path is to **integrate with fcitx5 instead of evicting it**. fcitx5
already is the input method, so it already knows exactly when a text field is
focused — which is the hard half of auto-show/hide. It exposes a virtual
keyboard interface on the session bus:

```
$ busctl --user introspect org.fcitx.Fcitx5 /virtualkeyboard
org.fcitx.Fcitx.VirtualKeyboard1   interface
.HideVirtualKeyboard               method  -  -
.ShowVirtualKeyboard               method  -  -
.ToggleVirtualKeyboard             method  -  -
```

plus `org.fcitx.Fcitx.Controller1` on `/controller` with
`AvailableKeyboardLayouts`, `CurrentInputMethod`, `CurrentInputMethodInfo`, and
`SetCurrentIM` — i.e. a ready-made layout-following surface that is more
authoritative than scraping Hyprland's socket2.

`/usr/lib/fcitx5/libkimpanel.so` is also present; kimpanel is fcitx5's
DBus protocol for driving an *external* panel UI, and is the standard way a
separate process is told about input-context focus and state.

### 3.1 RESOLVED — fcitx5's virtual-keyboard backend protocol

fcitx5 ships a `virtualkeyboard` addon, present and known to the running
instance:

```
/usr/share/fcitx5/addon/virtualkeyboard.conf
  Library=libvirtualkeyboard   Category=UI   UIType=OnScreenKeyboard
  OnDemand=True                Dependencies: dbus, core
```

It is `OnDemand`, so by default it is not the active UI (`CurrentUI` is
`classicui`) and its objects are not exported. **Two conditions together
activate it**, established experimentally:

1. a client owns the bus name `org.fcitx.Fcitx5.VirtualKeyboard`, and
2. something calls `ShowVirtualKeyboard`.

Holding the name alone does nothing; calling `ShowVirtualKeyboard` alone does
nothing. With both, `CurrentUI` flips to `virtualkeyboard`. Verified with a
small GDBus probe (`tools/vkprobe.c`) that owns the name, plus a `busctl` call:

```
=== calling ShowVirtualKeyboard while name is held ===
=== CurrentUI now ===
s "virtualkeyboard"
```

In that mode fcitx5 exports the full contract on `/virtualkeyboard`:

```
org.fcitx.Fcitx.VirtualKeyboard1
  .ShowVirtualKeyboard      ()
  .HideVirtualKeyboard      ()
  .ToggleVirtualKeyboard    ()

org.fcitx.Fcitx5.VirtualKeyboardBackend1
  .ProcessKeyEvent                  (uuubu)
  .ProcessVisibilityEvent           (b)
  .SelectCandidate                  (i)
  .NextPage                         ()
  .PrevPage                         ()
  .SetVirtualKeyboardFunctionMode   (u)
```

**`ProcessKeyEvent(uuubu)` is the injection path.** Our keyboard calls it and
fcitx5 does the rest — keymap, layout, AltGr, dead keys, `~/.XCompose`
sequences, candidates. The signature is (keysym, keycode, state, isRelease,
time).

The reciprocal half is that the client exports
`/org/fcitx/virtualkeyboard/impanel` under the name it owns; fcitx5 calls into
that to drive show/hide and candidate updates. That object is *not* on
`org.fcitx.Fcitx5` — confirmed by introspection failing there in every state —
because it belongs to the client, which is us.

### 3.2 What this means for the architecture

This replaces the brief's "fw12d becomes the seat's input-method client" design
wholesale, and it is a strictly better position:

| | brief's design | fcitx5-backend design |
|---|---|---|
| input-method-v2 slot | must seize it from fcitx5 | never touched |
| `~/.XCompose` compose keys | broken | preserved |
| auto-show/hide source | our own IM state machine | fcitx5 tells us |
| text injection | `commit_string` + virtual-keyboard-v1 | `ProcessKeyEvent` |
| layout following | scrape Hyprland socket2 | fcitx5 `CurrentInputMethod` |
| dead keys / AltGr | our problem | fcitx5's, already working |
| conflicts with Omarchy defaults | yes | no |

We stop being an input method and become a *rendering surface plus key source*
for the input method Omarchy already ships. Far less protocol state to own, and
nothing to fight over.

**Caveat to verify in Phase 1:** whether fcitx5 in `virtualkeyboard` UI mode
still delivers the auto-show trigger for apps whose text-input support is weak
(notably Ghostty). fcitx5 knows about focus only for clients that speak
text-input/IM protocols at all, so a client that never activates an input
context will not trigger the keyboard under *any* design. That limitation is
app-side and unavoidable, not something this architecture introduces.

**Housekeeping:** the probe left `CurrentUI` empty (`""`) after releasing the
name. `systemctl --user restart omarchy-fcitx5.service` restores
`classicui` + `keyboard-us` and the seat virtual keyboard. Any real
implementation must restore the UI on exit rather than leaving fcitx5 without
one.

### 3.3 Consequence for Phase 0.5 (squeekboard)

squeekboard is itself an input-method-v2 client and **will collide with
fcitx5**. Installing it would demonstrate the conflict rather than provide a
working baseline, and under the design above it is no longer on the
implementation path at all. Recommend **skipping the squeekboard interim step**
and reporting that, rather than installing a keyboard we already know cannot
coexist with the shipped input method.

---

## 4. Omarchy shell plugin contract

The brief points at `$OMARCHY_PATH/manual/32-shell-plugins.md`. **That file and
the entire `manual/` directory do not exist** in this install. The authoritative
docs are:

- `$OMARCHY_PATH/shell/README.md` (297 lines) — architecture, manifest schema,
  IPC contract, `shell.json` storage rules
- `$OMARCHY_PATH/shell/plugins/README.md` (116 lines) — first-party catalogue

### Manifest

```json
{
  "schemaVersion": 1,
  "id": "vendor.name",
  "name": "Display name",
  "version": "1.0.0",
  "author": "...",
  "description": "...",
  "kinds": ["service", "panel", "bar-widget"],
  "keepLoaded": true,
  "entryPoints": { "service": "Service.qml", "panel": "Panel.qml" }
}
```

Kinds: `bar-widget`, `panel`, `overlay`, `menu`, `service`, `bar`.
Services and `keepLoaded` panels mount at shell startup; other panels/overlays/
menus load on summon.

### Install path

A plugin is a **git repo with `manifest.json` at its root**, cloned to
`~/.config/omarchy/plugins/<manifest-id>/`:

```
omarchy plugin add <git-url> --enable --yes
omarchy plugin update <id>
omarchy plugin remove <id>
```

Plugins land **disabled** so the user can review code first. The installer
never runs plugin code, install hooks, or sudo. Saving any file under
`~/.config/omarchy/plugins/` hot-reloads plugin code.

Enabled state lives in `~/.config/omarchy/shell.json`; a third-party plugin is
enabled iff its id appears in that file. `version: 1` is required at top level.

### IPC

Single `shell` target plus per-plugin registered targets:

```
omarchy-shell shell ping | summon <id> <json> | hide <id> | toggle <id> <json>
              shell call <id> <method> <arg>
              shell rescanPlugins | reloadConfig | listPlugins
              shell setPluginEnabled <id> <"true"|other>
```

Note `setPluginEnabled` takes a **string**; only the literal `"true"` enables.

### Layer surfaces and focus

The OSD panel is the model for a bottom-anchored keyboard:

```qml
PanelWindow {
  anchors { top: true; bottom: true; left: true; right: true }
  WlrLayershell.namespace: "omarchy-osd"
  WlrLayershell.layer: WlrLayer.Overlay
  WlrLayershell.keyboardFocus: WlrKeyboardFocus.None   // <- no focus stealing
}
```

`WlrKeyboardFocus.None` is the documented, first-party-proven mechanism for the
"must not steal focus" requirement.

### Theming

Not a "24-color system". `qs.Commons`'s `Color.qml` singleton exposes a small
foundational palette — `foreground`, `background`, `accent`, `urgent`, `muted`
— plus per-surface roles loaded from the active theme's `shell.toml`, with
fallback to the foundational palette. `Style.space(n)` provides scaled spacing.
Reassigning `shellValues` wholesale is what triggers re-binding on theme swap.

---

## 5. Prior art on this machine: `fw12tab`

`~/fw12/Downloads/src/fw12tab` — MIT, Sven Mathieu, remote
`github.com/mechanicsunlocked/fw12tab`, 17 commits, June–July 2026. Written for
Omarchy 3 / hyprlang. It already implements a large part of this brief.

| Component | Language | What it does |
|---|---|---|
| `bin/fw12tab` | bash, 241 ln | orchestrator: daemons, toggles, setup, doctor |
| `lib/tabletmode.c` | C, 73 ln | finds the `SW_TABLET_MODE` evdev device, streams 1/0 on change |
| `lib/oskbd.c` | C/GTK4, 417 ln | layer-shell on-screen keyboard, virtual-keyboard-v1 |
| `lib/osk-button.c` | C/GTK4, 177 ln | floating draggable toggle button |
| `lib/touchlaunch.c` | C/GTK4, 149 ln | touch app launcher |
| `lib/edgeswipe.c` | C/GTK4, 91 ln | top-edge pull-down opener |
| `system/*` | bash + unit | `soc_button_array` bind at boot **and after resume** |

### What is worth keeping

**`oskbd.c`'s keymap strategy is better than the brief's Qt Virtual Keyboard
plan for this user's requirements.** It compiles the system xkb keymap with
`xkb_keymap_new_from_names()`, uploads it to the compositor over
`zwp_virtual_keyboard_v1.keymap`, and then sends **real evdev keycodes**. Key
legends are derived from the keymap at runtime via
`xkb_keymap_key_get_syms_by_level()`, including a dead-key glyph table
(`^ ´ \` ~ ¨ ° ˇ ¸ ...`), and are relabelled live as Shift/AltGr/Caps latch.

The consequence is that AltGr level-3, dead keys, and composition are handled
by **xkb**, not by the keyboard app. Any layout xkb can compile — including
`lb` — works with no per-locale layout file. Qt Virtual Keyboard would require
a hand-maintained QML layout per locale and has **no `lb` layout at all**,
which is precisely the gap the brief flags as an open question.

It also already solves focus: `gtk_layer_set_keyboard_mode(..., NONE)`.

Modifier latching is implemented as single-tap = one-shot, double-tap = lock,
with `on_cancel` wired alongside `on_released` so a key can never stick down.

### What is broken or missing relative to the new brief

1. **No auto-show/hide.** `oskbd` binds only `zwp_virtual_keyboard_v1`. It
   never binds `zwp_input_method_v2`, so it has no idea when a text field is
   focused. Showing it is manual (a floating button or `Super+Shift+K`). This
   is the single largest piece of new work.
2. **Layout detection reads `~/.config/hypr/input.conf`** — hyprlang. Omarchy 4
   is Lua (`input.lua`) and that file does not exist, so it silently falls
   through to `localectl` and then `"us"`. Broken on this install.
3. **Does not follow live layout changes.** Layout is read once at spawn; there
   is no `activelayout>>` subscription on Hyprland's socket2.
4. **Hardcoded German-ish legends** (`Strg`) on fixed-label keys, while derived
   keys come from the keymap — inconsistent under fr/lb.
5. **The orchestrator is bash** with `pgrep`/`pkill -x` process management,
   `sleep 0.3` restacking, and `sleep 2` reconnect loops. The new brief
   explicitly rules this out as a final mechanism.
6. **`tabletmode.c` exits when the device disappears** — the read loop just
   ends. Combined with the resume unbind (§7) this matters; the bash wrapper
   papers over it with a `sleep 2` retry loop.
7. Wires itself in via `source = ...` into `hyprland.conf`, which no longer
   matches Omarchy 4's Lua config.

### Contradiction in its own history, now resolved

Its README says the FW12 "exposes no tablet-mode switch or sensor interrupt"
and uses a 2 s hinge-angle poll. But commit `3d12479` is
*"Replace wvkbd with our own GTK4 keyboard; switch-based tablet detection"* and
`11a76c2` is *"Add tablet-switch bind service (boot + resume) for the
soc_button_array probe race"*.

Per the user: the project moved from hinge angle to the switch **because the
hinge sensor stopped working after resume from hibernation**. The README was
never updated. So the ordering is: hinge angle first → found to break across
hibernate → switched to `SW_TABLET_MODE` → which then needed the bind service
to exist at all, at boot and again after every resume.

**This is the most important open question in Phase 0** and is what §7 tests.

---

## 5.4 Omarchy's bar is hard to hit by touch — the real cause is target size

Observed live: tapping bar widgets on the touchscreen did nothing, and repeated
tapping produced a full-screen animation that looked like a wallpaper switch.

**Root cause: the taps were missing the bar entirely and landing on the
wallpaper behind it.** The background layer opens the wallpaper picker on
**double-click** — `$OMARCHY_PATH/shell/plugins/background/Background.qml:312`:

```qml
MouseArea {
  anchors.fill: parent
  acceptedButtons: Qt.LeftButton | Qt.RightButton
  onDoubleClicked: function(mouse) {
    if (mouse.button === Qt.RightButton) root.openThemeSwitcher()
    else root.openSelector()
  }
}
```

Two taps in quick succession near the top edge, both missing the 26 px bar,
register as a double-click on the background and open the image selector. That
is the animation that was reported.

> **Correction.** An earlier version of this section attributed the animation to
> `Bar.qml:1402`'s `pressAndHoldInterval: 200` bar-move drag gesture. That code
> is real, and 200 ms *is* short for touch, but it is **not** what was being
> hit. The observed symptom is a missed tap reaching the background layer. The
> size problem below is the actual defect; the hold interval is at most a
> secondary one.

**The actual defect — target size.** The bar is 26 logical px tall at
`scale: 2` on this panel:

```
eDP-1  1920x1200  scale: 2  transform: 0    -> 960x600 logical
reserved: 0 26 0 0
```

26 logical px = 52 device px. At 1200 px over 160 mm (7.5 px/mm) that is
**≈6.9 mm** — under the ~9 mm minimum usually recommended for touch targets.

**Confirmed not a regression from this session's work:** `omarchy-shell` (pid
1251) has been running untouched since boot at 14:34:54; every relevant
`hyprctl getoption` reports `set: false` (no runtime overrides were ever
issued); monitor is `transform: 0`, `scale: 2`, both defaults; and no stray
Quickshell, evtest, or layer surface remains from the probes.

**Why it appeared to be fixed by a reboot.** It was not. Measured before and
after, the geometry and source are byte-identical:

```
before:  xywh: 0 0 960 26   reserved: 0 26 0 0   scale: 2   transform: 0
after:   xywh: 0 0 960 26   reserved: 0 26 0 0   scale: 2   transform: 0
Bar.qml:1402  pressAndHoldInterval: 200   (unchanged)
```

The variable is aim, not system state. A 6.9 mm target near a screen edge is
hit-or-miss by finger, so the same bar genuinely works sometimes and not
others — which is exactly the intermittent behaviour observed across sessions.

**Implication for the plugin.** The fix is a bigger touch target in tablet
mode, which a plugin cannot impose on the first-party bar. Options:

1. File upstream: on a touchscreen, the bar needs a larger hit area (or an
   invisible extended input region) in tablet mode. Independent of this
   project. **Decided: do this.**
2. Ship a `kind: "bar"` replacement that is touch-tuned. The plugin system
   supports replacing the bar outright, but it is a large amount of work.
3. `omarchy plugin clone omarchy.bar` → `drotiesel.bar` and enlarge it. Quick,
   but forks the bar and drifts from upstream on every update.

**Decided (2026-08-15): option 1 only.** (2) and (3) are out of scope for the
first release.

A cheap mitigation worth noting in the README: the bar's hit area is only a
problem because a *miss* does something dramatic. Users bothered by accidental
wallpaper-picker launches can avoid it entirely, since the trigger is a
double-click on the background rather than anything the bar does.

### Note: `Ui/KeyboardPanel.qml` is **not** an on-screen keyboard

Despite the name, `$OMARCHY_PATH/shell/Ui/KeyboardPanel.qml` (418 lines) is a
layer-shell **popup container** for bar widget panels, "designed for
click-driven AND keyboard-driven panels (e.g. SUPER+CTRL+W summon)". It is used
by the clock, audio, network, bluetooth, power, weather, monitor, tailscale and
agents panels. "Keyboard" refers to keyboard *summoning*, not a virtual
keyboard. There is no existing OSK anywhere in Omarchy.

It is, however, the correct model for our own panel: it documents Omarchy's
working approach to layer-shell focus (`WlrKeyboardFocus.Exclusive` prime then
`OnDemand`), and why `PopupWindow`/xdg-popup was rejected.

## 5.5 Keyboard layout: there is no `lb` xkb layout, and the live layout is `de`

The brief treats Luxembourgish as an xkb layout that Qt Virtual Keyboard lacks.
In fact **xkb has no `lb` layout either**:

```
/usr/share/X11/xkb/symbols/lb                    does not exist
grep '^\s*lb\s' rules/base.lst, rules/evdev.lst  no match
grep -ri luxem /usr/share/X11/xkb/               no match
```

There is no Luxembourgish entry anywhere in `xkeyboard-config`. Luxembourg
users conventionally use `fr`, `de`, `ch(fr)`, or `ch(de)`.

The live configuration confirms this — every keyboard on the seat currently
reports:

```
at-translated-set-2-keyboard   |layout: de |active: German
```

So the machine is on plain `de` right now, and `~/.config/hypr/input.lua` has
its `kb_layout` block entirely commented out (Omarchy's default). fcitx5's own
profile says `Default Layout=us`, `DefaultIM=keyboard-us` — i.e. fcitx5 has
never been configured and is passing through.

**This dissolves the brief's `lb` question and replaces it with a better one.**
There is no `lb` layout to follow, in xkb or Qt VK. What a Luxembourg user
actually needs is either `fr`/`de`/`ch` layouts plus working compose sequences
for the accents — which is exactly what Omarchy's fcitx5 + `~/.XCompose`
already provides — or a genuinely new custom xkb symbols file.

### DECIDED (2026-08-15)

**Follow the active xkb layout; rely on dead keys and `~/.XCompose` for
accents. Do not author an `lb` layout.**

The user's stated plan makes this cleaner still: currently on `de`, **moving to
US International (`us(intl)`)**. That layout provides `´ \` ¨ ^ ~` as dead keys
natively, which composes é è ë ê ñ ä ö ü à — i.e. essentially the full
French/German/Luxembourgish accent set — without any custom layout, and AltGr
level-3 for `€ @ ~` etc.

Consequences for the design:

1. The OSK **must not** hardcode a layout. Legends have to be derived from the
   live xkb keymap, because the layout is changing from `de` to `us(intl)`
   under us. fw12tab's `oskbd.c` already does exactly this via
   `xkb_keymap_key_get_syms_by_level()`, and already ships the dead-key glyph
   table (`^ ´ \` ~ ¨ ° ˇ ¸ …`) that `us(intl)` depends on.
2. Dead keys are now load-bearing rather than a nice-to-have. Under the §3
   design fcitx5 composes them, which is the well-tested path.
3. **Open sub-question: physical key shape.** `oskbd.c` hardcodes an ISO body —
   `KEY_102ND` (the extra `< > |` key) and a tall ISO Enter. A US layout is
   ANSI: no `KEY_102ND`, wide flat Enter. If the user's FW12 is physically ISO
   but running `us(intl)`, the *keymap* is ANSI while the *hardware* is ISO.
   The OSK should follow the keymap, so it needs both an ISO and an ANSI body
   selected by whether the active keymap binds `KEY_102ND`. Small, but it must
   be handled or the OSK will show a key that types nothing.

---

## 5.6 Qt Virtual Keyboard inside Quickshell — loads, but is not usable as-is

`qt6-virtualkeyboard 6.11.1-1` installed. The QML module is present at
`/usr/lib/qt6/qml/QtQuick/VirtualKeyboard/` with `InputPanel.qml`.

A minimal Quickshell config (`tools/qsvktest/shell.qml`) importing
`QtQuick.VirtualKeyboard` and instantiating `InputPanel` inside a `PanelWindow`
was run against the live compositor:

```
DEBUG qml: QSVK: ShellRoot loaded
DEBUG qml: QSVK: available locales = []
DEBUG qml: QSVK: active locale =
DEBUG qml: QSVK: InputPanel INSTANTIATED ok
INFO : Configuration Loaded
WARN : input method is not set
WARN : input method is not set          (repeating)
```

**Result: it instantiates, but it is inert.**

1. `VirtualKeyboardSettings.availableLocales` is **empty**. Layouts are
   compiled into `libqtvkblayoutsplugin.so` as Qt resources
   (`prefer :/qt-project.org/imports/.../Layouts/`), and none are exposed at
   runtime here.
2. `input method is not set`, repeating indefinitely. Qt VK is designed to *be*
   the Qt platform input method — it expects `QT_IM_MODULE=qtvirtualkeyboard`.

Omarchy sets `QT_IM_MODULE=fcitx` globally
(`default/environment.d/10-omarchy-fcitx.conf`). Making Qt VK functional would
mean overriding that for the shell process, which would take Qt VK's input
method *instead of* fcitx5 inside `omarchy-shell` — reintroducing exactly the
conflict §3 avoids.

Combined with §5.5 (Qt VK has no `lb` layout, and neither does xkb), the Qt
Virtual Keyboard path is **not recommended**. The brief's stated goal — "the
Plasma 6 keyboard experience" — is better served by fcitx5's own virtual
keyboard protocol, which already provides the layouts, dead keys, compose
sequences and candidate handling that Qt VK would have to reimplement.

### Upstream state (checked 2026-08-15)

| project | state |
|---|---|
| `KDE/plasma-keyboard` | active (pushed 2026-08-13) but README still states it "uses the **input-method-v1** Wayland protocol"; KWin-configured via `kwriteconfig6 ... InputMethod`. Hyprland advertises **no** `zwp_input_method_manager_v1`, only v2 — so plasma-keyboard cannot run here. Brief's assumption holds. |
| `JeanSchoeller/hyprkbd` | **abandoned** — last commit 2024-07-23, 4 stars, C. No input-method-v2 auto-show landed. Not viable. |
| Omarchy marketplace | registry live, updated 2026-08-15, **164 plugin sources**. Searching every `id`/`name`/`description`/`tags` for tablet, keyboard, osk, rotate, touch, stylus, pen returns **zero matches**. This would indeed be the first. |

---

## 5.7 Hyprland's Lua config can do rotation itself — possibly no daemon needed

Discovered late (2026-08-15), while implementing the daemon. **This may remove
most of Component A and needs resolving before more code is written.**

### `keyword` does not work on Omarchy 4

The first implementation used `hyprctl keyword monitor ...` over the IPC socket,
as fw12tab did. Hyprland refuses it:

```
$ hyprctl keyword monitor "eDP-1,preferred,auto,2,transform,1"
keyword can't work with non-legacy parsers. Use eval.
```

Omarchy 4 configures Hyprland in **Lua**, and the Lua config manager rejects
`keyword` outright. The two are mutually exclusive — the binary also carries
`eval is only supported with the lua config manager`. Anything ported from an
Omarchy 3 / hyprlang setup will silently do nothing here: note that `hyprctl`
still **exits 0** while refusing the command.

The working form is `eval` with a Lua statement, verified applying and
reverting a real rotation:

```lua
local ms = hl.get_monitors()
local t = nil
for _, m in ipairs(ms) do if m.name:sub(1,3) == "eDP" then t = m break end end
if not t then t = ms[1] end
if t then hl.monitor({output=t.name, mode="preferred", position="auto",
                      scale=t.scale, transform=N}) end
hl.config({input={touchdevice={transform=N}, tablet={transform=N}}})
```

Result: `transform: 1`, `input:touchdevice:transform 1`,
`input:tablet:transform 1`, scale preserved at 2. Reverting to 0 restores it.

Note `eval` returns `"ok"` on the socket regardless of what the Lua returns —
return values are **not** surfaced. To get data out of Lua, write a file.

### The Lua environment is far more capable than expected

Verified by having Lua write its results to a file:

```
accel-display: iio:device0          <- found by label, probing iio:device0..9
raw: x=224 y=11472 z=11760          <- io.open on sysfs works
hl.timer exists: function           <- {timeout=ms, type="repeat"|"oneshot"}
hl.bind exists: function
hl.bind("switch:on:gpio-keys", ...) <- registers without error
```

So Hyprland's Lua has an unrestricted `io` library, can read the accelerometer
directly, has a repeating timer, and accepts `switch:on:` / `switch:off:` bind
keys. Lua has no directory listing, but probing `iio:device0..9` and matching
`label` sidesteps that and is boot-stable (§2.1).

**If the switch bind actually fires** — registered cleanly but *not yet observed
firing*, which needs a physical fold — then tablet detection and auto-rotation
can both live in a Lua config file with **no daemon at all**: no evdev watcher,
no inotify hotplug logic, no poll loop, no Hyprland IPC client, no socket
protocol, no systemd user unit.

### What Lua still cannot do

- **No UI.** The entire 1777-line `hl.meta.lua` API is configuration — binds,
  monitors, rules, dispatchers, notifications. There is no drawing, surface, or
  widget call. **An on-screen keyboard cannot be written in Hyprland Lua.**
- **No DBus.** So the fcitx5 virtual-keyboard bridge (§3.1) cannot be Lua
  either.

### The trade-off to weigh before deciding

A Lua timer polling the accelerometer runs **inside the compositor process**. A
callback that blocks or throws degrades the whole desktop, where a separate
process cannot. Three small sysfs reads at 4 Hz is cheap, and the reads are
from a kernel-backed virtual filesystem rather than disk — but it is still
compositor time, and it is the honest argument for keeping a daemon.

Against that: the Lua version deletes roughly 400 lines of C and every moving
part between the daemon and Hyprland, which is exactly the "as simple as
possible, bulletproof across versions" goal.

### RESOLVED — the Lua path works end to end

Switch binds fire in both directions:

```
16:00:32 TABLET ON  fired
16:00:39 TABLET OFF fired
```

And the full cycle was captured live, sampling switch state and all three
transforms at 1.5 s:

```
42  switch=1  mon=0  touch=0  pen=0     <- fold detected
43  switch=1  mon=3  touch=3  pen=3     <- rotation applied, all three together
    ...
    switch=0  mon=0  touch=0  pen=0     <- unfold reset everything
```

Monitor, touch and stylus move in lockstep, which is the property that has to
hold or the pen stops landing where the user points. Hyprland sits at **0.2%
CPU** with the 4 Hz poll running inside it.

### `hyprctl reload` rebuilds the entire Lua state

Worth recording because it removed code rather than adding it. The first
implementation carried reload-safety machinery — state kept on a global,
unbind-before-bind, timer teardown — on the assumption that Omarchy's
`bootstrap.lua` clearing `package.loaded` could leave stale binds and timers
behind.

It cannot. Setting a marker global and reloading three times:

```
marker before reload:  "set-before-reload"
marker after 3 reloads: nil
switch binds:          exactly 1 of each
SUPER+R binds:         1
Hyprland CPU:          0.2%
```

Hyprland destroys and rebuilds the Lua state on every reload, so nothing
survives to duplicate. The teardown code was guarding an impossible condition
and has been deleted.

**Install must go through `require()`, not `dofile`.** A module loaded with
`dofile` via `hyprctl eval` works until the first `hyprctl reload`, which
rebuilds the config from `hyprland.lua` and silently drops every bind the
module registered. The line
`require("hypr.fw12-tablet")` in `~/.config/hypr/hyprland.lua` is what makes it
survive.

### Still unverified

- More than one orientation change within a single tablet session.
- `SUPER + R` rotation lock behaviour.
- Suspend/resume while folded.

---

## 6. Language choice: C vs Rust

The brief mandates Rust or C for the daemon and prefers Rust.

Evidence pulling toward **C** on this specific machine:

- No Rust toolchain installed; `gcc`, `pkg-config` and `wayland-scanner` are.
- ~750 lines of working, readable, MIT-licensed C already exist here solving
  the same problems, by the same author, with correct protocol handling.
- The keyboard must link `libxkbcommon` and drive raw Wayland protocol; the C
  bindings are the reference implementation, and `wayland-scanner` generates
  C directly. The Rust path (`wayland-client`, `smithay-client-toolkit`) is
  good but would be a rewrite of code that already works.
- Every runtime dep is already a C library (gtk4-layer-shell, xkbcommon).

Evidence pulling toward **Rust**: the new daemon is materially more complex
than the old one (input-method-v2 state machine + Hyprland socket2 event
subscription + IIO/DBus + a Unix socket protocol), and that is where Rust's
error handling and lifetime discipline pay off most.

**Recommendation deferred to the architecture proposal**, but the honest read
is that C is the lower-risk choice here purely because so much of the hard part
is already written and proven on this exact hardware.

---

## 7. Open questions — blocked, and the hibernate test

### 7.1 Still to establish about the hinge/resume failure

The first hibernate cycle did not reproduce it. Before designing around it:

- Repeat hibernate at least once more, and test **s2idle/suspend** separately —
  the failure may be specific to one sleep path.
- Retest **after the switch is bound**, since binding `soc_button_array` changes
  what touches these ACPI/GPIO paths across sleep.
- Retest with a **poller actively reading** `in_angl_raw` across the transition;
  fw12tab polled every 2 s, and a read racing the EC's resume may be what broke
  it rather than sleep alone.
- Confirm touchscreen and stylus still work after resume, given the i2c
  `ENXIO` above.

### Blocked on root (user is enabling passwordless sudo)

- Bind `INT33D3:00` and confirm a `SW_TABLET_MODE` device appears.
- Install `iio-sensor-proxy`; confirm `monitor-sensor --accel` fires in all
  four orientations and that it follows `accel-display`, not `accel-base`.
- Install `wayland-utils`, `evtest`, `squeekboard`.
- Confirm whether `pinctrl_tigerlake` in `MODULES=` in mkinitcpio actually
  fixes the race at boot on this install, or whether the systemd bind unit is
  still required.

### fcitx5 integration (highest priority — decides the architecture)

- Which fcitx5 mechanism emits focus-in/focus-out to an external process:
  kimpanel DBus signals, or the `virtualkeyboard` addon?
- Does enabling fcitx5's `virtualkeyboard` addon give us show/hide events
  without also spawning fcitx5's own keyboard UI?
- Can our keyboard inject via `zwp_virtual_keyboard_v1` while fcitx5 also holds
  a virtual keyboard on the same seat? (Multiple virtual keyboards are allowed
  by the protocol — unlike input-method — but this needs confirming in
  Hyprland.)
- Confirm empirically that a second `get_input_method` really is refused, by
  installing squeekboard and watching it fail.

### Blocked on network

- `JeanSchoeller/hyprkbd` — has input-method-v2 auto-show landed?
- `KDE/plasma-keyboard` — still input-method-v1/KWin-only?
- omarchyplugins.com / `HANCORE-linux/omarchy-plugin-marketplace` registry —
  any existing OSK/tablet plugin, and the exact publish requirements.
- Qt VK inside Quickshell — needs `qt6-virtualkeyboard` installed to test.

### The hibernate test (planned)

The user will hibernate and resume. Pre-hibernate baseline is captured. On
resume, re-check, in order:

1. `/sys/bus/platform/devices/INT33D3:00` still present?
2. `soc_button_array` still bound to it?
3. `SW_TABLET_MODE` device still present and readable?
4. `/sys/bus/iio/devices/` — do all three cros-ec IIO devices survive?
5. `in_angl_raw` — **does the hinge angle still update?** This is the reported
   failure.
6. `/dev/cros_ec` still present?
7. Does `cros_ec` need a re-probe the way `soc_button_array` does?

### RESULT: hibernate test run 2026-08-15, S4 entry 14:28:21 → exit 14:29:42

**The reported failure did not reproduce.** A real hibernation cycle completed
(`PM: hibernation: hibernation entry` … `hibernation exit`, state S4, 19.5 GB
image) and afterwards:

| Check | Result |
|---|---|
| `INT33D3:00` present | yes (unchanged) |
| `soc_button_array` bound | still UNBOUND — same as before, so unrelated to sleep |
| all three cros-ec IIO devices | **all survived** |
| `/dev/cros_ec` | present, unchanged |
| cros_ec modules loaded | 12, unchanged |
| **hinge angle still tracks** | **yes** |

A 30-second sample at 1 Hz with the lid deliberately moved:

```
01 angle=124   07 angle=120   13 angle=123   19 angle=500   25 angle=74
02 angle=116   08 angle=127   14 angle=123   20 angle=122   26 angle=78
03 angle=116   09 angle=500   15 angle=123   21 angle=74    27 angle=81
04 angle=116   10 angle=119   16 angle=123   22 angle=85    28 angle=74
05 angle=115   11 angle=122   17 angle=123   23 angle=103   29 angle=75
06 angle=116   12 angle=123   18 angle=500   24 angle=76    30 angle=67
```

The angle follows the lid faithfully across 124° → 116° → 127° → 123° → 74° →
67°, and `accel-display` tracks alongside it. Post-hibernate the sensor is
fully alive.

**So the hinge angle is not inherently broken by hibernation on this kernel
(7.1.8-arch1-3).** Either it was fixed upstream since the June/July fw12tab
work, or the failure is intermittent, or it is specific to a different sleep
path (s2idle vs S4) or to a re-probe that fw12tab's own polling was provoking.
This needs at least one more cycle before we design around it — see §7.1.

### The `500` sentinel is real and must be handled

Samples 09, 18 and 19 read **`angle=500`** — during rapid lid movement, and
correlated with out-of-range accelerometer magnitudes (sample 09 reads
`z=-21200`, about −1.3 g). 500 is the EC's "angle indeterminate" sentinel, not
a 500° hinge.

fw12tab hit this too — commit `1a28707` *"watcher ignores >360 deg sensor
sentinel"* and `1f12e3c` *"angle state machine; 500 holds mode, not flips it"*.

Any angle-based state machine **must treat `>360` as "no reading, hold current
state"**, never as "past 360° therefore tablet". A naive threshold comparison
would flip into tablet mode every time the user moves the screen quickly. This
is a strong argument for preferring the `SW_TABLET_MODE` switch as the primary
signal once it is bound, with the angle as corroboration only.

### Unrelated but noted: i2c does not restore cleanly

```
spd5118 17-0050: PM: dpm_run_callback(): spd5118_resume [spd5118] returns -6
spd5118 17-0050: PM: failed to restore async: error -6
```

`-6` is `ENXIO`. This is the memory SPD sensor, not ours, but the touchscreen
(`ILIT2901:00`, i2c-0), stylus, and touchpad (`PIXA3854:00`, i2c-2) sit on the
same bus family. Touch and pen must be re-tested explicitly after resume.

### Reboot caveat

This install has not been rebooted since the initial install + update. Running
kernel `7.1.8-arch1-3` matches the installed `linux 7.1.8.arch1-3` package and
its module tree is intact, so there is no stale-module hazard. But the
`pinctrl_tigerlake` / `soc_button_array` probe order is decided at boot, so the
unbound switch must be re-confirmed on a fresh boot before we conclude the race
is deterministic here.

**Pre-hibernate baseline, 2026-08-15T14:20:09+02:00, uptime 38 min:**

```
INT33D3:00                   present
soc_button_array bound       UNBOUND
pinctrl_tigerlake            loaded (3 users)
SW_TABLET_MODE device        NONE
iio:device0  cros-ec-lid-angle
iio:device1  cros-ec-accel  label=accel-display
iio:device2  cros-ec-accel  label=accel-base
in_angl_raw                  99
accel-display raw            x=3440  y=15376  z=-1232
/dev/cros_ec                 present (crw------- root root 10,262)
```

Note the switch is already unbound *before* any suspend, so the boot race and
any resume race are separate failures that must be distinguished.
