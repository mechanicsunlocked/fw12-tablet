#include "hypr.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* One Lua statement does the whole job: pick the internal panel, keep its
 * current scale, set the transform, and move touch + stylus with it. Doing the
 * monitor selection in Lua rather than parsing `hyprctl monitors` in C removes
 * the only text-parsing in this daemon, and it is the part that would rot when
 * Hyprland changes its human-readable output.
 *
 * %d appears four times: monitor, touchdevice, tablet, and the returned name. */
static const char LUA_SET_TRANSFORM[] =
    "local ms = hl.get_monitors() "
    "local t = nil "
    "for _, m in ipairs(ms) do if m.name:sub(1,3) == \"eDP\" then t = m break end end "
    "if not t then t = ms[1] end "
    "if t then hl.monitor({output=t.name, mode=\"preferred\", position=\"auto\", "
    "scale=t.scale, transform=%d}) end "
    "hl.config({input={touchdevice={transform=%d}, tablet={transform=%d}}}) "
    "return t and t.name or \"?\"";

/* Legacy hyprlang fallback. Omarchy 4 uses the Lua config manager, where
 * `keyword` is refused outright ("keyword can't work with non-legacy parsers.
 * Use eval."). Kept so this still works on a hyprlang config. */
static const char KEYWORD_FALLBACK[] =
    "[[BATCH]]keyword input:touchdevice:transform %d;"
    "keyword input:tablet:transform %d";

/* Send one command and collect the reply. Returns 0 if Hyprland answered. */
static int hypr_request(const char *cmd, char *reply, size_t reply_sz)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    struct sockaddr_un addr;
    size_t off = 0;
    int fd;

    if (reply && reply_sz)
        reply[0] = '\0';

    if (!rt || !sig) {
        log_err("XDG_RUNTIME_DIR or HYPRLAND_INSTANCE_SIGNATURE unset; "
                "not running inside a Hyprland session?");
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if ((size_t)snprintf(addr.sun_path, sizeof addr.sun_path,
                         "%s/hypr/%s/.socket.sock", rt, sig) >=
        sizeof addr.sun_path) {
        log_err("Hyprland socket path too long");
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        log_errno("socket");
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        log_err("connect %s: %s", addr.sun_path, strerror(errno));
        close(fd);
        return -1;
    }
    if (write(fd, cmd, strlen(cmd)) < 0) {
        log_errno("write to Hyprland socket");
        close(fd);
        return -1;
    }

    /* Hyprland replies to every command; read it fully so we can tell an
     * accepted command from a rejected one. */
    for (;;) {
        char buf[1024];
        ssize_t r = read(fd, buf, sizeof buf);

        if (r <= 0)
            break;
        if (reply && off + (size_t)r < reply_sz) {
            memcpy(reply + off, buf, (size_t)r);
            off += (size_t)r;
            reply[off] = '\0';
        }
    }
    close(fd);
    return 0;
}

int hypr_set_transform(int transform)
{
    char lua[900];
    char cmd[1024];
    char reply[512];

    snprintf(lua, sizeof lua, LUA_SET_TRANSFORM, transform, transform, transform);
    snprintf(cmd, sizeof cmd, "eval %s", lua);

    if (hypr_request(cmd, reply, sizeof reply) < 0)
        return -1;

    /* The Lua config manager answers with the monitor name we returned.
     * A hyprlang config answers "eval is only supported with the lua config
     * manager" -- fall back rather than silently doing nothing. */
    if (strstr(reply, "only supported with the lua config manager")) {
        log_dbg("legacy hyprlang config detected; using keyword fallback");
        snprintf(cmd, sizeof cmd, KEYWORD_FALLBACK, transform, transform);
        if (hypr_request(cmd, reply, sizeof reply) < 0)
            return -1;
        log_info("transform %d applied (legacy keyword path)", transform);
        return 0;
    }

    if (strstr(reply, "Lua error") || strstr(reply, "invalid")) {
        log_err("Hyprland rejected the rotation: %s", reply);
        return -1;
    }

    log_info("transform %d applied to %s", transform, reply[0] ? reply : "?");
    return 0;
}
