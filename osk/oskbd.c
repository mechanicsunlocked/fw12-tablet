/* fw12tab on-screen keyboard — a GTK4 layer-shell keyboard that reproduces the
 * Framework Laptop 12 physical layout exactly and behaves like the real one.
 *
 * It uploads the system's actual xkb keymap to a Wayland virtual keyboard and
 * sends real evdev keycodes, so AltGr, dead keys (^ ´ ` ~), umlauts and Hyprland
 * Super-binds all work from a SINGLE layer — no symbol/nav layers needed. The
 * Framework-logo key is the real Super; the arrow cluster is faithful (full
 * height ← →, half-height stacked ↑ ↓). Replaces wvkbd + genlayout.
 *
 *   cc -O2 -o oskbd oskbd.c virtual-keyboard-unstable-v1-protocol.c \
 *      $(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 xkbcommon) -lm
 *
 * Layout/variant/options come from argv (fw12tab passes the detected layout),
 * else XKB_DEFAULT_* env, else "us".
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <gdk/wayland/gdkwayland.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Wayland modifier mask bits (wl_keyboard / xkb default keymap mod indices). */
enum { MShift = 1, MCaps = 2, MCtrl = 4, MAlt = 8, MSuper = 64, MAltGr = 128 };

typedef enum { KT_CODE, KT_MOD, KT_FN, KT_SUPER } ktype;

typedef struct Key {
  const char *label;   /* fixed label; NULL => derive from the keymap */
  uint32_t code;       /* evdev code (KT_CODE/KT_SUPER); unused for KT_MOD/KT_FN */
  uint32_t modbit;     /* KT_MOD: the modifier mask bit */
  ktype type;
  int col, row, wspan, hspan;
  uint32_t fn_code;    /* number row: evdev code while Fn is held (0 = none) */
  const char *fn_label;
  GtkWidget *button;   /* the key widget (a styled GtkBox) */
  GtkWidget *lbl;      /* GtkLabel inside (NULL for the SVG key) */
  uint32_t down_code;  /* evdev code currently held down (0 = none) */
  GtkGesture *gest;    /* the click gesture, so a lost release can be detected */
} Key;

/* ---------------------------------------------------------------------------
 * Geometry, measured off a top-down photograph of the Framework Laptop 12
 * keyboard rather than guessed. Method and numbers are in FINDINGS.md 10; the
 * short version:
 *
 *   * every row spans exactly 14.25 keys' worth of width, and all six rows
 *     share one left edge and one right edge. The stagger you see is entirely
 *     the width of each row's leading key -- 1.125u esc, 0.875u backtick,
 *     1.2u tab, 1.5u caps, 2u shift, 1u ctrl -- not a ragged margin;
 *   * the alphanumeric grid is square: 1u horizontal pitch equals 1u vertical
 *     pitch (56.9 px against 57 px on the photo);
 *   * the function row is short: 0.7u tall against 1u for the rest;
 *   * the gap between key faces is 0.105u, the same on both axes.
 *
 * The grid below is 40 sub-columns per key unit (570 across) and 10 sub-rows
 * per key unit (57 down: 7 for the function row, 10 for each of the other
 * five). 40 is the smallest divisor that makes every measured width a whole
 * number -- the 1.2u tab needs fifths, the 0.875u backtick needs eighths.
 *
 * The modelled keyboard is the US/ANSI one in the photograph, so there is no
 * ISO 102nd key. On a layout that has one, that character is unreachable from
 * here; everything else follows the keymap as usual.
 * ------------------------------------------------------------------------ */
#define KU 40              /* sub-columns per key unit */
#define KW 570             /* row width: 14.25u */

