#!/bin/bash
# fw12-tablet -- remove everything install.sh put in $HOME.
#
# The root boot fix is left alone: it is a generic kernel-module ordering fix
# that does no harm on its own, and removing it is printed rather than done so
# that nobody loses their tablet switch to a script they ran to tidy up.
set -euo pipefail

PLUGIN_ID="drotiesel.fw12-tablet"
here=$(cd "$(dirname "$0")" && pwd)
plugin_dir="$HOME/.config/omarchy/plugins/$PLUGIN_ID"

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
note() { printf '    %s\n' "$*"; }

say "Stopping the keyboard"
pkill -x fw12-oskbd 2>/dev/null && note "stopped" || note "not running"

say "Removing the keyboard"
rm -fv "$HOME/.local/bin/fw12-oskbd" \
       "$HOME/.local/share/fw12-tablet/framework-logo.svg" | sed 's/^/    /'
rmdir "$HOME/.local/share/fw12-tablet" 2>/dev/null || true

say "Removing the rotation module"
hl="$HOME/.config/hypr/hyprland.lua"
if [[ -f $hl ]] && grep -q 'require("hypr.fw12-tablet")' "$hl"; then
    # Take the comment with it only when it is ours and directly above; a line
    # someone wrote themselves is not this script's to delete.
    python3 - "$hl" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p).read()
s = re.sub(r'\n*-- Framework 12 tablet mode and auto-rotation \(fw12-tablet\)\.\n'
           r'require\("hypr\.fw12-tablet"\)\n', '\n', s)
s = re.sub(r'^require\("hypr\.fw12-tablet"\)\n', '', s, flags=re.M)
open(p, 'w').write(s)
PY
    note "removed the require line from hyprland.lua"
fi
rm -fv "$HOME/.config/hypr/fw12-tablet.lua" | sed 's/^/    /'

say "Removing the plugin"
omarchy-plugin-disable "$PLUGIN_ID" >/dev/null 2>&1 || true
if [[ $here -ef $plugin_dir ]]; then
    note "this script is inside $plugin_dir; remove it with:"
    note "    omarchy plugin remove $PLUGIN_ID"
else
    rm -rf "$plugin_dir"
    note "removed $plugin_dir"
fi
rm -f "$HOME/.local/state/omarchy/fw12-osk-button.json"

omarchy-shell shell rescanPlugins >/dev/null 2>&1 || true
omarchy-restart-shell >/dev/null 2>&1 || true
command -v hyprctl >/dev/null 2>&1 && hyprctl reload >/dev/null 2>&1 || true

say "Done"
cat <<EOF

    The root boot fix, if you installed it, is still in place. It is a
    module-ordering fix and harmless on its own. To take it out too:

        sudo systemctl disable --now fw12-tablet-switch.service
        sudo rm -f /usr/lib/systemd/system/fw12-tablet-switch.service \\
                   /usr/lib/systemd/system-sleep/fw12-tablet-switch \\
                   /etc/mkinitcpio.conf.d/fw12-tablet.conf
        sudo rm -rf /usr/lib/fw12-tablet
        sudo mkinitcpio -P
EOF
