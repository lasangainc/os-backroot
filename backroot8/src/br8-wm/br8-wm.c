/*
 * Backroot 8 window manager - minimal X11 WM
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>

#include "../br8-panel/emblem.h"

#define TITLE_H 30
#define CHARMS_EDGE_ZONE 8
#define CHARMS_STRIP_W 76
#define CHARMS_BTN_H 92
#define CHARMS_EMBLEM_SZ 36
#define CHARMS_TIME_W 320
#define CHARMS_TIME_H 108
#define CHARMS_TIME_PAD 20
#define METRO_SWIPE_ZONE 56
#define METRO_SWIPE_THRESHOLD 72
#define PANEL_H 32
#define MENU_W 240
#define MENU_ITEM_H 32
#define MENU_ITEMS 2
#define MENU_H (MENU_ITEMS * MENU_ITEM_H)
#define CRASH_BAR_H 52
#define CRASH_TOGGLE_ZONE_H 24
#define CRASH_DRAWER_MAX_H 300
#define CRASH_PAD 12
#define CRASH_LINE_H 15
#define CRASH_MAX_LINES 96
#define CRASH_TEXT_MAX 8192
#define CRASH_FILE "/tmp/br8-panel.crash"
#define CRASH_RESTART "/tmp/br8-panel.restart"
#define BTN_W 30
#define CLOSE_W 46
#define RESIZE_SZ 14
#define MIN_FRAME_W 200
#define MIN_FRAME_H 120
#define MAX_TITLE 256

typedef struct {
    Window frame;
    Window title;
    Window client;
    Window btn_min;
    Window btn_max;
    Window btn_close;
    Window resize_grip;
    char name[MAX_TITLE];
    int x, y, w, h;
    int mapped;
    int maximized;
    int saved_x, saved_y, saved_w, saved_h;
    int metro;
} Client;

static Display *dpy;
static int screen;
static Window root;
static Colormap cmap;
static XftFont *ui_font;
static Visual *visual;
static Colormap xft_cmap;
static GC gc_title, gc_btn, gc_close_fill, gc_close_x;
static Atom wm_protocols, wm_delete;
static Atom net_wm_name, net_client_list, utf8_string;
static Atom br8_frame, br8_client, br8_panel_rev, br8_activate;
static Atom br8_metro, br8_start_open, br8_metro_active;
static void unmap_client(Client *c);
static void remove_client(Client *c);
static void close_client(Client *c);
static void charms_hide(void);
static int metro_swipe;
static int metro_swipe_y;
static Window charms_strip, charms_clock;
static int charms_visible;
static int charms_hover;
static Pixmap charms_emblem_pm;
static int charms_emblem_ready;
static XftFont *charms_time_font;
static XftFont *charms_date_font;
static time_t charms_clock_last;
static Atom net_wm_window_type, net_wm_window_type_desktop;
static Client clients[256];
static int nclients;
static int dragging;
static int drag_x, drag_y;
static Client *drag_client;
static int resizing;
static int resize_x, resize_y;
static int resize_w, resize_h;
static Client *resize_client;
static Window menu_win;
static int menu_visible;
static int menu_hover = -1;
static unsigned long panel_rev;
static Window crash_bar, crash_drawer;
static int crash_visible;
static int crash_drawer_open;
static int crash_drawer_h;
static int root_w, root_h;
static time_t crash_mtime;
static char crash_text[CRASH_TEXT_MAX];
static char crash_lines[CRASH_MAX_LINES][256];
static int n_crash_lines;
static XftFont *mono_font;

static const char *const menu_labels[MENU_ITEMS] = {
    "New terminal at root",
    "Dolphin file explorer",
};

static unsigned long rgb(int r, int g, int b) {
    XColor c;
    c.red = r << 8;
    c.green = g << 8;
    c.blue = b << 8;
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

static int text_baseline(int height) {
    if (!ui_font)
        return height - 8;
    return (height + ui_font->ascent - ui_font->descent) / 2;
}

static void xft_draw(Window win, int x, int y, const char *text, int r, int g, int b) {
    if (!ui_font || !text || !text[0])
        return;
    XftDraw *xd = XftDrawCreate(dpy, win, visual, xft_cmap);
    if (!xd)
        return;
    XftColor col;
    XRenderColor rc = render_rgb(r, g, b);
    if (XftColorAllocValue(dpy, visual, xft_cmap, &rc, &col)) {
        XftDrawStringUtf8(xd, &col, ui_font, x, y, (FcChar8 *)text, (int)strlen(text));
        XftColorFree(dpy, visual, xft_cmap, &col);
    }
    XftDrawDestroy(xd);
}

static int btn_total_width(void) {
    return BTN_W + BTN_W + CLOSE_W;
}

static Client *find_client(Window w) {
    for (int i = 0; i < nclients; i++) {
        if (clients[i].frame == w || clients[i].title == w ||
            clients[i].client == w || clients[i].btn_min == w ||
            clients[i].btn_max == w || clients[i].btn_close == w ||
            clients[i].resize_grip == w)
            return &clients[i];
    }
    return NULL;
}

static Client *find_by_client(Window client) {
    for (int i = 0; i < nclients; i++)
        if (clients[i].client == client)
            return &clients[i];
    return NULL;
}

static Client *find_by_frame(Window frame) {
    for (int i = 0; i < nclients; i++)
        if (clients[i].frame == frame)
            return &clients[i];
    return NULL;
}

static int is_our_chrome(Window w) {
    if (!w)
        return 1;
    if (find_client(w) || find_by_frame(w) || find_by_client(w))
        return 1;
    Window root_ret, parent;
    Window *children = NULL;
    unsigned int nch = 0;
    if (!XQueryTree(dpy, w, &root_ret, &parent, &children, &nch)) {
        if (children)
            XFree(children);
        return 1;
    }
    if (children)
        XFree(children);
    for (int i = 0; i < nclients; i++) {
        if (parent == clients[i].frame)
            return 1;
    }
    return 0;
}

static void bump_panel(void) {
    panel_rev++;
    XChangeProperty(dpy, root, br8_panel_rev, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&panel_rev, 1);
}

static void update_net_client_list(void) {
    Window list[256];
    for (int i = 0; i < nclients; i++)
        list[i] = clients[i].client;
    XChangeProperty(dpy, root, net_client_list, XA_WINDOW, 32, PropModeReplace,
        (unsigned char *)list, nclients);
    bump_panel();
}

static void fetch_client_title(Client *c) {
    char *name = NULL;
    Atom type;
    int fmt;
    unsigned long n, bytes;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, c->client, net_wm_name, 0, 1024, False,
            utf8_string, &type, &fmt, &n, &bytes, &data) == Success && data && n > 0) {
        snprintf(c->name, MAX_TITLE, "%.*s", (int)(MAX_TITLE - 1), (char *)data);
        XFree(data);
        return;
    }

    if (XFetchName(dpy, c->client, &name) && name && name[0]) {
        strncpy(c->name, name, MAX_TITLE - 1);
        c->name[MAX_TITLE - 1] = '\0';
        XFree(name);
        return;
    }

    XClassHint hint;
    if (XGetClassHint(dpy, c->client, &hint)) {
        const char *use = hint.res_name && hint.res_name[0] ? hint.res_name :
                          hint.res_class && hint.res_class[0] ? hint.res_class : NULL;
        if (use) {
            strncpy(c->name, use, MAX_TITLE - 1);
            c->name[MAX_TITLE - 1] = '\0';
        }
        if (hint.res_name) XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
        if (c->name[0])
            return;
    }

    strncpy(c->name, "Window", MAX_TITLE);
}

static void draw_chrome_btn_bg(Window w) {
    XSetForeground(dpy, gc_title, rgb(43, 43, 43));
    XFillRectangle(dpy, w, gc_title, 0, 0, BTN_W, TITLE_H);
}

static void draw_min_button(Client *c) {
    draw_chrome_btn_bg(c->btn_min);
    XSetForeground(dpy, gc_btn, rgb(255, 255, 255));
    int cx = BTN_W / 2;
    int cy = TITLE_H / 2;
    XDrawLine(dpy, c->btn_min, gc_btn, cx - 5, cy, cx + 5, cy);
    XDrawLine(dpy, c->btn_min, gc_btn, cx - 5, cy + 1, cx + 5, cy + 1);
}

static void draw_max_button(Client *c) {
    draw_chrome_btn_bg(c->btn_max);
    XSetForeground(dpy, gc_btn, rgb(255, 255, 255));
    int cx = BTN_W / 2;
    int cy = TITLE_H / 2;
    if (c->maximized) {
        int s = 4;
        XDrawRectangle(dpy, c->btn_max, gc_btn, cx - s, cy - s + 2, s, s);
        XDrawRectangle(dpy, c->btn_max, gc_btn, cx - s + 3, cy - s - 1, s, s);
    } else {
        int s = 5;
        XDrawRectangle(dpy, c->btn_max, gc_btn, cx - s, cy - s, s * 2, s * 2);
    }
}

static void draw_close_button(Client *c) {
    /* Win7/8: wide solid red cap flush to top-right; white elongated X */
    XSetForeground(dpy, gc_close_fill, rgb(196, 43, 28));
    XFillRectangle(dpy, c->btn_close, gc_close_fill, 0, 0, CLOSE_W, TITLE_H);
    XSetForeground(dpy, gc_close_x, rgb(255, 255, 255));
    int cx = CLOSE_W / 2;
    int cy = TITLE_H / 2;
    int dx = 6;
    int dy = 5;
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx, cy - dy, cx + dx, cy + dy);
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx, cy + dy, cx + dx, cy - dy);
}