static Key keys[] = {
  /* Function row — sub-row 0, 7 sub-rows tall (0.7u).
   * Media legends sit under Fn, as they do on the machine. */
  {"esc",KEY_ESC,0,KT_CODE, 0,0,45,7, 0,NULL,0,0},
  {"F1", KEY_F1,0,KT_CODE,  45,0,40,7, KEY_MUTE,"🔇",0,0},
  {"F2", KEY_F2,0,KT_CODE,  85,0,40,7, KEY_VOLUMEDOWN,"🔉",0,0},
  {"F3", KEY_F3,0,KT_CODE, 125,0,40,7, KEY_VOLUMEUP,"🔊",0,0},
  {"F4", KEY_F4,0,KT_CODE, 165,0,40,7, KEY_PREVIOUSSONG,"⏮",0,0},
  {"F5", KEY_F5,0,KT_CODE, 205,0,40,7, KEY_PLAYPAUSE,"⏯",0,0},
  {"F6", KEY_F6,0,KT_CODE, 245,0,40,7, KEY_NEXTSONG,"⏭",0,0},
  {"F7", KEY_F7,0,KT_CODE, 285,0,40,7, KEY_BRIGHTNESSDOWN,"🔅",0,0},
  {"F8", KEY_F8,0,KT_CODE, 325,0,40,7, KEY_BRIGHTNESSUP,"🔆",0,0},
  {"F9", KEY_F9,0,KT_CODE, 365,0,40,7, 0,NULL,0,0},
  {"F10",KEY_F10,0,KT_CODE,405,0,40,7, 0,NULL,0,0},
  {"F11",KEY_F11,0,KT_CODE,445,0,40,7, KEY_SYSRQ,"prt",0,0},
  {"F12",KEY_F12,0,KT_CODE,485,0,40,7, 0,NULL,0,0},
  {"del",KEY_DELETE,0,KT_CODE, 525,0,45,7, KEY_INSERT,"ins",0,0},

  /* Number row — sub-row 7 */
  {NULL,KEY_GRAVE,0,KT_CODE,   0,7,35,10, 0,NULL,0,0},
  {NULL,KEY_1,0,KT_CODE,      35,7,40,10, 0,NULL,0,0},
  {NULL,KEY_2,0,KT_CODE,      75,7,40,10, 0,NULL,0,0},
  {NULL,KEY_3,0,KT_CODE,     115,7,40,10, 0,NULL,0,0},
  {NULL,KEY_4,0,KT_CODE,     155,7,40,10, 0,NULL,0,0},
  {NULL,KEY_5,0,KT_CODE,     195,7,40,10, 0,NULL,0,0},
  {NULL,KEY_6,0,KT_CODE,     235,7,40,10, 0,NULL,0,0},
  {NULL,KEY_7,0,KT_CODE,     275,7,40,10, 0,NULL,0,0},
  {NULL,KEY_8,0,KT_CODE,     315,7,40,10, 0,NULL,0,0},
  {NULL,KEY_9,0,KT_CODE,     355,7,40,10, 0,NULL,0,0},
  {NULL,KEY_0,0,KT_CODE,     395,7,40,10, 0,NULL,0,0},
  {NULL,KEY_MINUS,0,KT_CODE, 435,7,40,10, 0,NULL,0,0},
  {NULL,KEY_EQUAL,0,KT_CODE, 475,7,40,10, 0,NULL,0,0},
  {"⌫",KEY_BACKSPACE,0,KT_CODE, 515,7,55,10, 0,NULL,0,0},

  /* Top letter row — sub-row 17 */
  {"tab",KEY_TAB,0,KT_CODE,   0,17,48,10, 0,NULL,0,0},
  {NULL,KEY_Q,0,KT_CODE,     48,17,40,10, 0,NULL,0,0},
  {NULL,KEY_W,0,KT_CODE,     88,17,40,10, 0,NULL,0,0},
  {NULL,KEY_E,0,KT_CODE,    128,17,40,10, 0,NULL,0,0},
  {NULL,KEY_R,0,KT_CODE,    168,17,40,10, 0,NULL,0,0},
  {NULL,KEY_T,0,KT_CODE,    208,17,40,10, 0,NULL,0,0},
  {NULL,KEY_Y,0,KT_CODE,    248,17,40,10, 0,NULL,0,0},
  {NULL,KEY_U,0,KT_CODE,    288,17,40,10, 0,NULL,0,0},
  {NULL,KEY_I,0,KT_CODE,    328,17,40,10, 0,NULL,0,0},
  {NULL,KEY_O,0,KT_CODE,    368,17,40,10, 0,NULL,0,0},
  {NULL,KEY_P,0,KT_CODE,    408,17,40,10, 0,NULL,0,0},
  {NULL,KEY_LEFTBRACE,0,KT_CODE,  448,17,40,10, 0,NULL,0,0},
  {NULL,KEY_RIGHTBRACE,0,KT_CODE, 488,17,40,10, 0,NULL,0,0},
  {NULL,KEY_BACKSLASH,0,KT_CODE,  528,17,42,10, 0,NULL,0,0},

  /* Home row — sub-row 27 */
  {"⇪",KEY_CAPSLOCK,MCaps,KT_MOD, 0,27,60,10, 0,NULL,0,0},
  {NULL,KEY_A,0,KT_CODE,     60,27,40,10, 0,NULL,0,0},
  {NULL,KEY_S,0,KT_CODE,    100,27,40,10, 0,NULL,0,0},
  {NULL,KEY_D,0,KT_CODE,    140,27,40,10, 0,NULL,0,0},
  {NULL,KEY_F,0,KT_CODE,    180,27,40,10, 0,NULL,0,0},
  {NULL,KEY_G,0,KT_CODE,    220,27,40,10, 0,NULL,0,0},
  {NULL,KEY_H,0,KT_CODE,    260,27,40,10, 0,NULL,0,0},
  {NULL,KEY_J,0,KT_CODE,    300,27,40,10, 0,NULL,0,0},
  {NULL,KEY_K,0,KT_CODE,    340,27,40,10, 0,NULL,0,0},
  {NULL,KEY_L,0,KT_CODE,    380,27,40,10, 0,NULL,0,0},
  {NULL,KEY_SEMICOLON,0,KT_CODE,  420,27,40,10, 0,NULL,0,0},
  {NULL,KEY_APOSTROPHE,0,KT_CODE, 460,27,40,10, 0,NULL,0,0},
  {"⏎",KEY_ENTER,0,KT_CODE, 500,27,70,10, 0,NULL,0,0},

  /* Bottom letter row — sub-row 37 */
  {"⇧",KEY_LEFTSHIFT,MShift,KT_MOD, 0,37,80,10, 0,NULL,0,0},
  {NULL,KEY_Z,0,KT_CODE,     80,37,40,10, 0,NULL,0,0},
  {NULL,KEY_X,0,KT_CODE,    120,37,40,10, 0,NULL,0,0},
  {NULL,KEY_C,0,KT_CODE,    160,37,40,10, 0,NULL,0,0},
  {NULL,KEY_V,0,KT_CODE,    200,37,40,10, 0,NULL,0,0},
  {NULL,KEY_B,0,KT_CODE,    240,37,40,10, 0,NULL,0,0},
  {NULL,KEY_N,0,KT_CODE,    280,37,40,10, 0,NULL,0,0},
  {NULL,KEY_M,0,KT_CODE,    320,37,40,10, 0,NULL,0,0},
  {NULL,KEY_COMMA,0,KT_CODE,360,37,40,10, 0,NULL,0,0},
  {NULL,KEY_DOT,0,KT_CODE,  400,37,40,10, 0,NULL,0,0},
  {NULL,KEY_SLASH,0,KT_CODE,440,37,40,10, 0,NULL,0,0},
  {"⇧",KEY_RIGHTSHIFT,MShift,KT_MOD, 480,37,90,10, 0,NULL,0,0},

  /* Modifier row and arrow cluster — sub-row 47.
   * ↑/↓ stack half-height in one 1.25u column between full-height ←/→, which
   * is how the cluster is shaped on the machine. */
  {"ctrl",KEY_LEFTCTRL,MCtrl,KT_MOD,  0,47,40,10, 0,NULL,0,0},
  {"fn",0,0,KT_FN,                   40,47,40,10, 0,NULL,0,0},
  {NULL,KEY_LEFTMETA,MSuper,KT_SUPER, 80,47,40,10, 0,NULL,0,0},  /* Framework logo */
  {"alt",KEY_LEFTALT,MAlt,KT_MOD,   120,47,40,10, 0,NULL,0,0},
  {"",KEY_SPACE,0,KT_CODE,          160,47,200,10, 0,NULL,0,0},
  {"alt gr",KEY_RIGHTALT,MAltGr,KT_MOD, 360,47,40,10, 0,NULL,0,0},
  {"ctrl",KEY_RIGHTCTRL,MCtrl,KT_MOD,   400,47,40,10, 0,NULL,0,0},
  {"←",KEY_LEFT,0,KT_CODE,  440,47,40,10, 0,NULL,0,0},
  {"↑",KEY_UP,0,KT_CODE,    480,47,50,5,  0,NULL,0,0},
  {"↓",KEY_DOWN,0,KT_CODE,  480,52,50,5,  0,NULL,0,0},
  {"→",KEY_RIGHT,0,KT_CODE, 530,47,40,10, 0,NULL,0,0},
};
static const int NKEYS = sizeof keys / sizeof keys[0];

