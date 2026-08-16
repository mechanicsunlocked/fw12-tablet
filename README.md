# fw12-tablet

Tablet mode for the Framework Laptop 12 on Omarchy 4 / Hyprland.

Two halves, both small:

* **`lua/fw12-tablet.lua`** — tablet detection and auto-rotation, loaded
  straight into Hyprland's Lua config. Screen, touch and stylus rotate
  together. No daemon.
* **`plugin/`** — an Omarchy shell plugin: one round, draggable button that
  shows and hides `squeekboard`. No keyboard code of its own.

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

### 2. Keyboard button

```bash
sudo pacman -S squeekboard

mkdir -p ~/.config/omarchy/plugins/drotiesel.fw12-tablet
cp plugin/manifest.json plugin/Panel.qml ~/.config/omarchy/plugins/drotiesel.fw12-tablet/

omarchy-shell shell rescanPlugins
omarchy-shell shell setPluginEnabled drotiesel.fw12-tablet true
omarchy-restart-shell
```

The restart is not optional the first time. Enabling a third-party panel
plugin hot does mount it, but a later `rescanPlugins` leaves it unmounted
until the shell is restarted.

### 3. Boot fix (optional, root)

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

### Making it always visible

Set `tabletOnly` to `false` at the top of `Panel.qml`.

### Choosing a keyboard layout

`squeekboard` follows `org.gnome.desktop.input-sources`, which is unset on a
fresh Omarchy install and gives you a US layout. For German:

```bash
gsettings set org.gnome.desktop.input-sources sources "[('xkb','de')]"
```

It also ships `terminal/us` and `terminal/de` layouts, with Ctrl, Esc, Tab and
arrows.

---

## Checking on it

```bash
cat "$XDG_RUNTIME_DIR/fw12-tablet-mode"      # tablet | laptop
systemctl --user status mobi.phosh.OSK       # the keyboard
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