static void draw_resize_grip(Client *c) {
    int w = RESIZE_SZ;
    int h = RESIZE_SZ;

    XSetForeground(dpy, gc_title, rgb(28, 30, 38));
    XFillRectangle(dpy, c->resize_grip, gc_title, 0, 0, w, h);
    XSetForeground(dpy, gc_btn, rgb(170, 175, 190));
    for (int i = 0; i < 3; i++) {
        int off = 3 + i * 3;
        XDrawLine(dpy, c->resize_grip, gc_btn, w - off, h - 2, w - 2, h - off);
        XDrawLine(dpy, c->resize_grip, gc_btn, w - off - 1, h - 2, w - 2, h - off - 1);
    }
}

static void draw_title(Client *c) {
    int title_w = c->w - btn_total_width();

    XSetForeground(dpy, gc_title, rgb(43, 43, 43));
    XFillRectangle(dpy, c->title, gc_title, 0, 0, c->w, TITLE_H);

    if (!c->name[0])
        return;

    int len = (int)strlen(c->name);
    int x = 6;
    if (ui_font) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)c->name, len, &ext);
        x = (title_w - ext.xOff) / 2;
        if (x < 6)
            x = 6;
        if (x + ext.xOff > title_w - 4)
            x = title_w - ext.xOff - 4;
        if (x < 6)
            x = 6;
    }
    xft_draw(c->title, x, text_baseline(TITLE_H), c->name, 255, 255, 255);
}

static void draw_chrome(Client *c) {
    draw_title(c);
    draw_min_button(c);
    draw_max_button(c);
    draw_close_button(c);
    draw_resize_grip(c);
}

static void spawn_terminal_root(void) {
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid > 0)
        return;
    if (chdir("/") != 0)
        _exit(1);
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    setenv("TERM", "xterm-256color", 1);
    execl("/usr/bin/xterm", "xterm",
        "-fa", "Monospace", "-fs", "11",
        "-bg", "#1a1a22", "-fg", "#e8e8ec",
        "-title", "root@Backroot8",
        "-e", "/bin/bash", "-l",
        NULL);
    execl("/usr/bin/xfce4-terminal", "xfce4-terminal",
        "--working-directory=/", "--title=root@Backroot8", NULL);
    _exit(1);
}

static void spawn_dolphin_root(void) {
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid > 0)
        return;
    if (chdir("/") != 0)
        _exit(1);
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/usr/bin/pcmanfm", "pcmanfm", "/", NULL);
    _exit(1);
}

static void draw_menu(void);

static int menu_item_at(int y) {
    int item = y / MENU_ITEM_H;
    if (item < 0)
        return 0;
    if (item >= MENU_ITEMS)
        return MENU_ITEMS - 1;
    return item;
}

static void hide_menu(void) {
    if (menu_visible) {
        XUnmapWindow(dpy, menu_win);
        menu_visible = 0;
        menu_hover = -1;
    }
}

static void show_menu(int x, int y) {
    hide_menu();
    menu_hover = 0;
    XMoveWindow(dpy, menu_win, x, y);
    XMapRaised(dpy, menu_win);
    menu_visible = 1;
    draw_menu();
}

static void draw_menu(void) {
    XSetForeground(dpy, gc_title, rgb(43, 43, 43));
    XFillRectangle(dpy, menu_win, gc_title, 0, 0, MENU_W, MENU_H);

    for (int i = 0; i < MENU_ITEMS; i++) {
        int y0 = i * MENU_ITEM_H;
        if (i == menu_hover) {
            XSetForeground(dpy, gc_title, rgb(0, 120, 215));
            XFillRectangle(dpy, menu_win, gc_title, 0, y0, MENU_W, MENU_ITEM_H);
        }
        xft_draw(menu_win, 12, y0 + text_baseline(MENU_ITEM_H),
            menu_labels[i], 255, 255, 255);
    }
}

