/*
 * Backroot 8 start menu — GTK4 + CSS, Windows 8–style tile grid
 */
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define APP_ID "io.backroot.startmenu"
#define CSS_PATH "/usr/share/backroot/br8-start-menu/br8-start-menu.css"
#define CTL_FIFO "/tmp/br8-start-menu.ctl"
#define WALLPAPER "/usr/share/backgrounds/backroot8.jpg"
#define TILE_SMALL 118
#define TILE_LARGE 244
#define TILE_GAP 8

typedef struct {
    const char *label;
    const char *icon;
    const char *color;
    const char *exec_cmd;
    int large;
} PinnedTile;

typedef struct {
    char *name;
    char *exec;
    char *icon_char;
    char *color;
} AppEntry;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *root_stack;
    GtkWidget *tiles_page;
    GtkWidget *tile_scroll;
    GtkAdjustment *hadjust;
    GList *fly_tiles;
    gboolean visible;
    gboolean apps_mode;
    int drag_scroll_active;
    double drag_begin_y;
    int ctl_fd;
} AppState;

static AppState g_state;
static gboolean g_daemon_mode;

static const PinnedTile pinned_tiles[] = {
    { "Desktop", NULL, NULL, NULL, 1 },
    { "Terminal", ">_", "#0078d7",
      "xterm -fa Monospace -fs 11 -bg \"#1a1a22\" -fg \"#e8e8ec\" -title root@Backroot8 -e /bin/bash -l", 0 },
    { "Files", "\xE2\x9A\xB2", "#7fba00", "dolphin /", 0 },
    { "Settings", "\xE2\x9A\x99", "#5131a9", "/usr/local/bin/backroot-hello", 0 },
    { "Store", "S", "#5131a9", NULL, 0 },
    { "Maps", "\xE2\x97\x8F", "#00a300", NULL, 0 },
    { "Mail", "@", "#0078d7", NULL, 0 },
    { "Calendar", "31", "#007233", NULL, 0 },
    { "Photos", "\xE2\x99\xA6", "#ea005e", NULL, 0 },
    { "Music", "\xE2\x99\xAB", "#e3008c", NULL, 0 },
    { "Video", "\xE2\x96\xB6", "#e81123", NULL, 0 },
    { "Camera", "\xE2\x97\x8B", "#68217a", NULL, 0 },
};

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *paths[] = {
        CSS_PATH,
        "br8-start-menu.css",
        "../rootfs-overlay/usr/share/backroot/br8-start-menu/br8-start-menu.css",
        NULL,
    };

    for (int i = 0; paths[i]; i++) {
        if (g_file_test(paths[i], G_FILE_TEST_EXISTS)) {
            gtk_css_provider_load_from_path(provider, paths[i]);
            gtk_style_context_add_provider_for_display(
                gdk_display_get_default(),
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_object_unref(provider);
            return;
        }
    }
    g_object_unref(provider);
}

static void launch_exec(const char *cmd) {
    if (!cmd || !cmd[0])
        return;
    g_spawn_command_line_async(cmd, NULL);
}

static void apply_box_color(GtkWidget *box, const char *color, int w, int h) {
    char css_buf[128];
    GtkCssProvider *p;

    snprintf(css_buf, sizeof css_buf,
        "box { background-color: %s; min-width: %dpx; min-height: %dpx; }",
        color, w, h);
    p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p, css_buf);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(box),
        GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(p);
}

static void app_entry_free(gpointer p) {
    AppEntry *e = p;
    g_free(e->name);
    g_free(e->exec);
    g_free(e->icon_char);
    g_free(e->color);
    g_free(e);
}

static AppEntry *app_entry_new(const char *name, const char *exec,
    const char *icon, const char *color) {
    AppEntry *e = g_new0(AppEntry, 1);
    e->name = g_strdup(name);
    e->exec = g_strdup(exec);
    e->icon_char = g_strdup(icon);
    e->color = g_strdup(color);
    return e;
}

