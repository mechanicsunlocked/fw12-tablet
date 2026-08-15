#include "hypr.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int hypr_request(const char *cmd, char *reply, size_t reply_sz)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    struct sockaddr_un addr;
    size_t off = 0;
    int fd;

    if (reply && reply_sz)
        reply[0] = '\0';
    if (!rt || !sig)
        return -1;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if ((size_t)snprintf(addr.sun_path, sizeof addr.sun_path,
                         "%s/hypr/%s/.socket.sock", rt, sig) >=
        sizeof addr.sun_path)
        return -1;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    if (write(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return -1;
    }

    for (;;) {
        char buf[512];
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

int hypr_get_string(const char *option, char *out, size_t out_sz)
{
    char cmd[128];
    char reply[512];
    char *p, *nl;

    if (out && out_sz)
        out[0] = '\0';

    snprintf(cmd, sizeof cmd, "getoption %s", option);
    if (hypr_request(cmd, reply, sizeof reply) < 0)
        return -1;

    /* "str: de" -- take everything after the first "str:" up to the newline.
     * An unset option answers with an empty value, which is legitimate
     * (kb_variant is normally empty) and not an error. */
    p = strstr(reply, "str:");
    if (!p)
        return -1;
    p += 4;
    while (*p == ' ' || *p == '\t')
        p++;
    nl = strchr(p, '\n');
    if (nl)
        *nl = '\0';

    /* Hyprland spells "unset" as this literal in some versions. */
    if (!strcmp(p, "[[EMPTY]]"))
        p = (char *)"";

    snprintf(out, out_sz, "%s", p);
    return 0;
}