/* ---- Wayland virtual keyboard ---- */
static struct wl_display *wl_dpy;
static struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
static struct zwp_virtual_keyboard_v1 *vkbd;
static struct wl_seat *wl_seat_obj;
static uint32_t one_shot;   /* latched non-lock modifiers, cleared after next key */
static uint32_t locks;      /* locking modifiers (CapsLock) */
static gboolean fn_active;
static uint32_t evtime;     /* monotonic-ish event time counter */

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
  (void)d; (void)ver;
  if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name))
    vk_mgr = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d;(void)r;(void)name; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

/* Build the system keymap, hold it for label resolution, and upload it. */
static struct xkb_keymap *upload_keymap(const char *layout, const char *variant,
                                        const char *options) {
  struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_rule_names names = { .rules = NULL, .model = NULL,
    .layout = layout, .variant = variant, .options = options };
  struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref(ctx);   /* km holds its own ref to the context */
  if (!km) return NULL;
  char *str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
  size_t size = strlen(str) + 1;
  int fd = memfd_create("fw12kbd-keymap", MFD_CLOEXEC);
  if (fd >= 0 && ftruncate(fd, size) == 0) {
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map != MAP_FAILED) { memcpy(map, str, size); munmap(map, size); }
    zwp_virtual_keyboard_v1_keymap(vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    close(fd);
  }
  free(str);
  return km;  /* keep for labels */
}