static GtkWidget *make_tile_button(AppState *st, const PinnedTile *def) {
    GtkWidget *btn;
    GtkWidget *overlay;
    GtkWidget *inner;
    GtkWidget *icon;
    GtkWidget *label;
    int w = def->large ? TILE_LARGE : TILE_SMALL;
    int h = def->large ? TILE_LARGE : TILE_SMALL;
    const char *bg = def->color ? def->color : "#2d5a87";

    btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "tile");
    gtk_widget_add_css_class(btn, def->large ? "tile-large" : "tile-small");
    gtk_widget_add_css_class(btn, "tile-fly");
    gtk_widget_add_css_class(btn, "animating");
    if (def->large && !def->color)
        gtk_widget_add_css_class(btn, "tile-desktop");
    gtk_widget_set_size_request(btn, w, h);

    overlay = gtk_overlay_new();
    gtk_button_set_child(GTK_BUTTON(btn), overlay);

    if (def->large && !def->exec_cmd) {
        if (g_file_test(WALLPAPER, G_FILE_TEST_EXISTS)) {
            GtkWidget *pic = gtk_picture_new_for_filename(WALLPAPER);
            gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_COVER);
            gtk_widget_set_size_request(pic, w, h);
            gtk_overlay_set_child(GTK_OVERLAY(overlay), pic);
        } else {
            inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_widget_set_size_request(inner, w, h);
            apply_box_color(inner, "#2d5a87", w, h);
            gtk_overlay_set_child(GTK_OVERLAY(overlay), inner);
        }
    } else {
        inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_size_request(inner, w, h);
        apply_box_color(inner, bg, w, h);
        gtk_overlay_set_child(GTK_OVERLAY(overlay), inner);

        if (def->icon) {
            icon = gtk_label_new(def->icon);
            gtk_widget_add_css_class(icon, "tile-icon");
            gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
            gtk_widget_set_hexpand(icon, TRUE);
            gtk_widget_set_vexpand(icon, TRUE);
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), icon);
        }
    }

    label = gtk_label_new(def->label);
    gtk_widget_add_css_class(label, "tile-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_END);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), label);

    g_object_set_data(G_OBJECT(btn), "exec", (gpointer)def->exec_cmd);
    g_object_set_data(G_OBJECT(btn), "desktop",
        GINT_TO_POINTER(def->large && !def->exec_cmd ? 1 : 0));

    st->fly_tiles = g_list_append(st->fly_tiles, btn);
    return btn;
}

static GtkWidget *build_tile_columns(AppState *st) {
    GtkWidget *hbox;
    GtkWidget *col;

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, TILE_GAP);
    gtk_widget_add_css_class(hbox, "tile-columns");

    col = gtk_box_new(GTK_ORIENTATION_VERTICAL, TILE_GAP);
    gtk_widget_add_css_class(col, "tile-column");
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[0]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[1]));
    gtk_box_append(GTK_BOX(hbox), col);

    col = gtk_box_new(GTK_ORIENTATION_VERTICAL, TILE_GAP);
    gtk_widget_add_css_class(col, "tile-column");
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[2]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[3]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[4]));
    gtk_box_append(GTK_BOX(hbox), col);

    col = gtk_box_new(GTK_ORIENTATION_VERTICAL, TILE_GAP);
    gtk_widget_add_css_class(col, "tile-column");
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[5]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[6]));
    gtk_box_append(GTK_BOX(hbox), col);

    col = gtk_box_new(GTK_ORIENTATION_VERTICAL, TILE_GAP);
    gtk_widget_add_css_class(col, "tile-column");
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[7]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[8]));
    gtk_box_append(GTK_BOX(hbox), col);

    col = gtk_box_new(GTK_ORIENTATION_VERTICAL, TILE_GAP);
    gtk_widget_add_css_class(col, "tile-column");
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[9]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[10]));
    gtk_box_append(GTK_BOX(col), make_tile_button(st, &pinned_tiles[11]));
    gtk_box_append(GTK_BOX(hbox), col);

    return hbox;
}

