#include "hyprevt.h"
#include "log.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct hyprevt {
    int fd;
    void (*on_change)(void *user);
    void *user;
    char buf[4096];
    size_t len; /* bytes buffered, awaiting a complete line */
};

hyprevt *hyprevt_open(void (*on_change)(void *user), void *user)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    struct sockaddr_un addr;
    hyprevt *h;

    if (!rt || !sig) {
        log_warn("no Hyprland instance in the environment; "
                 "layout changes will not be followed");
        return NULL;
    }

    h = calloc(1, sizeof *h);
    if (!h)
        return NULL;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if ((size_t)snprintf(addr.sun_path, sizeof addr.sun_path,
                         "%s/hypr/%s/.socket2.sock", rt, sig) >=
        sizeof addr.sun_path) {
        log_warn("Hyprland event socket path too long");
        free(h);
        return NULL;
    }

    h->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (h->fd < 0) {
        log_errno("socket");
        free(h);
        return NULL;
    }
    if (connect(h->fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        log_warn("cannot reach the Hyprland event socket: %s", strerror(errno));
        close(h->fd);
        free(h);
        return NULL;
    }

    h->on_change = on_change;
    h->user = user;
    log_info("following Hyprland layout changes");
    return h;
}

void hyprevt_close(hyprevt *h)
{
    if (!h)
        return;
    if (h->fd >= 0)
        close(h->fd);
    free(h);
}

int hyprevt_fd(hyprevt *h) { return h ? h->fd : -1; }

/* Is this activelayout event about a keyboard a person is typing on?
 *
 * The payload is "activelayout>>DEVICE,LAYOUTNAME". Hyprland emits one for
 * every keyboard including the virtual ones -- ours and fcitx5's -- so typing
 * a single character produced two of these. Each one made the daemon re-read
 * three settings from Hyprland over its command socket, six blocking round
 * trips per keystroke, on the same loop that has to read the next keypress.
 * The result was a keyboard where pressing one key made the next one miss.
 *
 * A virtual keyboard cannot have its layout changed by a user, so an event
 * about one is never news. */
static bool is_real_keyboard(const char *data)
{
    return strncmp(data, "hl-virtual-keyboard", 19) != 0;
}

static void handle_line(hyprevt *h, const char *line)
{
    /* Lines are "event>>data". We only care that a layout may have moved,
     * not what it moved to -- the authoritative value is re-queried. */
    if (strncmp(line, "activelayout>>", 14) == 0) {
        if (!is_real_keyboard(line + 14)) {
            log_dbg("ignoring layout event for a virtual keyboard: %s", line);
            return;
        }
    } else if (strncmp(line, "configreloaded>>", 16) != 0) {
        return;
    }

    log_dbg("hyprland event: %s", line);
    if (h->on_change)
        h->on_change(h->user);
}

int hyprevt_dispatch(hyprevt *h)
{
    ssize_t r;
    char *nl;

    if (!h || h->fd < 0)
        return 0;

    r = read(h->fd, h->buf + h->len, sizeof h->buf - h->len - 1);
    if (r == 0) {
        log_warn("Hyprland closed the event socket");
        return -1;
    }
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        log_errno("read from Hyprland event socket");
        return -1;
    }

    h->len += (size_t)r;
    h->buf[h->len] = '\0';

    /* Process every complete line, keeping any partial tail for next time. */
    while ((nl = memchr(h->buf, '\n', h->len)) != NULL) {
        size_t line_len = (size_t)(nl - h->buf);
        size_t rest;

        *nl = '\0';
        handle_line(h, h->buf);

        rest = h->len - line_len - 1;
        memmove(h->buf, nl + 1, rest);
        h->len = rest;
        h->buf[h->len] = '\0';
    }

    /* A single line longer than the buffer would wedge us; drop it rather
     * than spin. Hyprland's events are far shorter than this. */
    if (h->len == sizeof h->buf - 1) {
        log_warn("oversized Hyprland event line, discarding");
        h->len = 0;
    }
    return 0;
}
