/* Hyprland event-socket subscriber (.socket2.sock).
 *
 * Only one event matters here: `activelayout`, emitted when the active keyboard
 * layout changes. The daemon reacts by re-reading the layout from Hyprland and
 * recompiling the keymap, so the on-screen keyboard follows the system instead
 * of being frozen at whatever was configured when it started.
 *
 * `configreloaded` is watched too, since a layout can change that way without
 * any activelayout event.
 *
 * The event payload carries a human-readable layout NAME ("German"), not an
 * xkb code, so it is used purely as a trigger; the authoritative value comes
 * from re-querying input:kb_layout.
 */
#ifndef FW12_HYPREVT_H
#define FW12_HYPREVT_H

typedef struct hyprevt hyprevt;

/* `on_change` fires when the layout may have changed. Returns NULL if the
 * event socket is unavailable, which is not fatal -- the daemon simply keeps
 * the layout it started with. */
hyprevt *hyprevt_open(void (*on_change)(void *user), void *user);
void hyprevt_close(hyprevt *h);

int hyprevt_fd(hyprevt *h);
/* Returns -1 if Hyprland closed the socket (session is going away). */
int hyprevt_dispatch(hyprevt *h);

#endif