static void send_mods(void) {
  if (vkbd) zwp_virtual_keyboard_v1_modifiers(vkbd, one_shot | locks, 0, locks, 0);
}
static void send_key(uint32_t code, uint32_t state) {
  if (vkbd) zwp_virtual_keyboard_v1_key(vkbd, evtime++, code, state);
}

/* ---- label resolution from the keymap ---- */
static void sym_to_text(xkb_keysym_t s, char *out, size_t n) {
  switch (s) {  /* dead keys: show the spacing glyph */
    case XKB_KEY_dead_circumflex: snprintf(out,n,"^"); return;
    case XKB_KEY_dead_grave:      snprintf(out,n,"`"); return;
    case XKB_KEY_dead_acute:      snprintf(out,n,"´"); return;
    case XKB_KEY_dead_tilde:      snprintf(out,n,"~"); return;
    case XKB_KEY_dead_diaeresis:  snprintf(out,n,"¨"); return;
    case XKB_KEY_dead_macron:     snprintf(out,n,"¯"); return;
    case XKB_KEY_dead_breve:      snprintf(out,n,"˘"); return;
    case XKB_KEY_dead_abovering:  snprintf(out,n,"°"); return;
    case XKB_KEY_dead_doubleacute:snprintf(out,n,"˝"); return;
    case XKB_KEY_dead_caron:      snprintf(out,n,"ˇ"); return;
    case XKB_KEY_dead_cedilla:    snprintf(out,n,"¸"); return;
    case XKB_KEY_dead_ogonek:     snprintf(out,n,"˛"); return;
    default: break;
  }
  if (xkb_keysym_to_utf8(s, out, n) > 0 && (unsigned char)out[0] >= ' ') return;
  out[0] = 0;  /* non-printable (other dead keys, named keys): show nothing */
}
static void level_text(struct xkb_keymap *km, uint32_t evdev, int level, char *out, size_t n) {
  const xkb_keysym_t *syms;
  out[0] = 0;
  if (xkb_keymap_key_get_syms_by_level(km, evdev + 8, 0, level, &syms) > 0
      && syms[0] != XKB_KEY_NoSymbol)
    sym_to_text(syms[0], out, n);
}

/* ---- key press handling ---- */
static void refresh_highlight(void) {
  for (int i = 0; i < NKEYS; i++) {
    Key *k = &keys[i];
    if (k->type == KT_MOD || k->type == KT_SUPER) {
      if ((one_shot | locks) & k->modbit) gtk_widget_add_css_class(k->button, "active-mod");
      else                                gtk_widget_remove_css_class(k->button, "active-mod");
    }
  }
}
static struct xkb_keymap *g_keymap;
/* Dynamic keycaps: each derived key shows the single symbol it would type RIGHT
 * NOW given the latched/locked modifiers — like a phone keyboard. Shift/AltGr
 * flip the legends live; Fn shows F1..F12 on the number row. */
static void relabel_keys(void) {
  uint32_t m = one_shot | locks;
  gboolean shift = m & MShift, altgr = m & MAltGr, caps = m & MCaps;
  for (int i = 0; i < NKEYS; i++) {
    Key *k = &keys[i];
    if (k->type != KT_CODE || k->label || !k->lbl || !g_keymap) continue;
    if (fn_active && k->fn_code) { gtk_label_set_text(GTK_LABEL(k->lbl), k->fn_label); continue; }
    char base[16]; level_text(g_keymap, k->code, 0, base, sizeof base);
    gboolean letter = base[0] && g_unichar_isalpha(g_utf8_get_char(base));
    gboolean sh = shift || (caps && letter);   /* CapsLock shifts letters only */
    int lvl = (altgr && sh) ? 3 : altgr ? 2 : sh ? 1 : 0;
    char t[16]; level_text(g_keymap, k->code, lvl, t, sizeof t);
    if (!t[0]) { if (lvl) level_text(g_keymap, k->code, 0, t, sizeof t); }  /* fall back to base */
    gtk_label_set_text(GTK_LABEL(k->lbl), t[0] ? t : base);
  }
}

