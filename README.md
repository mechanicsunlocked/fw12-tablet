# Gimbal

Tablet mode for the Framework Laptop 12 on Omarchy 4 / Hyprland.

Two halves, both small:

* **`lua/gimbal.lua`** — tablet detection and auto-rotation, loaded
  straight into Hyprland's Lua config. Screen, touch and stylus rotate
  together. No daemon.
* **`osk/`** — `fw12-oskbd`, a GTK4 layer-shell keyboard laid out like the
  Framework Laptop 12's own: function row under Fn, real Ctrl / Alt / AltGr,
  the Framework key as Super, and a proper arrow cluster. It uploads the
  system's xkb keymap, so it types and triggers keybinds exactly as the
  built-in keyboard does.
* **`Panel.qml` / `BarWidget.qml`** — an Omarchy shell plugin: two draggable
  thumb knobs that take every gesture, a bar icon for the keyboard, and a
  touch settings panel.

`fcitx5` is not touched, stopped, or reconfigured by any of this.

See `ARCHITECTURE.md` for how it works and `FINDINGS.md` for the measurements
behind each decision.

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

## Using it

### Four ways to show and hide the keyboard

All four toggle, so the same action puts it away again:

| | |
|---|---|
| **The bar icon** | the keyboard glyph in the top bar. It lights up while the keyboard is out. |
| **A knob** | one tap. Only while folded. |
| **`SUPER + B`** | works in laptop mode too. |
| **Swipe up on a knob** | while folded. |

Four of them because each covers where the others are awkward. The bar icon is
the one you can always see and it says whether the keyboard is up. A knob is
already under your thumb. And `SUPER + B` is the only one that works *from the
on-screen keyboard itself* — its Framework key is a real Super — which is how
you put the keyboard away without hunting for a control the keyboard may be
sitting on.

Unfolding puts the keyboard away and takes the knobs with it.

### The two knobs

While folded, two round knobs appear at the lower corners, marked with four
arrowheads. **Press one and drag** in any of the four directions:

| Gesture | What it does |
|---|---|
| swipe **up** | show/hide the keyboard |
| swipe **down** | the Omarchy menu |
| swipe **right** | previous workspace |
| swipe **left** | next workspace |

Both knobs do all four, so there is nothing to remember about which is which.
Each is its own switch in the settings, so you can run one thumb or two.

They start at the lower corners because that is where your thumbs already are
when you hold the machine, so they cost no reach. And because a knob is not
against an edge, all four directions have the whole screen to travel and one
threshold covers them.

That last part is why the knobs replaced screen-edge strips entirely. An edge
strip is a dead end in the direction it is anchored to — swipe left at the left
edge and the glass runs out, often after ten or fifteen millimetres — so its
threshold had to be scaled to whatever room was left, and outward swipes never
felt the same as inward ones. A knob has the whole screen either way.

Edge strips also could not hold their ground against a fullscreen window.
Measured with a fullscreen Moonlight session up: the monitor reported
`reserved: 0 26 0 0` while the strip was still on screen at `0 26 15 724` — it
kept taking touches and gave back no space, and only recreating the surface
restored the reservation. A knob reserves nothing, so it has nothing to lose,
and it sits on the overlay layer where a fullscreen window cannot cover it.

**One tap shows and hides the keyboard**, and the knob fills with the accent
colour while it is out, so it says which way it is set. **To move a knob, tap
it three times.** It swells and turns the accent colour to say it is loose;
then drag it anywhere. **Three taps again** sticks it down and saves it. The position is kept as a fraction of the screen, so it survives
rotation, and is remembered across reboots in
`~/.local/state/omarchy/gimbal-pads.json`.

Three taps rather than a hold, because a hold is what a swipe starts with. If
picking a knob up were a long press, then pressing, pausing to think, and
swiping would pick it up mid-gesture. Three taps is not something a hand does
by accident, which is exactly why the press-and-drag can start acting
immediately with nothing to arbitrate.

The single tap has to wait out the multi-tap window — about a third of a second
— before it acts. A triple tap opens with a single tap, so firing on the first
lift would toggle the keyboard three times on the way to unlocking a pad. That
delay is the price of putting two things on one control, and it is the reason
the bar icon exists for when you want the keyboard *now*.

### There are no screen-edge strips

There were, for a while: thin live bands down the sides of the screen. Two
things killed them, both measured rather than felt, and both written up in the
knobs section above — a threshold that could never be the same in every
direction, and a reservation a fullscreen window silently took away.

What went with them is worth saying, because it is all upside: the on-screen
keyboard has the full width of the screen again, which matters most in portrait
where the key pitch is tightest, and no window is ever narrower than the screen
just because tablet mode is on.

### Setting the gestures by hand

Everything the settings panel writes can also be set in your plugin entry in `~/.config/omarchy/shell.json` — the
same file and the same place Omarchy keeps every other plugin's settings:

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
command. Set a value to `""` to disable that swipe. The file is watched, so
changes take effect without restarting anything.

The two built-ins are named rather than spelled as commands for a reason: they
are the two that put something on top of whatever you are looking at, and
naming them is what lets a game refuse them (see **Moonlight** below). A
command you write yourself always runs.

Two things about those workspace commands. **`hyprctl dispatch` takes a Lua
expression on Omarchy 4**, not the words you would use on a hyprlang config —
`hyprctl dispatch workspace +1` fails with a Lua syntax error and silently does
nothing.

And **the selector is `r`, not `e` or a bare number**. Measured on this machine
with workspaces 1, 2 and 5 live:

| from | `+1` / `e+1` | `r+1` | `e-1` | `r-1` |
|---|---|---|---|---|
| workspace 5 | — | 6 | **2** | 4 |
| workspace 1 | 2 | 2 | — | **1** |

