/*
 * Backroot Hello — lightweight system configurator (GTK4)
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APP_ID "io.backroot.hello"
#define CSS_PATH "/usr/share/backroot/backroot-hello/backroot-hello.css"

typedef struct {
    GtkWidget *kb_dropdown;
    GtkWidget *tz_dropdown;
    GPtrArray *kb_codes;
    GPtrArray *tz_names;
    gboolean applying;
} AppState;

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *paths[] = {
        CSS_PATH,
        "backroot-hello.css",
        "../rootfs-overlay/usr/share/backroot/backroot-hello/backroot-hello.css",
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

static char *run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    char *out;

    if (!fp)
        return NULL;

    out = g_strdup("");
    for (;;) {
        char buf[512];
        if (!fgets(buf, sizeof buf, fp))
            break;
        {
            char *tmp = g_strconcat(out, buf, NULL);
            g_free(out);
            out = tmp;
        }
    }
    pclose(fp);
    g_strstrip(out);
    return out;
}

static char *current_keymap(void) {
    char *out = run_command("localectl status 2>/dev/null | awk '/VC Keymap:/ {print $3}'");

    if (!out || !out[0]) {
        g_free(out);
        return g_strdup("us");
    }
    return out;
}

static char *current_timezone(void) {
    char *out = run_command("timedatectl show --property=Timezone --value 2>/dev/null");

    if (!out || !out[0]) {
        g_free(out);
        return g_strdup("UTC");
    }
    return out;
}

static GPtrArray *list_keymaps(void) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    static const char *fallback[] = {
        "us", "gb", "de", "fr", "es", "it", "pt", "ru", "jp", "pl", "se", "no", NULL,
    };
    FILE *fp = popen("localectl list-keymaps 2>/dev/null", "r");
    char line[128];

    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            g_strstrip(line);
            if (line[0] && strchr(line, ' ') == NULL)
                g_ptr_array_add(arr, g_strdup(line));
        }
        pclose(fp);
    }

    if (arr->len == 0) {
        for (int i = 0; fallback[i]; i++)
            g_ptr_array_add(arr, g_strdup(fallback[i]));
    }

    return arr;
}

static GPtrArray *list_timezones(void) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    FILE *fp = popen("timedatectl list-timezones 2>/dev/null", "r");
    char line[128];

    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            g_strstrip(line);
            if (line[0])
                g_ptr_array_add(arr, g_strdup(line));
        }
        pclose(fp);
    }

    if (arr->len == 0) {
        g_ptr_array_add(arr, g_strdup("UTC"));
        g_ptr_array_add(arr, g_strdup("America/New_York"));
        g_ptr_array_add(arr, g_strdup("Europe/London"));
    }

    return arr;
}

static int ptr_index(GPtrArray *arr, const char *value) {
    for (guint i = 0; i < arr->len; i++) {
        if (g_strcmp0(g_ptr_array_index(arr, i), value) == 0)
            return (int)i;
    }
    return 0;
}

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    return rc;
}

static void apply_keymap(const char *code) {
    char cmd[512];

    snprintf(cmd, sizeof cmd, "localectl set-keymap %s", code);
    if (run_cmd(cmd) != 0)
        return;

    snprintf(cmd, sizeof cmd, "localectl set-x11-keymap %s", code);
    run_cmd(cmd);

    snprintf(cmd, sizeof cmd, "setxkbmap %s", code);
    run_cmd(cmd);
}

static void apply_timezone(const char *tz) {
    char cmd[512];

    snprintf(cmd, sizeof cmd, "timedatectl set-timezone %s", tz);
    run_cmd(cmd);
}

static void on_keymap_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    AppState *state = user_data;
    guint selected;
    const char *code;

    (void)obj;
    (void)pspec;

    if (state->applying)
        return;

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->kb_dropdown));
    if (selected >= state->kb_codes->len)
        return;

    code = g_ptr_array_index(state->kb_codes, selected);
    apply_keymap(code);
}

static void on_timezone_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    AppState *state = user_data;
    guint selected;
    const char *tz;

    (void)obj;
    (void)pspec;

    if (state->applying)
        return;

    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->tz_dropdown));
    if (selected >= state->tz_names->len)
        return;

    tz = g_ptr_array_index(state->tz_names, selected);
    apply_timezone(tz);
}

static GtkWidget *make_setting_row(const char *label_text, GtkWidget *dropdown) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *label = gtk_label_new(label_text);

    gtk_widget_add_css_class(box, "setting-row");
    gtk_widget_add_css_class(label, "setting-label");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(dropdown, GTK_ALIGN_FILL);

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), dropdown);
    return box;
}

static void on_startup(GtkApplication *app, gpointer user_data) {
    (void)app;
    (void)user_data;
    load_css();
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *outer;
    GtkWidget *title;
    GtkWidget *subtitle;
    GtkWidget *settings;
    GtkStringList *kb_list;
    GtkStringList *tz_list;
    char *cur_kb;
    char *cur_tz;
    AppState *state = user_data;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Backroot Hello");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 420);
    gtk_widget_add_css_class(window, "hello-window");

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(outer, "hello-content");
    gtk_widget_set_margin_start(outer, 36);
    gtk_widget_set_margin_end(outer, 36);
    gtk_widget_set_margin_top(outer, 36);
    gtk_widget_set_margin_bottom(outer, 36);

    title = gtk_label_new("Backroot Hello");
    gtk_widget_add_css_class(title, "hello-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    subtitle = gtk_label_new(
        "While Backroot is in prerelease you can use this application to configure "
        "your system. In the future there will be a proper app for this.");
    gtk_widget_add_css_class(subtitle, "hello-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(subtitle), PANGO_WRAP_WORD);
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);

    kb_list = gtk_string_list_new(NULL);
    for (guint i = 0; i < state->kb_codes->len; i++)
        gtk_string_list_append(kb_list, g_ptr_array_index(state->kb_codes, i));

    tz_list = gtk_string_list_new(NULL);
    for (guint i = 0; i < state->tz_names->len; i++)
        gtk_string_list_append(tz_list, g_ptr_array_index(state->tz_names, i));

    state->kb_dropdown = gtk_drop_down_new(G_LIST_MODEL(kb_list), NULL);
    state->tz_dropdown = gtk_drop_down_new(G_LIST_MODEL(tz_list), NULL);
    gtk_widget_add_css_class(state->kb_dropdown, "hello-dropdown");
    gtk_widget_add_css_class(state->tz_dropdown, "hello-dropdown");

    cur_kb = current_keymap();
    cur_tz = current_timezone();

    state->applying = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->kb_dropdown), (guint)ptr_index(state->kb_codes, cur_kb));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->tz_dropdown), (guint)ptr_index(state->tz_names, cur_tz));
    state->applying = FALSE;

    g_free(cur_kb);
    g_free(cur_tz);

    g_signal_connect(state->kb_dropdown, "notify::selected", G_CALLBACK(on_keymap_changed), state);
    g_signal_connect(state->tz_dropdown, "notify::selected", G_CALLBACK(on_timezone_changed), state);

    settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_add_css_class(settings, "hello-settings");
    gtk_box_append(GTK_BOX(settings), make_setting_row("Keyboard layout", state->kb_dropdown));
    gtk_box_append(GTK_BOX(settings), make_setting_row("Timezone", state->tz_dropdown));

    gtk_box_append(GTK_BOX(outer), title);
    gtk_box_append(GTK_BOX(outer), subtitle);
    gtk_box_append(GTK_BOX(outer), settings);

    gtk_window_set_child(GTK_WINDOW(window), outer);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    AppState state = {0};
    int status;

    state.kb_codes = list_keymaps();
    state.tz_names = list_timezones();

    app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &state);

    status = g_application_run(G_APPLICATION(app), argc, argv);

    g_clear_pointer(&state.kb_codes, g_ptr_array_unref);
    g_clear_pointer(&state.tz_names, g_ptr_array_unref);
    g_object_unref(app);
    return status;
}