/* Modifier tap: single tap = one-shot (applies to the next key, then clears);
 * double tap = toggle a persistent LOCK (stays on until double-tapped again). */
static void mod_tap(uint32_t bit, int np) {
  if (np >= 2) { locks ^= bit; one_shot &= ~bit; }  /* double-tap toggles lock */
  else         { one_shot ^= bit; }                 /* single tap = one-shot   */
}

/* ---------------------------------------------------------------------------
 * Palm guard and stuck-key watchdog
 *
 * Two failures seen in use, both from resting a hand on the keyboard rather
 * than typing with fingers:
 *
 *   1. Several keys go down at once. Worse, a modifier hit twice in quick
 *      succession reads as a double-tap and *locks*, so everything typed
 *      afterwards silently carries Ctrl or AltGr.
 *   2. A key goes down and its release never arrives -- GtkGestureClick tracks
 *      one touch sequence at a time, and a second contact on the same key can
 *      end the first without a "released". The compositor then repeats that key
 *      forever, which is what produced a screenful of ÄÄÄÄÄ.
 *
 * The guard: three contacts inside 150 ms is a hand, not fingers. Drop the lot,
 * clear every latch, and stay quiet until all contacts have lifted.
 *
 * The watchdog: rather than time keys out -- holding backspace is legitimate --
 * ask GTK whether each held key's gesture is still active. If it is not, the
 * release was lost and the key is freed. That is the actual fault condition,
 * so nothing legitimate is ever cut short.
 * ------------------------------------------------------------------------ */
static void key_up(Key *k);   /* defined below, next to the send path */

#define PALM_KEYS      3
#define PALM_WINDOW_US (150 * 1000)

static int      contacts;        /* touch sequences currently down */
static gint64   first_down_us;
static gboolean palming;
static guint    watchdog_id;

static void release_all(void) {
  for (int i = 0; i < NKEYS; i++)
    if (keys[i].down_code) key_up(&keys[i]);
  one_shot = 0; locks = 0; fn_active = FALSE;
  send_mods(); refresh_highlight(); relabel_keys();
  wl_display_flush(wl_dpy);
}

static gboolean watchdog(gpointer u) {
  (void)u;
  int still_held = 0;
  for (int i = 0; i < NKEYS; i++) {
    Key *k = &keys[i];
    if (!k->down_code) continue;
    if (k->gest && !gtk_gesture_is_active(k->gest)) {
      g_warning("fw12-oskbd: freeing stuck key %u (touch ended with no release)",
                k->down_code);
      key_up(k);
    } else {
      still_held++;
    }
  }
  if (still_held) return G_SOURCE_CONTINUE;
  watchdog_id = 0;
  return G_SOURCE_REMOVE;
}

static void arm_watchdog(void) {
  if (!watchdog_id) watchdog_id = g_timeout_add(250, watchdog, NULL);
}

/* Called for every release and cancel, so the contact count cannot drift. */
static void end_contact(Key *k) {
  if (contacts > 0) contacts--;
  if (contacts == 0) palming = FALSE;
  key_up(k);
}

static void on_pressed(GtkGestureClick *g, int np, double x, double y, gpointer u) {
  (void)g;(void)x;(void)y;
  Key *k = u;

  gint64 now = g_get_monotonic_time();
  if (contacts == 0) first_down_us = now;
  contacts++;
  if (palming) return;
  if (contacts >= PALM_KEYS && now - first_down_us < PALM_WINDOW_US) {
    palming = TRUE;
    release_all();
    return;
  }

  gtk_widget_add_css_class(k->button, "pressed");
  switch (k->type) {
    case KT_MOD:
      if (k->modbit == MCaps) locks ^= MCaps;  /* CapsLock is always a plain lock */
      else mod_tap(k->modbit, np);
      send_mods(); refresh_highlight(); relabel_keys(); break;
    case KT_SUPER:
      mod_tap(MSuper, np); send_mods(); refresh_highlight(); relabel_keys(); break;
    case KT_FN:
      fn_active = !fn_active; relabel_keys();
      if (fn_active) gtk_widget_add_css_class(k->button, "active-mod");
      else           gtk_widget_remove_css_class(k->button, "active-mod");
      break;
    case KT_CODE:
      k->down_code = (fn_active && k->fn_code) ? k->fn_code : k->code;
      send_mods();
      send_key(k->down_code, WL_KEYBOARD_KEY_STATE_PRESSED);
      break;
  }
  wl_display_flush(wl_dpy);
}