static void menu_set_hover(int item) {
    if (item < 0 || item >= MENU_ITEMS)
        item = -1;
    if (item == menu_hover)
        return;
    menu_hover = item;
    if (menu_visible)
        draw_menu();
}

static void create_menu(void) {
    menu_win = XCreateSimpleWindow(dpy, root, 0, 0, MENU_W, MENU_H, 1,
        rgb(60, 60, 60), rgb(43, 43, 43));
    XSelectInput(dpy, menu_win,
        ExposureMask | ButtonPressMask | PointerMotionMask | LeaveWindowMask);
    XUnmapWindow(dpy, menu_win);
    menu_visible = 0;
    menu_hover = -1;
}

static void update_root_geom(void) {
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    root_w = ra.width;
    root_h = ra.height;
}

static void clamp_client_geometry(Client *c) {
    int max_h = root_h - PANEL_H;
    int min_w = 200;
    int min_h = TITLE_H + 80;

    if (max_h < min_h)
        max_h = min_h;

    if (c->w > root_w)
        c->w = root_w;
    if (c->w < min_w)
        c->w = min_w;
    if (c->h > max_h)
        c->h = max_h;
    if (c->h < min_h)
        c->h = min_h;

    if (c->x < 0)
        c->x = 0;
    if (c->y < 0)
        c->y = 0;
    if (c->x + c->w > root_w)
        c->x = root_w - c->w;
    if (c->y + c->h > max_h)
        c->y = max_h - c->h;
    if (c->x < 0)
        c->x = 0;
    if (c->y < 0)
        c->y = 0;
}

static void read_client_hints(Window w, int *x, int *y, int *width, int *height) {
    XSizeHints hints;
    long supplied = 0;

    memset(&hints, 0, sizeof(hints));
    if (!XGetWMNormalHints(dpy, w, &hints, &supplied))
        return;

    if (supplied & USPosition) {
        *x = hints.x;
        *y = hints.y;
    }
    if ((supplied & USSize) ||
        ((supplied & PSize) && (hints.flags & PSize))) {
        if (hints.width > 0)
            *width = hints.width;
        if (hints.height > 0)
            *height = hints.height;
    }
}

static int is_background_window(Window w) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, w, net_wm_window_type, 0, 8, False, XA_ATOM,
            &actual, &fmt, &n, &bytes, &data) == Success && data && n >= 1) {
        Atom type = *(Atom *)data;
        XFree(data);
        if (type == net_wm_window_type_desktop)
            return 1;
    } else if (data) {
        XFree(data);
    }

    XClassHint hint;
    if (XGetClassHint(dpy, w, &hint)) {
        int skip = hint.res_class && strcasecmp(hint.res_class, "feh") == 0;
        if (hint.res_name)
            XFree(hint.res_name);
        if (hint.res_class)
            XFree(hint.res_class);
        if (skip)
            return 1;
    }
    return 0;
}

static void wrap_crash_lines(void) {
    int max_chars = (root_w - CRASH_PAD * 2) / 7;
    if (max_chars < 24)
        max_chars = 24;

    n_crash_lines = 0;
    const char *p = crash_text;
    while (*p && n_crash_lines < CRASH_MAX_LINES) {
        while (*p == '\n' || *p == '\r')
            p++;
        if (!*p)
            break;

        int len = 0;
        int last_space = -1;
        while (p[len] && p[len] != '\n' && p[len] != '\r' && len < max_chars) {
            if (isspace((unsigned char)p[len]))
                last_space = len;
            len++;
        }

        if (p[len] && p[len] != '\n' && p[len] != '\r' && last_space > 8)
            len = last_space + 1;

        snprintf(crash_lines[n_crash_lines], sizeof(crash_lines[0]), "%.*s", len, p);
        n_crash_lines++;
        p += len;
        while (*p == ' ')
            p++;
    }
    if (n_crash_lines == 0) {
        strncpy(crash_lines[0], "(no error output captured)", sizeof(crash_lines[0]) - 1);
        n_crash_lines = 1;
    }
}

static int crash_drawer_height(void) {
    int h = n_crash_lines * CRASH_LINE_H + CRASH_PAD * 2;
    if (h > CRASH_DRAWER_MAX_H)
        h = CRASH_DRAWER_MAX_H;
    if (h < CRASH_LINE_H + CRASH_PAD * 2)
        h = CRASH_LINE_H + CRASH_PAD * 2;
    return h;
}

static void layout_crash_windows(void) {
    update_root_geom();
    wrap_crash_lines();
    crash_drawer_h = crash_drawer_open ? crash_drawer_height() : 0;

    int bar_y = root_h - CRASH_BAR_H;
    XMoveResizeWindow(dpy, crash_bar, 0, bar_y, root_w, CRASH_BAR_H);

    if (crash_drawer_open) {
        int drawer_y = bar_y - crash_drawer_h;
        XMoveResizeWindow(dpy, crash_drawer, 0, drawer_y, root_w, crash_drawer_h);
        XMapRaised(dpy, crash_drawer);
    } else {
        XUnmapWindow(dpy, crash_drawer);
    }
    XMapRaised(dpy, crash_bar);
}

static void draw_crash_bar(void) {
    XSetForeground(dpy, gc_title, rgb(120, 28, 28));
    XFillRectangle(dpy, crash_bar, gc_title, 0, 0, root_w, CRASH_BAR_H);
    XSetForeground(dpy, gc_title, rgb(180, 40, 40));
    XFillRectangle(dpy, crash_bar, gc_title, 0, CRASH_BAR_H - CRASH_TOGGLE_ZONE_H,
        root_w, CRASH_TOGGLE_ZONE_H);

    xft_draw(crash_bar, CRASH_PAD, text_baseline(20),
        "The taskbar crashed! Click to restart it or see the error below",
        255, 255, 255);

    const char *hint = crash_drawer_open ? "▼ hide error" : "▶ see the error below";
    xft_draw(crash_bar, CRASH_PAD, CRASH_BAR_H - 8,
        hint, 180, 210, 255);
}

static void draw_crash_drawer(void) {
    XSetForeground(dpy, gc_title, rgb(24, 24, 30));
    XFillRectangle(dpy, crash_drawer, gc_title, 0, 0, root_w, crash_drawer_h);

    XftFont *font = mono_font ? mono_font : ui_font;
    int y = CRASH_PAD + (font ? font->ascent : 12);
    int max_y = crash_drawer_h - CRASH_PAD;
    for (int i = 0; i < n_crash_lines && y < max_y; i++) {
        if (font) {
            XftDraw *xd = XftDrawCreate(dpy, crash_drawer, visual, xft_cmap);
            if (xd) {
                XftColor col;
                XRenderColor rc = render_rgb(220, 220, 230);
                if (XftColorAllocValue(dpy, visual, xft_cmap, &rc, &col)) {
                    XftDrawStringUtf8(xd, &col, font, CRASH_PAD, y,
                        (FcChar8 *)crash_lines[i], (int)strlen(crash_lines[i]));
                    XftColorFree(dpy, visual, xft_cmap, &col);
                }
                XftDrawDestroy(xd);
            }
        }
        y += CRASH_LINE_H;
    }
}