static GPtrArray *scan_desktop_files(void) {
    GPtrArray *apps = g_ptr_array_new_with_free_func(app_entry_free);
    GDir *d = g_dir_open("/usr/share/applications", 0, NULL);
    const char *name;

    if (!d)
        return apps;

    while ((name = g_dir_read_name(d)) != NULL) {
        char *path;
        GKeyFile *kf;
        char *app_name;
        char *exec;
        AppEntry *e;

        if (!g_str_has_suffix(name, ".desktop"))
            continue;

        path = g_build_filename("/usr/share/applications", name, NULL);
        kf = g_key_file_new();
        if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
            g_key_file_free(kf);
            g_free(path);
            continue;
        }
        if (g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", NULL) ||
            g_key_file_get_boolean(kf, "Desktop Entry", "Hidden", NULL)) {
            g_key_file_free(kf);
            g_free(path);
            continue;
        }

        app_name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
        exec = g_key_file_get_string(kf, "Desktop Entry", "Exec", NULL);
        if (!app_name || !exec) {
            g_free(app_name);
            g_free(exec);
            g_key_file_free(kf);
            g_free(path);
            continue;
        }

        {
            gchar **parts = g_strsplit(exec, " ", -1);
            GString *cmd = g_string_new(NULL);
            gchar **p;

            for (p = parts; p && *p; p++) {
                if ((*p)[0] == '%')
                    continue;
                if (cmd->len)
                    g_string_append_c(cmd, ' ');
                g_string_append(cmd, *p);
            }
            g_free(exec);
            exec = g_string_free(cmd, FALSE);
            g_strfreev(parts);
        }

        e = g_new0(AppEntry, 1);
        e->name = app_name;
        e->exec = exec;
        e->icon_char = g_utf8_substring(app_name, 0, 1);
        e->color = g_strdup("#0078d7");
        g_ptr_array_add(apps, e);
        g_key_file_free(kf);
        g_free(path);
    }
    g_dir_close(d);
    return apps;
}

static GtkWidget *make_app_row(AppEntry *e) {
    GtkWidget *btn;
    GtkWidget *box;
    GtkWidget *icon_box;
    GtkWidget *icon_lbl;
    GtkWidget *name_lbl;

    btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "app-row");
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_button_set_child(GTK_BUTTON(btn), box);

    icon_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(icon_box, "app-icon");
    apply_box_color(icon_box, e->color ? e->color : "#0078d7", 32, 32);

    icon_lbl = gtk_label_new(e->icon_char && e->icon_char[0] ? e->icon_char : "?");
    gtk_widget_set_halign(icon_lbl, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(icon_box), icon_lbl);
    gtk_box_append(GTK_BOX(box), icon_box);

    name_lbl = gtk_label_new(e->name);
    gtk_widget_add_css_class(name_lbl, "app-name");
    gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), name_lbl);

    g_object_set_data_full(G_OBJECT(btn), "exec", g_strdup(e->exec), g_free);
    return btn;
}

static GtkWidget *build_apps_column(const char *title, GPtrArray *entries) {
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *heading = gtk_label_new(title);
    guint i;

    gtk_widget_add_css_class(heading, "apps-group-title");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(col), heading);

    for (i = 0; i < entries->len; i++)
        gtk_box_append(GTK_BOX(col), make_app_row(g_ptr_array_index(entries, i)));
    return col;
}

static GtkWidget *build_apps_page(void) {
    GtkWidget *outer;
    GtkWidget *header;
    GtkWidget *scroll;
    GtkWidget *hbox;
    GPtrArray *all;
    GPtrArray *system;
    GPtrArray *installed;
    guint i;

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(outer, "apps-overlay");
    gtk_widget_set_hexpand(outer, TRUE);
    gtk_widget_set_vexpand(outer, TRUE);

    header = gtk_label_new("Home");
    gtk_widget_add_css_class(header, "apps-header");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(outer), header);

    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_add_css_class(scroll, "tile-scroll");

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    gtk_widget_add_css_class(hbox, "apps-columns");

    all = scan_desktop_files();
    system = g_ptr_array_new_with_free_func(app_entry_free);
    installed = g_ptr_array_new_with_free_func(app_entry_free);

    for (i = 0; i < all->len; i++) {
        AppEntry *e = g_ptr_array_index(all, i);
        if (strstr(e->exec, "backroot-hello") || strstr(e->name, "Backroot")) {
            g_ptr_array_add(system,
                app_entry_new(e->name, e->exec, e->icon_char, e->color));
        } else if (strstr(e->exec, "xterm") || strstr(e->exec, "dolphin")) {
            g_ptr_array_add(installed,
                app_entry_new(e->name, e->exec, e->icon_char, e->color));
        }
    }

    if (installed->len == 0) {
        g_ptr_array_add(installed,
            app_entry_new("Terminal",
                "xterm -fa Monospace -fs 11 -bg \"#1a1a22\" -fg \"#e8e8ec\" "
                "-title root@Backroot8 -e /bin/bash -l",
                ">_", "#0078d7"));
        g_ptr_array_add(installed,
            app_entry_new("Dolphin", "dolphin /", "\xE2\x9A\xB2", "#7fba00"));
    }
    if (system->len == 0) {
        g_ptr_array_add(system,
            app_entry_new("Backroot Hello", "/usr/local/bin/backroot-hello", "S", "#5131a9"));
    }

    gtk_box_append(GTK_BOX(hbox), build_apps_column("System", system));
    gtk_box_append(GTK_BOX(hbox), build_apps_column("Installed", installed));

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), hbox);
    gtk_box_append(GTK_BOX(outer), scroll);

    g_ptr_array_unref(all);
    g_ptr_array_unref(system);
    g_ptr_array_unref(installed);
    return outer;
}

