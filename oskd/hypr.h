/* Minimal Hyprland IPC reader.
 *
 * Only used to ask Hyprland which xkb layout it is running, so the keymap we
 * upload to the compositor matches the one the user actually has. Getting this
 * wrong is not subtle: on a `de` system an upload of `us` turns Y into Z and
 * loses the umlaut keys entirely.
 *
 * Talks to $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock
 * rather than forking hyprctl -- no subprocess, and nothing to break if PATH
 * is unusual under systemd.
 */
#ifndef FW12_HYPR_H
#define FW12_HYPR_H

#include <stddef.h>

/* Read a config option, e.g. "input:kb_layout". Hyprland answers lines like
 *   str: de
 * and this returns just the value. Returns 0 on success, -1 otherwise.
 * `out` is always NUL-terminated, empty on failure. */
int hypr_get_string(const char *option, char *out, size_t out_sz);

#endif
