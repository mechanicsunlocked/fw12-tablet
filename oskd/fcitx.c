#include "fcitx.h"
#include "log.h"

#include <systemd/sd-bus.h>

#include <stdlib.h>
#include <string.h>

#define FCITX_SERVICE "org.fcitx.Fcitx5"
#define VK_NAME       "org.fcitx.Fcitx5.VirtualKeyboard"
#define VK_PATH       "/org/fcitx/virtualkeyboard/impanel"
#define VK_IFACE      "org.fcitx.Fcitx5.VirtualKeyboard1"

#define FCITX_CTRL_PATH  "/controller"
#define FCITX_CTRL_IFACE "org.fcitx.Fcitx.Controller1"

#define FCITX_VK_PATH    "/virtualkeyboard"
#define FCITX_VK_IFACE   "org.fcitx.Fcitx.VirtualKeyboard1"
#define FCITX_BACKEND    "org.fcitx.Fcitx5.VirtualKeyboardBackend1"

struct fcitx {
    sd_bus *bus;
    sd_bus_slot *obj_slot;
    sd_bus_slot *owner_slot;
    fcitx_callbacks cb;
    bool owns_name;
    bool activated; /* we asked fcitx5 to switch to virtualkeyboard mode */
    bool vk_mode;   /* fcitx5 confirmed it, as of the last check */
    bool warned_activate_fail;
    bool swallow_next_show;
};

/* ---- methods fcitx5 calls on us -------------------------------------- */

static int m_show(sd_bus_message *m, void *userdata, sd_bus_error *e)
{
    fcitx *f = userdata;

    log_dbg("fcitx5 -> ShowVirtualKeyboard");

    /* Our own registration echoing back. Forwarding it would put the keyboard
     * on screen because the daemon re-registered, not because the user
     * touched anything. See fcitx_register(). */
    if (f->swallow_next_show) {
        f->swallow_next_show = false;
        log_dbg("  (echo of our own registration, ignored)");
        return sd_bus_reply_method_return(m, "");
    }

    if (f->cb.on_show)
        f->cb.on_show(f->cb.user);
    return sd_bus_reply_method_return(m, "");
}

static int m_hide(sd_bus_message *m, void *userdata, sd_bus_error *e)
{
    fcitx *f = userdata;

    log_dbg("fcitx5 -> HideVirtualKeyboard");
    if (f->cb.on_hide)
        f->cb.on_hide(f->cb.user);
    return sd_bus_reply_method_return(m, "");
}

static int m_im_activated(sd_bus_message *m, void *userdata, sd_bus_error *e)
{
    fcitx *f = userdata;
    const char *name = NULL;
    int r = sd_bus_message_read(m, "s", &name);

    if (r < 0)
        return r;
    log_dbg("fcitx5 -> NotifyIMActivated(%s)", name ? name : "");
    if (f->cb.on_im_changed && name)
        f->cb.on_im_changed(name, f->cb.user);
    return sd_bus_reply_method_return(m, "");
}

/* Accepted and ignored, but they MUST still be answered or fcitx5 logs errors
 * against us. Preedit and candidates only matter for composing input methods;
 * for a Latin layout they arrive empty ("" and -1). */
