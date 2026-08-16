#!/bin/bash
# Install the tablet-switch boot fix. Run with sudo.
#
# The Framework 12 exposes SW_TABLET_MODE through ACPI INT33D3, bound by
# soc_button_array -- but only if the Tiger Lake pin controller is already
# registered when it probes. That ordering is a race, and it is not
# deterministic: measured on this machine as one loss in three boots, with no
# configuration change in between. When it loses, /dev/input has no gpio-keys
# node at all, Hyprland never sees a tablet switch, and auto-rotation is simply
# dead with nothing in any log to say why.
#
# Three layers, because the first one is not quite a guarantee:
#   1. load both modules from the initramfs, in order -- removes the race
#   2. a boot unit that binds the device if it still came up unbound
#   3. a resume hook, for the same reason after suspend
#
# Everything here is idempotent and silent when there is nothing to do.
set -euo pipefail

if [ "$(id -u)" != 0 ]; then
    echo "run with sudo" >&2
    exit 1
fi

here=$(cd "$(dirname "$0")" && pwd)

# Sanity-check the pin controller before writing anything. Some 13th-gen
# machines need pinctrl_alderlake instead, and installing the wrong module name
# would leave the user with a fix that quietly does nothing.
if ! lsmod | grep -q '^pinctrl_tigerlake'; then
    echo "warning: pinctrl_tigerlake is not loaded on this machine." >&2
    echo "Check 'lsmod | grep pinctrl' and edit fw12-tablet.conf before" >&2
    echo "trusting the initramfs part of this." >&2
fi

install -Dm755 "$here/fw12-bind-tablet-switch" /usr/lib/fw12-tablet/bind-tablet-switch
install -Dm644 "$here/fw12-tablet-switch.service" /usr/lib/systemd/system/fw12-tablet-switch.service
install -Dm755 "$here/fw12-tablet-switch-sleep" /usr/lib/systemd/system-sleep/fw12-tablet-switch
install -Dm644 "$here/fw12-tablet.conf" /etc/mkinitcpio.conf.d/fw12-tablet.conf

systemctl daemon-reload
systemctl enable --now fw12-tablet-switch.service

# Slow, and the only step that needs a reboot to take effect. The bind above
# has already fixed the running system.
mkinitcpio -P

echo
if grep -q gpio-keys /proc/bus/input/devices; then
    echo "SW_TABLET_MODE switch is bound. Rotation should work now, without a reboot."
else
    echo "The switch is still not bound. Rotation will not work; do not reboot"
    echo "expecting it to fix itself -- say so and we will look at it."
fi