/* Release on BOTH "released" and "cancel" so a key can never get stuck down
 * (a stuck key-down makes the compositor auto-repeat forever). */
static void key_up(Key *k) {
  gtk_widget_remove_css_class(k->button, "pressed");
  if (k->type == KT_CODE && k->down_code) {
    send_key(k->down_code, WL_KEYBOARD_KEY_STATE_RELEASED);
    k->down_code = 0;
    if (one_shot) { one_shot = 0; send_mods(); refresh_highlight(); relabel_keys(); }
    wl_display_flush(wl_dpy);
  }
}
static void on_released(GtkGestureClick *g, int np, double x, double y, gpointer u) {
  (void)g;(void)np;(void)x;(void)y; end_contact((Key *)u);
}
static void on_cancel(GtkGesture *g, GdkEventSequence *seq, gpointer u) {
  (void)g;(void)seq; end_contact((Key *)u);
}

/* Filled in at runtime. The gap between key faces on the machine is 0.105u, so
 * it has to scale with the key rather than sit at a fixed 2 px: at the sizes
 * this thing actually renders, a fixed gap reads as a hairline in landscape
 * and a canyon in portrait. Margin is half the gap, because two adjacent keys
 * each contribute one. */
static const char *CSS_FMT =
  "window { background: rgba(20,20,20,0.92); }"
  ".key { border-radius: %dpx; background: #2b2b2b; color: #eee;"
  "       font-size: %dpx; }"
  ".key.half { font-size: %dpx; }"
  ".key label { color: #eee; }"
  ".key.pressed { background: #555; }"
  ".key.active-mod { background: #3584e4; }"
  ".key.active-mod label { color: #fff; }";

/* Where the Framework mark for the Super key lives. Checked in order so that a
 * rootless `make install` (into ~/.local) and a packaged one (into /usr) both
 * work without anything having to pass the path in. Falls through to a glyph
 * in the caller if none of these exist. */
static const char *logo_path(void) {
  static char buf[512];
  const char *e = g_getenv("FW12TAB_LOGO");
  if (e && *e) return e;

  const char *home = g_get_home_dir();
  if (home) {
    g_snprintf(buf, sizeof buf, "%s/.local/share/fw12-tablet/framework-logo.svg", home);
    if (g_file_test(buf, G_FILE_TEST_EXISTS)) return buf;
  }
  return "/usr/share/fw12-tablet/framework-logo.svg";
}


/* ---------------------------------------------------------------------------
 * Sizing and placement, recomputed rather than fixed at startup.
 *
 * The keyboard has to be re-laid-out when the display rotates: this machine
 * spends its life folding between 1200x750 and 750x1200, and a keyboard sized
 * once for landscape is 845 px wide on a 750 px screen -- measured at x=-48,
 * hanging off the left edge, because the compositor centres what it is given.
 * ------------------------------------------------------------------------ */
static GtkWidget *g_win, *g_fixed;
static GtkCssProvider *g_css;

