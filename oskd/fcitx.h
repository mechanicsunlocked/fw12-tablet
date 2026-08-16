/* fcitx5 virtual-keyboard backend bridge.
 *
 * We do NOT become the seat's input method. Omarchy ships fcitx5 and runs it
 * for ~/.XCompose compose sequences, and a Wayland seat allows exactly one
 * zwp_input_method_v2 client. Instead we register as fcitx5's *virtual
 * keyboard*, which means fcitx5 keeps doing keymap, layout, dead keys, compose
 * and candidates, and simply tells us when to appear.
 *
 * Two conditions together activate the mode (both required, verified):
 *   1. own the bus name org.fcitx.Fcitx5.VirtualKeyboard
 *   2. call ShowVirtualKeyboard on fcitx5
 *
 * The full contract was captured empirically with tools/vkspy.c; see
 * FINDINGS.md 3.1a. Signatures are not discoverable from the shared object and
 * a wrong one fails silently, so they are pinned here from observation.
 */
#ifndef FW12_FCITX_H
#define FW12_FCITX_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* fcitx5 asks for the keyboard to be shown or hidden. This is the
     * auto-show source -- protocol truth, not a focus heuristic. */
    void (*on_show)(void *user);
    void (*on_hide)(void *user);
    /* Active input method changed, e.g. "keyboard-us" -> "keyboard-de".
     * Used to re-derive the on-screen legends. */
    void (*on_im_changed)(const char *im_name, void *user);
    /* Is the keyboard on screen right now? Asked before re-registering, so a
     * re-registration does not put it there. */
    bool (*on_screen)(void *user);
    void *user;
} fcitx_callbacks;

typedef struct fcitx fcitx;

/* Connect to the session bus, export the client object, own the name, and ask
 * fcitx5 to switch to virtual-keyboard mode. Returns NULL on failure. */
fcitx *fcitx_open(const fcitx_callbacks *cb);

/* Restore fcitx5's previous UI and release everything. Must be called: simply
 * dropping the bus name leaves fcitx5 with CurrentUI empty and no visible
 * input method UI at all. */
void fcitx_close(fcitx *f);

/* File descriptor to poll, and the events to wait for. */
int fcitx_fd(fcitx *f);
int fcitx_events(fcitx *f);
/* Timeout in milliseconds for poll(), or -1. */
int fcitx_timeout_ms(fcitx *f);

/* Drive the bus. Returns 0 on success, -1 if the connection is gone. */
int fcitx_dispatch(fcitx *f);

/* Type. `keysym` and `keycode` are X11 keysym and evdev+8 keycode; `states` is
 * the modifier mask. fcitx5 does the rest -- including dead keys and
 * ~/.XCompose sequences, which is exactly why we route through it. */
int fcitx_send_key(fcitx *f, uint32_t keysym, uint32_t keycode,
                   uint32_t states, bool is_release);

/* Ask fcitx5 to switch to virtual-keyboard mode. Called at startup and again
 * whenever fcitx5 restarts -- a new instance does not know we are its keyboard
 * even though we still hold the bus name it watches.
 *
 * `on_screen` must say whether the keyboard is currently visible: fcitx5 has
 * no registration call that does not also mean "show yourself", so when it is
 * false the resulting show is dropped rather than forwarded. */
void fcitx_register(fcitx *f, bool on_screen);

/* Tell fcitx5 whether our surface is actually visible. */
int fcitx_set_visible(fcitx *f, bool visible);

/* Confirm fcitx5 is still routing input through us, and re-register if it has
 * quietly stopped. Call every few seconds; there is no signal for this. */
void fcitx_reconcile(fcitx *f);

#endif