static void set_apps_mode(AppState *st, gboolean apps) {
    st->apps_mode = apps;
    if (apps)
        gtk_stack_set_visible_child_name(GTK_STACK(st->root_stack), "apps");
    else
        gtk_stack_set_visible_child_name(GTK_STACK(st->root_stack), "tiles");
}

static void hide_menu(AppState *st) {
    if (!st->visible)
        return;
    st->visible = FALSE;
    gtk_widget_set_visible(st->window, FALSE);
    set_apps_mode(st, FALSE);
}

static gboolean reveal_tile_cb(gpointer data) {
    gtk_widget_add_css_class(GTK_WIDGET(data), "revealed");
    return G_SOURCE_REMOVE;
}

static gboolean fade_overlay_cb(gpointer data) {
    AppState *st = data;
    if (st->tiles_page)
        gtk_widget_add_css_class(st->tiles_page, "visible");
    return G_SOURCE_REMOVE;
}

static void reveal_tiles_staggered(AppState *st) {
    guint delay = 0;
    for (GList *l = st->fly_tiles; l; l = l->next) {
        g_timeout_add(delay, reveal_tile_cb, l->data);
        delay += 45;
    }
}

static void show_menu(AppState *st) {
    if (st->visible) {
        hide_menu(st);
        return;
    }

    st->visible = TRUE;
    set_apps_mode(st, FALSE);

    if (st->tiles_page) {
        gtk_widget_remove_css_class(st->tiles_page, "visible");
        gtk_widget_add_css_class(st->tiles_page, "visible");
    }

    gtk_widget_set_visible(st->window, TRUE);
    gtk_window_fullscreen(GTK_WINDOW(st->window));
    gtk_window_present(GTK_WINDOW(st->window));

    for (GList *l = st->fly_tiles; l; l = l->next)
        gtk_widget_remove_css_class(GTK_WIDGET(l->data), "revealed");

    g_timeout_add(20, fade_overlay_cb, st);
    reveal_tiles_staggered(st);
}

static void on_tile_clicked(GtkButton *btn, gpointer user_data) {
    AppState *st = user_data;
    const char *exec;
    gboolean desktop;

    desktop = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "desktop"));
    if (desktop) {
        hide_menu(st);
        return;
    }

    exec = g_object_get_data(G_OBJECT(btn), "exec");
    if (exec && exec[0]) {
        launch_exec(exec);
        hide_menu(st);
    }
}

static void on_app_row_clicked(GtkButton *btn, gpointer user_data) {
    AppState *st = user_data;
    const char *exec = g_object_get_data(G_OBJECT(btn), "exec");
    if (exec && exec[0]) {
        launch_exec(exec);
        hide_menu(st);
    }
}

static void connect_tile_clicks(GtkWidget *widget, AppState *st) {
    if (GTK_IS_BUTTON(widget)) {
        g_signal_connect(widget, "clicked", G_CALLBACK(on_tile_clicked), st);
        return;
    }
    if (GTK_IS_BOX(widget)) {
        GtkWidget *child = gtk_widget_get_first_child(widget);
        while (child) {
            connect_tile_clicks(child, st);
            child = gtk_widget_get_next_sibling(child);
        }
    }
}

static void connect_app_clicks(GtkWidget *widget, AppState *st) {
    if (GTK_IS_BUTTON(widget) && g_object_get_data(G_OBJECT(widget), "exec")) {
        g_signal_connect(widget, "clicked", G_CALLBACK(on_app_row_clicked), st);
        return;
    }
    if (GTK_IS_BOX(widget)) {
        GtkWidget *child = gtk_widget_get_first_child(widget);
        while (child) {
            connect_app_clicks(child, st);
            child = gtk_widget_get_next_sibling(child);
        }
    }
}

