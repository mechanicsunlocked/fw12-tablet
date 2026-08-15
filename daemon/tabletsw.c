#include "tabletsw.h"
#include "log.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define NLONGS(x) (((x) / (sizeof(long) * 8)) + 1)
#define test_bit(b, a) \
    (((a)[(b) / (sizeof(long) * 8)] >> ((b) % (sizeof(long) * 8))) & 1UL)

static int has_tablet_sw(int fd)
{
    unsigned long ev[NLONGS(EV_MAX)] = {0}, sw[NLONGS(SW_MAX)] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) < 0 || !test_bit(EV_SW, ev))
        return 0;
    if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw) < 0)
        return 0;
    return test_bit(SW_TABLET_MODE, sw);
}

static int read_level(int fd)
{
    unsigned long st[NLONGS(SW_MAX)] = {0};

    if (ioctl(fd, EVIOCGSW(sizeof st), st) < 0)
        return TSW_UNKNOWN;
    return test_bit(SW_TABLET_MODE, st) ? TSW_TABLET : TSW_LAPTOP;
}

/* Scan /dev/input for the first device advertising SW_TABLET_MODE.
 * Deliberately scans rather than trusting a fixed path: the event number is
 * not stable (event5 and event7 both observed across boots, FINDINGS.md 1.4). */
static void try_attach(tabletsw *t)
{
    DIR *d;
    struct dirent *e;
    char path[sizeof t->path];

    if (t->dev_fd >= 0)
        return;

    d = opendir("/dev/input");
    if (!d)
        return;

    while ((e = readdir(d))) {
        int fd;

        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        /* Precision bounds d_name so the compiler can see this cannot
         * truncate; real event node names are far shorter than this. */
        snprintf(path, sizeof path, "/dev/input/%.240s", e->d_name);

        fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (!has_tablet_sw(fd)) {
            close(fd);
            continue;
        }

        t->dev_fd = fd;
        snprintf(t->path, sizeof t->path, "%s", path);
        /* EVIOCGSW, not "wait for an edge": we may start already folded. */
        t->state = read_level(fd);
        log_info("tablet switch attached: %s (state=%s)", t->path,
                 t->state == TSW_TABLET ? "tablet" : "laptop");
        break;
    }
    closedir(d);
}

static void detach(tabletsw *t)
{
    if (t->dev_fd < 0)
        return;
    log_warn("tablet switch disappeared (%s); waiting for it to return", t->path);
    close(t->dev_fd);
    t->dev_fd = -1;
    t->path[0] = '\0';
    t->state = TSW_UNKNOWN;
}

int tabletsw_open(tabletsw *t)
{
    t->dev_fd = -1;
    t->state = TSW_UNKNOWN;
    t->path[0] = '\0';

    t->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (t->inotify_fd < 0) {
        log_errno("inotify_init1");
        return -1;
    }
    if (inotify_add_watch(t->inotify_fd, "/dev/input",
                          IN_CREATE | IN_DELETE | IN_ATTRIB) < 0) {
        log_errno("inotify_add_watch(/dev/input)");
        close(t->inotify_fd);
        t->inotify_fd = -1;
        return -1;
    }

    try_attach(t);
    if (t->dev_fd < 0)
        log_warn("no SW_TABLET_MODE device yet "
                 "(soc_button_array probe race, or missing group `input`); waiting");
    return 0;
}

void tabletsw_close(tabletsw *t)
{
    if (t->dev_fd >= 0)
        close(t->dev_fd);
    if (t->inotify_fd >= 0)
        close(t->inotify_fd);
    t->dev_fd = t->inotify_fd = -1;
}

int tabletsw_pollfds(tabletsw *t, struct pollfd *out)
{
    int n = 0;

    if (t->inotify_fd >= 0) {
        out[n].fd = t->inotify_fd;
        out[n].events = POLLIN;
        n++;
    }
    if (t->dev_fd >= 0) {
        out[n].fd = t->dev_fd;
        out[n].events = POLLIN;
        n++;
    }
    return n;
}

int tabletsw_dispatch(tabletsw *t, struct pollfd *fds, int nfds)
{
    int changed = 0;
    int i;

    for (i = 0; i < nfds; i++) {
        if (fds[i].fd == t->inotify_fd && (fds[i].revents & POLLIN)) {
            /* Drain; we don't care which file changed, only that something did. */
            char buf[4096];
            while (read(t->inotify_fd, buf, sizeof buf) > 0)
                ;
            if (t->dev_fd < 0) {
                int before = t->state;
                try_attach(t);
                if (t->dev_fd >= 0 && t->state != before)
                    changed = 1;
            }
            continue;
        }

        if (t->dev_fd >= 0 && fds[i].fd == t->dev_fd) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                detach(t);
                continue;
            }
            if (!(fds[i].revents & POLLIN))
                continue;

            for (;;) {
                struct input_event ev;
                ssize_t r = read(t->dev_fd, &ev, sizeof ev);

                if (r == (ssize_t)sizeof ev) {
                    if (ev.type == EV_SW && ev.code == SW_TABLET_MODE) {
                        int next = ev.value ? TSW_TABLET : TSW_LAPTOP;
                        if (next != t->state) {
                            t->state = next;
                            changed = 1;
                            log_dbg("SW_TABLET_MODE -> %d", next);
                        }
                    }
                    continue;
                }
                if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break; /* drained */
                /* ENODEV on unbind, or a short read we cannot interpret. */
                detach(t);
                break;
            }
        }
    }
    return changed;
}
