# Gimbal

**Tablet mode for the Framework Laptop 12, on Omarchy 4 / Hyprland.**

Fold the screen back and the machine becomes a tablet: the display, the
touchscreen and the stylus rotate together, two thumb knobs appear for
gestures, and an on-screen keyboard is one tap away — laid out like the
Laptop 12's own keyboard, because it is the one your hands already know.

Unfold it and everything goes away again.

`fcitx5` is not touched, stopped, or reconfigured by any of this.

---

## Install

```bash
omarchy plugin add https://github.com/mechanicsunlocked/gimbal.git --enable --yes
~/.config/omarchy/plugins/io.github.mechanicsunlocked.gimbal/install.sh
```

That is the whole of it. No root, nothing outside `$HOME`, and running the same
two lines again is also how you upgrade.

### The one part that needs root

```bash
sudo ~/.config/omarchy/plugins/io.github.mechanicsunlocked.gimbal/system/install.sh
```

Closes a firmware probe race that costs the tablet switch on roughly one boot
in three. Nothing breaks without it — an affected boot simply comes up in
laptop mode — so it is worth doing and safe to skip. `install.sh` prints this
line at the end rather than running it for you.

### Removing it

```bash
~/.config/omarchy/plugins/io.github.mechanicsunlocked.gimbal/uninstall.sh
omarchy plugin remove io.github.mechanicsunlocked.gimbal
```

The boot fix is left in place; `uninstall.sh` prints the commands to take that
out too, rather than doing it, because it is a generic module-ordering fix that
is harmless on its own.

---

## How to use it

### The two knobs

Fold the screen back and two round knobs appear at the lower corners, marked
with four arrowheads. They are the whole control surface.

| Do this | Get this |
|---|---|
| **one tap** | show / hide the keyboard |
| **press and drag up** | show / hide the keyboard |
| **press and drag down** | the Omarchy menu |
| **press and drag left** | next workspace |
| **press and drag right** | previous workspace |
| **three quick taps** | unlock the knob — then drag it anywhere |
| **three quick taps again** | stick it back down |

Both knobs do all four gestures, so there is nothing to remember about which is
which. Each is its own switch in the settings, so you can run one thumb or two.

They start at the lower corners because that is where your thumbs already are
when you hold the machine. Positions are kept as a fraction of the screen, so
they survive rotation, and they are remembered across reboots.

A knob fills with your accent colour while the keyboard is out, so it says
which way it is set, and swells while it is unlocked for moving.

### Showing the keyboard

Four ways, and all four toggle, so the same action puts it away again:

| | |
|---|---|
| **Tap a knob** | while folded |
| **Swipe up on a knob** | while folded |
| **The bar icon** | the keyboard glyph in the top bar; it lights up while the keyboard is out |
| **`SUPER + B`** | works in laptop mode too |

Each covers where the others are awkward. A knob is already under your thumb.
The bar icon is the one you can always see, and it is instant — a knob tap has
to wait out the triple-tap window first, about a third of a second. And
`SUPER + B` is the only one that works *from the on-screen keyboard itself* —
its Framework key is a real Super — which is how you put the keyboard away
without hunting for a control the keyboard may be sitting on.

### The keyboard

It is the Laptop 12's own layout: function row under `Fn`, real Ctrl / Alt /
AltGr, the Framework key as Super, and a proper arrow cluster.

Hold `Fn` for F1–F12 on the number row. Tap a modifier once for one-shot, twice
to lock it. AltGr and dead keys work exactly as they do on the built-in
keyboard — `AltGr` then `'` then `e` gives `é`.

---

## What it does

### Rotates everything together

Folding past 200° switches to tablet mode: the display transform, the
touchscreen and the stylus all rotate as one, so a tap lands where you touched
and the pen draws under its own tip. It runs inside Hyprland's Lua config —
there is no daemon.

### Types like the real keyboard

The on-screen keyboard uploads the system's own xkb keymap, so its keys arrive
with the same keycodes as the built-in keyboard's. Keybinds match. Dead keys
compose. Nothing needs special-casing for it.

