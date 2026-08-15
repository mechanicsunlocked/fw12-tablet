/* Unix socket between the daemon and the Quickshell plugin.
 *
 * Newline-delimited JSON, because QML parses it with JSON.parse and nothing
 * else is needed. The daemon generates JSON (easy in C) and hand-parses the
 * few small messages coming back, which avoids a JSON library for a protocol
 * we own both ends of.
 *
 * Daemon -> plugin:
 *   {"t":"keymap","layout":"de","variant":"","iso":true,"rows":[[{...}]]}
 *   {"t":"show"}
 *   {"t":"hide"}
 *
 * Plugin -> daemon:
 *   {"t":"key","code":30,"mods":1}     press+release with mods held
 *   {"t":"mods","depressed":1,"locked":0}
 *
 * Exactly one client is expected -- the shell. A second connection replaces
 * the first rather than being queued, so a shell restart reconnects cleanly
 * instead of wedging behind a dead socket.
 */
#ifndef FW12_IPC_H
#define FW12_IPC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* The plugin tapped a key: press and release with `mods` held around it. */
    void (*on_key)(uint32_t code, uint32_t mods, void *user);
    /* The plugin latched or released modifiers. */
    void (*on_mods)(uint32_t depressed, uint32_t locked, void *user);
    /* A client connected and needs the current keymap. */
    void (*on_connect)(void *user);
    void *user;
} ipc_callbacks;

typedef struct ipc ipc;

/* Listen on $XDG_RUNTIME_DIR/fw12-oskd.sock. Returns NULL on failure. */
ipc *ipc_open(const ipc_callbacks *cb);
void ipc_close(ipc *s);

/* Two fds to poll: the listener, and the connected client (-1 when none). */
int ipc_listen_fd(ipc *s);
int ipc_client_fd(ipc *s);

/* Handle readability on either fd. */
void ipc_accept(ipc *s);
void ipc_dispatch(ipc *s);

/* Send a line to the client, if any. Silently does nothing when no client is
 * connected -- the keyboard UI simply is not running, which is normal. */
void ipc_send(ipc *s, const char *line);
bool ipc_has_client(ipc *s);

#endif
