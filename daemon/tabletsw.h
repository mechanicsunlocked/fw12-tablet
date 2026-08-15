/* SW_TABLET_MODE switch watcher with hotplug tolerance.
 *
 * The Framework 12 exposes SW_TABLET_MODE through INT33D3 / soc_button_array.
 * That driver loses a probe race against pinctrl_tigerlake on some boots, so
 * the device may be ABSENT at startup and may APPEAR later (bind service, or a
 * manual bind), or DISAPPEAR on unbind. This watcher handles all three without
 * polling or sleeping: an inotify watch on /dev/input drives reattachment.
 *
 * See FINDINGS.md 1.3 for the measurement showing the race is non-deterministic.
 */
#ifndef FW12_TABLETSW_H
#define FW12_TABLETSW_H

#include <poll.h>

enum { TSW_UNKNOWN = -1, TSW_LAPTOP = 0, TSW_TABLET = 1 };

typedef struct {
    int dev_fd;     /* evdev fd, or -1 when no device is attached */
    int inotify_fd; /* watch on /dev/input for appear/disappear   */
    int state;      /* TSW_* */
    char path[256]; /* attached device path, "" when detached     */
} tabletsw;

/* Sets up the inotify watch and attaches if a device is already present.
 * Returns 0 on success, -1 only if inotify itself is unavailable (fatal).
 * A missing switch device is NOT an error -- that is the race, and we wait. */
int tabletsw_open(tabletsw *t);

void tabletsw_close(tabletsw *t);

/* Fill up to 2 pollfds. Returns how many were written. */
int tabletsw_pollfds(tabletsw *t, struct pollfd *out);

/* Handle whatever became readable. Returns 1 if the tablet state changed
 * (caller should act), 0 otherwise. */
int tabletsw_dispatch(tabletsw *t, struct pollfd *fds, int nfds);

#endif