static void hide_crash_ui(void) {
    if (!crash_visible)
        return;
    crash_visible = 0;
    crash_drawer_open = 0;
    XUnmapWindow(dpy, crash_drawer);
    XUnmapWindow(dpy, crash_bar);
}

static void show_crash_ui(void) {
    crash_visible = 1;
    layout_crash_windows();
    draw_crash_bar();
    if (crash_drawer_open)
        draw_crash_drawer();
}

static int load_crash_file(void) {
    struct stat st;
    if (stat(CRASH_FILE, &st) != 0)
        return 0;
    if (crash_visible && st.st_mtime == crash_mtime)
        return 1;

    FILE *f = fopen(CRASH_FILE, "r");
    if (!f)
        return 0;

    size_t n = fread(crash_text, 1, sizeof(crash_text) - 1, f);
    fclose(f);
    crash_text[n] = '\0';
    crash_mtime = st.st_mtime;
    return 1;
}

static void poll_panel_crash(void) {
    if (load_crash_file()) {
        if (!crash_visible)
            show_crash_ui();
        else
            layout_crash_windows();
    } else if (crash_visible) {
        hide_crash_ui();
    }
}

static void panel_restart_request(void) {
    FILE *f = fopen(CRASH_RESTART, "w");
    if (f)
        fclose(f);
    crash_drawer_open = 0;
    hide_crash_ui();
}

static void toggle_crash_drawer(void) {
    if (!crash_visible)
        return;
    crash_drawer_open = !crash_drawer_open;
    layout_crash_windows();
    draw_crash_bar();
    if (crash_drawer_open)
        draw_crash_drawer();
}

static int crash_bar_click(int y) {
    return y >= CRASH_BAR_H - CRASH_TOGGLE_ZONE_H;
}

static void create_crash_windows(void) {
    unsigned long bar_border = rgb(200, 60, 60);
    unsigned long bar_bg = rgb(120, 28, 28);
    unsigned long drawer_border = rgb(80, 80, 90);
    unsigned long drawer_bg = rgb(24, 24, 30);

    crash_bar = XCreateSimpleWindow(dpy, root, 0, 0, 640, CRASH_BAR_H, 1,
        bar_border, bar_bg);
    crash_drawer = XCreateSimpleWindow(dpy, root, 0, 0, 640, CRASH_DRAWER_MAX_H, 1,
        drawer_border, drawer_bg);

    XSetWindowAttributes attr;
    attr.override_redirect = True;
    XChangeWindowAttributes(dpy, crash_bar, CWOverrideRedirect, &attr);
    XChangeWindowAttributes(dpy, crash_drawer, CWOverrideRedirect, &attr);

    XSelectInput(dpy, crash_bar, ExposureMask | ButtonPressMask);
    XSelectInput(dpy, crash_drawer, ExposureMask | ButtonPressMask);
    XUnmapWindow(dpy, crash_bar);
    XUnmapWindow(dpy, crash_drawer);
    crash_visible = 0;
    crash_drawer_open = 0;
}

static int client_has_metro_flag(Window w) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    int metro = 0;

    if (XGetWindowProperty(dpy, w, br8_metro, 0, 1, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success &&
        data && n > 0 && data[0])
        metro = 1;
    if (data)
        XFree(data);
    return metro;
}

static void update_metro_root_state(void) {
    int any = 0;
    for (int i = 0; i < nclients; i++) {
        if (clients[i].metro && clients[i].mapped) {
            any = 1;
            break;
        }
    }
    unsigned long v = any ? 1 : 0;
    XChangeProperty(dpy, root, br8_metro_active, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&v, 1);
    if (!any)
        charms_hide();
}

static void open_start_menu(void) {
    unsigned long one = 1;
    XChangeProperty(dpy, root, br8_start_open, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&one, 1);
}

static Client *top_metro_client(void) {
    Client *best = NULL;
    for (int i = 0; i < nclients; i++) {
        if (!clients[i].metro || !clients[i].mapped)
            continue;
        if (!best)
            best = &clients[i];
        else
            best = &clients[i];
    }
    return best;
}

static void metro_return_to_start(Client *c) {
    if (!c || !c->metro)
        return;
    unmap_client(c);
    open_start_menu();
}

static void charms_emblem_init(void) {
    int disp = CHARMS_EMBLEM_SZ;
    int depth = DefaultDepth(dpy, screen);
    charms_emblem_pm = XCreatePixmap(dpy, root, disp, disp, depth);
    XImage *xi = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, disp, disp, 32, 0);
    if (!xi)
        return;
    xi->data = calloc((size_t)xi->bytes_per_line * (unsigned int)disp, 1);
    if (!xi->data) {
        XDestroyImage(xi);
        return;
    }
    for (int dy = 0; dy < disp; dy++) {
        for (int dx = 0; dx < disp; dx++) {
            int sx = dx * EMBLEM_W / disp;
            int sy = dy * EMBLEM_H / disp;
            size_t idx = (size_t)(sy * EMBLEM_W + sx) * 3;
            XColor c;
            c.red = (unsigned short)(emblem_rgb[idx] << 8);
            c.green = (unsigned short)(emblem_rgb[idx + 1] << 8);
            c.blue = (unsigned short)(emblem_rgb[idx + 2] << 8);
            c.flags = DoRed | DoGreen | DoBlue;
            if (!XAllocColor(dpy, cmap, &c))
                c.pixel = BlackPixel(dpy, screen);
            XPutPixel(xi, dx, dy, c.pixel);
        }
    }
    GC egc = XCreateGC(dpy, charms_emblem_pm, 0, NULL);
    XPutImage(dpy, charms_emblem_pm, egc, xi, 0, 0, 0, 0, disp, disp);
    XFreeGC(dpy, egc);
    XDestroyImage(xi);
    charms_emblem_ready = 1;
}

static void layout_charms_windows(void) {
    int strip_x = root_w - CHARMS_STRIP_W;
    int clock_x = CHARMS_TIME_PAD;
    int clock_y = root_h - CHARMS_TIME_H - CHARMS_TIME_PAD;
    if (clock_y < CHARMS_TIME_PAD)
        clock_y = CHARMS_TIME_PAD;
    XMoveResizeWindow(dpy, charms_strip, strip_x, 0, CHARMS_STRIP_W, root_h);
    XMoveResizeWindow(dpy, charms_clock, clock_x, clock_y, CHARMS_TIME_W, CHARMS_TIME_H);
}

