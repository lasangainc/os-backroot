/*
 * Backroot Hello — keyboard layout and timezone configurator (X11 + Xft)
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#define WIN_W 520
#define WIN_H 460
#define MARGIN 36
#define COMBO_H 38
#define COMBO_GAP 20
#define POPUP_W 420
#define POPUP_VISIBLE 10
#define POPUP_ITEM_H 28
#define POPUP_H (POPUP_VISIBLE * POPUP_ITEM_H)
#define APPLY_H 40
#define APPLY_W 120

#define BG_R 81
#define BG_G 49
#define BG_B 169
#define POPUP_BG_R 63
#define POPUP_BG_G 39
#define POPUP_BG_B 133

#define WALLPAPER "/usr/share/backgrounds/backroot8.jpg"

typedef struct {
    char **items;
    int n;
} StrList;

typedef struct {
    StrList kb;
    StrList tz;
    int kb_sel;
    int tz_sel;
    int active_combo; /* 0 none, 1 kb, 2 tz */
    int popup_scroll;
    Window popup;
    int popup_visible;
    int apply_hover;
} AppState;

static Display *dpy;
static int screen;
static Window root, win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static XftFont *title_font, *body_font, *label_font;
static AppState app;
static int win_x, win_y;

static void draw_all(void);

static unsigned long rgb(int r, int g, int b) {
    XColor c;
    c.red = (unsigned short)(r << 8);
    c.green = (unsigned short)(g << 8);
    c.blue = (unsigned short)(b << 8);
    c.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, cmap, &c))
        return BlackPixel(dpy, screen);
    return c.pixel;
}

static XRenderColor render_rgb(int r, int g, int b) {
    XRenderColor c;
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    c.alpha = 0xffff;
    return c;
}

static int text_baseline(int height, XftFont *font) {
    if (!font)
        return height - 10;
    return (height + font->ascent - font->descent) / 2;
}

static void xft_draw(Drawable draw, int x, int y, const char *text, int r, int g, int b,
        XftFont *font) {
    if (!font || !text || !text[0])
        return;
    XftDraw *xd = XftDrawCreate(dpy, draw, visual, cmap);
    if (!xd)
        return;
    XftColor col;
    XRenderColor rc = render_rgb(r, g, b);
    if (XftColorAllocValue(dpy, visual, cmap, &rc, &col)) {
        XftDrawStringUtf8(xd, &col, font, x, y, (FcChar8 *)text, (int)strlen(text));
        XftColorFree(dpy, visual, cmap, &col);
    }
    XftDrawDestroy(xd);
}

static void strlist_free(StrList *sl) {
    if (!sl)
        return;
    for (int i = 0; i < sl->n; i++)
        free(sl->items[i]);
    free(sl->items);
    sl->items = NULL;
    sl->n = 0;
}

static void strlist_add(StrList *sl, const char *s) {
    char **next = realloc(sl->items, (size_t)(sl->n + 1) * sizeof(char *));
    if (!next)
        return;
    sl->items = next;
    sl->items[sl->n++] = strdup(s);
}

static char *run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    char buf[512];
    char *out = NULL;
    size_t len = 0;

    if (!fp)
        return NULL;
    while (fgets(buf, sizeof buf, fp)) {
        size_t bl = strlen(buf);
        char *tmp = realloc(out, len + bl + 1);
        if (!tmp) {
            free(out);
            pclose(fp);
            return NULL;
        }
        out = tmp;
        memcpy(out + len, buf, bl);
        len += bl;
        out[len] = '\0';
    }
    pclose(fp);
    if (out) {
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' '))
            out[--len] = '\0';
    }
    return out;
}

static char *current_keymap(void) {
    char *out = run_command("localectl status 2>/dev/null | awk '/VC Keymap:/ {print $3}'");
    if (!out || !out[0]) {
        free(out);
        return strdup("us");
    }
    return out;
}

