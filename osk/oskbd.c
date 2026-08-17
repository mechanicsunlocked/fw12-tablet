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
} Key;

/* The grid is 60 sub-columns wide and 10 sub-rows tall; each main key row is two
 * sub-rows high, so the arrow cluster can stack half-height ↑/↓ between
 * full-height ←/→.  Widths in sub-cols: 1u = 4. */
static Key keys[] = {
  /* Row 1 — number row (sub-row 0) */
  {NULL,KEY_GRAVE,0,KT_CODE, 0,0,4,2, 0,NULL,0,0},                /* ^ ° */
  {NULL,KEY_1,0,KT_CODE,  4,0,4,2, KEY_F1,"F1",0,0},
  {NULL,KEY_2,0,KT_CODE,  8,0,4,2, KEY_F2,"F2",0,0},
  {NULL,KEY_3,0,KT_CODE, 12,0,4,2, KEY_F3,"F3",0,0},
  {NULL,KEY_4,0,KT_CODE, 16,0,4,2, KEY_F4,"F4",0,0},
  {NULL,KEY_5,0,KT_CODE, 20,0,4,2, KEY_F5,"F5",0,0},
  {NULL,KEY_6,0,KT_CODE, 24,0,4,2, KEY_F6,"F6",0,0},
  {NULL,KEY_7,0,KT_CODE, 28,0,4,2, KEY_F7,"F7",0,0},
  {NULL,KEY_8,0,KT_CODE, 32,0,4,2, KEY_F8,"F8",0,0},
  {NULL,KEY_9,0,KT_CODE, 36,0,4,2, KEY_F9,"F9",0,0},
  {NULL,KEY_0,0,KT_CODE, 40,0,4,2, KEY_F10,"F10",0,0},
  {NULL,KEY_MINUS,0,KT_CODE, 44,0,4,2, KEY_F11,"F11",0,0},   /* ß ? */
  {NULL,KEY_EQUAL,0,KT_CODE, 48,0,4,2, KEY_F12,"F12",0,0},   /* ´ ` */
  {"⌫",KEY_BACKSPACE,0,KT_CODE, 52,0,8,2, 0,NULL,0,0},

  /* Row 2 (sub-row 2) */
  {"Tab",KEY_TAB,0,KT_CODE, 0,2,6,2, 0,NULL,0,0},
  {NULL,KEY_Q,0,KT_CODE,  6,2,4,2, 0,NULL,0,0},
  {NULL,KEY_W,0,KT_CODE, 10,2,4,2, 0,NULL,0,0},
  {NULL,KEY_E,0,KT_CODE, 14,2,4,2, 0,NULL,0,0},
  {NULL,KEY_R,0,KT_CODE, 18,2,4,2, 0,NULL,0,0},
  {NULL,KEY_T,0,KT_CODE, 22,2,4,2, 0,NULL,0,0},
  {NULL,KEY_Y,0,KT_CODE, 26,2,4,2, 0,NULL,0,0},   /* Z on QWERTZ */
  {NULL,KEY_U,0,KT_CODE, 30,2,4,2, 0,NULL,0,0},
  {NULL,KEY_I,0,KT_CODE, 34,2,4,2, 0,NULL,0,0},
  {NULL,KEY_O,0,KT_CODE, 38,2,4,2, 0,NULL,0,0},
  {NULL,KEY_P,0,KT_CODE, 42,2,4,2, 0,NULL,0,0},
  {NULL,KEY_LEFTBRACE,0,KT_CODE, 46,2,4,2, 0,NULL,0,0},   /* Ü */
  {NULL,KEY_RIGHTBRACE,0,KT_CODE, 50,2,4,2, 0,NULL,0,0},  /* + */
  {"⏎",KEY_ENTER,0,KT_CODE, 54,2,6,4, 0,NULL,0,0},        /* tall ISO Enter */

  /* Row 3 (sub-row 4) */
  {"⇪",KEY_CAPSLOCK,MCaps,KT_MOD, 0,4,6,2, 0,NULL,0,0},
  {NULL,KEY_A,0,KT_CODE,  6,4,4,2, 0,NULL,0,0},
  {NULL,KEY_S,0,KT_CODE, 10,4,4,2, 0,NULL,0,0},
  {NULL,KEY_D,0,KT_CODE, 14,4,4,2, 0,NULL,0,0},
  {NULL,KEY_F,0,KT_CODE, 18,4,4,2, 0,NULL,0,0},
  {NULL,KEY_G,0,KT_CODE, 22,4,4,2, 0,NULL,0,0},
  {NULL,KEY_H,0,KT_CODE, 26,4,4,2, 0,NULL,0,0},
  {NULL,KEY_J,0,KT_CODE, 30,4,4,2, 0,NULL,0,0},
  {NULL,KEY_K,0,KT_CODE, 34,4,4,2, 0,NULL,0,0},
  {NULL,KEY_L,0,KT_CODE, 38,4,4,2, 0,NULL,0,0},
  {NULL,KEY_SEMICOLON,0,KT_CODE, 42,4,4,2, 0,NULL,0,0},   /* Ö */
  {NULL,KEY_APOSTROPHE,0,KT_CODE, 46,4,4,2, 0,NULL,0,0},  /* Ä */
  {NULL,KEY_BACKSLASH,0,KT_CODE, 50,4,4,2, 0,NULL,0,0},   /* # */

  /* Row 4 (sub-row 6) */
  {"⇧",KEY_LEFTSHIFT,MShift,KT_MOD, 0,6,6,2, 0,NULL,0,0},
  {NULL,KEY_102ND,0,KT_CODE, 6,6,4,2, 0,NULL,0,0},   /* < > | */
  {NULL,KEY_Z,0,KT_CODE, 10,6,4,2, 0,NULL,0,0},      /* Y on QWERTZ */
  {NULL,KEY_X,0,KT_CODE, 14,6,4,2, 0,NULL,0,0},
  {NULL,KEY_C,0,KT_CODE, 18,6,4,2, 0,NULL,0,0},
  {NULL,KEY_V,0,KT_CODE, 22,6,4,2, 0,NULL,0,0},
  {NULL,KEY_B,0,KT_CODE, 26,6,4,2, 0,NULL,0,0},
  {NULL,KEY_N,0,KT_CODE, 30,6,4,2, 0,NULL,0,0},
  {NULL,KEY_M,0,KT_CODE, 34,6,4,2, 0,NULL,0,0},
  {NULL,KEY_COMMA,0,KT_CODE, 38,6,4,2, 0,NULL,0,0},
  {NULL,KEY_DOT,0,KT_CODE, 42,6,4,2, 0,NULL,0,0},
  {NULL,KEY_SLASH,0,KT_CODE, 46,6,4,2, 0,NULL,0,0},  /* - _ */
  {"⇧",KEY_RIGHTSHIFT,MShift,KT_MOD, 50,6,10,2, 0,NULL,0,0},

  /* Row 5 — modifier row + arrow cluster (sub-rows 8-9) */
  {"Strg",KEY_LEFTCTRL,MCtrl,KT_MOD, 0,8,6,2, 0,NULL,0,0},
  {"Fn",0,0,KT_FN, 6,8,4,2, 0,NULL,0,0},
  {NULL,KEY_LEFTMETA,MSuper,KT_SUPER, 10,8,4,2, 0,NULL,0,0},   /* Framework logo */
  {"Alt",KEY_LEFTALT,MAlt,KT_MOD, 14,8,4,2, 0,NULL,0,0},
  {"",KEY_SPACE,0,KT_CODE, 18,8,20,2, 0,NULL,0,0},
  {"AltGr",KEY_RIGHTALT,MAltGr,KT_MOD, 38,8,4,2, 0,NULL,0,0},
  {"Strg",KEY_RIGHTCTRL,MCtrl,KT_MOD, 42,8,6,2, 0,NULL,0,0},
  {"←",KEY_LEFT,0,KT_CODE, 48,8,4,2, 0,NULL,0,0},        /* full height */
  {"↑",KEY_UP,0,KT_CODE, 52,8,4,1, 0,NULL,0,0},          /* top half */
  {"↓",KEY_DOWN,0,KT_CODE, 52,9,4,1, 0,NULL,0,0},        /* bottom half */
  {"→",KEY_RIGHT,0,KT_CODE, 56,8,4,2, 0,NULL,0,0},       /* full height */
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

static void on_pressed(GtkGestureClick *g, int np, double x, double y, gpointer u) {
  (void)g;(void)x;(void)y;
  Key *k = u;
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
  (void)g;(void)np;(void)x;(void)y; key_up((Key *)u);
}
static void on_cancel(GtkGesture *g, GdkEventSequence *seq, gpointer u) {
  (void)g;(void)seq; key_up((Key *)u);
}

static const char *CSS =
  "window { background: rgba(20,20,20,0.92); }"
  ".key { margin: 2px; border-radius: 6px; background: #2b2b2b; color: #eee;"
  "       font-size: 18px; }"
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
  gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);

  GtkCssProvider *css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(css, CSS);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
      GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_widget_set_vexpand(grid, TRUE);
  gtk_widget_set_hexpand(grid, TRUE);

  /* Height: full width (anchored L+R); height ~ width/3 so the keys are roughly
   * square like the real keyboard, capped at half the screen so it is always a
   * bottom strip and never fills the display. */
  int kbd_h = 320;
  {
    GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
    GdkMonitor *m0 = mons ? g_list_model_get_item(mons, 0) : NULL;
    if (m0) {
      GdkRectangle geo; gdk_monitor_get_geometry(m0, &geo);
      kbd_h = geo.width / 3;
      int cap = geo.height * 2 / 5;   /* never taller than 40% of the screen */
      if (kbd_h > cap) kbd_h = cap;
      g_object_unref(m0);   /* g_list_model_get_item() returns an owned ref */
    }
    const char *he = g_getenv("FW12TAB_OSK_HEIGHT");
    if (he && *he) kbd_h = atoi(he);
  }
  gtk_window_set_default_size(GTK_WINDOW(win), -1, kbd_h);
  gtk_widget_set_size_request(win, -1, kbd_h);
  /* Reserve the strip so tiled windows shrink to sit ABOVE the keyboard instead
   * of being covered by it (same mechanism waybar uses). */
  gtk_layer_set_exclusive_zone(GTK_WINDOW(win), kbd_h);

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
    gtk_grid_attach(GTK_GRID(grid), key, k->col, k->row, k->wspan, k->hspan);
    k->button = key;
  }

  relabel_keys();   /* set initial keycap symbols from the keymap */
  gtk_window_set_child(GTK_WINDOW(win), grid);
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