The `e` selectors walk to the next workspace that *has a window on it*, so
swiping back from 5 landed on 2 whenever 3 and 4 were empty — which reads as a
swipe that overshot. `r` counts in plain numbers, so one swipe moves one
workspace whatever is or is not on them, and it stops at 1 rather than
wrapping.

## Settings

Gimbal puts an icon in the bar. Tapping it opens a settings panel built on
Omarchy's own controls, so it takes your theme and matches the Wi-Fi panel next
to it. It is a touch UI rather than a config file or a TUI for one reason: this
is a tablet's settings screen, and the tablet has no keyboard out unless you
ask for one.

| Setting | What it does |
|---|---|
| **Interaction** | Left knob and Right knob, each its own switch |
| **Gestures** | the command each of the four swipes runs |
| **Gaming** | hold the keyboard and the menu back while Moonlight is up |

Interaction is two coloured boxes rather than a list or a slider. One thumb or
two, or neither, so each knob is its own switch. Green is on, red is off: at
arm's length on a tablet that is the state you can read without looking twice.

**Gestures** takes any shell command. `@keyboard` and `@menu` are the two
built-ins. An empty field falls back to the default, which is shown greyed in
the box.

Settings are written to `~/.config/omarchy/gimbal.json`. That is deliberately
not this plugin's entry in `shell.json`: `shell.json` belongs to Omarchy, and a
plugin that rewrites another program's config file will eventually lose a race
with it. Values in our file win; anything left unset falls back to the
`shell.json` entry, which stays usable for anyone who would rather set things
by hand.

### Moonlight

Streaming a desktop game to the tablet, every gesture is aimed at the remote
machine, and a keyboard sliding up over the picture is never what you meant. So
while you are **on the workspace a Moonlight window is on**, nothing summons
the keyboard — not a swipe, not a knob, not the bar icon, not `SUPER + B` — and
one already up is dismissed when you switch to it. The Omarchy menu is held
back the same way, for a reason worth knowing: it is a full-screen layer
surface that takes keyboard focus, and a client capturing input for a stream
does not reliably take that capture back afterwards. One swipe for the menu was
enough to leave a game that no longer answered the touchscreen at all. Measured
and ruled out first: the menu itself unmaps cleanly and focus does return to
the window, so it is the client's capture, not a surface left behind — which
also means it is not ours to fix, only to avoid.

Workspace swipes keep working, because leaving the game is exactly what you
still want. Only the two built-in actions can be refused; a command you wrote
yourself always runs.

Only that workspace. A stream running on workspace 2 is no reason to lose the
keyboard on workspace 1, and going somewhere else on the tablet while a game
sits where you left it is the ordinary thing to do.

Which workspace a window is on is a Hyprland question rather than a Wayland
one — the foreign-toplevel protocol has no notion of workspaces — so it is
asked of Hyprland directly, and it arrives as an event rather than a poll:
there is no interval to pick and nothing to be stale by.

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

**US International is not a different keyboard from US.** The physical board is
the same ANSI board with the same keys in the same places; `intl` is purely the
software variant, and all it does is turn `'` `"` `` ` `` `~` `^` into dead keys
and hang more characters off AltGr. So a plain US ANSI machine can run either,
and the choice is only about how you want to type accents:

| `kb_variant` | `'` then `e` | good for |
|---|---|---|
| *(empty)* | `'e` | typing English and nothing else |
| `intl` | `é` | typing accents constantly; the price is that `don't` needs a space after the apostrophe |
| `altgr-intl` | `'e`, and `AltGr+'` then `e` gives `é` | mostly English, accents when you need them — the apostrophe stays an apostrophe |

`altgr-intl` is the one to reach for if `intl` starts fighting you over
apostrophes. Whatever you pick, the on-screen keyboard follows it; there is
nothing to change here.

Because it uploads the real keymap rather than inventing one, AltGr and dead
keys work on it exactly as they do on the built-in keyboard — `AltGr` then `'`
then `e` gives `é`. Hold `Fn` for F1–F12 on the number row. Tap a modifier once
for one-shot, twice to lock it.

---

## About the install

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
at its root — so everything `omarchy plugin` knows how to do applies to it.
`omarchy plugin --help` lists the rest, including `update` and `remove`.

**Requirements:** `gtk4`, `gtk4-layer-shell`, `libxkbcommon`, `wayland`,
`pkgconf`, `gcc` — all in the official Arch repos, nothing from the AUR.
`install.sh` checks for them before it builds anything and prints the one
`pacman` line that fixes it.

---

## Checking on it

```bash
cat "$XDG_RUNTIME_DIR/gimbal-mode"      # tablet | laptop
pgrep -x fw12-oskbd                          # is the keyboard up
hyprctl layers | grep -E 'osk|gimbal'        # what is on screen
hyprctl eval 'require("hypr.gimbal").status()'
```

To put the knobs back where they started without touching them, delete
`~/.local/state/omarchy/gimbal-pads.json` and restart the shell; they go back
to the two lower corners.

---

## Trademarks

Gimbal is an independent community project. It is not made by, endorsed by, or
affiliated with Framework Computer Inc.

"Framework" and the Framework logo are trademarks of Framework Computer Inc.
It appears here in one place, descriptively: the Super key of the on-screen
keyboard, which reproduces the legend printed on that key of the actual
laptop.

The logo is an optional asset, not part of the program. If it is not installed,
the Super key falls back to a `❖` glyph and everything else works unchanged, so
the mark can be removed entirely by deleting one file:

```bash
rm ~/.local/share/gimbal/framework-logo.svg
```