It also reads `input:kb_layout` and `input:kb_variant` from Hyprland, so it is
always the same layout as the physical keyboard and there is nothing to set.
Change Hyprland and the on-screen keyboard follows.

<details>
<summary>Picking a US variant</summary>

**US International is not a different keyboard from US.** The physical board is
the same ANSI board with the same keys in the same places; `intl` is purely the
software variant. It turns `'` `"` `` ` `` `~` `^` into dead keys and hangs
more characters off AltGr:

| `kb_variant` | `'` then `e` | good for |
|---|---|---|
| *(empty)* | `'e` | typing English and nothing else |
| `intl` | `é` | typing accents constantly; the price is that `don't` needs a space after the apostrophe |
| `altgr-intl` | `'e`, and `AltGr+'` then `e` gives `é` | mostly English, accents when you need them — the apostrophe stays an apostrophe |

`altgr-intl` is the one to reach for if `intl` starts fighting you over
apostrophes.

```
input {
    kb_layout = us
    kb_variant = intl
}
```
</details>

### Stays out of a game

Streaming a game to the tablet, every gesture is aimed at the remote machine,
and a keyboard sliding up over the picture is never what you meant. So while
you are **on the workspace a Moonlight window is on**, nothing summons the
keyboard — not a tap, not a swipe, not the bar icon, not `SUPER + B` — and one
already up is dismissed when you switch to it.

The Omarchy menu is held back the same way, for a reason worth knowing: it is a
full-screen layer surface that takes keyboard focus, and a client capturing
input for a stream does not reliably take that capture back afterwards. One
swipe for the menu was enough to leave a game that no longer answered the
touchscreen at all. Ruled out first, by measurement: the menu unmaps cleanly
and focus does return to the window, so it is the client's capture and not a
surface left behind — which also means it is not ours to fix, only to avoid.

**Only that workspace.** A stream on workspace 2 is no reason to lose the
keyboard on workspace 1. And workspace swipes keep working from inside the
game, because leaving is exactly what you still want.

### Keeps typing working while folded

Folding sets `input:follow_mouse = 2` and unfolding puts it back to `1`.
Without it, keyboard focus detaches from the window you are typing into for as
long as a finger rests on the keyboard — what is under your finger is a layer
surface, not a window — and everything typed in that time goes nowhere. See
`FINDINGS.md` §11. If your laptop-mode setting is not Hyprland's default of
`1`, change `LAPTOP_FOLLOW_MOUSE` at the top of the Lua.

---

## Settings

Gimbal puts an icon in the bar. Tapping it opens a settings panel built on
Omarchy's own controls, so it takes your theme and matches the Wi-Fi panel next
to it. A touch UI rather than a config file or a TUI, for one reason: this is a
tablet's settings screen, and the tablet has no keyboard out unless you ask for
one.

| Setting | What it does |
|---|---|
| **Interaction** | Left knob and Right knob, each its own switch |
| **Gestures** | the command each of the four swipes runs |
| **Gaming** | hold the keyboard and the menu back while Moonlight is up |

Interaction is two coloured boxes rather than a list or a slider — one thumb or
two, or neither, so each knob is its own switch. Green is on, red is off: at
arm's length on a tablet that is the state you can read without looking twice.

Settings are written to `~/.config/omarchy/gimbal.json`. That is deliberately
not this plugin's entry in `shell.json`: `shell.json` belongs to Omarchy, and a
plugin that rewrites another program's config file will eventually lose a race
with it. Values in our file win; anything left unset falls back to the
`shell.json` entry.

### Setting the gestures by hand

Everything the panel writes can also be set in your plugin entry in
`~/.config/omarchy/shell.json`, the same place Omarchy keeps every other
plugin's settings:

```json
{
  "id": "io.github.mechanicsunlocked.gimbal",
  "swipeUp": "@keyboard",
  "swipeDown": "@menu",
  "swipeRight": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"r-1\" })'",
  "swipeLeft": "hyprctl dispatch 'hl.dsp.focus({ workspace = \"r+1\" })'",
  "padLeft": true,
  "padRight": true,
  "swipeThreshold": 30
}
```