static void apply_geometry(void) {
  if (!g_win || !g_fixed) return;

  /* The board is 14.25u wide and 5.7u tall, so its aspect is exactly 2.5:1.
   * Full screen width is right in portrait: 750 px across gives a 300 px
   * strip, a quarter of the display. In landscape the same rule gives
   * 1200x480, nearly two thirds of the screen and far too much -- so cap the
   * height and letterbox, rather than squashing the keys out of shape to fill
   * the width. Keeping 2.5:1 is the whole point; a stretched keyboard is
   * exactly what stops it feeling like the real one.
   *
   * Keys land at ~12.8 mm across in landscape and ~11.2 mm in portrait, both
   * past the ~9 mm where taps start being missed (section 5.4). */
  const double ASPECT  = 14.25 / 5.7;   /* 2.5 */
  const double MAXFRAC = 0.45;
  int sw = 1200, sh = 750;
  {
    GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
    GdkMonitor *m0 = mons ? g_list_model_get_item(mons, 0) : NULL;
    if (m0) {
      GdkRectangle geo; gdk_monitor_get_geometry(m0, &geo);
      sw = geo.width; sh = geo.height;
      g_object_unref(m0);   /* g_list_model_get_item() returns an owned ref */
    }
  }
  int kbd_w = sw, kbd_h = (int)lround(sw / ASPECT);
  int cap = (int)lround(sh * MAXFRAC);
  if (kbd_h > cap) { kbd_h = cap; kbd_w = (int)lround(kbd_h * ASPECT); }
  const char *he = g_getenv("FW12TAB_OSK_HEIGHT");
  if (he && *he) { kbd_h = atoi(he); kbd_w = (int)lround(kbd_h * ASPECT); }

  /* Key metrics follow the rendered size rather than a constant. The gap
   * between key faces is 0.105u on the machine; it is applied as an inset when
   * each key is placed, not as a CSS margin, so the geometry stays exactly
   * what was measured. */
  double unit   = kbd_w / 14.25;
  int    gap_px = (int)lround(unit * 0.105); if (gap_px < 2) gap_px = 2;

  if (g_css) {
    int radius   = (int)lround(unit * 0.105);
    int fontpx   = (int)lround(unit * 0.30);
    /* The stacked arrows are half a row tall. At the normal size their labels
     * alone demand more height than half a row and the whole keyboard grows to
     * satisfy them, silently breaking the proportion this is all in aid of.
     * Measured: 399 px tall instead of 338. */
    int fonthalf = (int)lround(unit * 0.22);
    char *sheet = g_strdup_printf(CSS_FMT, radius, fontpx, fonthalf);
    gtk_css_provider_load_from_string(g_css, sheet);
    g_free(sheet);
  }

  /* Slot edges first, then inset half a gap on each side, so the gap between
   * two neighbours is exactly one gap and no seam drifts. */
  double sx = (double)kbd_w / KW, sy = (double)kbd_h / 57.0;
  int inset = gap_px / 2;
  for (int i = 0; i < NKEYS; i++) {
    Key *k = &keys[i];
    if (!k->button) continue;
    int x0 = (int)lround(k->col * sx), x1 = (int)lround((k->col + k->wspan) * sx);
    int y0 = (int)lround(k->row * sy), y1 = (int)lround((k->row + k->hspan) * sy);
    gtk_widget_set_size_request(k->button, (x1 - x0) - 2 * inset, (y1 - y0) - 2 * inset);
    gtk_fixed_move(GTK_FIXED(g_fixed), k->button, x0 + inset, y0 + inset);
  }

  gtk_widget_set_size_request(g_fixed, kbd_w, kbd_h);
  gtk_widget_set_size_request(g_win, kbd_w, kbd_h);
  gtk_window_set_default_size(GTK_WINDOW(g_win), kbd_w, kbd_h);
  /* Reserve the strip so tiled windows shrink to sit ABOVE the keyboard
   * instead of being covered by it (the mechanism waybar uses). */
  gtk_layer_set_exclusive_zone(GTK_WINDOW(g_win), kbd_h);
}

static void on_monitor_changed(GObject *o, GParamSpec *p, gpointer u) {
  (void)o; (void)p; (void)u;
  apply_geometry();
}

