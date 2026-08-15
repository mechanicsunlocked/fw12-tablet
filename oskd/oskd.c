/* fw12-oskd -- on-screen keyboard bridge for Omarchy 4 on the Framework 12.
 *
 * Two halves, because neither mechanism can do the whole job:
 *
 *   fcitx5 (DBus)             tells us when to show and hide, and which input
 *                             method is active. This is protocol truth rather
 *                             than a focus heuristic.
 *   zwp_virtual_keyboard_v1   types. fcitx5's own ProcessKeyEvent cannot
 *                             produce capital letters on Wayland (measured;
 *                             see FINDINGS.md 3.1c), so keys go through the
 *                             compositor, which resolves shift, AltGr, dead
 *                             keys and compose properly.
 *
 * fcitx5 remains the input method on the seat, so it still sees the keys we
 * inject and ~/.XCompose keeps working.
 *
 * Draws nothing: the Quickshell plugin owns the pixels. Tablet detection and
 * rotation live in lua/fw12-tablet.lua, so nothing here can cost the user
 * auto-rotation.
 */
#include "fcitx.h"
#include "hypr.h"
#include "log.h"
#include "vkbd.h"

#include <linux/input-event-codes.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

int fw12_debug;

typedef struct {
    vkbd *kbd;
    bool selftest;
    bool selftest_done;
    bool visible;
} app;

/* Press and release one key with a modifier mask held around it. This is the
 * shape every real keypress takes: set modifiers, key down, key up, clear. */
static void tap(vkbd *v, uint32_t mods, uint32_t code)
{
    if (mods)
        vkbd_set_modifiers(v, mods, 0);
    vkbd_key(v, code, true);
    vkbd_key(v, code, false);
    if (mods)
        vkbd_set_modifiers(v, 0, 0);
    vkbd_flush(v);
}

/* Types the four things fcitx5's ProcessKeyEvent could not: an uppercase
 * letter, a lowercase letter, a layout-specific letter, and an AltGr symbol.
 * On a `de` layout this should produce "Haö€". */
static void selftest(app *a)
{
    char label[16];

    log_info("self-test: typing uppercase / lowercase / umlaut / AltGr");

    vkbd_key_label(a->kbd, KEY_H, 1, label, sizeof label);
    log_info("  KEY_H shift level -> '%s'", label);
    tap(a->kbd, VKBD_SHIFT, KEY_H);

    usleep(120 * 1000);
    tap(a->kbd, 0, KEY_A);

    usleep(120 * 1000);
    vkbd_key_label(a->kbd, KEY_SEMICOLON, 0, label, sizeof label);
    log_info("  KEY_SEMICOLON base level -> '%s'", label);
    tap(a->kbd, 0, KEY_SEMICOLON);

    usleep(120 * 1000);
    vkbd_key_label(a->kbd, KEY_E, 2, label, sizeof label);
    log_info("  KEY_E AltGr level -> '%s'", label);
    tap(a->kbd, VKBD_ALTGR, KEY_E);

    log_info("self-test done");
}

static void on_show(void *user)
{
    app *a = user;

    a->visible = true;
    log_info("show");

    if (a->selftest && !a->selftest_done) {
        a->selftest_done = true;
        usleep(400 * 1000); /* let the surface settle before typing */
        selftest(a);
    }
}

static void on_hide(void *user)
{
    app *a = user;

    a->visible = false;
    log_info("hide");
}

static void on_im_changed(const char *name, void *user)
{
    log_info("input method: %s", name);
}

/* Print what each key would show at every shift level. Runs standalone -- no
 * fcitx5, no focused text field -- so a layout can be checked in isolation.
 * This is also the data the on-screen keyboard will render. */
