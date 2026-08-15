#include "ipc.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct ipc {
    int listen_fd;
    int client_fd;
    ipc_callbacks cb;
    char buf[4096];
    size_t len;
    char path[108]; /* sized to sockaddr_un.sun_path */
};

static void sock_path(char *out, size_t n)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");

    snprintf(out, n, "%s/fw12-oskd.sock", rt ? rt : "/tmp");
}

ipc *ipc_open(const ipc_callbacks *cb)
{
    struct sockaddr_un addr;
    ipc *s = calloc(1, sizeof *s);

    if (!s)
        return NULL;
    s->client_fd = -1;
    if (cb)
        s->cb = *cb;

    sock_path(s->path, sizeof s->path);

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", s->path);

    /* A socket left behind by a previous run would make bind() fail with
     * EADDRINUSE even though nothing is listening. */
    unlink(s->path);

    s->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (s->listen_fd < 0) {
        log_errno("socket");
        free(s);
        return NULL;
    }
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        log_err("bind %s: %s", s->path, strerror(errno));
        close(s->listen_fd);
        free(s);
        return NULL;
    }
    if (listen(s->listen_fd, 1) < 0) {
        log_errno("listen");
        close(s->listen_fd);
        unlink(s->path);
        free(s);
        return NULL;
    }

    log_info("listening on %s", s->path);
    return s;
}

void ipc_close(ipc *s)
{
    if (!s)
        return;
    if (s->client_fd >= 0)
        close(s->client_fd);
    if (s->listen_fd >= 0)
        close(s->listen_fd);
    unlink(s->path);
    free(s);
}

int ipc_listen_fd(ipc *s) { return s ? s->listen_fd : -1; }
int ipc_client_fd(ipc *s) { return s ? s->client_fd : -1; }
bool ipc_has_client(ipc *s) { return s && s->client_fd >= 0; }

void ipc_accept(ipc *s)
{
    int fd = accept4(s->listen_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);

    if (fd < 0)
        return;

    /* One client only. A new connection replaces the old, so restarting the
     * shell reconnects rather than queueing behind a dead socket. */
    if (s->client_fd >= 0) {
        log_dbg("replacing the previous client");
        close(s->client_fd);
    }
    s->client_fd = fd;
    s->len = 0;
    log_info("shell connected");

    if (s->cb.on_connect)
        s->cb.on_connect(s->cb.user);
}

static void drop_client(ipc *s)
{
    if (s->client_fd < 0)
        return;
    close(s->client_fd);
    s->client_fd = -1;
    s->len = 0;
    log_info("shell disconnected");
}

/* Pull an integer field out of a JSON object. Deliberately minimal: this
 * protocol has both ends in this repo, the messages are one level deep, and a
 * JSON library would be a dependency for four fields. */
static bool json_int(const char *line, const char *key, uint32_t *out)
{
    char pat[32];
    const char *p;

    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(line, pat);
    if (!p)
        return false;
    p = strchr(p, ':');
    if (!p)
        return false;
    p++;
    while (*p == ' ')
        p++;
    if (*p < '0' || *p > '9')
        return false;
    *out = (uint32_t)strtoul(p, NULL, 10);
    return true;
}

static void handle_line(ipc *s, const char *line)
{
    uint32_t code = 0, mods = 0, depressed = 0, locked = 0;

    if (!*line)
        return;
    log_dbg("shell -> %s", line);

    if (strstr(line, "\"key\"")) {
        if (!json_int(line, "code", &code))
            return;
        json_int(line, "mods", &mods);
        if (s->cb.on_key)
            s->cb.on_key(code, mods, s->cb.user);
        return;
    }
    if (strstr(line, "\"mods\"")) {
        json_int(line, "depressed", &depressed);
        json_int(line, "locked", &locked);
        if (s->cb.on_mods)
            s->cb.on_mods(depressed, locked, s->cb.user);
        return;
    }
}

void ipc_dispatch(ipc *s)
{
    ssize_t r;
    char *nl;

    if (s->client_fd < 0)
        return;

    r = read(s->client_fd, s->buf + s->len, sizeof s->buf - s->len - 1);
    if (r == 0) {
        drop_client(s);
        return;
    }
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return;
        drop_client(s);
        return;
    }

    s->len += (size_t)r;
    s->buf[s->len] = '\0';

    while ((nl = memchr(s->buf, '\n', s->len)) != NULL) {
        size_t line_len = (size_t)(nl - s->buf);
        size_t rest;

        *nl = '\0';
        handle_line(s, s->buf);

        rest = s->len - line_len - 1;
        memmove(s->buf, nl + 1, rest);
        s->len = rest;
        s->buf[s->len] = '\0';
    }

    if (s->len == sizeof s->buf - 1) {
        log_warn("oversized line from the shell, discarding");
        s->len = 0;
    }
}

void ipc_send(ipc *s, const char *line)
{
    size_t len;
    ssize_t w;

    if (!s || s->client_fd < 0)
        return;

    len = strlen(line);
    w = write(s->client_fd, line, len);
    if (w < 0 || (size_t)w != len) {
        /* Short or failed write means the shell went away mid-message; the
         * client would be left reading a truncated line. */
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            drop_client(s);
    }
}
