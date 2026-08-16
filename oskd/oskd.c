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
#include "hyprevt.h"
#include "ipc.h"
#include "keys.h"
#include "log.h"
#include "vkbd.h"

#include <linux/input-event-codes.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

int fw12_debug;

/* How often to confirm fcitx5 is still using us. It reverts silently and there
 * is no signal to watch, so this interval is how long auto-show can stay dead
 * before it repairs itself. Three seconds is short enough that a user reaching
 * for a text field twice does not notice, and long enough that the cost is a
 * local DBus round trip every three seconds. */
#define FCITX_CHECK_MS 3000

/* CLOCK_MONOTONIC: this must not jump when the clock is set, and the machine
 * this runs on suspends and resumes constantly. */
static int64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

typedef struct {
    vkbd *kbd;
    hyprevt *evt;
    ipc *sock;
    char layout[64];
    char variant[64];
    bool selftest;
    bool selftest_done;
    bool visible;
    fcitx *fcitx;
    int64_t last_inject;
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

/* JSON string escaping. Key labels are arbitrary UTF-8 from the keymap and do
 * include the quote and backslash characters on most layouts. */
static void json_escape(const char *in, char *out, size_t out_sz)
{
    size_t o = 0;

    for (; *in && o + 7 < out_sz; in++) {
        unsigned char c = (unsigned char)*in;

        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            o += (size_t)snprintf(out + o, out_sz - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/* Send the whole keyboard: geometry and, for each key, what it prints at each
 * of the four shift levels. The plugin renders this and nothing more, so a
 * key's label can never disagree with what it types. */
static void send_keymap(app *a)
{
    const keyrow *rows;
    int nrows, r, k, lv;
    char msg[16384];
    size_t off = 0;
    bool iso = vkbd_is_iso(a->kbd);

    if (!ipc_has_client(a->sock))
        return;

    nrows = keys_rows(iso, &rows);

    off += (size_t)snprintf(msg + off, sizeof msg - off,
                            "{\"t\":\"keymap\",\"layout\":\"%s\",\"variant\":\"%s\","
                            "\"iso\":%s,\"rows\":[",
                            a->layout, a->variant, iso ? "true" : "false");

    for (r = 0; r < nrows; r++) {
        off += (size_t)snprintf(msg + off, sizeof msg - off, "%s[",
                                r ? "," : "");
        for (k = 0; k < rows[r].count; k++) {
            const keydef *kd = &rows[r].keys[k];
            char lbl[4][16], esc[4][48];

            for (lv = 0; lv < 4; lv++) {
                if (kd->label)
                    snprintf(lbl[lv], sizeof lbl[lv], "%s", kd->label);
                else
                    vkbd_key_label(a->kbd, kd->code, lv, lbl[lv],
                                   sizeof lbl[lv]);
                json_escape(lbl[lv], esc[lv], sizeof esc[lv]);
            }

            off += (size_t)snprintf(
                msg + off, sizeof msg - off,
                "%s{\"code\":%u,\"w\":%u,\"type\":%d,\"mod\":%u,"
                "\"l\":[\"%s\",\"%s\",\"%s\",\"%s\"]}",
                k ? "," : "", kd->code, kd->width, (int)kd->type, kd->modbit,
                esc[0], esc[1], esc[2], esc[3]);
        }
        off += (size_t)snprintf(msg + off, sizeof msg - off, "]");
    }

    off += (size_t)snprintf(msg + off, sizeof msg - off, "]}\n");

    if (off >= sizeof msg) {
        log_err("keymap message truncated; not sending");
        return;
    }
    ipc_send(a->sock, msg);
    log_dbg("sent keymap (%zu bytes, %s)", off, iso ? "ISO" : "ANSI");
}

static void on_ipc_connect(void *user)
{
    send_keymap((app *)user);
}

static void on_ipc_key(uint32_t code, uint32_t mods, void *user)
{
    app *a = user;

    a->last_inject = now_ms();
    tap(a->kbd, mods, code);
}

static void on_ipc_mods(uint32_t depressed, uint32_t locked, void *user)
{
    app *a = user;

    vkbd_set_modifiers(a->kbd, depressed, locked);
}

static void on_show(void *user)
{
    app *a = user;

    a->visible = true;
    ipc_send(a->sock, "{\"t\":\"show\"}\n");
    log_info("show");

    if (a->selftest && !a->selftest_done) {
        a->selftest_done = true;
        usleep(400 * 1000); /* let the surface settle before typing */
        selftest(a);
    }
}

/* fcitx5 hides the on-screen keyboard the moment it sees a key event, on the
 * assumption that the user reached for the hardware keyboard. Every key we
 * type is a key event, so without this the keyboard closed itself after each
 * tap -- and fcitx5 left virtual-keyboard mode with it, so it took a few
 * seconds to come back. Measured: one tap produced HideVirtualKeyboard,
 * `activelayout>>hl-virtual-keyboard-fcitx5`, and a fallback to classicui.
 *
 * This is the one place a time window is the honest answer. fcitx5's hide
 * carries no reason, and DBus replies do not arrive in step with Wayland
 * events, so "did we cause this?" can only be answered by when it arrived.
 * The window is short enough that a real hide -- focus leaving, which needs a
 * tap somewhere else -- cannot fit inside it. */
#define SELF_HIDE_MS 300

static void on_hide(void *user)
{
    app *a = user;

    if (a->last_inject && now_ms() - a->last_inject < SELF_HIDE_MS) {
        log_dbg("ignoring hide caused by our own keystroke");
        /* Say we are still here, but do NOT re-register: registering means
         * ShowVirtualKeyboard, fcitx5 echoes a show back, and inside this
         * 300 ms window that show can produce another hide. It did -- one
         * session logged 249 shows against 26 hides. This call carries no
         * such echo. */
        fcitx_set_visible(a->fcitx, true);
        return;
    }

    a->visible = false;
    ipc_send(a->sock, "{\"t\":\"hide\"}\n");
    log_info("hide");
}

static bool on_screen(void *user)
{
    const app *a = user;

    return a->visible;
}

static void on_im_changed(const char *name, void *user)
{
    log_info("input method: %s", name);
}

/* Hyprland says the layout may have moved. Re-read the authoritative value and
 * rebuild the keymap. The event payload carries a display name ("German"), not
 * an xkb code, so it serves only as a trigger. */
static void on_layout_change(void *user)
{
    app *a = user;
    char layout[64] = "", variant[64] = "", options[128] = "";

    hypr_get_string("input:kb_layout", layout, sizeof layout);
    hypr_get_string("input:kb_variant", variant, sizeof variant);
    hypr_get_string("input:kb_options", options, sizeof options);

    if (vkbd_set_layout(a->kbd, layout, variant, options) == 1) {
        snprintf(a->layout, sizeof a->layout, "%s", layout);
        snprintf(a->variant, sizeof a->variant, "%s", variant);
        send_keymap(a); /* legends must follow the layout */
    }
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
        .on_screen = on_screen,
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
    snprintf(a.layout, sizeof a.layout, "%s", layout);
    snprintf(a.variant, sizeof a.variant, "%s", variant);

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
    /* on_hide needs to re-assert virtual-keyboard mode, so the callbacks need
     * a way back to the connection that delivered them. */
    a.fcitx = f;

    /* Not fatal if unavailable: without it the keyboard simply keeps the
     * layout it started with. */
    a.evt = hyprevt_open(on_layout_change, &a);

    {
        ipc_callbacks icb = {
            .on_key = on_ipc_key,
            .on_mods = on_ipc_mods,
            .on_connect = on_ipc_connect,
            .user = &a,
        };
        a.sock = ipc_open(&icb);
    }

    int64_t last_fcitx_check = now_ms();

    for (;;) {
        struct pollfd fds[6];
        int nfds = 3;
        int timeout;
        int ready;
        int64_t now;

        fds[0].fd = sig_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = fcitx_fd(f);
        fds[1].events = (short)fcitx_events(f);
        fds[1].revents = 0;
        fds[2].fd = vkbd_fd(a.kbd);
        fds[2].events = POLLIN;
        fds[2].revents = 0;

        int evt_i = -1, lis_i = -1, cli_i = -1;

        if (a.evt) {
            evt_i = nfds;
            fds[nfds].fd = hyprevt_fd(a.evt);
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }
        if (a.sock) {
            lis_i = nfds;
            fds[nfds].fd = ipc_listen_fd(a.sock);
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
            if (ipc_has_client(a.sock)) {
                cli_i = nfds;
                fds[nfds].fd = ipc_client_fd(a.sock);
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                nfds++;
            }
        }

        vkbd_flush(a.kbd);

        /* Cap the wait so the fcitx5 check below runs on schedule even when
         * nothing else is happening -- which is exactly when the revert would
         * otherwise go unnoticed. */
        timeout = fcitx_timeout_ms(f);
        if (timeout < 0 || timeout > FCITX_CHECK_MS)
            timeout = FCITX_CHECK_MS;

        ready = poll(fds, nfds, timeout);
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

        if (evt_i >= 0 && (fds[evt_i].revents & (POLLIN | POLLHUP))) {
            if (hyprevt_dispatch(a.evt) < 0) {
                /* Keep running without layout following rather than dying. */
                hyprevt_close(a.evt);
                a.evt = NULL;
            }
        }
        if (lis_i >= 0 && (fds[lis_i].revents & POLLIN))
            ipc_accept(a.sock);
        if (cli_i >= 0 && (fds[cli_i].revents & (POLLIN | POLLHUP)))
            ipc_dispatch(a.sock);

        if (fcitx_dispatch(f) < 0) {
            rc = 1;
            break;
        }

        /* Not once per poll wakeup: typing produces a burst of them, and this
         * is a round trip to another process. */
        now = now_ms();
        if (now - last_fcitx_check >= FCITX_CHECK_MS) {
            last_fcitx_check = now;
            fcitx_reconcile(f);
        }
    }

    ipc_close(a.sock);
    hyprevt_close(a.evt);
    fcitx_close(f);
    vkbd_close(a.kbd);
    close(sig_fd);
    return rc;
}