static char *current_timezone(void) {
    char *out = run_command("timedatectl show --property=Timezone --value 2>/dev/null");
    if (!out || !out[0]) {
        free(out);
        return strdup("UTC");
    }
    return out;
}

static void load_keymaps(StrList *sl) {
    static const char *fallback[] = {
        "us", "gb", "de", "fr", "es", "it", "pt", "ru", "jp", "pl", "se", "no", NULL,
    };
    FILE *fp = popen("localectl list-keymaps 2>/dev/null", "r");
    char line[128];

    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = '\0';
            if (line[0] && strchr(line, ' ') == NULL)
                strlist_add(sl, line);
        }
        pclose(fp);
    }
    if (sl->n == 0) {
        for (int i = 0; fallback[i]; i++)
            strlist_add(sl, fallback[i]);
    }
}

static void load_timezones(StrList *sl) {
    FILE *fp = popen("timedatectl list-timezones 2>/dev/null", "r");
    char line[128];

    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = '\0';
            if (line[0])
                strlist_add(sl, line);
        }
        pclose(fp);
    }
    if (sl->n == 0) {
        strlist_add(sl, "UTC");
        strlist_add(sl, "America/New_York");
        strlist_add(sl, "Europe/London");
    }
}

static int strlist_index(const StrList *sl, const char *value) {
    for (int i = 0; i < sl->n; i++) {
        if (strcmp(sl->items[i], value) == 0)
            return i;
    }
    return 0;
}

