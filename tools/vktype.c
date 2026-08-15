/* Phase 1 probe: find out how fcitx5 turns ProcessKeyEvent into characters.
 *
 * ProcessKeyEvent(keysym, keycode, states, isRelease, time) carries BOTH a
 * keysym and a keycode, and it matters which one wins:
 *
 *   - if the KEYSYM is authoritative, the on-screen keyboard decides the
 *     character and can render whatever layout it likes, independent of what
 *     fcitx5 or the compositor think the layout is;
 *   - if the KEYCODE is authoritative, fcitx5 re-derives the character through
 *     its own layout, and our legends must match that layout exactly or keys
 *     will type the wrong thing.
 *
 * This machine is a good test case: fcitx5 reports "keyboard-us" while
 * Hyprland's xkb layout is "de", so the two disagree and the result is
 * unambiguous.
 *
 * Focus a text field, then run. It waits for fcitx5 to ask for the keyboard
 * and types a short sequence.
 *
 *   cc -O2 -o vktype vktype.c $(pkg-config --cflags --libs libsystemd)
 */
#include <systemd/sd-bus.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FCITX     "org.fcitx.Fcitx5"
#define VK_NAME   "org.fcitx.Fcitx5.VirtualKeyboard"
#define VK_PATH   "/org/fcitx/virtualkeyboard/impanel"
#define VK_IFACE  "org.fcitx.Fcitx5.VirtualKeyboard1"
#define FC_PATH   "/virtualkeyboard"
#define FC_BACKEND "org.fcitx.Fcitx5.VirtualKeyboardBackend1"
#define FC_IFACE  "org.fcitx.Fcitx.VirtualKeyboard1"

static int shown;

static int on_msg(sd_bus_message *m, void *u, sd_bus_error *e)
{
    const char *mem = sd_bus_message_get_member(m);
    const char *path = sd_bus_message_get_path(m);

    if (!sd_bus_message_is_method_call(m, NULL, NULL)) return 0;
    if (!path || strcmp(path, VK_PATH)) return 0;
    if (mem && !strcmp(mem, "ShowVirtualKeyboard")) shown = 1;
    sd_bus_reply_method_return(m, "");
    return 1;
}

/* One half of a key event: press (is_release=0) or release (is_release=1).
 * Needed on its own so a modifier can be held down across another key, which
 * round 1 showed is the only way modifiers take effect. */
static void raw(sd_bus *bus, uint32_t sym, uint32_t code, uint32_t states,
                int is_release)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(bus, FCITX, FC_PATH, FC_BACKEND,
                               "ProcessKeyEvent", &err, NULL, "uuubu",
                               sym, code, states, is_release, 0u);

    if (r < 0)
        printf("    %s: ERROR %s\n", is_release ? "release" : "press",
               err.message ? err.message : strerror(-r));
    sd_bus_error_free(&err);
}

/* keysym, keycode (evdev+8, 0 = "none"), modifier state mask */
static void key(sd_bus *bus, const char *what, uint32_t sym, uint32_t code,
                uint32_t states)
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r;

    printf("  typing %-22s keysym=0x%04x keycode=%u states=0x%x\n",
           what, sym, code, states);
    fflush(stdout);

    r = sd_bus_call_method(bus, FCITX, FC_PATH, FC_BACKEND, "ProcessKeyEvent",
                           &err, NULL, "uuubu", sym, code, states, 0, 0u);
    if (r < 0) printf("    press: ERROR %s\n", err.message ? err.message : strerror(-r));
    sd_bus_error_free(&err);

    r = sd_bus_call_method(bus, FCITX, FC_PATH, FC_BACKEND, "ProcessKeyEvent",
                           &err, NULL, "uuubu", sym, code, states, 1, 0u);
    if (r < 0) printf("    release: ERROR %s\n", err.message ? err.message : strerror(-r));
    sd_bus_error_free(&err);

    usleep(300 * 1000);
}

int main(void)
{
    sd_bus *bus = NULL;
    sd_bus_slot *slot = NULL;
    int r, waited = 0;

    if (sd_bus_open_user(&bus) < 0) { fprintf(stderr, "no session bus\n"); return 1; }
    sd_bus_add_filter(bus, &slot, on_msg, NULL);
    r = sd_bus_request_name(bus, VK_NAME, SD_BUS_NAME_REPLACE_EXISTING);
    if (r < 0) { fprintf(stderr, "cannot own %s: %s\n", VK_NAME, strerror(-r)); return 1; }
    sd_bus_call_method(bus, FCITX, FC_PATH, FC_IFACE, "ShowVirtualKeyboard",
                       NULL, NULL, "");

    printf("waiting for a focused text field (up to 30s)...\n");
    fflush(stdout);
    while (!shown && waited < 30000) {
        sd_bus_process(bus, NULL);
        sd_bus_wait(bus, 100000);
        waited += 100;
    }
    if (!shown) {
        printf("fcitx5 never asked to show -- no text field focused. Aborting.\n");
        goto out;
    }

    /* fcitx5 may gate key events on the keyboard actually being visible --
     * a first run that sent none of this produced no characters at all. */
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        r = sd_bus_call_method(bus, FCITX, FC_PATH, FC_BACKEND,
                               "ProcessVisibilityEvent", &err, NULL, "b", 1);
        printf("ProcessVisibilityEvent(true): %s\n",
               r < 0 ? (err.message ? err.message : strerror(-r)) : "ok");
        sd_bus_error_free(&err);
    }

    printf("typing test sequence in 2s -- keep the text field focused\n");
    fflush(stdout);
    sleep(2);

    /* ROUND 4: uppercase ASCII. Rounds 1-3 showed the states mask is ignored,
     * a held Shift key is ignored, and a bare uppercase keysym produces
     * nothing. CapsLock is the remaining candidate -- it is a latching state
     * fcitx5 may track rather than a transient modifier. */
    key(bus, "baseline lowercase 'a'", 0, 38, 0);

    printf("  CapsLock on, then 'a', then CapsLock off\n"); fflush(stdout);
    raw(bus, 0xffe5, 66, 0, 0);   /* Caps_Lock press  KEY_CAPSLOCK(58)+8 */
    raw(bus, 0xffe5, 66, 0, 1);
    usleep(200*1000);
    key(bus, "'a' with caps on", 0, 38, 2);   /* states: CapsLock = 1<<1 */
    raw(bus, 0xffe5, 66, 0, 0);
    raw(bus, 0xffe5, 66, 0, 1);
    usleep(200*1000);

    /* Shift press with the shift bit already set on the modifier event itself,
     * in case fcitx5 wants the state to arrive WITH the modifier. */
    printf("  Shift(with mask) held across 'a'\n"); fflush(stdout);
    raw(bus, 0xffe1, 50, 1, 0);
    raw(bus, 0x0041, 38, 1, 0);
    raw(bus, 0x0041, 38, 1, 1);
    raw(bus, 0xffe1, 50, 1, 1);
    usleep(300*1000);

    /* Non-ASCII uppercase via keysym only -- does the ASCII restriction apply
     * to accented capitals too? */
    key(bus, "keysym Adiaeresis only", 0x00c4, 0, 0);

    printf("\ndone -- read the text field and report what appeared, in order.\n");

out:
    sd_bus_call_method(bus, FCITX, FC_PATH, FC_IFACE, "HideVirtualKeyboard",
                       NULL, NULL, "");
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);
    return 0;
}
