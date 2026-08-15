#include "vkbd.h"
#include "log.h"

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct vkbd {
    struct wl_display *dpy;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *mgr;
    struct zwp_virtual_keyboard_v1 *kbd;
    struct xkb_context *ctx;
    struct xkb_keymap *keymap;
    char layout[64];
    char variant[64];
    char options[128];
};

/* Compile a keymap from rule names, or NULL if the names are not valid. */
static struct xkb_keymap *compile_keymap(struct xkb_context *ctx,
                                         const char *layout,
                                         const char *variant,
                                         const char *options)
{
    struct xkb_rule_names names;

    memset(&names, 0, sizeof names);
    names.layout = (layout && *layout) ? layout : NULL;
    names.variant = (variant && *variant) ? variant : NULL;
    names.options = (options && *options) ? options : NULL;

    return xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

static void reg_global(void *data, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver)
{
    vkbd *v = data;

    if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name))
        v->mgr = wl_registry_bind(r, name,
                                  &zwp_virtual_keyboard_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !v->seat)
        v->seat = wl_registry_bind(r, name, &wl_seat_interface,
                                   ver < 7 ? ver : 7);
}

static void reg_remove(void *data, struct wl_registry *r, uint32_t name) {}

static const struct wl_registry_listener reg_listener = {
    reg_global,
    reg_remove,
};

/* Upload the keymap over a memfd. The compositor keeps its own copy, so the
 * fd can be closed straight away. */
static int upload_keymap(vkbd *v)
{
    char *str = xkb_keymap_get_as_string(v->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    size_t size;
    int fd;
    void *map;

    if (!str) {
        log_err("could not serialise the keymap");
        return -1;
    }
    size = strlen(str) + 1;

    fd = memfd_create("fw12-keymap", MFD_CLOEXEC);
    if (fd < 0) {
        log_errno("memfd_create");
        free(str);
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        log_errno("ftruncate");
        close(fd);
        free(str);
        return -1;
    }
    map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        log_errno("mmap");
        close(fd);
        free(str);
        return -1;
    }
    memcpy(map, str, size);
    munmap(map, size);
    free(str);

    zwp_virtual_keyboard_v1_keymap(v->kbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                   fd, (uint32_t)size);
    close(fd);

    /* Round-trip rather than just flush: the compositor must have processed
     * the keymap before any key request, or the keys are dropped. */
    wl_display_roundtrip(v->dpy);

    /* Establish a known modifier state. Without an initial modifiers request
     * the compositor has no state for this keyboard at all. */
    zwp_virtual_keyboard_v1_modifiers(v->kbd, 0, 0, 0, 0);
    wl_display_roundtrip(v->dpy);
    return 0;
}

/* Milliseconds from a monotonic clock. The protocol's `time` is a timestamp,
 * not an opaque counter; a naive incrementing integer starting at zero is not
 * a plausible event time and compositors are free to discard it. */
static uint32_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

vkbd *vkbd_open(const char *layout, const char *variant, const char *options)
{
    vkbd *v = calloc(1, sizeof *v);

    if (!v)
        return NULL;

    v->dpy = wl_display_connect(NULL);
    if (!v->dpy) {
        log_err("cannot connect to Wayland (WAYLAND_DISPLAY unset?)");
        free(v);
        return NULL;
    }

    v->registry = wl_display_get_registry(v->dpy);
    wl_registry_add_listener(v->registry, &reg_listener, v);
    wl_display_roundtrip(v->dpy);

    if (!v->mgr || !v->seat) {
        log_err("compositor does not offer zwp_virtual_keyboard_manager_v1");
        vkbd_close(v);
        return NULL;
    }

    v->kbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(v->mgr,
                                                                     v->seat);
    if (!v->kbd) {
        log_err("could not create a virtual keyboard");
        vkbd_close(v);
        return NULL;
    }

    v->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!v->ctx) {
        log_err("xkb_context_new failed");
        vkbd_close(v);
        return NULL;
    }

    v->keymap = compile_keymap(v->ctx, layout, variant, options);
    if (!v->keymap) {
        log_err("could not compile keymap for layout '%s' variant '%s'",
                layout ? layout : "(default)", variant ? variant : "");
        vkbd_close(v);
        return NULL;
    }

    if (upload_keymap(v) < 0) {
        vkbd_close(v);
        return NULL;
    }
    snprintf(v->layout, sizeof v->layout, "%s", layout ? layout : "");
    snprintf(v->variant, sizeof v->variant, "%s", variant ? variant : "");
    snprintf(v->options, sizeof v->options, "%s", options ? options : "");

    log_info("virtual keyboard ready (layout '%s'%s%s, %s body)",
             layout && *layout ? layout : "default",
             variant && *variant ? " variant " : "",
             variant && *variant ? variant : "",
             vkbd_is_iso(v) ? "ISO" : "ANSI");
    return v;
}