`@keyboard` and `@menu` are the two built-in actions; anything else is run as a
shell command. Set a value to `""` to disable that swipe. The file is watched,
so changes take effect without restarting anything.

The two built-ins are named rather than spelled as commands for a reason: they
are the two that put something on top of whatever you are looking at, and
naming them is what lets a game refuse them. A command you write yourself
always runs.

<details>
<summary>Why the workspace commands look like that</summary>

**`hyprctl dispatch` takes a Lua expression on Omarchy 4**, not the words you
would use on a hyprlang config — `hyprctl dispatch workspace +1` fails with a
Lua syntax error and silently does nothing.

**And the selector is `r`, not `e` or a bare number.** Measured with workspaces
1, 2 and 5 live:

| from | `+1` / `e+1` | `r+1` | `e-1` | `r-1` |
|---|---|---|---|---|
| workspace 5 | — | 6 | **2** | 4 |
| workspace 1 | 2 | 2 | — | **1** |

The `e` selectors walk to the next workspace that *has a window on it*, so
swiping back from 5 landed on 2 whenever 3 and 4 were empty — which reads as a
swipe that overshot. `r` counts in plain numbers, so one swipe moves one
workspace whatever is or is not on them, and it stops at 1 rather than
wrapping.
</details>

### Always-visible knobs

Set `tabletOnly` to `false` at the top of `Panel.qml` to keep them in laptop
mode too.

---

## Under the hood

Three parts, all small:

* **`lua/gimbal.lua`** — tablet detection and auto-rotation, loaded straight
  into Hyprland's Lua config. No daemon.
* **`osk/`** — `fw12-oskbd`, a GTK4 layer-shell keyboard in C.
* **`Panel.qml`** / **`BarWidget.qml`** — the Omarchy shell plugin: the knobs,
  the bar icon, and the settings panel.

`ARCHITECTURE.md` is how it works; `FINDINGS.md` is the measurements behind
each decision.

### Checking on it

```bash
cat "$XDG_RUNTIME_DIR/gimbal-mode"           # tablet | laptop
pgrep -x fw12-oskbd                          # is the keyboard up
hyprctl layers | grep -E 'osk|gimbal'        # what is on screen
hyprctl eval 'require("hypr.gimbal").status()'
```

To put the knobs back where they started, delete
`~/.local/state/omarchy/gimbal-pads.json` and restart the shell.

### About the install

Two commands rather than one because Omarchy's installer deliberately never
runs code from a plugin it has just cloned, which is the right call. So the
second one is yours to read first:

```bash
less ~/.config/omarchy/plugins/io.github.mechanicsunlocked.gimbal/install.sh
```

Or from an ordinary clone, if you would rather not install it as a plugin at
all until you have looked at it. `install.sh` works out which of the two it is
and does not copy the plugin over itself:

```bash
git clone https://github.com/mechanicsunlocked/gimbal.git
./gimbal/install.sh
```

It builds the keyboard, installs the rotation module, adds one `require` line
to your Hyprland config, and restarts the shell.

Gimbal is an ordinary Omarchy shell plugin — a git repo with a `manifest.json`
at its root — so everything `omarchy plugin` knows how to do applies to it;
`omarchy plugin --help` lists the rest.

**Requirements:** `gtk4`, `gtk4-layer-shell`, `libxkbcommon`, `wayland`,
`pkgconf`, `gcc` — all in the official Arch repos, nothing from the AUR.
`install.sh` checks for them before it builds anything and prints the one
`pacman` line that fixes it.

---

## Trademarks

Gimbal is an independent community project. It is not made by, endorsed by, or
affiliated with Framework Computer Inc.

"Framework" and the Framework logo are trademarks of Framework Computer Inc.
The logo appears here in one place, descriptively: the Super key of the
on-screen keyboard, which reproduces the legend printed on that key of the
actual laptop.

It is an optional asset, not part of the program. If it is not installed, the
Super key falls back to a `❖` glyph and everything else works unchanged, so the
mark can be removed entirely by deleting one file:

```bash
rm ~/.local/share/gimbal/framework-logo.svg
```

---

MIT. See `LICENSE`.