static void draw_charms_clock(void) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char time_buf[32];
    char date_buf[64];
    strftime(time_buf, sizeof(time_buf), "%-I:%M", &tm);
    strftime(date_buf, sizeof(date_buf), "%A\n%B %-d", &tm);

    XSetForeground(dpy, gc_title, rgb(0, 0, 0));
    XFillRectangle(dpy, charms_clock, gc_title, 0, 0, CHARMS_TIME_W, CHARMS_TIME_H);

    int ty = CHARMS_TIME_PAD + (charms_time_font ? charms_time_font->ascent : 36);
    if (charms_time_font) {
        XftDraw *xd = XftDrawCreate(dpy, charms_clock, visual, xft_cmap);
        if (xd) {
            XftColor col;
            XRenderColor rc = render_rgb(255, 255, 255);
            if (XftColorAllocValue(dpy, visual, xft_cmap, &rc, &col)) {
                XftDrawStringUtf8(xd, &col, charms_time_font, CHARMS_TIME_PAD, ty,
                    (FcChar8 *)time_buf, (int)strlen(time_buf));
                XftColorFree(dpy, visual, xft_cmap, &col);
            }
            XftDrawDestroy(xd);
        }
    }

    int dy = ty + (charms_date_font ? charms_date_font->height + 6 : 28);
    if (charms_date_font) {
        XftDraw *xd = XftDrawCreate(dpy, charms_clock, visual, xft_cmap);
        if (xd) {
            XftColor col;
            XRenderColor rc = render_rgb(220, 220, 230);
            if (XftColorAllocValue(dpy, visual, xft_cmap, &rc, &col)) {
                char *line = date_buf;
                while (line && *line) {
                    char *nl = strchr(line, '\n');
                    int len = nl ? (int)(nl - line) : (int)strlen(line);
                    XftDrawStringUtf8(xd, &col, charms_date_font, CHARMS_TIME_PAD, dy,
                        (FcChar8 *)line, len);
                    dy += charms_date_font->height + 2;
                    line = nl ? nl + 1 : NULL;
                }
                XftColorFree(dpy, visual, xft_cmap, &col);
            }
            XftDrawDestroy(xd);
        }
    }
    charms_clock_last = now;
}

static void draw_charms_strip(void) {
    int btn0_y = (root_h - CHARMS_BTN_H * 2) / 2;
    int btn1_y = btn0_y + CHARMS_BTN_H;

    XSetForeground(dpy, gc_title, rgb(16, 16, 16));
    XFillRectangle(dpy, charms_strip, gc_title, 0, 0, CHARMS_STRIP_W, root_h);

    for (int b = 0; b < 2; b++) {
        int y0 = b == 0 ? btn0_y : btn1_y;
        if (charms_hover == b + 1) {
            XSetForeground(dpy, gc_title, rgb(55, 55, 58));
            XFillRectangle(dpy, charms_strip, gc_title, 0, y0, CHARMS_STRIP_W, CHARMS_BTN_H);
        }
    }

    if (charms_emblem_ready) {
        int ex = (CHARMS_STRIP_W - CHARMS_EMBLEM_SZ) / 2;
        int ey = btn0_y + (CHARMS_BTN_H - CHARMS_EMBLEM_SZ) / 2;
        XCopyArea(dpy, charms_emblem_pm, charms_strip, gc_title,
            0, 0, CHARMS_EMBLEM_SZ, CHARMS_EMBLEM_SZ, ex, ey);
    }

    xft_draw(charms_strip, 10, btn1_y + text_baseline(CHARMS_BTN_H / 2),
        "Close", 255, 255, 255);
    xft_draw(charms_strip, 18, btn0_y + CHARMS_BTN_H - 14, "Home", 200, 200, 210);
}

static void charms_show(void) {
    if (charms_visible || !top_metro_client())
        return;
    layout_charms_windows();
    charms_visible = 1;
    charms_hover = 0;
    XMapRaised(dpy, charms_clock);
    XMapRaised(dpy, charms_strip);
    draw_charms_clock();
    draw_charms_strip();
}

static void charms_hide(void) {
    if (!charms_visible)
        return;
    charms_visible = 0;
    charms_hover = 0;
    XUnmapWindow(dpy, charms_strip);
    XUnmapWindow(dpy, charms_clock);
}

static int charms_btn_at(int x, int y) {
    if (x < 0 || x >= CHARMS_STRIP_W)
        return 0;
    int btn0_y = (root_h - CHARMS_BTN_H * 2) / 2;
    if (y >= btn0_y && y < btn0_y + CHARMS_BTN_H)
        return 1;
    if (y >= btn0_y + CHARMS_BTN_H && y < btn0_y + CHARMS_BTN_H * 2)
        return 2;
    return 0;
}

static void charms_pointer_update(int x, int y) {
    if (!top_metro_client()) {
        charms_hide();
        return;
    }
    int in_edge = x >= root_w - CHARMS_EDGE_ZONE;
    int in_strip = charms_visible && x >= root_w - CHARMS_STRIP_W;
    if (in_edge || in_strip) {
        if (!charms_visible)
            charms_show();
        if (in_strip) {
            int rel_y = y;
            int btn = charms_btn_at(x - (root_w - CHARMS_STRIP_W), rel_y);
            if (btn != charms_hover) {
                charms_hover = btn;
                draw_charms_strip();
            }
        } else if (charms_hover) {
            charms_hover = 0;
            draw_charms_strip();
        }
    } else {
        charms_hide();
    }
}

static void charms_handle_click(int btn) {
    Client *c = top_metro_client();
    if (!c || !btn)
        return;
    charms_hide();
    if (btn == 1)
        metro_return_to_start(c);
    else if (btn == 2)
        close_client(c);
}

static void charms_tick_clock(void) {
    if (!charms_visible)
        return;
    time_t now = time(NULL);
    if (now != charms_clock_last)
        draw_charms_clock();
}

static void create_charms_windows(void) {
    XSetWindowAttributes attr;
    unsigned long black = rgb(16, 16, 16);

    charms_strip = XCreateSimpleWindow(dpy, root, 0, 0, CHARMS_STRIP_W, root_h, 0, 0, black);
    charms_clock = XCreateSimpleWindow(dpy, root, 0, 0, CHARMS_TIME_W, CHARMS_TIME_H, 0, 0, black);

    attr.override_redirect = True;
    attr.event_mask = ExposureMask | ButtonPressMask | PointerMotionMask | LeaveWindowMask;
    XChangeWindowAttributes(dpy, charms_strip, CWOverrideRedirect | CWEventMask, &attr);
    attr.event_mask = ExposureMask;
    XChangeWindowAttributes(dpy, charms_clock, CWOverrideRedirect | CWEventMask, &attr);

    XUnmapWindow(dpy, charms_strip);
    XUnmapWindow(dpy, charms_clock);
    charms_visible = 0;

    charms_emblem_init();

    static const char *const time_fonts[] = {
        "sans-serif-48:weight=light",
        "Segoe UI-48:weight=light",
        "DejaVu Sans-48",
        NULL
    };
    for (int i = 0; time_fonts[i]; i++) {
        charms_time_font = XftFontOpenName(dpy, screen, time_fonts[i]);
        if (charms_time_font && charms_time_font->ascent > 0)
            break;
        if (charms_time_font) {
            XftFontClose(dpy, charms_time_font);
            charms_time_font = NULL;
        }
    }
    static const char *const date_fonts[] = {
        "sans-serif-13",
        "DejaVu Sans-13",
        NULL
    };
    for (int i = 0; date_fonts[i]; i++) {
        charms_date_font = XftFontOpenName(dpy, screen, date_fonts[i]);
        if (charms_date_font && charms_date_font->ascent > 0)
            break;
        if (charms_date_font) {
            XftFontClose(dpy, charms_date_font);
            charms_date_font = NULL;
        }
    }
}

