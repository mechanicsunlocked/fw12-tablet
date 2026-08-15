#!/bin/bash
# FW12 tablet-mode hardware snapshot. Run before/after boot, suspend, hibernate.
echo "### SNAPSHOT $(date -Iseconds)  uptime=$(uptime -p)  kernel=$(uname -r)"

echo "--- INT33D3 platform device ---"
ls -d /sys/bus/platform/devices/INT33D3:00 2>/dev/null || echo "ABSENT"

echo "--- soc_button_array bound? ---"
ls /sys/bus/platform/drivers/soc_button_array/ 2>/dev/null | grep -E "^INT33D3" || echo "UNBOUND"

echo "--- pinctrl_tigerlake ---"
lsmod | grep -E "^pinctrl_tigerlake" || echo "NOT_LOADED"

echo "--- SW_TABLET_MODE device present? ---"
found=no
for d in /sys/class/input/input*; do
  sw=$(cat "$d/capabilities/sw" 2>/dev/null) || continue
  [ -n "$sw" ] || continue
  last=${sw##* }
  if [ $(( 0x$last & 2 )) -ne 0 ]; then
    echo "  YES: $(cat "$d/name") -> $(ls "$d" | grep -m1 '^event')"
    found=yes
  fi
done
[ $found = no ] && echo "  NONE"

echo "--- IIO devices ---"
for d in /sys/bus/iio/devices/iio:device*; do
  [ -e "$d" ] || continue
  echo "  $d name=$(cat "$d/name" 2>/dev/null) label=$(cat "$d/label" 2>/dev/null)"
done
[ -e /sys/bus/iio/devices/iio:device0 ] || echo "  NO IIO DEVICES AT ALL"

# Resolve by name/label, NEVER by iio:deviceN index -- the numbering is not
# stable across boots (see FINDINGS.md 2.1).
ANGLE=$(grep -l "^cros-ec-lid-angle$" /sys/bus/iio/devices/iio:device*/name 2>/dev/null | head -1 | xargs -r dirname)
DISP=$(grep -l "^accel-display$"      /sys/bus/iio/devices/iio:device*/label 2>/dev/null | head -1 | xargs -r dirname)
BASE=$(grep -l "^accel-base$"         /sys/bus/iio/devices/iio:device*/label 2>/dev/null | head -1 | xargs -r dirname)
echo "--- resolved paths ---"
echo "  angle: ${ANGLE:-NOT FOUND}"
echo "  disp : ${DISP:-NOT FOUND}"
echo "  base : ${BASE:-NOT FOUND}"

echo "--- hinge angle (twice, 1s apart) ---"
a1=$(cat "$ANGLE/in_angl_raw" 2>/dev/null || echo "ERR")
sleep 1
a2=$(cat "$ANGLE/in_angl_raw" 2>/dev/null || echo "ERR")
echo "  angle: $a1 then $a2   (>360 = EC 'indeterminate' sentinel)"

echo "--- accel-display raw (twice, 1s apart) ---"
for pass in 1 2; do
  printf "  pass%s:" $pass
  for ax in x y z; do
    printf " %s=%s" $ax "$(cat "$DISP/in_accel_${ax}_raw" 2>/dev/null || echo ERR)"
  done
  echo
  [ $pass = 1 ] && sleep 1
done

echo "--- accel-base raw ---"
for ax in x y z; do
  printf " %s=%s" $ax "$(cat "$BASE/in_accel_${ax}_raw" 2>/dev/null || echo ERR)"
done; echo

echo "--- /dev/cros_ec ---"
ls -l /dev/cros_ec 2>/dev/null || echo "ABSENT"

echo "--- touchscreen / stylus present? ---"
grep -E "^N: Name" /proc/bus/input/devices | grep -iE "ILIT|stylus|touch" || echo "  none found"

echo "--- input-method owner (fcitx5) ---"
pgrep -a fcitx5 || echo "  fcitx5 not running"
hyprctl devices -j 2>/dev/null | grep -o '"name": "[^"]*virtual-keyboard[^"]*"' || echo "  no virtual keyboards on seat"
