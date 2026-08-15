/* Hyprland IPC: apply a display rotation.
 *
 * Talks to $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock
 * directly rather than forking hyprctl -- one connect+write per command, no
 * subprocess, no shell, and nothing to go wrong if PATH is odd under systemd.
 *
 * Omarchy 4 configures Hyprland in Lua, and on a Lua config Hyprland refuses
 * `keyword` outright:
 *
 *     keyword can't work with non-legacy parsers. Use eval.
 *
 * So the primary path is `eval` with a Lua statement, with a `keyword` fallback
 * for a legacy hyprlang config. Measured on Hyprland 0.56.2.
 */
#ifndef FW12_HYPR_H
#define FW12_HYPR_H

/* Apply `transform` (0/1/2/3) to the internal panel, plus the matching touch
 * and tablet(stylus) input transforms. All three move together or the pen and
 * finger stop landing where the user is pointing.
 *
 * Returns 0 on success, -1 if Hyprland could not be reached or refused. */
int hypr_set_transform(int transform);

#endif
