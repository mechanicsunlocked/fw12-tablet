/* Phase 1 probe: learn fcitx5's virtual-keyboard client contract empirically.
 *
 * fcitx5's virtualkeyboard addon calls into a client object at
 * /org/fcitx/virtualkeyboard/impanel on whoever owns the bus name
 * org.fcitx.Fcitx5.VirtualKeyboard. The method NAMES are visible in
 * libvirtualkeyboard.so (ShowVirtualKeyboard, UpdateCandidateArea,
 * NotifyIMActivated, ...) but their SIGNATURES are not, and guessing them
 * would produce silent mismatches.
 *
 * So: own the name, install a message filter that logs every incoming message
 * with its member and signature, and reply to method calls so fcitx5 does not
 * see errors. Then focus a text field and watch what actually arrives.
 *
 *   cc -O2 -o vkspy vkspy.c $(pkg-config --cflags --libs libsystemd)
 */
#include <systemd/sd-bus.h>

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define VK_NAME  "org.fcitx.Fcitx5.VirtualKeyboard"
#define VK_PATH  "/org/fcitx/virtualkeyboard/impanel"
#define VK_IFACE "org.fcitx.Fcitx5.VirtualKeyboard1"

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

/* Print a message's body generically, so we learn the shape without knowing it
 * in advance. Only handles the scalar types fcitx5 is likely to use. */
static void dump_body(sd_bus_message *m)
{
    const char *sig = sd_bus_message_get_signature(m, 1);

    if (!sig || !*sig) {
        printf("      (no arguments)\n");
        return;
    }
    printf("      signature: \"%s\"\n", sig);

    for (const char *p = sig; *p; p++) {
        int r = 0;
        switch (*p) {
        case 's': {
            const char *v = NULL;
            r = sd_bus_message_read_basic(m, 's', &v);
            if (r >= 0) printf("      arg s: \"%s\"\n", v ? v : "(null)");
            break;
        }
        case 'b': {
            int v = 0;
            r = sd_bus_message_read_basic(m, 'b', &v);
            if (r >= 0) printf("      arg b: %s\n", v ? "true" : "false");
            break;
        }
        case 'i': {
            int32_t v = 0;
            r = sd_bus_message_read_basic(m, 'i', &v);
            if (r >= 0) printf("      arg i: %d\n", v);
            break;
        }
        case 'u': {
            uint32_t v = 0;
            r = sd_bus_message_read_basic(m, 'u', &v);
            if (r >= 0) printf("      arg u: %u\n", v);
            break;
        }
        default:
            printf("      arg %c: (not decoded -- container or unhandled)\n", *p);
            return; /* stop: we would desync reading a container blindly */
        }
        if (r < 0) {
            printf("      (read failed at '%c': %s)\n", *p, strerror(-r));
            return;
        }
    }
}

static int on_message(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
    const char *iface = sd_bus_message_get_interface(m);
    const char *member = sd_bus_message_get_member(m);
    const char *path = sd_bus_message_get_path(m);

    if (!sd_bus_message_is_method_call(m, NULL, NULL))
        return 0;
    if (!path || strcmp(path, VK_PATH) != 0)
        return 0;

    printf("  <- CALL %s.%s\n", iface ? iface : "(none)", member ? member : "(none)");
    dump_body(m);
    fflush(stdout);

    /* Reply empty so fcitx5 does not log an error and give up on us. */
    sd_bus_reply_method_return(m, "");
    return 1; /* handled */
}

int main(void)
{
    sd_bus *bus = NULL;
    sd_bus_slot *slot = NULL;
    int r;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_open_user: %s\n", strerror(-r));
        return 1;
    }

    r = sd_bus_add_filter(bus, &slot, on_message, NULL);
    if (r < 0) {
        fprintf(stderr, "sd_bus_add_filter: %s\n", strerror(-r));
        return 1;
    }

    r = sd_bus_request_name(bus, VK_NAME, SD_BUS_NAME_REPLACE_EXISTING);
    if (r < 0) {
        fprintf(stderr, "request_name %s: %s\n", VK_NAME, strerror(-r));
        return 1;
    }
    printf("owning %s, listening on %s\n", VK_NAME, VK_PATH);

    /* Ask fcitx5 to switch its UI to the on-screen-keyboard backend. Holding
     * the name is not enough on its own -- both are required. */
    r = sd_bus_call_method(bus, "org.fcitx.Fcitx5", "/virtualkeyboard",
                           "org.fcitx.Fcitx.VirtualKeyboard1",
                           "ShowVirtualKeyboard", NULL, NULL, "");
    printf("ShowVirtualKeyboard: %s\n", r < 0 ? strerror(-r) : "ok");
    printf("--- now focus a text field; Ctrl-C to stop ---\n");
    fflush(stdout);

    while (!stop) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) {
            fprintf(stderr, "sd_bus_process: %s\n", strerror(-r));
            break;
        }
        if (r > 0)
            continue;
        r = sd_bus_wait(bus, 500000); /* 0.5 s */
        if (r < 0 && r != -EINTR) {
            fprintf(stderr, "sd_bus_wait: %s\n", strerror(-r));
            break;
        }
    }

    /* Leave fcitx5 with a UI: releasing the name while it is in
     * virtualkeyboard mode leaves CurrentUI empty. */
    sd_bus_call_method(bus, "org.fcitx.Fcitx5", "/virtualkeyboard",
                       "org.fcitx.Fcitx.VirtualKeyboard1",
                       "HideVirtualKeyboard", NULL, NULL, "");
    printf("\nreleased; asked fcitx5 to hide the virtual keyboard\n");

    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);
    return 0;
}