static gboolean on_scroll(GtkEventControllerScroll *ctrl, double dx, double dy,
    gpointer user_data) {
    AppState *st = user_data;
    GtkScrolledWindow *sw;
    GtkAdjustment *adj;
    double step;

    (void)ctrl;
    sw = GTK_SCROLLED_WINDOW(st->tile_scroll);
    adj = gtk_scrolled_window_get_hadjustment(sw);
    if (!adj)
        return FALSE;

    step = (fabs(dx) > fabs(dy) ? dx : dy) * 48.0;
    gtk_adjustment_set_value(adj,
        CLAMP(gtk_adjustment_get_value(adj) + step,
            gtk_adjustment_get_lower(adj),
            gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj)));
    return TRUE;
}

static double drag_last_offset;

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data) {
    AppState *st = user_data;
    (void)gesture;
    (void)x;
    st->drag_scroll_active = 1;
    st->drag_begin_y = y;
    drag_last_offset = 0.0;
}

static void on_drag_update_simple(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer user_data) {
    AppState *st = user_data;
    double delta;

    (void)offset_y;
    (void)gesture;
    if (!st->drag_scroll_active || !st->hadjust)
        return;

    delta = offset_x - drag_last_offset;
    drag_last_offset = offset_x;

    gtk_adjustment_set_value(st->hadjust,
        CLAMP(gtk_adjustment_get_value(st->hadjust) - delta,
            gtk_adjustment_get_lower(st->hadjust),
            gtk_adjustment_get_upper(st->hadjust) - gtk_adjustment_get_page_size(st->hadjust)));
}

static void on_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer user_data) {
    AppState *st = user_data;
    (void)gesture;
    (void)offset_x;

    if (!st->apps_mode && offset_y < -80.0)
        set_apps_mode(st, TRUE);
    else if (st->apps_mode && offset_y > 80.0)
        set_apps_mode(st, FALSE);

    st->drag_scroll_active = 0;
}

static void on_swipe(GtkGestureSwipe *gesture, double velocity_x, double velocity_y,
    gpointer user_data) {
    AppState *st = user_data;
    (void)gesture;
    (void)velocity_x;

    if (velocity_y < -400.0)
        set_apps_mode(st, TRUE);
    else if (velocity_y > 400.0 && st->apps_mode)
        set_apps_mode(st, FALSE);
}

static gboolean on_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
    GdkModifierType state, gpointer user_data) {
    AppState *st = user_data;
    (void)ctrl;
    (void)keycode;
    (void)state;

    if (keyval == GDK_KEY_Escape) {
        if (st->apps_mode)
            set_apps_mode(st, FALSE);
        else
            hide_menu(st);
        return TRUE;
    }
    return FALSE;
}

static gboolean ctl_io_cb(GIOChannel *source, GIOCondition cond, gpointer user_data) {
    char buf[8];
    gsize nread = 0;
    GIOStatus status;
    AppState *st = user_data;

    (void)cond;
    status = g_io_channel_read_chars(source, buf, sizeof buf - 1, &nread, NULL);
    if (status == G_IO_STATUS_NORMAL && nread > 0)
        show_menu(st);
    return TRUE;
}

static void setup_ctl_fifo(AppState *st) {
    GIOChannel *ch;

    unlink(CTL_FIFO);
    if (mkfifo(CTL_FIFO, 0600) != 0 && errno != EEXIST)
        return;

    st->ctl_fd = open(CTL_FIFO, O_RDONLY | O_NONBLOCK);
    if (st->ctl_fd < 0)
        return;

    ch = g_io_channel_unix_new(st->ctl_fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(ch, G_IO_IN | G_IO_PRI, ctl_io_cb, st);
    g_io_channel_unref(ch);
}

static GtkWidget *build_tiles_page(AppState *st) {
    GtkWidget *page;
    GtkWidget *top;
    GtkWidget *header;
    GtkWidget *hint;
    GtkWidget *tiles;
    GtkEventController *scroll_ctrl;
    GtkGesture *drag;
    GtkGesture *swipe;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "start-overlay");
    gtk_widget_add_css_class(page, "animating");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(top, TRUE);
    header = gtk_label_new("Home");
    gtk_widget_add_css_class(header, "start-header");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_widget_set_hexpand(header, TRUE);
    gtk_box_append(GTK_BOX(top), header);
    gtk_box_append(GTK_BOX(page), top);

    st->tile_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(st->tile_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_add_css_class(st->tile_scroll, "tile-scroll");
    gtk_widget_set_vexpand(st->tile_scroll, TRUE);

    tiles = build_tile_columns(st);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(st->tile_scroll), tiles);
    connect_tile_clicks(tiles, st);

    st->hadjust = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(st->tile_scroll));

    scroll_ctrl = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_scroll), st);
    gtk_widget_add_controller(st->tile_scroll, scroll_ctrl);

    drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), st);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update_simple), st);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), st);
    gtk_widget_add_controller(st->tile_scroll, GTK_EVENT_CONTROLLER(drag));

    swipe = gtk_gesture_swipe_new();
    gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(swipe), FALSE);
    g_signal_connect(swipe, "swipe", G_CALLBACK(on_swipe), st);
    gtk_widget_add_controller(page, GTK_EVENT_CONTROLLER(swipe));

    gtk_box_append(GTK_BOX(page), st->tile_scroll);

    hint = gtk_label_new("Swipe up for all apps");
    gtk_widget_add_css_class(hint, "swipe-hint");
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), hint);

    return page;
}