static void metro_check_swipe_gesture(XMotionEvent *ev) {
    if (!metro_swipe)
        return;
    Client *c = top_metro_client();
    if (!c)
        return;
    if (metro_swipe_y - ev->y_root >= METRO_SWIPE_THRESHOLD)
        metro_return_to_start(c);
    metro_swipe = 0;
}

static void layout_metro_client(Client *c) {
    XMoveResizeWindow(dpy, c->client, 0, 0, c->w, c->h);
}

static void hide_metro_chrome(Client *c) {
    XUnmapWindow(dpy, c->title);
    XUnmapWindow(dpy, c->btn_min);
    XUnmapWindow(dpy, c->btn_max);
    XUnmapWindow(dpy, c->btn_close);
    XUnmapWindow(dpy, c->resize_grip);
}

static void handle_crash_button(XButtonEvent *btn) {
    if (btn->window == crash_bar) {
        if (crash_bar_click((int)btn->y))
            toggle_crash_drawer();
        else
            panel_restart_request();
        return;
    }
    if (btn->window == crash_drawer)
        toggle_crash_drawer();
}

static void layout_client(Client *c) {
    int tx = c->w - btn_total_width();
    int grip_x = c->w - RESIZE_SZ;
    int grip_y = c->h - RESIZE_SZ;

    if (grip_x < 0)
        grip_x = 0;
    if (grip_y < TITLE_H)
        grip_y = TITLE_H;

    XMoveResizeWindow(dpy, c->title, 0, 0, c->w, TITLE_H);
    XMoveResizeWindow(dpy, c->client, 0, TITLE_H, c->w, c->h - TITLE_H);
    XMoveResizeWindow(dpy, c->btn_min, tx, 0, BTN_W, TITLE_H);
    tx += BTN_W;
    XMoveResizeWindow(dpy, c->btn_max, tx, 0, BTN_W, TITLE_H);
    tx += BTN_W;
    XMoveResizeWindow(dpy, c->btn_close, tx, 0, CLOSE_W, TITLE_H);
    XMoveResizeWindow(dpy, c->resize_grip, grip_x, grip_y, RESIZE_SZ, RESIZE_SZ);
    XRaiseWindow(dpy, c->resize_grip);
    draw_chrome(c);
}

static void resize_client_to(Client *c, int w, int h) {
    if (w < MIN_FRAME_W)
        w = MIN_FRAME_W;
    if (h < MIN_FRAME_H)
        h = MIN_FRAME_H;
    c->w = w;
    c->h = h;
    XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
    layout_client(c);
}

static void map_client(Client *c) {
    XMapWindow(dpy, c->frame);
    if (c->metro)
        XMapWindow(dpy, c->client);
    else
        XMapSubwindows(dpy, c->frame);
    c->mapped = 1;
    bump_panel();
    update_metro_root_state();
}

static void unmap_client(Client *c) {
    XUnmapWindow(dpy, c->frame);
    c->mapped = 0;
    bump_panel();
    update_metro_root_state();
}

static void close_client(Client *c) {
    if (!c)
        return;
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = c->client;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    XSendEvent(dpy, c->client, False, NoEventMask, &ev);
    XFlush(dpy);
    if (c->metro)
        remove_client(c);
}

static void maximize_client(Client *c) {
    if (c->maximized) {
        c->maximized = 0;
        c->x = c->saved_x;
        c->y = c->saved_y;
        c->w = c->saved_w;
        c->h = c->saved_h;
        update_root_geom();
        clamp_client_geometry(c);
        XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
    } else {
        XWindowAttributes ra;
        XGetWindowAttributes(dpy, root, &ra);
        c->saved_x = c->x;
        c->saved_y = c->y;
        c->saved_w = c->w;
        c->saved_h = c->h;
        c->maximized = 1;
        c->x = 0;
        c->y = 0;
        c->w = ra.width;
        c->h = ra.height - PANEL_H;
        XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
    }
    layout_client(c);
}

static void tag_frame(Client *c) {
    unsigned long one = 1;
    XChangeProperty(dpy, c->frame, br8_frame, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&one, 1);
    XChangeProperty(dpy, c->frame, br8_client, XA_WINDOW, 32, PropModeReplace,
        (unsigned char *)&c->client, 1);
}

static void raise_client(Client *c) {
    if (!c)
        return;
    XRaiseWindow(dpy, c->frame);
    XSetInputFocus(dpy, c->client, RevertToParent, CurrentTime);
}

static void restore_client(Client *c) {
    XWindowAttributes wa;

    if (!c)
        return;
    if (!XGetWindowAttributes(dpy, c->frame, &wa)) {
        map_client(c);
        return;
    }
    if (wa.map_state == IsUnmapped || !c->mapped)
        map_client(c);
    raise_client(c);
}