void vkbd_close(vkbd *v)
{
    if (!v)
        return;
    /* Drop any held modifiers, or the compositor keeps them latched after we
     * are gone -- a stuck Super is very hard to diagnose from the outside. */
    if (v->kbd) {
        zwp_virtual_keyboard_v1_modifiers(v->kbd, 0, 0, 0, 0);
        wl_display_flush(v->dpy);
        zwp_virtual_keyboard_v1_destroy(v->kbd);
    }
    if (v->keymap)
        xkb_keymap_unref(v->keymap);
    if (v->ctx)
        xkb_context_unref(v->ctx);
    if (v->mgr)
        zwp_virtual_keyboard_manager_v1_destroy(v->mgr);
    if (v->seat)
        wl_seat_destroy(v->seat);
    if (v->registry)
        wl_registry_destroy(v->registry);
    if (v->dpy)
        wl_display_disconnect(v->dpy);
    free(v);
}

int vkbd_set_layout(vkbd *v, const char *layout, const char *variant,
                    const char *options)
{
    struct xkb_keymap *next;

    layout = layout ? layout : "";
    variant = variant ? variant : "";
    options = options ? options : "";

    if (!strcmp(layout, v->layout) && !strcmp(variant, v->variant) &&
        !strcmp(options, v->options))
        return 0; /* unchanged */

    next = compile_keymap(v->ctx, layout, variant, options);
    if (!next) {
        /* Keep the working keymap rather than ending up with none: a bad
         * layout string should degrade to "stale legends", not "cannot type". */
        log_warn("layout '%s' variant '%s' did not compile; keeping '%s'",
                 layout, variant, v->layout);
        return -1;
    }

    xkb_keymap_unref(v->keymap);
    v->keymap = next;

    if (upload_keymap(v) < 0) {
        log_err("could not upload the new keymap");
        return -1;
    }

    snprintf(v->layout, sizeof v->layout, "%s", layout);
    snprintf(v->variant, sizeof v->variant, "%s", variant);
    snprintf(v->options, sizeof v->options, "%s", options);

    log_info("layout changed to '%s'%s%s (%s body)", layout,
             *variant ? " variant " : "", variant,
             vkbd_is_iso(v) ? "ISO" : "ANSI");
    return 1;
}

int vkbd_fd(vkbd *v) { return wl_display_get_fd(v->dpy); }

int vkbd_dispatch(vkbd *v)
{
    if (wl_display_dispatch(v->dpy) < 0) {
        log_err("Wayland connection lost");
        return -1;
    }
    return 0;
}

int vkbd_flush(vkbd *v)
{
    return wl_display_flush(v->dpy) < 0 ? -1 : 0;
}

void vkbd_set_modifiers(vkbd *v, uint32_t depressed, uint32_t locked)
{
    zwp_virtual_keyboard_v1_modifiers(v->kbd, depressed, 0, locked, 0);
    wl_display_flush(v->dpy);
}

void vkbd_key(vkbd *v, uint32_t evdev_code, bool pressed)
{
    zwp_virtual_keyboard_v1_key(v->kbd, now_ms(), evdev_code,
                                pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                        : WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(v->dpy);
}

/* Dead keys have no printable UTF-8 of their own; show the spacing glyph so a
 * key that composes an accent is not blank on screen. */
static const char *dead_key_glyph(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_dead_circumflex:   return "^";
    case XKB_KEY_dead_grave:        return "`";
    case XKB_KEY_dead_acute:        return "´";
    case XKB_KEY_dead_tilde:        return "~";
    case XKB_KEY_dead_diaeresis:    return "¨";
    case XKB_KEY_dead_macron:       return "¯";
    case XKB_KEY_dead_breve:        return "˘";
    case XKB_KEY_dead_abovering:    return "°";
    case XKB_KEY_dead_doubleacute:  return "˝";
    case XKB_KEY_dead_caron:        return "ˇ";
    case XKB_KEY_dead_cedilla:      return "¸";
    case XKB_KEY_dead_ogonek:       return "˛";
    default:                        return NULL;
    }
}

void vkbd_key_label(vkbd *v, uint32_t evdev_code, int level, char *out,
                    size_t out_sz)
{
    const xkb_keysym_t *syms;
    const char *dead;
    int n;

    if (out_sz)
        out[0] = '\0';
    if (!v->keymap)
        return;

    /* xkb keycodes are evdev + 8. */
    n = xkb_keymap_key_get_syms_by_level(v->keymap, evdev_code + 8, 0, level,
                                         &syms);
    if (n <= 0 || syms[0] == XKB_KEY_NoSymbol)
        return;

    dead = dead_key_glyph(syms[0]);
    if (dead) {
        snprintf(out, out_sz, "%s", dead);
        return;
    }

    if (xkb_keysym_to_utf8(syms[0], out, out_sz) <= 0)
        out[0] = '\0';
    else if ((unsigned char)out[0] < ' ')
        out[0] = '\0'; /* control character: nothing sensible to draw */
}

bool vkbd_is_iso(vkbd *v)
{
    const xkb_keysym_t *syms;

    if (!v->keymap)
        return false;
    /* KEY_102ND is 86; the extra key left of Z on an ISO body. */
    return xkb_keymap_key_get_syms_by_level(v->keymap, 86 + 8, 0, 0, &syms) > 0;
}
