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

It is a git repo with a `manifest.json` at its root, which is what Omarchy's
plugin installer expects, so:

```bash
omarchy plugin add https://github.com/mechanicsunlocked/fw12-tablet.git --enable --yes
~/.config/omarchy/plugins/drotiesel.fw12-tablet/install.sh
```

The first command clones and enables the button. The second builds the
keyboard, installs the rotation module, adds one `require` line to your
Hyprland config, and restarts the shell. It needs no root, touches nothing
outside `$HOME`, and is also how you upgrade — run it again after
`omarchy plugin update drotiesel.fw12-tablet`.

Two commands rather than one because Omarchy's installer deliberately never
runs code from a plugin it has just cloned, which is the right call. So the
second one is yours to read first:

```bash
less ~/.config/omarchy/plugins/drotiesel.fw12-tablet/install.sh
```

Or from an ordinary clone, if you would rather not install it as a plugin at
all until you have looked at it:

```bash
git clone https://github.com/mechanicsunlocked/fw12-tablet.git
./fw12-tablet/install.sh
```

`install.sh` figures out which of the two it is and does not copy the plugin
over itself.

### The one part that needs root

```bash
sudo ~/.config/omarchy/plugins/drotiesel.fw12-tablet/system/install.sh
```

Closes a firmware probe race that costs the tablet switch on roughly one boot
in three. Nothing breaks without it — an affected boot simply comes up in
laptop mode — so it is worth doing and safe to skip. `install.sh` prints this
line at the end rather than running it for you.

### Removing it

```bash
~/.config/omarchy/plugins/drotiesel.fw12-tablet/uninstall.sh
omarchy plugin remove drotiesel.fw12-tablet
```

The boot fix is left in place; `uninstall.sh` prints the commands to take that
out too, rather than doing it, because it is a generic module-ordering fix that
is harmless on its own.

### Requirements

`gtk4`, `gtk4-layer-shell`, `libxkbcommon`, `wayland`, `pkgconf`, `gcc` — all
in the official repos, nothing from the AUR. `install.sh` checks for them
before it builds anything and prints the one `pacman` line that fixes it.

---

## Using it

The button appears only while the machine is folded into a tablet.

* **Tap** it to show or hide the keyboard.
* **Drag** it anywhere; the position is remembered, and it is stored as a
  fraction of the screen so it survives rotation.

Unfolding puts the keyboard away and takes the button with it.

### Keybinds

| | |
|---|---|
| `SUPER + B` | show/hide the keyboard, in either mode |
| `SUPER + R` | lock/unlock auto-rotation; bound only while folded, and the lock clears when you unfold |

`SUPER + B` is bound in laptop mode too, because its most useful job is the
reverse of what it sounds like: dismissing the on-screen keyboard *from* the
on-screen keyboard, whose Framework key is a real Super. Two taps, and no
aiming. `SUPER + K` would have been the obvious letter but it is Omarchy's own
keybindings menu; on a stock install the free `SUPER` letters are A B D E H I M
N Q U Y Z, and `SUPER + CTRL + <anything>` is completely unused.

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

That combination is checked: `us`/`intl` comes up as a US board with `alt gr`
and an acute dead key on the apostrophe, which is the whole point of the
variant. Nothing in the keyboard needed changing to support it — it reads the
layout, it does not carry a copy of one.

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