static void on_activate(GtkApplication *app, gpointer u) {
  char **argv = u;
  const char *layout  = argv[1] && *argv[1] ? argv[1] : (g_getenv("XKB_DEFAULT_LAYOUT") ?: "us");
  const char *variant = argv[2] ? argv[2] : "";
  const char *options = argv[3] ? argv[3] : "";

  /* The Ctrl caps are the only fixed labels that are language-specific: a
   * German board says Strg, everyone else says Ctrl. Every other derived key
   * gets its legend from the keymap in relabel_keys(), so this is the one
   * place the layout has to be consulted by hand. */
  if (g_str_has_prefix(layout, "de") || g_str_has_prefix(layout, "at")
      || g_str_has_prefix(layout, "ch")) {
    /* keep the table's default */
  } else {
    for (int i = 0; i < NKEYS; i++)
      if (keys[i].label && !g_strcmp0(keys[i].label, "Strg"))
        keys[i].label = "Ctrl";
  }

  GtkWidget *win = gtk_application_window_new(app);
  gtk_layer_init_for_window(GTK_WINDOW(win));
  gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_TOP);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  gtk_layer_set_namespace(GTK_WINDOW(win), "fw12tab-osk");
  gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);

  /* GtkFixed, not GtkGrid.
   *
   * The measured layout needs 40 sub-columns per key, and 570 of those across
   * an 845 px keyboard is 1.48 px each. GtkGrid works in whole pixels, so a
   * homogeneous 570-column grid hands out 1 px to some columns and 2 px to
   * others; spans of 40 accumulate that error and the right-hand half of every
   * row comes out visibly narrower than the left. Seen, not guessed.
   *
   * Placing each key at an exact pixel rectangle computed from the real
   * numbers rounds once per edge instead, so keys stay within a pixel of the
   * geometry and adjacent keys always share an edge. */
  GtkWidget *grid = gtk_fixed_new();

  /* Anchored to the bottom edge only, so the surface is exactly as wide as the
   * keyboard and the compositor centres it. Anchoring left and right as well
   * would stretch it across the display, and a widget inside cannot be held
   * narrower than its natural width, so centring within a full-width window
   * does not work. Actual sizes come from apply_geometry() below. */
  g_win = win;
  g_fixed = grid;
  g_css = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
      GTK_STYLE_PROVIDER(g_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  /* Wayland virtual keyboard: bind the manager on GDK's display + seat. */
  GdkDisplay *disp = gdk_display_get_default();
  wl_dpy = gdk_wayland_display_get_wl_display(GDK_WAYLAND_DISPLAY(disp));
  wl_seat_obj = gdk_wayland_seat_get_wl_seat(GDK_WAYLAND_SEAT(gdk_display_get_default_seat(disp)));
  struct wl_registry *reg = wl_display_get_registry(wl_dpy);
  wl_registry_add_listener(reg, &reg_listener, NULL);
  wl_display_roundtrip(wl_dpy);
  if (vk_mgr && wl_seat_obj) {
    vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vk_mgr, wl_seat_obj);
    g_keymap = upload_keymap(layout, variant, options);
  }
  if (!g_keymap) g_warning("fw12tab oskbd: no virtual keyboard / keymap; keys will not type");

  for (int i = 0; i < NKEYS; i++) {
    Key *k = &keys[i];
    /* A plain styled box (not GtkButton): its sole gesture fires pressed AND
     * released reliably — GtkButton's internal gesture would swallow "released"
     * and leave the key auto-repeating forever. */
    GtkWidget *key = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(key, "key");
    GtkWidget *child;
    if (k->type == KT_SUPER) {
      const char *lp = logo_path();
      if (g_file_test(lp, G_FILE_TEST_EXISTS)) {
        child = gtk_picture_new_for_filename(lp);
        gtk_picture_set_content_fit(GTK_PICTURE(child), GTK_CONTENT_FIT_CONTAIN);
        gtk_widget_set_size_request(child, 22, 22);
      } else {
        child = gtk_label_new("❖");   /* fallback glyph if the logo asset is missing */
      }
    } else {
      child = gtk_label_new(k->label ? k->label : "");
      /* Without this a label refuses to be narrower than its text, every key
       * inherits that as a minimum, and 570 homogeneous columns multiply it
       * into a keyboard far wider than the proportions asked for -- measured
       * 1140 px where 845 was wanted. Ellipsising lets the grid be the size we
       * computed; legends are sized to fit it anyway. */
      gtk_label_set_ellipsize(GTK_LABEL(child), PANGO_ELLIPSIZE_END);
      k->lbl = child;   /* derived keys get their symbol from relabel_keys() below */
    }
    gtk_widget_set_hexpand(child, TRUE);
    gtk_widget_set_halign(child, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(child, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(key), child);

    GtkGesture *gc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gc), 0);  /* any button + touch */
    g_signal_connect(gc, "pressed", G_CALLBACK(on_pressed), k);
    g_signal_connect(gc, "released", G_CALLBACK(on_released), k);
    g_signal_connect(gc, "cancel", G_CALLBACK(on_cancel), k);
    gtk_widget_add_controller(key, GTK_EVENT_CONTROLLER(gc));
    k->gest = gc;
    if (k->hspan * 2 <= 10) gtk_widget_add_css_class(key, "half");
    gtk_fixed_put(GTK_FIXED(grid), key, 0, 0);   /* positioned by apply_geometry() */
    k->button = key;
  }

  relabel_keys();   /* set initial keycap symbols from the keymap */
  gtk_window_set_child(GTK_WINDOW(win), grid);

  apply_geometry();
  {
    GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
    GdkMonitor *m0 = mons ? g_list_model_get_item(mons, 0) : NULL;
    if (m0) {
      /* Rotating the panel changes the monitor's geometry rather than
       * replacing the monitor, so one notify is enough to catch a fold. The
       * reference is deliberately kept: the handler outlives this scope. */
      g_signal_connect(m0, "notify::geometry", G_CALLBACK(on_monitor_changed), NULL);
    }
  }
  gtk_window_present(GTK_WINDOW(win));
}

/* Clear any latched modifiers on exit so nothing sticks in the compositor. */
static void on_shutdown(GApplication *app, gpointer u) {
  (void)app;(void)u;
  if (vkbd) { one_shot = 0; locks = 0; send_mods(); if (wl_dpy) wl_display_flush(wl_dpy); }
}

int main(int argc, char **argv) {
  (void)argc;
  GtkApplication *app = gtk_application_new("org.fw12.osk", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), argv);
  g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
  /* Don't let GTK parse our positional layout args. */
  int r = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return r;
}
