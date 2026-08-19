#!/bin/bash
# Gimbal -- install everything except the optional root boot fix.
#
# No sudo, no install hooks, nothing outside $HOME. Idempotent: running it
# twice is the same as running it once, and it is also how you upgrade.
#
# Four parts, and they are genuinely separate pieces of software:
#
#   1. fw12-oskbd    the keyboard          -> ~/.local/bin
#   2. gimbal.lua  rotation           -> ~/.config/hypr, plus one require
#                                             line in hyprland.lua
#   3. the shell plugin  button + swipes   -> ~/.config/omarchy/plugins/<id>
#   4. the boot fix                        -> root, printed at the end, skipped
#                                             here
#
# Run it from a git clone, or from the plugin directory after
# `omarchy plugin add` -- it works out which it is and does not copy the
# plugin over itself.
set -euo pipefail

PLUGIN_ID="io.github.mechanicsunlocked.gimbal"
here=$(cd "$(dirname "$0")" && pwd)
plugin_dir="$HOME/.config/omarchy/plugins/$PLUGIN_ID"
warnings=()

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }
warn() { warnings+=("$*"); printf '    \033[33mwarning:\033[0m %s\n' "$*"; }
die()  { printf '\n\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[[ ${1:-} == -h || ${1:-} == --help ]] && { sed -n '2,20p' "$0" | sed 's/^# \?//'; exit 0; }

# --------------------------------------------------------------------------
# 0. Dependencies
#
# All of these are in the official repositories (core/extra). Nothing here
# wants the AUR, and the script will not reach for it.
# --------------------------------------------------------------------------
say "Checking dependencies"
missing=()
for pkg in gtk4 gtk4-layer-shell libxkbcommon wayland pkgconf gcc; do
    pacman -Qq "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
done
if (( ${#missing[@]} )); then
    die "missing packages: ${missing[*]}
    All are in the official repos:
        sudo pacman -S --needed ${missing[*]}"
fi
note "all present"

command -v omarchy-shell >/dev/null 2>&1 || die "omarchy-shell not found; this needs Omarchy 4."

# --------------------------------------------------------------------------
# 1. The keyboard
# --------------------------------------------------------------------------
say "Building and installing fw12-oskbd"
make -C "$here/osk" --no-print-directory
make -C "$here/osk" --no-print-directory install
note "$HOME/.local/bin/fw12-oskbd"

# The plugin starts the keyboard by name, so it has to be findable in the
# environment omarchy-shell inherited -- which is the session's, not this
# shell's. A PATH that is fine here and wrong there is the one failure of this
# script that would look like a broken button rather than a missing directory.
case ":${PATH}:" in
    *":$HOME/.local/bin:"*) ;;
    *) warn "$HOME/.local/bin is not on PATH; the button will not find the keyboard.
             Add it to ~/.bashrc (or ~/.config/uwsm/env) and log out and in." ;;
esac

# --------------------------------------------------------------------------
# 2. Rotation
# --------------------------------------------------------------------------
say "Installing the rotation module"
install -Dm644 "$here/lua/gimbal.lua" "$HOME/.config/hypr/gimbal.lua"
note "$HOME/.config/hypr/gimbal.lua"

hl="$HOME/.config/hypr/hyprland.lua"
if [[ ! -f $hl ]]; then
    warn "no $hl -- add this line to your Hyprland config by hand:
             require(\"hypr.gimbal\")"
elif grep -q 'require("hypr.gimbal")' "$hl"; then
    note "already required from hyprland.lua"
else
    cat >>"$hl" <<'LUA'

-- Framework 12 tablet mode and auto-rotation (gimbal).
require("hypr.gimbal")
LUA
    note "added the require line to hyprland.lua"
fi

# --------------------------------------------------------------------------
# 3. The plugin
#
# `omarchy plugin add` has already put the whole repo here, in which case
# copying it onto itself is at best pointless. Detect that by inode, not by
# path text, so a symlinked or differently-spelled clone is still recognised.
# --------------------------------------------------------------------------
say "Installing the shell plugin"
if [[ -d $plugin_dir ]] && [[ $here -ef $plugin_dir ]]; then
    note "already installed here by 'omarchy plugin add'"
else
    mkdir -p "$plugin_dir"
    install -Dm644 "$here/manifest.json" "$plugin_dir/manifest.json"
    install -Dm644 "$here/Panel.qml"     "$plugin_dir/Panel.qml"
    note "$plugin_dir"
fi

omarchy-shell shell rescanPlugins >/dev/null 2>&1 || warn "rescanPlugins failed; is the shell running?"
if omarchy-plugin-list --json 2>/dev/null | jq -e --arg id "$PLUGIN_ID" \
        'any(.[]; .id == $id and .enabled == true)' >/dev/null 2>&1; then
    note "already enabled"
else
    omarchy-plugin-enable "$PLUGIN_ID" >/dev/null 2>&1 \
        || omarchy-shell shell setPluginEnabled "$PLUGIN_ID" true >/dev/null 2>&1 \
        || warn "could not enable the plugin; run: omarchy plugin enable $PLUGIN_ID"
    note "enabled"
fi

# --------------------------------------------------------------------------
# 4. Make it live
#
# The shell restart is not optional the first time: enabling a third-party
# panel hot does mount it, but a later rescanPlugins leaves it unmounted until
# the shell restarts. `hyprctl reload` is what picks up the rotation module.
# --------------------------------------------------------------------------
say "Restarting the shell and reloading Hyprland"
omarchy-restart-shell >/dev/null 2>&1 || warn "omarchy-restart-shell failed; restart it by hand"
if command -v hyprctl >/dev/null 2>&1; then
    hyprctl reload >/dev/null 2>&1 || warn "hyprctl reload failed; log out and in"
else
    warn "no hyprctl; log out and in to load the rotation module"
fi

# --------------------------------------------------------------------------
say "Done"
cat <<EOF

    The button appears when you fold the machine into a tablet.
    SUPER + B toggles the keyboard from either mode.

    One optional step is left, and it is the only one that needs root:

        sudo $here/system/install.sh

    It closes a firmware probe race that costs the tablet switch on roughly
    one boot in three. Without it nothing breaks -- an affected boot simply
    stays in laptop mode -- so it is worth doing, and safe to skip.
EOF

if (( ${#warnings[@]} )); then
    printf '\n\033[33m    %d warning(s) above.\033[0m\n' "${#warnings[@]}"
fi
