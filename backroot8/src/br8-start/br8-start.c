/*
 * Backroot 8 start menu - Windows 8-style Home / Apps launcher
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/select.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

#define PANEL_H 32
#define TILE 150
#define TILE_GAP 10
#define MARGIN 48
#define HEADER_H 72
#define APP_ROW_H 56
#define APP_ICON 48
#define APP_COL_W 240
#define APP_GAP 8
#define MAX_APPS 256
#define WALLPAPER "/usr/share/backgrounds/backroot8.jpg"

typedef enum {
    ACT_TERMINAL,
    ACT_DOLPHIN,
    ACT_HELLO,
    ACT_DESKTOP,
    ACT_EXEC
} Action;

typedef struct {
    char label[64];
    int x, y, w, h;
    int cr, cg, cb;
    char letter;
    Action act;
    char exec_cmd[512];
} Tile;

typedef struct {
    char name[128];
    char exec_cmd[512];
    int cr, cg, cb;
    char letter;
} AppEntry;

static Display *dpy;
static int screen;
static Window root, start_win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static XftFont *ui_font, *header_font;
static int win_w, win_h, root_h;
static Atom br8_start_open;
static int visible;
static int scroll_y;
static int content_h;
static int home_h;
static int apps_cols;
static Pixmap wallpaper_pm;
static int wallpaper_ready;
static Tile home_tiles[8];
static int n_home_tiles;
static AppEntry apps[MAX_APPS];
static int n_apps;

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
        return height - 8;
    return (height + font->ascent - font->descent) / 2;
}

static void xft_draw(Drawable draw, XftFont *font, int x, int y,
        const char *text, int r, int g, int b) {
    if (!font || !text || !text[0])
        return;
    XftDraw *xd = XftDrawCreate(dpy, draw, visual, cmap);
    if (!xd)
        return;
    XftColor col;
    XRenderColor rc = render_rgb(r, g, b);
    if (XftColorAllocValue(dpy, visual, cmap, &rc, &col)) {
        XftDrawStringUtf8(xd, &col, font, x, y,
            (FcChar8 *)text, (int)strlen(text));
        XftColorFree(dpy, visual, cmap, &col);
    }
    XftDrawDestroy(xd);
}

static void app_color(AppEntry *a) {
    unsigned h = 0;
    for (const char *p = a->name; p && *p; p++)
        h = h * 31 + (unsigned char)*p;
    a->cr = 60 + (h & 0x7f);
    a->cg = 60 + ((h >> 8) & 0x7f);
    a->cb = 60 + ((h >> 16) & 0x7f);
    a->letter = a->name[0] ? a->name[0] : '?';
    if (a->letter >= 'a' && a->letter <= 'z')
        a->letter = (char)(a->letter - 'a' + 'A');
}

static int read_open(void) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    int open = 0;
    if (XGetWindowProperty(dpy, root, br8_start_open, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        open = data[0] ? 1 : 0;
    if (data)
        XFree(data);
    return open;
}

static void set_open(int open) {
    unsigned long v = open ? 1 : 0;
    XChangeProperty(dpy, root, br8_start_open, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&v, 1);
    XFlush(dpy);
}

static void hide_menu(void) {
    if (!visible)
        return;
    XUnmapWindow(dpy, start_win);
    visible = 0;
    scroll_y = 0;
    set_open(0);
}

static void show_menu(void) {
    if (visible)
        return;
    scroll_y = 0;
    visible = 1;
    XMapRaised(dpy, start_win);
    draw_all();
}

static void spawn_terminal(void) {
    pid_t pid = fork();
    if (pid != 0)
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

static void spawn_dolphin(void) {
    pid_t pid = fork();
    if (pid != 0)
        return;
    if (chdir("/") != 0)
        _exit(1);
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/usr/bin/dolphin", "dolphin", "/", NULL);
    _exit(1);
}

static void spawn_hello(void) {
    pid_t pid = fork();
    if (pid != 0)
        return;
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/usr/local/bin/backroot-hello", "backroot-hello", NULL);
    _exit(1);
}

static void spawn_exec(const char *cmd) {
    if (!cmd || !cmd[0])
        return;
    pid_t pid = fork();
    if (pid != 0)
        return;
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/bin/sh", "sh", "-c", cmd, NULL);
    _exit(1);
}

static void launch_action(Action act, const char *exec_cmd) {
    hide_menu();
    switch (act) {
    case ACT_TERMINAL: spawn_terminal(); break;
    case ACT_DOLPHIN: spawn_dolphin(); break;
    case ACT_HELLO: spawn_hello(); break;
    case ACT_DESKTOP: break;
    case ACT_EXEC: spawn_exec(exec_cmd); break;
    }
}

static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

static void strip_exec_field_codes(char *cmd) {
    char out[512];
    size_t o = 0;
    for (size_t i = 0; cmd[i] && o + 1 < sizeof(out); i++) {
        if (cmd[i] == '%') {
            char c = cmd[i + 1];
            if (c && strchr("fFuUdDnNickvm", c))
                i++;
            continue;
        }
        out[o++] = cmd[i];
    }
    out[o] = '\0';
    trim(out);
    snprintf(cmd, 512, "%s", out);
}

static int parse_desktop(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[512];
    char name[128] = "";
    char exec[512] = "";
    int hidden = 0;
    int nodisplay = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Name=", 5) == 0) {
            snprintf(name, sizeof(name), "%.*s",
                (int)(sizeof(name) - 1), line + 5);
            trim(name);
        } else if (strncmp(line, "Exec=", 5) == 0) {
            snprintf(exec, sizeof(exec), "%s", line + 5);
            trim(exec);
        } else if (strncmp(line, "Hidden=", 7) == 0) {
            hidden = (line[7] == 't' || line[7] == 'T' || line[7] == '1');
        } else if (strncmp(line, "NoDisplay=", 10) == 0) {
            nodisplay = (line[10] == 't' || line[10] == 'T' || line[10] == '1');
        }
    }
    fclose(f);
    if (hidden || nodisplay || !name[0] || !exec[0])
        return 0;
    strip_exec_field_codes(exec);
    if (!exec[0])
        return 0;
    for (int i = 0; i < n_apps; i++) {
        if (strcmp(apps[i].name, name) == 0)
            return 0;
    }
    if (n_apps >= MAX_APPS)
        return 0;
    AppEntry *a = &apps[n_apps++];
    snprintf(a->name, sizeof(a->name), "%s", name);
    snprintf(a->exec_cmd, sizeof(a->exec_cmd), "%s", exec);
    app_color(a);
    return 1;
}

static int cmp_app(const void *a, const void *b) {
    return strcasecmp(((const AppEntry *)a)->name, ((const AppEntry *)b)->name);
}

static void scan_desktop_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *ent;
    char path[512];
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        size_t len = strlen(ent->d_name);
        if (len < 8 || strcmp(ent->d_name + len - 8, ".desktop") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        parse_desktop(path);
    }
    closedir(d);
}

static void load_apps(void) {
    n_apps = 0;
    scan_desktop_dir("/usr/share/applications");
    scan_desktop_dir("/usr/local/share/applications");
    qsort(apps, (size_t)n_apps, sizeof(AppEntry), cmp_app);
}

static void add_tile(int x, int y, int w, int h, const char *label,
        int cr, int cg, int cb, char letter, Action act, const char *exec) {
    if (n_home_tiles >= (int)(sizeof(home_tiles) / sizeof(home_tiles[0])))
        return;
    Tile *t = &home_tiles[n_home_tiles++];
    t->x = x;
    t->y = y;
    t->w = w;
    t->h = h;
    strncpy(t->label, label, sizeof(t->label) - 1);
    t->cr = cr;
    t->cg = cg;
    t->cb = cb;
    t->letter = letter;
    t->act = act;
    if (exec)
        strncpy(t->exec_cmd, exec, sizeof(t->exec_cmd) - 1);
    else
        t->exec_cmd[0] = '\0';
}

static void layout_home(void) {
    n_home_tiles = 0;
    int col1 = MARGIN;
    int col2 = MARGIN + TILE + TILE_GAP;
    int y0 = HEADER_H;

    add_tile(col1, y0, TILE, TILE, "Dolphin", 0, 120, 215, 'D', ACT_DOLPHIN, NULL);
    add_tile(col2, y0, TILE, TILE, "Terminal", 16, 124, 16, 'T', ACT_TERMINAL, NULL);
    add_tile(col1, y0 + TILE + TILE_GAP, TILE, TILE, "Backroot Hello",
        92, 45, 145, 'B', ACT_HELLO, NULL);
    add_tile(col1, y0 + 2 * (TILE + TILE_GAP), TILE * 2 + TILE_GAP, TILE,
        "Desktop", 0, 0, 0, ' ', ACT_DESKTOP, NULL);

    home_h = y0 + 3 * (TILE + TILE_GAP);
}

static int apps_section_h(void) {
    if (n_apps == 0)
        return HEADER_H + 40;
    apps_cols = (win_w - MARGIN * 2) / APP_COL_W;
    if (apps_cols < 1)
        apps_cols = 1;
    int rows = (n_apps + apps_cols - 1) / apps_cols;
    return HEADER_H + rows * (APP_ROW_H + APP_GAP) + MARGIN;
}

static void layout_content(void) {
    layout_home();
    content_h = home_h + apps_section_h();
    int max_scroll = content_h - win_h;
    if (max_scroll < 0)
        max_scroll = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    if (scroll_y < 0)
        scroll_y = 0;
}

static Pixmap scale_image_to_pixmap(unsigned char *src, int sw, int sh, int dw, int dh) {
    int depth = DefaultDepth(dpy, screen);
    Pixmap pm = XCreatePixmap(dpy, start_win, dw, dh, depth);
    XImage *xi = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, dw, dh, 32, 0);
    if (!xi) {
        XFreePixmap(dpy, pm);
        return 0;
    }
    xi->data = calloc((size_t)xi->bytes_per_line * dh, 1);
    if (!xi->data) {
        XDestroyImage(xi);
        XFreePixmap(dpy, pm);
        return 0;
    }
    for (int dy = 0; dy < dh; dy++) {
        int sy = sh > 0 ? dy * sh / dh : 0;
        for (int dx = 0; dx < dw; dx++) {
            int sx = sw > 0 ? dx * sw / dw : 0;
            size_t idx = (size_t)(sy * sw + sx) * 3;
            unsigned long pix = rgb(src[idx], src[idx + 1], src[idx + 2]);
            XPutPixel(xi, dx, dy, pix);
        }
    }
    GC pg = XCreateGC(dpy, pm, 0, NULL);
    XPutImage(dpy, pm, pg, xi, 0, 0, 0, 0, dw, dh);
    XFreeGC(dpy, pg);
    XDestroyImage(xi);
    return pm;
}

static void load_wallpaper(void) {
    if (wallpaper_pm) {
        XFreePixmap(dpy, wallpaper_pm);
        wallpaper_pm = 0;
    }
    wallpaper_ready = 0;
    int iw = 0, ih = 0, comp = 0;
    unsigned char *data = stbi_load(WALLPAPER, &iw, &ih, &comp, 3);
    if (!data || iw <= 0 || ih <= 0)
        return;
    int tw = TILE * 2 + TILE_GAP;
    int th = TILE;
    wallpaper_pm = scale_image_to_pixmap(data, iw, ih, tw, th);
    stbi_image_free(data);
    wallpaper_ready = wallpaper_pm != 0;
}

static void draw_bg(void) {
    XSetForeground(dpy, gc, rgb(30, 46, 76));
    XFillRectangle(dpy, start_win, gc, 0, 0, win_w, win_h);

    XSetForeground(dpy, gc, rgb(40, 58, 90));
    for (int i = 0; i < 12; i++) {
        int cx = (i * 173 + 60) % (win_w + 200) - 100;
        int cy = (i * 131 + 40) % (content_h + 200) - scroll_y - 100;
        int r = 40 + (i * 37) % 80;
        if (cy + r < 0 || cy - r > win_h)
            continue;
        XFillArc(dpy, start_win, gc, cx - r, cy - r, r * 2, r * 2, 0, 360 * 64);
    }
}

static void draw_tile_glyph(const Tile *t, int sx, int sy) {
    if (t->act == ACT_DESKTOP && wallpaper_ready) {
        XCopyArea(dpy, wallpaper_pm, start_win, gc,
            0, 0, t->w, t->h, sx, sy);
        return;
    }
    XSetForeground(dpy, gc, rgb(t->cr, t->cg, t->cb));
    XFillRectangle(dpy, start_win, gc, sx, sy, t->w, t->h);

    if (t->act == ACT_TERMINAL) {
        xft_draw(start_win, ui_font, sx + t->w / 2 - 18, sy + t->h / 2 + 6,
            ">_", 255, 255, 255);
    } else if (t->letter) {
        char s[2] = { t->letter, 0 };
        if (ui_font) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)s, 1, &ext);
            xft_draw(start_win, ui_font,
                sx + (t->w - ext.xOff) / 2,
                sy + text_baseline(t->h, ui_font),
                s, 255, 255, 255);
        }
    }
}

static void draw_tile_label(const Tile *t, int sx, int sy) {
    xft_draw(start_win, ui_font, sx + 8, sy + t->h - 10, t->label, 255, 255, 255);
}

static void draw_home_section(void) {
    if (header_font)
        xft_draw(start_win, header_font, MARGIN, MARGIN + header_font->ascent,
            "Home", 255, 255, 255);

    for (int i = 0; i < n_home_tiles; i++) {
        const Tile *t = &home_tiles[i];
        int sy = t->y - scroll_y;
        if (sy + t->h < 0 || sy > win_h)
            continue;
        draw_tile_glyph(t, t->x, sy);
        draw_tile_label(t, t->x, sy);
    }
}

static void draw_apps_section(void) {
    int base_y = home_h - scroll_y;
    if (base_y > win_h)
        return;

    if (header_font)
        xft_draw(start_win, header_font, MARGIN, base_y + header_font->ascent,
            "Apps", 255, 255, 255);

    XSetForeground(dpy, gc, rgb(100, 170, 220));
    int line_y = base_y + HEADER_H - 8;
    if (line_y >= 0 && line_y < win_h)
        XDrawLine(dpy, start_win, gc, MARGIN, line_y, win_w - MARGIN, line_y);

    if (apps_cols < 1)
        apps_cols = 1;

    for (int i = 0; i < n_apps; i++) {
        int col = i % apps_cols;
        int row = i / apps_cols;
        int ax = MARGIN + col * APP_COL_W;
        int ay = base_y + HEADER_H + row * (APP_ROW_H + APP_GAP);
        if (ay + APP_ROW_H < 0 || ay > win_h)
            continue;

        const AppEntry *a = &apps[i];
        XSetForeground(dpy, gc, rgb(a->cr, a->cg, a->cb));
        XFillRectangle(dpy, start_win, gc, ax, ay, APP_ICON, APP_ICON);
        char s[2] = { a->letter, 0 };
        if (ui_font) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)s, 1, &ext);
            xft_draw(start_win, ui_font,
                ax + (APP_ICON - ext.xOff) / 2,
                ay + text_baseline(APP_ICON, ui_font),
                s, 255, 255, 255);
        }
        xft_draw(start_win, ui_font, ax + APP_ICON + 12,
            ay + text_baseline(APP_ROW_H, ui_font), a->name, 255, 255, 255);
    }
}

static void draw_all(void) {
    if (!visible)
        return;
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    root_h = ra.height;
    win_w = ra.width;
    win_h = root_h - PANEL_H;
    XMoveResizeWindow(dpy, start_win, 0, 0, win_w, win_h);
    layout_content();
    draw_bg();
    draw_home_section();
    draw_apps_section();
    XRaiseWindow(dpy, start_win);
}

static Tile *tile_at_content(int cx, int cy) {
    if (cy < HEADER_H)
        return NULL;
    for (int i = 0; i < n_home_tiles; i++) {
        Tile *t = &home_tiles[i];
        if (cx >= t->x && cx < t->x + t->w &&
            cy >= t->y && cy < t->y + t->h)
            return t;
    }
    return NULL;
}

static AppEntry *app_at_content(int cx, int cy) {
    int base_y = home_h;
    if (cy < base_y + HEADER_H)
        return NULL;
    if (apps_cols < 1)
        apps_cols = 1;
    int rel_y = cy - base_y - HEADER_H;
    int row = rel_y / (APP_ROW_H + APP_GAP);
    int in_row = rel_y % (APP_ROW_H + APP_GAP);
    if (in_row >= APP_ROW_H)
        return NULL;
    int col = (cx - MARGIN) / APP_COL_W;
    if (col < 0 || col >= apps_cols)
        return NULL;
    if (cx - MARGIN - col * APP_COL_W > APP_COL_W - 20)
        return NULL;
    int idx = row * apps_cols + col;
    if (idx < 0 || idx >= n_apps)
        return NULL;
    return &apps[idx];
}

static void handle_click(int x, int y) {
    int cy = y + scroll_y;
    Tile *t = tile_at_content(x, cy);
    if (t) {
        launch_action(t->act, t->exec_cmd);
        return;
    }
    AppEntry *a = app_at_content(x, cy);
    if (a)
        launch_action(ACT_EXEC, a->exec_cmd);
}

static void handle_scroll(int dir) {
    layout_content();
    int step = APP_ROW_H + APP_GAP;
    scroll_y += dir * step;
    layout_content();
    draw_all();
}

static void resize_to_root(void) {
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    root_h = ra.height;
    win_w = ra.width;
    win_h = root_h - PANEL_H;
    XMoveResizeWindow(dpy, start_win, 0, 0, win_w, win_h);
    if (visible)
        draw_all();
}

static void open_fonts(void) {
    static const char *const ui_names[] = {
        "Segoe UI-10:antialias=true",
        "sans-serif-10:antialias=true",
        "DejaVu Sans-10",
        NULL
    };
    static const char *const hdr_names[] = {
        "Segoe UI Light-42:antialias=true",
        "Segoe UI-36:antialias=true",
        "sans-serif-36:antialias=true",
        "DejaVu Sans-36",
        NULL
    };
    for (int i = 0; ui_names[i]; i++) {
        ui_font = XftFontOpenName(dpy, screen, ui_names[i]);
        if (ui_font && ui_font->ascent > 0)
            break;
        if (ui_font) {
            XftFontClose(dpy, ui_font);
            ui_font = NULL;
        }
    }
    for (int i = 0; hdr_names[i]; i++) {
        header_font = XftFontOpenName(dpy, screen, hdr_names[i]);
        if (header_font && header_font->ascent > 0)
            break;
        if (header_font) {
            XftFontClose(dpy, header_font);
            header_font = NULL;
        }
    }
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-start: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    XSetErrorHandler(NULL);

    br8_start_open = XInternAtom(dpy, "_BR8_START_OPEN", False);
    set_open(0);

    open_fonts();
    load_apps();
    layout_home();

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    root_h = ra.height;
    win_w = ra.width;
    win_h = root_h - PANEL_H;

    start_win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0, 0, rgb(30, 46, 76));
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask;
    XChangeWindowAttributes(dpy, start_win, CWOverrideRedirect | CWEventMask, &attr);
    gc = XCreateGC(dpy, start_win, 0, NULL);
    load_wallpaper();
    XUnmapWindow(dpy, start_win);
    visible = 0;

    XSelectInput(dpy, root, PropertyChangeMask | StructureNotifyMask);

    int xfd = ConnectionNumber(dpy);
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        select(xfd + 1, &fds, NULL, NULL, &tv);

        int want = read_open();
        if (want && !visible)
            show_menu();
        else if (!want && visible)
            hide_menu();

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0 && visible)
                draw_all();
            else if (ev.type == ConfigureNotify && ev.xconfigure.window == root)
                resize_to_root();
            else if (ev.type == PropertyNotify &&
                     ev.xproperty.window == root &&
                     ev.xproperty.atom == br8_start_open) {
                want = read_open();
                if (want && !visible)
                    show_menu();
                else if (!want && visible)
                    hide_menu();
            } else if (ev.type == ButtonPress && ev.xbutton.window == start_win && visible) {
                if (ev.xbutton.button == Button4)
                    handle_scroll(-1);
                else if (ev.xbutton.button == Button5)
                    handle_scroll(1);
                else if (ev.xbutton.button == Button1)
                    handle_click(ev.xbutton.x, ev.xbutton.y);
            } else if (ev.type == KeyPress && ev.xkey.window == start_win && visible) {
                KeySym sym = XLookupKeysym(&ev.xkey, 0);
                if (sym == XK_Escape)
                    hide_menu();
            }
        }
    }
    return 0;
}