static int m_ignore(sd_bus_message *m, void *userdata, sd_bus_error *e)
{
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable vk_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ShowVirtualKeyboard", "", "", m_show, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("HideVirtualKeyboard", "", "", m_hide, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyIMActivated", "s", "", m_im_activated, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyIMDeactivated", "s", "", m_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("NotifyIMListChanged", "", "", m_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UpdatePreeditArea", "s", "", m_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UpdatePreeditCaret", "i", "", m_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UpdateCandidateArea", "asbbii", "", m_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* ---- lifecycle -------------------------------------------------------- */

static int call_vk(fcitx *f, const char *method)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(f->bus, FCITX_SERVICE, FCITX_VK_PATH,
                               FCITX_VK_IFACE, method, &err, NULL, "");

    if (r < 0) {
        /* Debug, not warn: fcitx_reconcile retries this every few seconds, so
         * a persistent failure would otherwise fill the log. The callers warn
         * once instead. */
        log_dbg("%s: %s", method, err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return -1;
    }
    sd_bus_error_free(&err);
    return 0;
}

/* Tell fcitx5 we are its keyboard.
 *
 * `on_screen` is whether the keyboard is actually up. It matters because
 * fcitx5 has no separate "register" call: ShowVirtualKeyboard means both "you
 * are the keyboard" and "show yourself", and fcitx5 answers by calling
 * ShowVirtualKeyboard back on us. Registering while nothing is focused would
 * therefore put the keyboard on screen for no reason -- which it did, 249
 * shows against 26 hides in one session, because the periodic check
 * re-registered every 3 s and every registration echoed a show. When we are
 * not on screen, that one echo is dropped. */
void fcitx_register(fcitx *f, bool on_screen)
{
    f->swallow_next_show = !on_screen;

    if (call_vk(f, "ShowVirtualKeyboard") != 0) {
        f->swallow_next_show = false;
        /* Say this once. fcitx_reconcile retries on a timer, and repeating the
         * warning every few seconds would bury whatever comes next. */
        if (!f->warned_activate_fail) {
            log_warn("fcitx5 did not accept ShowVirtualKeyboard; "
                     "auto-show will not work");
            f->warned_activate_fail = true;
        }
        f->activated = false;
        return;
    }
    f->warned_activate_fail = false;
    f->activated = true;

    /* Always true, whatever is on screen. Read literally this is a lie, but
     * what fcitx5 does with it is decide whether a keyboard exists at all:
     * report false and it concludes there is none and reverts CurrentUI to
     * classicui, at which point it stops sending the show events we need to
     * know when to appear. "A keyboard is available" is the useful meaning,
     * and that is always true while the daemon is running. */
    fcitx_set_visible(f, true);
}

/* fcitx5 came or went. On `came back`, re-assert virtual-keyboard mode: the
 * new instance has no idea we are its keyboard, even though we still hold the
 * bus name it watches. */
static int on_name_owner_changed(sd_bus_message *m, void *userdata,
                                 sd_bus_error *e)
{
    fcitx *f = userdata;
    const char *name = NULL, *old_owner = NULL, *new_owner = NULL;

    if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0)
        return 0;
    if (!name || strcmp(name, FCITX_SERVICE) != 0)
        return 0;

    if (new_owner && *new_owner) {
        log_info("fcitx5 restarted; re-registering as the virtual keyboard");
        fcitx_register(f, f->cb.on_screen && f->cb.on_screen(f->cb.user));
    } else {
        log_warn("fcitx5 went away; auto-show is paused until it returns");
        f->activated = false;
    }
    return 0;
}