static int run_cmd(const char *cmd) {
    return system(cmd);
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

static void bump_panel_rev(void) {
    Atom rev_atom = XInternAtom(dpy, "_BR8_PANEL_REV", False);
    unsigned long rev = 1;
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;

    if (XGetWindowProperty(dpy, root, rev_atom, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        rev = data[0] + 1;
    if (data)
        XFree(data);
    XChangeProperty(dpy, root, rev_atom, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&rev, 1);
    XFlush(dpy);
}

static void refresh_desktop(void) {
    char cmd[512];

    snprintf(cmd, sizeof cmd,
        "command -v feh >/dev/null && feh --bg-fill '%s' 2>/dev/null; "
        "command -v xsetroot >/dev/null && xsetroot -solid '#1e2030'",
        WALLPAPER);
    run_cmd(cmd);
    bump_panel_rev();
}

static void apply_settings(void) {
    const char *kb = app.kb.items[app.kb_sel];
    const char *tz = app.tz.items[app.tz_sel];

    apply_keymap(kb);
    apply_timezone(tz);
    refresh_desktop();
}

static StrList *active_list(void) {
    if (app.active_combo == 1)
        return &app.kb;
    if (app.active_combo == 2)
        return &app.tz;
    return NULL;
}

static int active_selected(void) {
    if (app.active_combo == 1)
        return app.kb_sel;
    if (app.active_combo == 2)
        return app.tz_sel;
    return 0;
}

static void set_active_selected(int idx) {
    if (app.active_combo == 1)
        app.kb_sel = idx;
    else if (app.active_combo == 2)
        app.tz_sel = idx;
}

static void hide_popup(void) {
    if (!app.popup_visible)
        return;
    XUnmapWindow(dpy, app.popup);
    app.popup_visible = 0;
    app.active_combo = 0;
    draw_all();
}

static void popup_geom(int *px, int *py) {
    int combo_y = MARGIN + 120 + COMBO_GAP + COMBO_H;
    if (app.active_combo == 2)
        combo_y += COMBO_GAP + COMBO_H;
    *px = win_x + MARGIN;
    *py = win_y + combo_y + COMBO_H;
}

static void draw_popup(void) {
    StrList *sl = active_list();
    int sel;
    int x, y;

    if (!sl || !app.popup_visible)
        return;

    popup_geom(&x, &y);
    XMoveResizeWindow(dpy, app.popup, x, y, POPUP_W, POPUP_H);

    XSetForeground(dpy, gc, rgb(POPUP_BG_R, POPUP_BG_G, POPUP_BG_B));
    XFillRectangle(dpy, app.popup, gc, 0, 0, POPUP_W, POPUP_H);

    sel = active_selected();
    for (int row = 0; row < POPUP_VISIBLE; row++) {
        int idx = app.popup_scroll + row;
        int y0 = row * POPUP_ITEM_H;
        if (idx >= sl->n)
            break;
        if (idx == sel) {
            XSetForeground(dpy, gc, rgb(120, 100, 180));
            XFillRectangle(dpy, app.popup, gc, 0, y0, POPUP_W, POPUP_ITEM_H);
        }
        xft_draw(app.popup, 12, y0 + text_baseline(POPUP_ITEM_H, body_font),
            sl->items[idx], 255, 255, 255, body_font);
    }
}

static void show_popup(int combo) {
    StrList *sl;
    int sel;

    hide_popup();
    app.active_combo = combo;
    sl = active_list();
    if (!sl || sl->n == 0)
        return;

    sel = active_selected();
    app.popup_scroll = sel - POPUP_VISIBLE / 2;
    if (app.popup_scroll < 0)
        app.popup_scroll = 0;
    if (app.popup_scroll > sl->n - POPUP_VISIBLE)
        app.popup_scroll = sl->n - POPUP_VISIBLE;
    if (app.popup_scroll < 0)
        app.popup_scroll = 0;

    app.popup_visible = 1;
    draw_popup();
    XMapRaised(dpy, app.popup);
}

static int combo_y_for(int combo) {
    int y = MARGIN + 120;
    if (combo == 2)
        y += COMBO_GAP + COMBO_H;
    return y;
}

static void draw_subtitle(Drawable draw, int x, int y, XftFont *font) {
    static const char *lines[] = {
        "While Backroot is in prerelease you can use this application to",
        "configure your system. In the future there will be a proper app",
        "for this.",
        NULL
    };
    int line_y = y;

    for (int i = 0; lines[i]; i++) {
        xft_draw(draw, x, line_y, lines[i], 255, 255, 255, font);
        line_y += font ? font->height + 4 : 16;
    }
}

static void draw_combo(int y, const char *label, const StrList *sl, int sel) {
    int x0 = MARGIN;
    int w = WIN_W - MARGIN * 2;

    xft_draw(win, x0, y - 18, label, 255, 255, 255, label_font);

    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XDrawRectangle(dpy, win, gc, x0, y, w, COMBO_H);

    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XFillRectangle(dpy, win, gc, x0 + 1, y + 1, w - 2, COMBO_H - 2);
    XSetForeground(dpy, gc, rgb(BG_R, BG_G, BG_B));
    XFillRectangle(dpy, win, gc, x0 + 2, y + 2, w - 4, COMBO_H - 4);

    if (sel >= 0 && sel < sl->n)
        xft_draw(win, x0 + 12, y + text_baseline(COMBO_H, body_font),
            sl->items[sel], 255, 255, 255, body_font);

    xft_draw(win, x0 + w - 24, y + text_baseline(COMBO_H, body_font), "v", 255, 255, 255, body_font);
}

static int apply_button_y(void) {
    return combo_y_for(2) + COMBO_GAP + COMBO_H + 28;
}

static void draw_apply_button(void) {
    int y = apply_button_y();
    int x = MARGIN;
    int bg_r = app.apply_hover ? 255 : 255;
    int bg_g = app.apply_hover ? 255 : 255;
    int bg_b = app.apply_hover ? 255 : 255;
    int fg_r = app.apply_hover ? BG_R : 255;
    int fg_g = app.apply_hover ? BG_G : 255;
    int fg_b = app.apply_hover ? BG_B : 255;

    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XDrawRectangle(dpy, win, gc, x, y, APPLY_W, APPLY_H);
    XSetForeground(dpy, gc, rgb(bg_r, bg_g, bg_b));
    XFillRectangle(dpy, win, gc, x + 1, y + 1, APPLY_W - 2, APPLY_H - 2);
    xft_draw(win, x + 28, y + text_baseline(APPLY_H, label_font), "Apply",
        fg_r, fg_g, fg_b, label_font);
}

static void draw_all(void) {
    XSetForeground(dpy, gc, rgb(BG_R, BG_G, BG_B));
    XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);

    xft_draw(win, MARGIN, MARGIN + text_baseline(42, title_font),
        "Backroot Hello", 255, 255, 255, title_font);
    draw_subtitle(win, MARGIN, MARGIN + 56, body_font);

    draw_combo(combo_y_for(1), "Keyboard layout", &app.kb, app.kb_sel);
    draw_combo(combo_y_for(2), "Timezone", &app.tz, app.tz_sel);
    draw_apply_button();

    if (app.popup_visible)
        draw_popup();
}

static int point_in_combo(int combo, int x, int y) {
    int x0 = MARGIN;
    int w = WIN_W - MARGIN * 2;
    int y0 = combo_y_for(combo);
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + COMBO_H;
}

static int point_in_apply(int x, int y) {
    int y0 = apply_button_y();
    return x >= MARGIN && x < MARGIN + APPLY_W && y >= y0 && y < y0 + APPLY_H;
}

static int popup_item_at(int py) {
    int row = py / POPUP_ITEM_H;
    if (row < 0 || row >= POPUP_VISIBLE)
        return -1;
    return app.popup_scroll + row;
}

static void popup_scroll_by(int delta) {
    StrList *sl = active_list();
    if (!sl)
        return;
    app.popup_scroll += delta;
    if (app.popup_scroll < 0)
        app.popup_scroll = 0;
    if (app.popup_scroll > sl->n - POPUP_VISIBLE)
        app.popup_scroll = sl->n - POPUP_VISIBLE;
    if (app.popup_scroll < 0)
        app.popup_scroll = 0;
    draw_popup();
}

static void set_wm_name(void) {
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    const char *name = "Backroot Hello";
    XChangeProperty(dpy, win, net_wm_name, utf8, 8, PropModeReplace,
        (unsigned char *)name, (int)strlen(name));
    XStoreName(dpy, win, name);
}

static void open_fonts(void) {
    static const char *const title_names[] = {
        "Segoe UI Light-42:antialias=true",
        "Cantarell Light-42:antialias=true",
        "DejaVu Sans-42:antialias=true",
        NULL
    };
    static const char *const body_names[] = {
        "Segoe UI-10:antialias=true",
        "Cantarell-10:antialias=true",
        "DejaVu Sans-10:antialias=true",
        NULL
    };
    static const char *const label_names[] = {
        "Segoe UI Semibold-11:antialias=true",
        "Segoe UI-11:antialias=true",
        "Cantarell-11:antialias=true",
        NULL
    };

    for (int i = 0; title_names[i]; i++) {
        title_font = XftFontOpenName(dpy, screen, title_names[i]);
        if (title_font && title_font->ascent > 0)
            break;
        if (title_font) {
            XftFontClose(dpy, title_font);
            title_font = NULL;
        }
    }
    for (int i = 0; body_names[i]; i++) {
        body_font = XftFontOpenName(dpy, screen, body_names[i]);
        if (body_font && body_font->ascent > 0)
            break;
        if (body_font) {
            XftFontClose(dpy, body_font);
            body_font = NULL;
        }
    }
    for (int i = 0; label_names[i]; i++) {
        label_font = XftFontOpenName(dpy, screen, label_names[i]);
        if (label_font && label_font->ascent > 0)
            break;
        if (label_font) {
            XftFontClose(dpy, label_font);
            label_font = NULL;
        }
    }
}

int main(void) {
    char *cur_kb;
    char *cur_tz;
    XSetWindowAttributes attr;
    XWMHints hints;
    XClassHint class_hint = {
        .res_name = (char *)"backroot-hello",
        .res_class = (char *)"BackrootHello",
    };

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "backroot-hello: cannot open display\n");
        return 1;
    }

    XSetErrorHandler(NULL);
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    open_fonts();

    load_keymaps(&app.kb);
    load_timezones(&app.tz);
    cur_kb = current_keymap();
    cur_tz = current_timezone();
    app.kb_sel = strlist_index(&app.kb, cur_kb);
    app.tz_sel = strlist_index(&app.tz, cur_tz);
    free(cur_kb);
    free(cur_tz);

    win = XCreateSimpleWindow(dpy, root, 80, 60, WIN_W, WIN_H, 0,
        BlackPixel(dpy, screen), rgb(BG_R, BG_G, BG_B));
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
        PointerMotionMask;
    XChangeWindowAttributes(dpy, win, CWEventMask, &attr);

    hints.flags = StateHint;
    hints.initial_state = NormalState;
    XSetWMHints(dpy, win, &hints);
    XSetClassHint(dpy, win, &class_hint);
    set_wm_name();

    gc = XCreateGC(dpy, win, 0, NULL);

    app.popup = XCreateSimpleWindow(dpy, root, 0, 0, POPUP_W, POPUP_H, 1,
        rgb(255, 255, 255), rgb(POPUP_BG_R, POPUP_BG_G, POPUP_BG_B));
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
    XChangeWindowAttributes(dpy, app.popup, CWOverrideRedirect | CWEventMask, &attr);
    XUnmapWindow(dpy, app.popup);

    XMapRaised(dpy, win);
    draw_all();

    int xfd = ConnectionNumber(dpy);
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        select(xfd + 1, &fds, NULL, NULL, &tv);

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);

            if (ev.type == ConfigureNotify && ev.xconfigure.window == win) {
                win_x = ev.xconfigure.x;
                win_y = ev.xconfigure.y;
                if (app.popup_visible)
                    draw_popup();
            } else if (ev.type == Expose) {
                if (ev.xexpose.count == 0) {
                    if (ev.xexpose.window == win)
                        draw_all();
                    else if (ev.xexpose.window == app.popup)
                        draw_popup();
                }
            } else if (ev.type == MotionNotify && ev.xmotion.window == win) {
                int hover = point_in_apply((int)ev.xmotion.x, (int)ev.xmotion.y);
                if (hover != app.apply_hover) {
                    app.apply_hover = hover;
                    draw_all();
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.button == Button4) {
                    if (ev.xbutton.window == app.popup)
                        popup_scroll_by(-1);
                } else if (ev.xbutton.button == Button5) {
                    if (ev.xbutton.window == app.popup)
                        popup_scroll_by(1);
                } else if (ev.xbutton.window == app.popup) {
                    StrList *sl = active_list();
                    int idx = popup_item_at((int)ev.xbutton.y);
                    if (sl && idx >= 0 && idx < sl->n) {
                        set_active_selected(idx);
                        hide_popup();
                        draw_all();
                    }
                } else if (ev.xbutton.window == win) {
                    if (point_in_apply((int)ev.xbutton.x, (int)ev.xbutton.y)) {
                        apply_settings();
                    } else if (point_in_combo(1, (int)ev.xbutton.x, (int)ev.xbutton.y)) {
                        show_popup(1);
                    } else if (point_in_combo(2, (int)ev.xbutton.x, (int)ev.xbutton.y)) {
                        show_popup(2);
                    } else if (app.popup_visible) {
                        hide_popup();
                    }
                } else if (app.popup_visible) {
                    hide_popup();
                }
            }
        }
    }

    strlist_free(&app.kb);
    strlist_free(&app.tz);
    return 0;
}