static void add_client(Window w) {
    if (nclients >= 256)
        return;
    if (is_our_chrome(w))
        return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa))
        return;
    if (wa.override_redirect)
        return;
    if (is_background_window(w))
        return;
    Client *c = &clients[nclients++];
    memset(c, 0, sizeof(*c));
    c->client = w;
    int client_w = wa.width < 400 ? 640 : wa.width;
    int client_h = wa.height < 300 ? 400 : wa.height;
    c->x = 80 + nclients * 24;
    c->y = 60 + nclients * 24;
    c->metro = client_has_metro_flag(w);
    read_client_hints(w, &c->x, &c->y, &client_w, &client_h);
    if (c->metro) {
        update_root_geom();
        c->x = 0;
        c->y = 0;
        c->w = root_w;
        c->h = root_h;
        client_w = root_w;
        client_h = root_h;
    } else {
        c->w = client_w;
        c->h = client_h + TITLE_H;
        update_root_geom();
        clamp_client_geometry(c);
    }

    c->frame = XCreateSimpleWindow(dpy, root, c->x, c->y, c->w, c->h, 2,
        rgb(70, 75, 90), rgb(28, 30, 38));
    c->title = XCreateSimpleWindow(dpy, c->frame, 0, 0, c->w, TITLE_H, 0, 0, 0);
    c->btn_min = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W, TITLE_H, 0, 0, 0);
    c->btn_max = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W, TITLE_H, 0, 0, 0);
    c->btn_close = XCreateSimpleWindow(dpy, c->frame, 0, 0, CLOSE_W, TITLE_H, 0, 0, 0);
    c->resize_grip = XCreateSimpleWindow(dpy, c->frame, 0, 0, RESIZE_SZ, RESIZE_SZ, 0, 0, 0);

    XSelectInput(dpy, c->frame, SubstructureRedirectMask | SubstructureNotifyMask);
    XSelectInput(dpy, c->title, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
    XSelectInput(dpy, c->btn_min, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->btn_max, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->btn_close, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->resize_grip,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
    XSelectInput(dpy, c->client, PropertyChangeMask);

    if (c->metro) {
        XReparentWindow(dpy, w, c->frame, 0, 0);
        hide_metro_chrome(c);
        layout_metro_client(c);
        XSelectInput(dpy, c->frame,
            SubstructureRedirectMask | SubstructureNotifyMask |
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    } else {
        XReparentWindow(dpy, w, c->frame, 0, TITLE_H);
        layout_client(c);
    }
    tag_frame(c);
    fetch_client_title(c);
    map_client(c);
    update_net_client_list();

    if (c->metro) {
        raise_client(c);
        update_metro_root_state();
        return;
    }

    XGrabButton(dpy, Button1, AnyModifier, c->title, False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, root, None);
    {
        Cursor sz_cur = XCreateFontCursor(dpy, XC_bottom_right_corner);
        XDefineCursor(dpy, c->resize_grip, sz_cur);
    }
    XGrabButton(dpy, Button1, AnyModifier, c->resize_grip, False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, root, None);
}

static void remove_client(Client *c) {
    int idx = c - clients;
    if (idx < 0 || idx >= nclients)
        return;
    XDestroyWindow(dpy, c->frame);
    memmove(&clients[idx], &clients[idx + 1], (nclients - idx - 1) * sizeof(Client));
    nclients--;
    update_net_client_list();
    update_metro_root_state();
}

static void handle_root_activate(void) {
    Window frame = 0;
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, root, br8_activate, 0, 1, True, XA_WINDOW,
            &actual, &fmt, &n, &bytes, &data) != Success || !data || n < 1) {
        if (data)
            XFree(data);
        return;
    }
    frame = *(Window *)data;
    XFree(data);

    Client *c = find_by_frame(frame);
    if (c)
        restore_client(c);
}

static void handle_property(XPropertyEvent *e) {
    if (e->window == root && e->atom == br8_activate) {
        handle_root_activate();
        return;
    }

    Client *c = find_by_client(e->window);
    if (!c)
        return;
    if (e->atom == net_wm_name || e->atom == XA_WM_NAME) {
        fetch_client_title(c);
        draw_title(c);
        bump_panel();
    }
}

static void handle_map_request(XMapRequestEvent *e) {
    Window w = e->window;

    if (is_our_chrome(w)) {
        /* Never wrap our own frames/decorations (fixes stacked title bars). */
        XMapWindow(dpy, w);
        return;
    }

    Client *c = find_by_client(w);
    if (c) {
        if (!c->mapped)
            map_client(c);
        XMapWindow(dpy, w);
        return;
    }

    add_client(w);
}

static void adopt_existing_clients(void) {
    Window root_ret, parent;
    Window *children = NULL;
    unsigned int nch = 0;

    if (!XQueryTree(dpy, root, &root_ret, &parent, &children, &nch))
        return;

    for (unsigned int i = 0; i < nch; i++) {
        Window w = children[i];
        XWindowAttributes wa;

        if (is_our_chrome(w) || find_by_client(w))
            continue;
        if (!XGetWindowAttributes(dpy, w, &wa))
            continue;
        if (wa.override_redirect || wa.map_state != IsViewable)
            continue;
        add_client(w);
    }
    if (children)
        XFree(children);
}

static void handle_client_message(XClientMessageEvent *e) {
    if (e->message_type != br8_activate)
        return;
    Client *c = find_by_frame((Window)e->data.l[0]);
    if (c)
        restore_client(c);
}