fcitx *fcitx_open(const fcitx_callbacks *cb)
{
    fcitx *f = calloc(1, sizeof *f);
    int r;

    if (!f)
        return NULL;
    if (cb)
        f->cb = *cb;

    r = sd_bus_open_user(&f->bus);
    if (r < 0) {
        log_err("cannot connect to the session bus: %s", strerror(-r));
        free(f);
        return NULL;
    }

    /* Every call we make on this bus is to fcitx5, a local process that should
     * answer instantly. The default 25 s timeout would stall our poll loop --
     * and with it typing and layout tracking -- if it ever stopped answering.
     * One second is far beyond a healthy reply and short enough not to matter. */
    sd_bus_set_method_call_timeout(f->bus, 1000000);

    r = sd_bus_add_object_vtable(f->bus, &f->obj_slot, VK_PATH, VK_IFACE,
                                 vk_vtable, f);
    if (r < 0) {
        log_err("cannot export %s: %s", VK_PATH, strerror(-r));
        goto fail;
    }

    /* REPLACE_EXISTING so a stale instance does not lock us out; fcitx5
     * watches the name and re-targets whoever holds it. */
    r = sd_bus_request_name(f->bus, VK_NAME, SD_BUS_NAME_REPLACE_EXISTING);
    if (r < 0) {
        log_err("cannot own %s: %s -- is another on-screen keyboard running?",
                VK_NAME, strerror(-r));
        goto fail;
    }
    f->owns_name = true;

    /* fcitx5 restarting drops its virtualkeyboard UI mode while we keep the
     * bus name, so nothing looks wrong from here -- we simply stop receiving
     * show/hide and auto-show dies silently. Watch for it coming back and
     * re-assert. Observed in practice, not theoretical. */
    r = sd_bus_match_signal(f->bus, &f->owner_slot, "org.freedesktop.DBus",
                            "/org/freedesktop/DBus", "org.freedesktop.DBus",
                            "NameOwnerChanged", on_name_owner_changed, f);
    if (r < 0)
        log_warn("cannot watch for fcitx5 restarts: %s", strerror(-r));

    /* Owning the name is not enough on its own: fcitx5 only switches its UI to
     * the on-screen-keyboard backend once this is also called. */
    /* Nothing is on screen yet: the shell has not even connected. */
    fcitx_register(f, false);
    /* Believe the registration we just did, so the first fcitx_reconcile() has
     * something to compare against and does not report a recovery that never
     * happened. */
    f->vk_mode = f->activated;

    log_info("registered with fcitx5 as the virtual keyboard");
    return f;

fail:
    if (f->obj_slot)
        sd_bus_slot_unref(f->obj_slot);
    sd_bus_unref(f->bus);
    free(f);
    return NULL;
}

void fcitx_close(fcitx *f)
{
    if (!f)
        return;

    /* Releasing the name while fcitx5 is in virtualkeyboard mode leaves
     * CurrentUI empty -- no candidate window, no UI at all. Hand the mode back
     * before letting go. */
    if (f->activated)
        call_vk(f, "HideVirtualKeyboard");
    if (f->owns_name)
        sd_bus_release_name(f->bus, VK_NAME);
    if (f->owner_slot)
        sd_bus_slot_unref(f->owner_slot);
    if (f->obj_slot)
        sd_bus_slot_unref(f->obj_slot);
    sd_bus_flush(f->bus);
    sd_bus_unref(f->bus);
    free(f);
}

/* ---- event loop integration ------------------------------------------ */

int fcitx_fd(fcitx *f) { return sd_bus_get_fd(f->bus); }

int fcitx_events(fcitx *f)
{
    int r = sd_bus_get_events(f->bus);
    return r < 0 ? 0 : r;
}

int fcitx_timeout_ms(fcitx *f)
{
    uint64_t usec = 0;

    if (sd_bus_get_timeout(f->bus, &usec) < 0)
        return -1;
    if (usec == UINT64_MAX)
        return -1;
    return (int)(usec / 1000);
}

int fcitx_dispatch(fcitx *f)
{
    for (;;) {
        int r = sd_bus_process(f->bus, NULL);

        if (r < 0) {
            log_err("bus error: %s", strerror(-r));
            return -1;
        }
        if (r == 0)
            return 0;
    }
}

/* ---- outbound --------------------------------------------------------- */

int fcitx_send_key(fcitx *f, uint32_t keysym, uint32_t keycode,
                   uint32_t states, bool is_release)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r;

    /* ProcessKeyEvent(keysym, keycode, states, isRelease, time).
     * time = 0 lets fcitx5 stamp it. */
    r = sd_bus_call_method(f->bus, FCITX_SERVICE, FCITX_VK_PATH, FCITX_BACKEND,
                           "ProcessKeyEvent", &err, NULL, "uuubu",
                           keysym, keycode, states, (int)is_release, 0u);
    if (r < 0) {
        log_warn("ProcessKeyEvent: %s",
                 err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return -1;
    }
    sd_bus_error_free(&err);
    return 0;
}

