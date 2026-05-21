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
#include <ctype.h>

#define TITLE_H 30
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
#define CLOSE_W 34
#define MAX_TITLE 256

typedef struct {
    Window frame;
    Window title;
    Window client;
    Window btn_min;
    Window btn_max;
    Window btn_close;
    char name[MAX_TITLE];
    int x, y, w, h;
    int mapped;
    int maximized;
    int saved_x, saved_y, saved_w, saved_h;
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
static Atom net_wm_window_type, net_wm_window_type_desktop;
static Client clients[256];
static int nclients;
static int dragging;
static int drag_x, drag_y;
static Client *drag_client;
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
            clients[i].btn_max == w || clients[i].btn_close == w)
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
    XSetForeground(dpy, gc_close_fill, rgb(196, 43, 28));
    XFillRectangle(dpy, c->btn_close, gc_close_fill, 0, 0, CLOSE_W, TITLE_H);
    XSetForeground(dpy, gc_close_x, rgb(255, 255, 255));
    int cx = CLOSE_W / 2;
    int cy = TITLE_H / 2;
    int dx = 5;
    int dy = 5;
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx, cy - dy, cx + dx, cy + dy);
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx, cy + dy, cx + dx, cy - dy);
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
    execl("/usr/bin/dolphin", "dolphin", "/", NULL);
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
    XMoveResizeWindow(dpy, c->title, 0, 0, c->w, TITLE_H);
    XMoveResizeWindow(dpy, c->client, 0, TITLE_H, c->w, c->h - TITLE_H);
    XMoveResizeWindow(dpy, c->btn_min, tx, 0, BTN_W, TITLE_H);
    tx += BTN_W;
    XMoveResizeWindow(dpy, c->btn_max, tx, 0, BTN_W, TITLE_H);
    tx += BTN_W;
    XMoveResizeWindow(dpy, c->btn_close, tx, 0, CLOSE_W, TITLE_H);
    draw_chrome(c);
}

static void map_client(Client *c) {
    XMapWindow(dpy, c->frame);
    XMapSubwindows(dpy, c->frame);
    c->mapped = 1;
    bump_panel();
}

static void unmap_client(Client *c) {
    XUnmapWindow(dpy, c->frame);
    c->mapped = 0;
    bump_panel();
}

static void close_client(Client *c) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = c->client;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    XSendEvent(dpy, c->client, False, NoEventMask, &ev);
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
    read_client_hints(w, &c->x, &c->y, &client_w, &client_h);
    c->w = client_w;
    c->h = client_h + TITLE_H;
    update_root_geom();
    clamp_client_geometry(c);

    c->frame = XCreateSimpleWindow(dpy, root, c->x, c->y, c->w, c->h, 2,
        rgb(70, 75, 90), rgb(28, 30, 38));
    c->title = XCreateSimpleWindow(dpy, c->frame, 0, 0, c->w, TITLE_H, 0, 0, 0);
    c->btn_min = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W, TITLE_H, 0, 0, 0);
    c->btn_max = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W, TITLE_H, 0, 0, 0);
    c->btn_close = XCreateSimpleWindow(dpy, c->frame, 0, 0, CLOSE_W, TITLE_H, 0, 0, 0);

    XSelectInput(dpy, c->frame, SubstructureRedirectMask | SubstructureNotifyMask);
    XSelectInput(dpy, c->title, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
    XSelectInput(dpy, c->btn_min, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->btn_max, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->btn_close, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->client, PropertyChangeMask);

    XReparentWindow(dpy, w, c->frame, 0, TITLE_H);
    tag_frame(c);
    fetch_client_title(c);
    layout_client(c);
    map_client(c);
    update_net_client_list();

    XGrabButton(dpy, Button1, AnyModifier, c->title, False,
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
        if (clients[i].client == e->window) {
            remove_client(&clients[i]);
            return;
        }
        if (clients[i].frame == e->window) {
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
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask |
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
    bump_panel();
    poll_panel_crash();

    Cursor cur = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cur);

    while (1) {
        poll_panel_crash();

        if (!XPending(dpy)) {
            struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 };
            int xfd = ConnectionNumber(dpy);
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            select(xfd + 1, &fds, NULL, NULL, &tv);
            poll_panel_crash();
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
            if (crash_visible)
                layout_crash_windows();
        } else if (ev.type == ButtonPress) {
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
            hide_menu();
            Client *c = find_client(ev.xbutton.window);
            if (!c)
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
            }
        } else if (ev.type == ButtonRelease && dragging) {
            dragging = 0;
            drag_client = NULL;
        } else if (ev.type == MotionNotify) {
            if (menu_visible && ev.xmotion.window == menu_win) {
                menu_set_hover(menu_item_at((int)ev.xmotion.y));
                continue;
            }
            if (dragging && drag_client) {
                drag_client->x = ev.xmotion.x_root - drag_x;
                drag_client->y = ev.xmotion.y_root - drag_y;
                clamp_client_geometry(drag_client);
                XMoveWindow(dpy, drag_client->frame, drag_client->x, drag_client->y);
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
                }
            }
        }
    }
    return 0;
}
