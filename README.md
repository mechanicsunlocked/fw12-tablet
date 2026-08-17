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

`SUPER + R` locks and unlocks auto-rotation while folded; the lock clears when
you unfold. `SUPER + B` toggles the on-screen keyboard.

`SUPER + B` is bound always, not only while folded, because its most useful job
is the reverse of what it sounds like: dismissing the on-screen keyboard *from*
the on-screen keyboard, whose Framework key is a real Super. Two taps, and no
aiming. `SUPER + K` would have been the obvious letter but it is Omarchy's own
keybindings menu; on a stock install the free `SUPER` letters are A B D E H I M
N Q U Y Z, and `SUPER + CTRL + <anything>` is completely unused.

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

Three edge strips, active only while folded:

| Swipe | Default |
|---|---|
| up, from the bottom edge | show/hide the keyboard |
| down, on either side edge | `omarchy-menu` |
| right, from the left edge | previous workspace |
| left, from the right edge | next workspace |

Two placements are deliberate and worth knowing:

**There is no top-edge strip.** The bar owns the real top edge, so a swipe that
starts at the top of the screen starts on the bar and the bar gets it.

**The menu is on the side edges, not the bottom.** A downward swipe starting on
the bottom strip has only the height of the strip before the finger runs off
the display — less than the threshold, so it could never complete. The side
strips are full height.

The side strips are 16 px; the bottom one is 32, because it is the one you have
to find by feel — when the keyboard is out it is the band between the keys and
the window above them. Strips sit in whatever space the bar and keyboard are
not using, so one is never on top of a key or a bar widget.

Change any of it in your plugin entry in `~/.config/omarchy/shell.json` — the
same file and the same place Omarchy keeps every other plugin's settings:

```json
{
  "id": "drotiesel.fw12-tablet",
  "swipeUp": "@keyboard",
  "swipeDown": "omarchy-menu",
  "swipeRight": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"-1\" })'",
  "swipeLeft": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"+1\" })'",
  "swipeEdge": 16,
  "swipeEdgeBottom": 32,
  "swipeThreshold": 30
}
```

`@keyboard` is the one built-in action; anything else is run as a command. Set
a value to `""` to disable that swipe. The file is watched, so changes take
effect without restarting anything.

Two things about those workspace commands. **`hyprctl dispatch` takes a Lua
expression on Omarchy 4**, not the words you would use on a hyprlang config —
`hyprctl dispatch workspace +1` fails with a Lua syntax error and silently does
nothing. And **`+1`/`-1` rather than `e+1`/`e-1`**: the `e` forms only visit
workspaces that already have something on them, so the gesture skips past empty
ones instead of walking the whole set.

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