/* Is fcitx5 still routing input through us?
 *
 * Returns 1 yes, 0 no, -1 could not ask (fcitx5 is down or not answering,
 * which NameOwnerChanged already covers). */
static int in_vk_mode(fcitx *f)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *ui = NULL;
    int r, result = -1;

    r = sd_bus_call_method(f->bus, FCITX_SERVICE, FCITX_CTRL_PATH,
                           FCITX_CTRL_IFACE, "CurrentUI", &err, &reply, "");
    if (r >= 0 && sd_bus_message_read(reply, "s", &ui) >= 0 && ui)
        result = strcmp(ui, "virtualkeyboard") == 0 ? 1 : 0;

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return result;
}

/* fcitx5 drops out of virtual-keyboard mode on its own, and says nothing when
 * it does: we keep the bus name, keep the exported object, and simply stop
 * being called. Auto-show dies in a way that looks like nothing happened.
 *
 * There is no signal to hang this off -- org.fcitx.Fcitx.Controller1 exposes
 * exactly one, InputMethodGroupsChanged, and CurrentUI is a method, not a
 * property, so it does not even emit PropertiesChanged. Asking periodically is
 * the only mechanism fcitx5 offers.
 *
 * Measured: with fcitx5 reverted, ShowVirtualKeyboard flips CurrentUI straight
 * back to 'virtualkeyboard' and a show event arrives immediately, so recovery
 * is exactly the same call we make at startup.
 *
 * I could not find what triggers the revert. It survived focus changes between
 * a text field and a terminal, killing the focused client outright, and shell
 * restarts -- all of which I tried specifically to provoke it. Reconciling
 * against fcitx5's own answer does not depend on knowing the cause, which is
 * why it is written this way rather than as a fix for a specific trigger. */
void fcitx_reconcile(fcitx *f)
{
    int state;

    if (!f)
        return;

    /* Bound how long a swallow can sit unused. fcitx5 has always echoed our
     * registration, but if it ever did not, a stale flag would eat the next
     * real show instead -- and that show is the user tapping a text field. */
    f->swallow_next_show = false;

    state = in_vk_mode(f);
    if (state < 0)
        return;

    if (state == 1) {
        if (!f->vk_mode)
            log_info("fcitx5 is using the on-screen keyboard again");
        f->vk_mode = true;
        return;
    }

    /* Log the drop once, not every few seconds while it stays broken. */
    if (f->vk_mode)
        log_warn("fcitx5 stopped using the on-screen keyboard; re-registering");
    f->vk_mode = false;
    fcitx_register(f, f->cb.on_screen && f->cb.on_screen(f->cb.user));
}

/* Sent without waiting for a reply, because this sits in the typing path.
 *
 * fcitx5 hides the keyboard on every key it sees, so every keystroke produced
 * a hide, and answering that hide with a blocking call meant a full round trip
 * to another process between one keypress and the next -- from inside a DBus
 * message handler at that. Every key felt slow, and the space bar, pressed
 * fastest, felt worst.
 *
 * Nothing here reads the reply, and there is nothing useful to do if it fails:
 * the periodic check in fcitx_reconcile() notices a lost registration anyway.
 * So do not ask for one. */
int fcitx_set_visible(fcitx *f, bool visible)
{
    sd_bus_message *m = NULL;
    int r;

    r = sd_bus_message_new_method_call(f->bus, &m, FCITX_SERVICE, FCITX_VK_PATH,
                                       FCITX_BACKEND, "ProcessVisibilityEvent");
    if (r < 0)
        goto out;
    r = sd_bus_message_append(m, "b", (int)visible);
    if (r < 0)
        goto out;
    r = sd_bus_message_set_expect_reply(m, 0);
    if (r < 0)
        goto out;

    r = sd_bus_send(f->bus, m, NULL);

out:
    sd_bus_message_unref(m);
    if (r < 0) {
        log_dbg("ProcessVisibilityEvent: %s", strerror(-r));
        return -1;
    }
    return 0;
}
