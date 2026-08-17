# fw12-tablet

Tablet mode for the Framework Laptop 12 on Omarchy 4 / Hyprland.

Two halves, both small:

* **`lua/fw12-tablet.lua`** — tablet detection and auto-rotation, loaded
  straight into Hyprland's Lua config. Screen, touch and stylus rotate
  together. No daemon.
* **`osk/`** — `fw12-oskbd`, a GTK4 layer-shell keyboard laid out like the
  Framework Laptop 12's own: function row under Fn, real Ctrl / Alt / AltGr,
  the Framework key as Super, and a proper arrow cluster. It uploads the
  system's xkb keymap, so it types and triggers keybinds exactly as the
  built-in keyboard does.
* **`plugin/`** — an Omarchy shell plugin: one round, draggable button that
  shows and hides the keyboard.

`fcitx5` is not touched, stopped, or reconfigured by any of this.

See `ARCHITECTURE.md` for how it works and `FINDINGS.md` for the measurements
behind each decision.

---

## Install

### 1. Rotation

```bash
cp lua/fw12-tablet.lua ~/.config/hypr/
```

Then add one line to `~/.config/hypr/hyprland.lua`:

```lua
require("hypr.fw12-tablet")
```

`SUPER + R` locks and unlocks auto-rotation while folded. The lock clears when
you unfold.

### 2. Keyboard

Needs `gtk4`, `gtk4-layer-shell`, `libxkbcommon` and `wayland` — all in
`extra`. Installs into `~/.local`, no root:

```bash
make -C osk
make -C osk install
```

`make -C osk install PREFIX=/usr` as root puts it in the usual place instead.

### 3. Keyboard button

```bash
mkdir -p ~/.config/omarchy/plugins/drotiesel.fw12-tablet
cp plugin/manifest.json plugin/Panel.qml ~/.config/omarchy/plugins/drotiesel.fw12-tablet/

omarchy-shell shell rescanPlugins
omarchy-shell shell setPluginEnabled drotiesel.fw12-tablet true
omarchy-restart-shell
```

The restart is not optional the first time. Enabling a third-party panel
plugin hot does mount it, but a later `rescanPlugins` leaves it unmounted
until the shell is restarted.

### 4. Boot fix (optional, root)

```bash
sudo system/install.sh
```

Closes a firmware probe race that costs the tablet switch on some boots. Not
needed at runtime — without it the rotation half simply stays in laptop mode
on an affected boot.

---

## Using it

The button appears only while the machine is folded into a tablet.

* **Tap** it to show or hide the keyboard.
* **Drag** it anywhere; the position is remembered, and it is stored as a
  fraction of the screen so it survives rotation.

Unfolding puts the keyboard away and takes the button with it.

### Swipes

Three edge strips, active only while folded. The bottom one carries both
vertical gestures:

| Swipe | Default |
|---|---|
| up, from the bottom edge | show/hide the keyboard |
| down, on the bottom strip | `omarchy-menu` |
| right, from the left edge | previous workspace |
| left, from the right edge | next workspace |

There is deliberately no top-edge strip: the bar owns the real top edge, so a
swipe that starts there starts on the bar and the bar gets it.

The strips are 16 px and sit in whatever space the bar and keyboard are not
using, so one is never on top of a key or a bar widget — when the keyboard is
out, the bottom strip moves up to sit just above it.

Change them in your plugin entry in `~/.config/omarchy/shell.json` — the same
file and the same place Omarchy keeps every other plugin's settings:

```json
{
  "id": "drotiesel.fw12-tablet",
  "swipeUp": "@keyboard",
  "swipeDown": "omarchy-menu",
  "swipeRight": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"e-1\" })'",
  "swipeLeft": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"e+1\" })'",
  "swipeEdge": 16,
  "swipeThreshold": 30
}
```

`@keyboard` is the one built-in action; anything else is run as a command. Set
a value to `""` to disable that swipe. The file is watched, so changes take
effect without restarting anything.

Note the shape of those workspace commands. **`hyprctl dispatch` takes a Lua
expression on Omarchy 4**, not the words you would use on a hyprlang config —
`hyprctl dispatch workspace e+1` fails with a Lua syntax error and silently
does nothing. `hyprctl dispatch 'hl.dsp.focus({ workspace = "e+1" })'` is the
form Omarchy's own workspace widget uses.

### Making it always visible

Set `tabletOnly` to `false` at the top of `Panel.qml`.

### Focus while folded

Folding sets `input:follow_mouse = 2` and unfolding puts it back to `1`.
Without it, keyboard focus detaches from the window you are typing into for as
long as a finger rests on the keyboard — because what is under your finger is a
layer surface, not a window — and everything typed in that time goes nowhere.
See `FINDINGS.md` §11. If your laptop-mode setting is not Hyprland's default of
`1`, change `LAPTOP_FOLLOW_MOUSE` at the top of the Lua.

### Keyboard layout

There is nothing to set. The keyboard reads `input:kb_layout` and
`input:kb_variant` from Hyprland and takes its keymap and its key legends from
there, so it is always the same layout as the physical keyboard. Change
Hyprland and it follows. For US International:

```
input {
    kb_layout = us
    kb_variant = intl
}
```

Because it uploads the real keymap rather than inventing one, AltGr and dead
keys work on it exactly as they do on the built-in keyboard — `AltGr` then `'`
then `e` gives `é`. Hold `Fn` for F1–F12 on the number row. Tap a modifier once
for one-shot, twice to lock it.

---

## Checking on it

```bash
cat "$XDG_RUNTIME_DIR/fw12-tablet-mode"      # tablet | laptop
pgrep -x fw12-oskbd                          # is the keyboard up
hyprctl layers | grep -E 'osk|fw12'          # what is on screen
hyprctl eval 'require("hypr.fw12-tablet").status()'
```

To move the button without touching it, delete
`~/.local/state/omarchy/fw12-osk-button.json` and restart the shell; it goes
back to the right edge.

---

## Notes

The button carries the Framework logo, which is a Framework Computer Inc.
trademark. It is used here to mark hardware-specific controls on a Framework
machine and implies no affiliation or endorsement.
