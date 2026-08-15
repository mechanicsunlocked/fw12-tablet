/* Key injection over zwp_virtual_keyboard_v1.
 *
 * fcitx5's ProcessKeyEvent cannot type capital letters on Wayland -- measured
 * here across four rounds, and documented upstream by fcitx5-osk as fcitx5 not
 * forwarding modifier events correctly on Wayland (FINDINGS.md 3.1c). So keys
 * go through the compositor instead:
 *
 *   - we upload the system's xkb keymap once
 *   - we send evdev keycodes and an explicit modifier mask
 *   - the COMPOSITOR resolves shift, AltGr, dead keys and compose
 *
 * fcitx5 is still the input method on the seat, so it sees the resulting keys
 * and ~/.XCompose keeps working. Unlike input-method-v2, the protocol allows
 * several virtual keyboards per seat, so this coexists with the one fcitx5
 * already creates.
 *
 * The same keymap is kept in memory to derive on-screen legends, so what a key
 * shows and what it types cannot drift apart.
 */
#ifndef FW12_VKBD_H
#define FW12_VKBD_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Modifier bits, matching the xkb default keymap's modifier indices. These are
 * what gets sent in the `depressed` mask. */
enum {
    VKBD_SHIFT = 1 << 0,
    VKBD_CAPS  = 1 << 1,
    VKBD_CTRL  = 1 << 2,
    VKBD_ALT   = 1 << 3,
    VKBD_SUPER = 1 << 6,
    VKBD_ALTGR = 1 << 7,
};

typedef struct vkbd vkbd;

/* Connect to Wayland, bind the virtual-keyboard manager, and upload a keymap
 * built from `layout`/`variant`/`options`. Any of them may be NULL or empty.
 * Returns NULL if the compositor does not offer the protocol. */
vkbd *vkbd_open(const char *layout, const char *variant, const char *options);
void vkbd_close(vkbd *v);

/* Recompile and re-upload the keymap after the system layout changed.
 * Returns 1 if the keymap actually changed, 0 if it was already current
 * (so callers can avoid pointless legend refreshes), -1 on failure -- in
 * which case the previous keymap is kept rather than leaving none. */
int vkbd_set_layout(vkbd *v, const char *layout, const char *variant,
                    const char *options);

/* Wayland fd, for the caller's poll() loop. */
int vkbd_fd(vkbd *v);
/* Prepare/read/dispatch. Returns -1 if the connection died. */
int vkbd_dispatch(vkbd *v);
int vkbd_flush(vkbd *v);

/* Set the modifier mask that applies to subsequent keys. */
void vkbd_set_modifiers(vkbd *v, uint32_t depressed, uint32_t locked);

/* Press or release one evdev keycode (KEY_A, not KEY_A+8). */
void vkbd_key(vkbd *v, uint32_t evdev_code, bool pressed);

/* The text a key would produce at the given shift level, for legends.
 * Writes UTF-8 into `out`; empty string if the key produces nothing printable.
 * `level` is 0 base, 1 shift, 2 AltGr, 3 shift+AltGr. */
void vkbd_key_label(vkbd *v, uint32_t evdev_code, int level, char *out,
                    size_t out_sz);

/* True if the active keymap binds KEY_102ND -- i.e. an ISO body with the extra
 * key next to left shift, rather than ANSI. The on-screen body must match, or
 * it will show a key that types nothing. */
bool vkbd_is_iso(vkbd *v);

#endif
