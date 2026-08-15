/* fw12-oskd -- on-screen keyboard bridge for Omarchy 4 on the Framework 12.
 *
 * This process does not draw anything and does not touch Wayland. It is the
 * piece that cannot be Lua or QML:
 *
 *   - Hyprland's Lua config API has no DBus and no drawing calls at all.
 *   - Quickshell exposes no generic DBus client to QML; its DBus use is
 *     internal and wrapped into fixed services.
 *
 * So this owns the fcitx5 conversation, and the Quickshell plugin owns the
 * pixels. Tablet detection and rotation are elsewhere entirely, in
 * lua/fw12-tablet.lua, so a failure here cannot cost the user auto-rotation.
 */
#include "fcitx.h"
#include "log.h"

#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

int fw12_debug;

static void on_show(void *user)
{
    log_info("show");
}

static void on_hide(void *user)
{
    log_info("hide");
}

static void on_im_changed(const char *name, void *user)
{
    log_info("input method: %s", name);
}

int main(void)
{
    fcitx_callbacks cb = {
        .on_show = on_show,
        .on_hide = on_hide,
        .on_im_changed = on_im_changed,
    };
    fcitx *f;
    sigset_t mask;
    int sig_fd;
    int rc = 0;

    fw12_debug = getenv("FW12_OSKD_DEBUG")
                 && !strcmp(getenv("FW12_OSKD_DEBUG"), "1");

    /* Block these so they arrive via signalfd and wake poll() deterministically
     * rather than racing an interrupted syscall. Clean shutdown matters more
     * than usual here: fcitx5 is left without a UI if we do not hand the mode
     * back on the way out. */
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

    f = fcitx_open(&cb);
    if (!f) {
        close(sig_fd);
        return 1;
    }

    for (;;) {
        struct pollfd fds[2];
        int timeout;
        int ready;

        fds[0].fd = sig_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = fcitx_fd(f);
        fds[1].events = (short)fcitx_events(f);
        fds[1].revents = 0;

        timeout = fcitx_timeout_ms(f);

        ready = poll(fds, 2, timeout);
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

        /* Always dispatch, even on timeout: sd_bus has its own timers. */
        if (fcitx_dispatch(f) < 0) {
            rc = 1;
            break;
        }
    }

    fcitx_close(f);
    close(sig_fd);
    return rc;
}