static void handle_destroy(XDestroyWindowEvent *e) {
    if (e->window == menu_win)
        return;
    for (int i = 0; i < nclients; i++) {
        if (clients[i].client == e->window || clients[i].frame == e->window) {
            remove_client(&clients[i]);
            return;
        }
    }
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-wm: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    xft_cmap = DefaultColormap(dpy, screen);
    static const char *const font_names[] = {
        "Segoe UI-10:antialias=true:hinting=true",
        "sans-serif-10:antialias=true:hinting=true",
        "DejaVu Sans-10:antialias=true",
        "Liberation Sans-10:antialias=true",
        "FreeSans-10",
        NULL
    };
    for (int i = 0; font_names[i]; i++) {
        ui_font = XftFontOpenName(dpy, screen, font_names[i]);
        if (ui_font && ui_font->ascent > 0)
            break;
        if (ui_font) {
            XftFontClose(dpy, ui_font);
            ui_font = NULL;
        }
    }

    gc_title = XCreateGC(dpy, root, 0, NULL);
    gc_btn = XCreateGC(dpy, root, 0, NULL);
    gc_close_fill = XCreateGC(dpy, root, 0, NULL);
    gc_close_x = XCreateGC(dpy, root, 0, NULL);
    XSetLineAttributes(dpy, gc_btn, 1, LineSolid, CapRound, JoinRound);
    XSetLineAttributes(dpy, gc_close_x, 2, LineSolid, CapRound, JoinRound);

    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    br8_frame = XInternAtom(dpy, "_BR8_FRAME", False);
    br8_client = XInternAtom(dpy, "_BR8_CLIENT", False);
    br8_panel_rev = XInternAtom(dpy, "_BR8_PANEL_REV", False);
    br8_activate = XInternAtom(dpy, "_BR8_ACTIVATE", False);
    br8_metro = XInternAtom(dpy, "_BR8_METRO", False);
    br8_start_open = XInternAtom(dpy, "_BR8_START_OPEN", False);
    br8_metro_active = XInternAtom(dpy, "_BR8_METRO_ACTIVE", False);
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask |
        ButtonReleaseMask | PointerMotionMask |
        PropertyChangeMask | StructureNotifyMask);
    XSetErrorHandler(NULL);
    signal(SIGCHLD, SIG_IGN);

    mono_font = XftFontOpenName(dpy, screen,
        "monospace-9:antialias=true:hinting=true");
    if (mono_font && mono_font->ascent <= 0) {
        XftFontClose(dpy, mono_font);
        mono_font = NULL;
    }

    update_root_geom();
    adopt_existing_clients();
    create_menu();
    create_crash_windows();
    create_charms_windows();
    bump_panel();
    poll_panel_crash();

    Cursor cur = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cur);

    while (1) {
        poll_panel_crash();

        if (!XPending(dpy)) {
            if (charms_visible)
                charms_tick_clock();
            struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 };
            if (charms_visible)
                tv.tv_usec = 500000;
            int xfd = ConnectionNumber(dpy);
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            select(xfd + 1, &fds, NULL, NULL, &tv);
            poll_panel_crash();
            if (charms_visible)
                charms_tick_clock();
        }

        XEvent ev;
        XNextEvent(dpy, &ev);

        if (ev.type == MapRequest)
            handle_map_request(&ev.xmaprequest);
        else if (ev.type == ClientMessage)
            handle_client_message(&ev.xclient);
        else if (ev.type == DestroyNotify)
            handle_destroy(&ev.xdestroywindow);
        else if (ev.type == PropertyNotify)
            handle_property(&ev.xproperty);
        else if (ev.type == MapNotify) {
            Client *c = find_by_frame(ev.xmap.window);
            if (c)
                c->mapped = 1;
        } else if (ev.type == UnmapNotify) {
            Client *c = find_by_frame(ev.xunmap.window);
            if (c && ev.xunmap.window == c->frame)
                c->mapped = 0;
        } else if (ev.type == ConfigureNotify && ev.xconfigure.window == root) {
            update_root_geom();
            for (int i = 0; i < nclients; i++) {
                if (clients[i].metro && clients[i].mapped) {
                    clients[i].x = 0;
                    clients[i].y = 0;
                    clients[i].w = root_w;
                    clients[i].h = root_h;
                    XMoveResizeWindow(dpy, clients[i].frame, 0, 0, root_w, root_h);
                    layout_metro_client(&clients[i]);
                }
            }
            if (crash_visible)
                layout_crash_windows();
            if (charms_visible)
                layout_charms_windows();
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.window == charms_strip) {
                int btn = charms_btn_at((int)ev.xbutton.x, (int)ev.xbutton.y);
                charms_handle_click(btn);
                continue;
            }
            if (ev.xbutton.window == crash_bar || ev.xbutton.window == crash_drawer) {
                handle_crash_button(&ev.xbutton);
                continue;
            }
            if (menu_visible && ev.xbutton.window == menu_win) {
                int item = menu_item_at((int)ev.xbutton.y);
                hide_menu();
                if (item == 0)
                    spawn_terminal_root();
                else
                    spawn_dolphin_root();
                continue;
            }
            if (ev.xbutton.button == Button3 && ev.xbutton.window == root) {
                show_menu(ev.xbutton.x_root, ev.xbutton.y_root);
                continue;
            }
            if (ev.xbutton.window == root) {
                if (ev.xbutton.y_root >= root_h - METRO_SWIPE_ZONE) {
                    metro_swipe = 1;
                    metro_swipe_y = ev.xbutton.y_root;
                }
                hide_menu();
                continue;
            }
            hide_menu();
            Client *c = find_client(ev.xbutton.window);
            if (!c)
                continue;
            if (c->metro && ev.xbutton.y_root >= root_h - METRO_SWIPE_ZONE) {
                metro_swipe = 1;
                metro_swipe_y = ev.xbutton.y_root;
                continue;
            }
            if (c->metro)
                continue;
            if (ev.xbutton.window == c->btn_close)
                close_client(c);
            else if (ev.xbutton.window == c->btn_min)
                unmap_client(c);
            else if (ev.xbutton.window == c->btn_max)
                maximize_client(c);
            else if (ev.xbutton.window == c->title && ev.xbutton.button == Button1) {
                raise_client(c);
                dragging = 1;
                drag_client = c;
                drag_x = ev.xbutton.x;
                drag_y = ev.xbutton.y;
            } else if (ev.xbutton.window == c->resize_grip && ev.xbutton.button == Button1 &&
                       !c->maximized) {
                raise_client(c);
                resizing = 1;
                resize_client = c;
                resize_x = ev.xbutton.x_root;
                resize_y = ev.xbutton.y_root;
                resize_w = c->w;
                resize_h = c->h;
            }
        } else if (ev.type == ButtonRelease) {
            if (metro_swipe)
                metro_swipe = 0;
            if (dragging || resizing) {
                dragging = 0;
                drag_client = NULL;
                resizing = 0;
                resize_client = NULL;
            }
        } else if (ev.type == MotionNotify) {
            if (metro_swipe && (ev.xmotion.state & Button1Mask))
                metro_check_swipe_gesture(&ev.xmotion);
            charms_pointer_update(ev.xmotion.x_root, ev.xmotion.y_root);
            if (menu_visible && ev.xmotion.window == menu_win) {
                menu_set_hover(menu_item_at((int)ev.xmotion.y));
                continue;
            }
            if (dragging && drag_client && !drag_client->metro) {
                drag_client->x = ev.xmotion.x_root - drag_x;
                drag_client->y = ev.xmotion.y_root - drag_y;
                clamp_client_geometry(drag_client);
                XMoveWindow(dpy, drag_client->frame, drag_client->x, drag_client->y);
            } else if (resizing && resize_client) {
                int nw = resize_w + (ev.xmotion.x_root - resize_x);
                int nh = resize_h + (ev.xmotion.y_root - resize_y);
                resize_client_to(resize_client, nw, nh);
            }
        } else if (ev.type == LeaveNotify && menu_visible &&
                   ev.xcrossing.window == menu_win) {
            menu_set_hover(-1);
        } else if (ev.type == Expose) {
            if (ev.xexpose.window == crash_bar && ev.xexpose.count == 0)
                draw_crash_bar();
            else if (ev.xexpose.window == crash_drawer && ev.xexpose.count == 0)
                draw_crash_drawer();
            else if (ev.xexpose.window == menu_win && ev.xexpose.count == 0)
                draw_menu();
            else if (ev.xexpose.window == charms_strip && ev.xexpose.count == 0)
                draw_charms_strip();
            else if (ev.xexpose.window == charms_clock && ev.xexpose.count == 0)
                draw_charms_clock();
            else {
                Client *c = find_client(ev.xexpose.window);
                if (c) {
                    if (ev.xexpose.window == c->title)
                        draw_title(c);
                    else if (ev.xexpose.window == c->btn_min)
                        draw_min_button(c);
                    else if (ev.xexpose.window == c->btn_max)
                        draw_max_button(c);
                    else if (ev.xexpose.window == c->btn_close)
                        draw_close_button(c);
                    else if (ev.xexpose.window == c->resize_grip)
                        draw_resize_grip(c);
                }
            }
        }
    }
    return 0;
}