static void on_window_realize(GtkWidget *widget, gpointer user_data) {
#ifdef GDK_WINDOWING_X11
    GdkDisplay *gdpy;
    GdkSurface *surface;
    Display *xdpy;
    Window xwin;
    XClassHint hint;
    Atom wtype;
    Atom splash;

    (void)user_data;
    gdpy = gtk_widget_get_display(widget);
    if (!GDK_IS_X11_DISPLAY(gdpy))
        return;

    surface = gtk_native_get_surface(GTK_NATIVE(widget));
    if (!surface)
        return;

    xdpy = gdk_x11_display_get_xdisplay(gdpy);
    xwin = gdk_x11_surface_get_xid(surface);
    hint.res_name = "br8-start-menu";
    hint.res_class = "br8-start-menu";
    XSetClassHint(xdpy, xwin, &hint);

    wtype = XInternAtom(xdpy, "_NET_WM_WINDOW_TYPE", False);
    splash = XInternAtom(xdpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    XChangeProperty(xdpy, xwin, wtype, XA_ATOM, 32, PropModeReplace,
        (unsigned char *)&splash, 1);
#else
    (void)widget;
    (void)user_data;
#endif
}

static void activate(GtkApplication *app, gpointer user_data) {
    AppState *st = user_data;
    GtkWidget *apps_page;
    GtkEventController *key_ctrl;

    if (st->window)
        return;

    st->app = app;
    st->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(st->window), "");
    gtk_widget_set_name(st->window, "br8-start-menu");
    gtk_widget_add_css_class(st->window, "start-menu");
    gtk_window_set_decorated(GTK_WINDOW(st->window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(st->window));
    gtk_widget_set_visible(st->window, FALSE);
    g_signal_connect(st->window, "realize", G_CALLBACK(on_window_realize), NULL);

    st->root_stack = gtk_stack_new();
    gtk_widget_set_hexpand(st->root_stack, TRUE);
    gtk_widget_set_vexpand(st->root_stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(st->root_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(st->root_stack), 200);

    st->tiles_page = build_tiles_page(st);
    apps_page = build_apps_page();
    connect_app_clicks(apps_page, st);

    gtk_stack_add_named(GTK_STACK(st->root_stack), st->tiles_page, "tiles");
    gtk_stack_add_named(GTK_STACK(st->root_stack), apps_page, "apps");

    gtk_window_set_child(GTK_WINDOW(st->window), st->root_stack);

    key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_key), st);
    gtk_widget_add_controller(st->window, key_ctrl);

    {
        GtkGesture *swipe = gtk_gesture_swipe_new();
        gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(swipe), FALSE);
        g_signal_connect(swipe, "swipe", G_CALLBACK(on_swipe), st);
        gtk_widget_add_controller(apps_page, GTK_EVENT_CONTROLLER(swipe));
    }

    setup_ctl_fifo(st);
}

static gboolean startup_activate_idle(gpointer data) {
    g_application_activate(G_APPLICATION(data));
    return G_SOURCE_REMOVE;
}

static void on_startup(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    load_css();
    /* Always build hidden UI + control fifo when launched from xinitrc. */
    g_idle_add(startup_activate_idle, app);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0)
            g_daemon_mode = TRUE;
        else if (strcmp(argv[i], "--toggle") == 0) {
            int fd = open(CTL_FIFO, O_WRONLY | O_NONBLOCK);
            if (fd >= 0) {
                char c = 't';
                (void)write(fd, &c, 1);
                close(fd);
            }
            return 0;
        }
    }

    memset(&g_state, 0, sizeof g_state);
    g_set_prgname("br8-start-menu");

    app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &g_state);

    return g_application_run(G_APPLICATION(app), argc, argv);
}
