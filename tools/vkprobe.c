#include <string.h>
/* Phase 0 probe: claim the fcitx5 virtual-keyboard client bus name and see
 * whether fcitx5 responds by exporting its VirtualKeyboardBackend object
 * (/org/fcitx/virtualkeyboard/impanel) and driving us.
 *
 * If it does, the fw12 keyboard can be an fcitx5 virtual-keyboard backend
 * rather than an input-method-v2 client -- which means it never has to fight
 * fcitx5 for the seat's single input-method slot.
 *
 *   cc -O2 -o vkprobe vkprobe.c $(pkg-config --cflags --libs gio-2.0)
 */
#include <gio/gio.h>
#include <stdio.h>

#define VK_NAME  "org.fcitx.Fcitx5.VirtualKeyboard"
#define VK_PATH  "/org/fcitx/virtualkeyboard/impanel"
#define VK_IFACE "org.fcitx.Fcitx5.VirtualKeyboard1"

static GDBusConnection *conn;

static void check_backend(const char *when) {
  GError *err = NULL;
  GVariant *r = g_dbus_connection_call_sync(
      conn, "org.fcitx.Fcitx5", VK_PATH,
      "org.freedesktop.DBus.Introspectable", "Introspect", NULL, NULL,
      G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &err);
  if (err) {
    printf("[%s] backend object NOT exported: %s\n", when, err->message);
    g_error_free(err);
    return;
  }
  const char *xml = NULL;
  g_variant_get(r, "(&s)", &xml);
  printf("[%s] backend object IS exported. Introspection:\n", when);
  /* print only method/interface lines to keep it readable */
  for (const char *p = xml; (p = strstr(p, "<")); p++) {
    if (!strncmp(p, "<interface", 10) || !strncmp(p, "<method", 7) ||
        !strncmp(p, "<signal", 7) || !strncmp(p, "<arg", 4)) {
      const char *e = strchr(p, '>');
      if (!e) break;
      printf("    %.*s\n", (int)(e - p + 1), p);
    }
  }
  g_variant_unref(r);
}

static void on_acquired(GDBusConnection *c, const gchar *name, gpointer u) {
  printf(">>> acquired bus name %s\n", name);
  g_usleep(1500 * 1000);
  check_backend("after-acquire");
}

static void on_lost(GDBusConnection *c, const gchar *name, gpointer u) {
  printf(">>> could NOT acquire %s (already owned, or refused)\n", name);
}

static gboolean quit_cb(gpointer loop) {
  g_main_loop_quit((GMainLoop *)loop);
  return FALSE;
}

int main(void) {
  GError *err = NULL;
  conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
  if (!conn) { fprintf(stderr, "no session bus: %s\n", err->message); return 1; }

  check_backend("before-acquire");

  guint id = g_bus_own_name_on_connection(conn, VK_NAME,
      G_BUS_NAME_OWNER_FLAGS_REPLACE, on_acquired, on_lost, NULL, NULL);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  g_timeout_add_seconds(8, quit_cb, loop);
  g_main_loop_run(loop);

  check_backend("final");
  g_bus_unown_name(id);
  return 0;
}
