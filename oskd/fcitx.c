#include "fcitx.h"
#include "log.h"

#include <systemd/sd-bus.h>

#include <stdlib.h>
#include <string.h>

#define FCITX_SERVICE "org.fcitx.Fcitx5"
#define VK_NAME       "org.fcitx.Fcitx5.VirtualKeyboard"
#define VK_PATH       "/org/fcitx/virtualkeyboard/impanel"
#define VK_IFACE      "org.fcitx.Fcitx5.VirtualKeyboard1"

#define FCITX_VK_PATH    "/virtualkeyboard"
#define FCITX_VK_IFACE   "org.fcitx.Fcitx.VirtualKeyboard1"
#define FCITX_BACKEND    "org.fcitx.Fcitx5.VirtualKeyboardBackend1"

struct fcitx {
    sd_bus *bus;
    sd_bus_slot *obj_slot;
    fcitx_callbacks cb;
    bool owns_name;
    bool activated; /* we asked fcitx5 to switch to virtualkeyboard mode */
};

/* ---- methods fcitx5 calls on us -------------------------------------- */

static int m_show(sd_bus_message *m, void *userdata, sd_bus_error *e)
{
    fcitx *f = userdata;

    log_dbg("fcitx5 -> ShowVirtualKeyboard");
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
        log_warn("%s: %s", method, err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return -1;
    }
    sd_bus_error_free(&err);
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

    /* Owning the name is not enough on its own: fcitx5 only switches its UI to
     * the on-screen-keyboard backend once this is also called. */
    if (call_vk(f, "ShowVirtualKeyboard") == 0)
        f->activated = true;
    else
        log_warn("fcitx5 did not accept ShowVirtualKeyboard; "
                 "auto-show may not work");

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

int fcitx_set_visible(fcitx *f, bool visible)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(f->bus, FCITX_SERVICE, FCITX_VK_PATH,
                               FCITX_BACKEND, "ProcessVisibilityEvent", &err,
                               NULL, "b", (int)visible);

    if (r < 0) {
        log_warn("ProcessVisibilityEvent: %s",
                 err.message ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        return -1;
    }
    sd_bus_error_free(&err);
    return 0;
}
