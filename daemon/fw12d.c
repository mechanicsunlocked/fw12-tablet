/* fw12d -- Framework Laptop 12 tablet-mode daemon for Omarchy / Hyprland.
 *
 * Single-threaded poll() loop, no threads, no timers beyond timerfd, no
 * subprocesses, no sleeps. Runs unprivileged: membership in group `input` is
 * enough to read the tablet-mode switch.
 *
 * See ARCHITECTURE.md for the design and FINDINGS.md for the hardware
 * measurements it is built on.
 */
#include "log.h"
#include "tabletsw.h"

#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

int fw12_debug;

#define MAX_FDS 8

int main(void)
{
    tabletsw sw;
    sigset_t mask;
    int sig_fd;
    int rc = 0;

    fw12_debug = getenv("FW12D_DEBUG") && !strcmp(getenv("FW12D_DEBUG"), "1");

    /* Block the signals we want to receive via signalfd, so they wake poll()
     * deterministically instead of racing an interrupted syscall. */
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

    if (tabletsw_open(&sw) < 0)
        return 1;

    log_info("started (tablet mode: %s)",
             sw.state == TSW_TABLET   ? "tablet"
             : sw.state == TSW_LAPTOP ? "laptop"
                                      : "unknown -- no switch device");

    for (;;) {
        struct pollfd fds[MAX_FDS];
        int n = 0;
        int ready;

        fds[n].fd = sig_fd;
        fds[n].events = POLLIN;
        n++;

        n += tabletsw_pollfds(&sw, fds + n);

        ready = poll(fds, n, -1);
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

        if (tabletsw_dispatch(&sw, fds + 1, n - 1)) {
            /* Rotation and the OSK hook onto this transition; for now the
             * state change is the observable behaviour. */
            log_info("tablet mode: %s", sw.state == TSW_TABLET ? "ON" : "OFF");
        }
    }

    tabletsw_close(&sw);
    close(sig_fd);
    return rc;
}