static void dump_legends(vkbd *v)
{
    static const struct {
        uint32_t code;
        const char *name;
    } keys[] = {
        { KEY_Q, "Q" },           { KEY_W, "W" },
        { KEY_E, "E" },           { KEY_Y, "Y" },
        { KEY_Z, "Z" },           { KEY_A, "A" },
        { KEY_S, "S" },           { KEY_LEFTBRACE, "LEFTBRACE" },
        { KEY_SEMICOLON, "SEMICOLON" }, { KEY_APOSTROPHE, "APOSTROPHE" },
        { KEY_MINUS, "MINUS" },   { KEY_EQUAL, "EQUAL" },
        { KEY_SLASH, "SLASH" },   { KEY_102ND, "102ND" },
        { KEY_2, "2" },           { KEY_GRAVE, "GRAVE" },
    };
    size_t i;

    printf("%-12s  %-6s %-6s %-6s %-6s\n", "key", "base", "shift", "altgr",
           "sh+agr");
    printf("%-12s  %-6s %-6s %-6s %-6s\n", "---", "----", "-----", "-----",
           "------");
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        char l[4][16];
        int lv;

        for (lv = 0; lv < 4; lv++)
            vkbd_key_label(v, keys[i].code, lv, l[lv], sizeof l[lv]);
        printf("%-12s  %-6s %-6s %-6s %-6s\n", keys[i].name,
               l[0][0] ? l[0] : "-", l[1][0] ? l[1] : "-",
               l[2][0] ? l[2] : "-", l[3][0] ? l[3] : "-");
    }
    printf("\nbody: %s\n", vkbd_is_iso(v) ? "ISO (has KEY_102ND)" : "ANSI");
}

int main(int argc, char **argv)
{
    app a = {0};
    fcitx_callbacks cb = {
        .on_show = on_show,
        .on_hide = on_hide,
        .on_im_changed = on_im_changed,
        .user = &a,
    };
    fcitx *f;
    char layout[64] = "", variant[64] = "", options[128] = "";
    sigset_t mask;
    int sig_fd;
    int rc = 0;

    fw12_debug = getenv("FW12_OSKD_DEBUG")
                 && !strcmp(getenv("FW12_OSKD_DEBUG"), "1");
    a.selftest = (argc > 1 && !strcmp(argv[1], "--selftest"));

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        log_errno("sigprocmask");
        return 1;
    }
    sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd < 0) {
        log_errno("signalfd");
        return 1;
    }

    /* Match the compositor's layout exactly. Uploading `us` on a `de` system
     * would turn Y into Z and lose the umlaut keys entirely. */
    hypr_get_string("input:kb_layout", layout, sizeof layout);
    hypr_get_string("input:kb_variant", variant, sizeof variant);
    hypr_get_string("input:kb_options", options, sizeof options);
    log_info("layout from Hyprland: '%s' variant '%s' options '%s'",
             layout, variant, options);

    a.kbd = vkbd_open(layout, variant, options);
    if (!a.kbd) {
        close(sig_fd);
        return 1;
    }

    /* --type 35,30,28 : press/release the given evdev keycodes in order.
     * A generic injector, so a target app can be tested without inventing a
     * new self-test for each one. */
    if (argc > 2 && !strcmp(argv[1], "--type")) {
        char *s = argv[2];
        char *tok;

        sleep(1); /* let whatever we are typing into settle */
        for (tok = strtok(s, ","); tok; tok = strtok(NULL, ",")) {
            uint32_t code = (uint32_t)atoi(tok);
            if (!code)
                continue;
            log_info("  key %u", code);
            tap(a.kbd, 0, code);
            usleep(120 * 1000);
        }
        vkbd_flush(a.kbd);
        usleep(300 * 1000);
        vkbd_close(a.kbd);
        close(sig_fd);
        return 0;
    }

    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        dump_legends(a.kbd);
        vkbd_close(a.kbd);
        close(sig_fd);
        return 0;
    }

    f = fcitx_open(&cb);
    if (!f) {
        vkbd_close(a.kbd);
        close(sig_fd);
        return 1;
    }

    for (;;) {
        struct pollfd fds[3];
        int timeout;
        int ready;

        fds[0].fd = sig_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = fcitx_fd(f);
        fds[1].events = (short)fcitx_events(f);
        fds[1].revents = 0;
        fds[2].fd = vkbd_fd(a.kbd);
        fds[2].events = POLLIN;
        fds[2].revents = 0;

        vkbd_flush(a.kbd);
        timeout = fcitx_timeout_ms(f);

        ready = poll(fds, 3, timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            log_errno("poll");
            rc = 1;
            break;
        }

        if (fds[0].revents & POLLIN) {
            struct signalfd_siginfo si;
            if (read(sig_fd, &si, sizeof si) == (ssize_t)sizeof si)
                log_info("signal %u, shutting down", si.ssi_signo);
            break;
        }

        if (fds[2].revents & POLLIN) {
            if (vkbd_dispatch(a.kbd) < 0) {
                rc = 1;
                break;
            }
        }

        if (fcitx_dispatch(f) < 0) {
            rc = 1;
            break;
        }
    }

    fcitx_close(f);
    vkbd_close(a.kbd);
    close(sig_fd);
    return rc;
}
