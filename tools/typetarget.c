/* A GTK4 text field that logs everything it receives, so the injection tests
 * verify themselves instead of relying on someone reading the screen.
 *
 * It takes focus on map, so fcitx5 sees a focused text input and asks the
 * virtual keyboard to show. Every committed character is appended to
 * $FW12_TYPETARGET_LOG (default /tmp/fw12-typetarget.log) and flushed
 * immediately, so another process can read the result as it arrives.
 *
 *   cc -O2 -o typetarget typetarget.c $(pkg-config --cflags --libs gtk4)
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <unistd.h>

static FILE *outf;

static const char *log_path(void)
{
    const char *e = g_getenv("FW12_TYPETARGET_LOG");
    return (e && *e) ? e : "/tmp/fw12-typetarget.log";
}

static void on_changed(GtkEditable *ed, gpointer user)
{
    const char *text = gtk_editable_get_text(ed);

    if (!outf)
        return;
    /* Rewrite the whole buffer each time: simplest thing that cannot get out
     * of sync with the widget, and these strings are tiny. */
    fseek(outf, 0, SEEK_SET);
    if (ftruncate(fileno(outf), 0) != 0) { /* ignore */ }
    fprintf(outf, "%s\n", text ? text : "");
    fflush(outf);
}

static void on_activate(GtkApplication *app, gpointer user)
{
    GtkWidget *win = gtk_application_window_new(app);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *label = gtk_label_new("fw12 injection target -- typed text is logged");
    GtkWidget *entry = gtk_entry_new();

    gtk_window_set_title(GTK_WINDOW(win), "fw12-typetarget");
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 120);

    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_window_set_child(GTK_WINDOW(win), box);

    g_signal_connect(entry, "changed", G_CALLBACK(on_changed), NULL);

    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_grab_focus(entry);
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    outf = fopen(log_path(), "w+");
    if (outf)
        setvbuf(outf, NULL, _IONBF, 0);

    app = gtk_application_new("org.fw12.typetarget", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    if (outf)
        fclose(outf);
    return status;
}
