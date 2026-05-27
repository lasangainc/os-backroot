/*
 * Backroot 8 metro helper — Metro fullscreen + charms when using Openbox.
 * Works alongside an external WM; keeps br8-start / _BR8_METRO protocol.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#include "../br8-panel/emblem.h"

#define CHARMS_EDGE_ZONE 8
#define CHARMS_STRIP_W 76
#define CHARMS_BTN_H 92
#define CHARMS_EMBLEM_SZ 36
#define CHARMS_TIME_W 320
#define CHARMS_TIME_H 108
#define CHARMS_TIME_PAD 20
#define METRO_SWIPE_ZONE 56
#define METRO_SWIPE_THRESHOLD 72
#define PANEL_H 40
#define MAX_METRO 64

typedef struct {
    Window client;
    int mapped;
} MetroWin;

static Display *dpy;
static int screen;
static Window root;
static Colormap cmap;
static Visual *visual;
static Colormap xft_cmap;
static GC gc_title;
static XftFont *ui_font;
static Atom wm_protocols, wm_delete;
static Atom br8_metro, br8_start_open, br8_metro_active, br8_panel_rev;
static Atom net_wm_state, net_wm_state_fullscreen, net_wm_state_hidden;
static Atom net_wm_window_type, net_wm_window_type_desktop;
static MetroWin metros[MAX_METRO];
static int nmetros;
static unsigned long panel_rev;
static int root_w, root_h;
static int metro_swipe, metro_swipe_y;
static Window charms_strip, charms_clock;
static int charms_visible, charms_hover;
static Pixmap charms_emblem_pm;
static int charms_emblem_ready;
static XftFont *charms_time_font, *charms_date_font;
static time_t charms_clock_last;

static void charms_hide(void);

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

static void update_root_geom(void) {
    XWindowAttributes ra;
    if (XGetWindowAttributes(dpy, root, &ra)) {
        root_w = ra.width;
        root_h = ra.height;
    }
}

static void bump_panel(void) {
    panel_rev++;
    XChangeProperty(dpy, root, br8_panel_rev, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&panel_rev, 1);
}

static int client_has_metro(Window w) {
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

static MetroWin *find_metro(Window w) {
    for (int i = 0; i < nmetros; i++)
        if (metros[i].client == w)
            return &metros[i];
    return NULL;
}

static int is_background_window(Window w) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    Atom *data = NULL;
    int skip = 0;
    if (XGetWindowProperty(dpy, w, net_wm_window_type, 0, 16, False, XA_ATOM,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) != Success || !data)
        return 0;
    for (unsigned long i = 0; i < n; i++) {
        if (data[i] == net_wm_window_type_desktop) {
            skip = 1;
            break;
        }
    }
    XFree(data);
    return skip;
}

static void set_net_wm_state(Window w, Atom state, int add) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = net_wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = add ? 1 : 0;
    ev.xclient.data.l[1] = (long)state;
    ev.xclient.data.l[2] = 0;
    XSendEvent(dpy, root, False,
        SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&ev);
}

static void apply_metro_geometry(MetroWin *m) {
    if (!m || !m->mapped)
        return;
    set_net_wm_state(m->client, net_wm_state_fullscreen, 1);
    XMoveResizeWindow(dpy, m->client, 0, 0, root_w, root_h);
    XRaiseWindow(dpy, m->client);
}

static void update_metro_root_state(void) {
    int any = 0;
    for (int i = 0; i < nmetros; i++) {
        if (metros[i].mapped) {
            any = 1;
            break;
        }
    }
    unsigned long v = any ? 1 : 0;
    XChangeProperty(dpy, root, br8_metro_active, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&v, 1);
    if (!any)
        charms_hide();
    bump_panel();
}

static void open_start_menu(void) {
    unsigned long one = 1;
    XChangeProperty(dpy, root, br8_start_open, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&one, 1);
}

static MetroWin *top_metro(void) {
    MetroWin *best = NULL;
    for (int i = 0; i < nmetros; i++) {
        if (!metros[i].mapped)
            continue;
        best = &metros[i];
    }
    return best;
}

static void metro_return_to_start(MetroWin *m) {
    if (!m)
        return;
    m->mapped = 0;
    set_net_wm_state(m->client, net_wm_state_fullscreen, 0);
    set_net_wm_state(m->client, net_wm_state_hidden, 1);
    XIconifyWindow(dpy, m->client, screen);
    update_metro_root_state();
    open_start_menu();
}

static void close_metro(MetroWin *m) {
    if (!m)
        return;
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = m->client;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)wm_delete;
    XSendEvent(dpy, m->client, False, NoEventMask, &ev);
    XFlush(dpy);
}

static void remove_metro(MetroWin *m) {
    int idx = (int)(m - metros);
    if (idx < 0 || idx >= nmetros)
        return;
    memmove(&metros[idx], &metros[idx + 1], (size_t)(nmetros - idx - 1) * sizeof(MetroWin));
    nmetros--;
    update_metro_root_state();
}

static void track_metro(Window w) {
    if (nmetros >= MAX_METRO || find_metro(w) || !client_has_metro(w))
        return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect)
        return;
    if (is_background_window(w))
        return;
    MetroWin *m = &metros[nmetros++];
    m->client = w;
    m->mapped = (wa.map_state == IsViewable);
    apply_metro_geometry(m);
    update_metro_root_state();
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
    char time_buf[32], date_buf[64];
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
    xft_draw(charms_strip, 10, btn1_y + text_baseline(CHARMS_BTN_H / 2), "Close", 255, 255, 255);
    xft_draw(charms_strip, 18, btn0_y + CHARMS_BTN_H - 14, "Home", 200, 200, 210);
}

static void charms_show(void) {
    if (charms_visible || !top_metro())
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
    if (!top_metro()) {
        charms_hide();
        return;
    }
    int in_edge = x >= root_w - CHARMS_EDGE_ZONE;
    int in_strip = charms_visible && x >= root_w - CHARMS_STRIP_W;
    if (in_edge || in_strip) {
        if (!charms_visible)
            charms_show();
        if (in_strip) {
            int btn = charms_btn_at(x - (root_w - CHARMS_STRIP_W), y);
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
    MetroWin *m = top_metro();
    if (!m || !btn)
        return;
    charms_hide();
    if (btn == 1)
        metro_return_to_start(m);
    else if (btn == 2)
        close_metro(m);
}

static void charms_tick_clock(void) {
    if (!charms_visible)
        return;
    time_t now = time(NULL);
    if (now != charms_clock_last)
        draw_charms_clock();
}

static void create_charms_windows(void) {
    unsigned long black = rgb(16, 16, 16);
    charms_strip = XCreateSimpleWindow(dpy, root, 0, 0, CHARMS_STRIP_W, root_h, 0, 0, black);
    charms_clock = XCreateSimpleWindow(dpy, root, 0, 0, CHARMS_TIME_W, CHARMS_TIME_H, 0, 0, black);
    XSetWindowAttributes attr;
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
        "sans-serif-48:weight=light", "Segoe UI-48:weight=light", "DejaVu Sans-48", NULL
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
    static const char *const date_fonts[] = { "sans-serif-13", "DejaVu Sans-13", NULL };
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

static void metro_check_swipe(XMotionEvent *ev) {
    if (!metro_swipe)
        return;
    MetroWin *m = top_metro();
    if (!m)
        return;
    if (metro_swipe_y - ev->y_root >= METRO_SWIPE_THRESHOLD)
        metro_return_to_start(m);
    metro_swipe = 0;
}

static void scan_existing(void) {
    Window root_ret, parent;
    Window *children = NULL;
    unsigned int nch;
    if (!XQueryTree(dpy, root, &root_ret, &parent, &children, &nch))
        return;
    for (unsigned int i = 0; i < nch; i++)
        track_metro(children[i]);
    if (children)
        XFree(children);
}

static void handle_map(Window w) {
    MetroWin *m = find_metro(w);
    if (m) {
        m->mapped = 1;
        apply_metro_geometry(m);
        update_metro_root_state();
        return;
    }
    track_metro(w);
}

static void handle_unmap(Window w) {
    MetroWin *m = find_metro(w);
    if (!m)
        return;
    m->mapped = 0;
    update_metro_root_state();
}

static void handle_destroy(Window w) {
    MetroWin *m = find_metro(w);
    if (m)
        remove_metro(m);
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-metro-helper: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    xft_cmap = DefaultColormap(dpy, screen);
    static const char *const font_names[] = {
        "Segoe UI-10:antialias=true", "sans-serif-10", "DejaVu Sans-10", NULL
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

    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    br8_metro = XInternAtom(dpy, "_BR8_METRO", False);
    br8_start_open = XInternAtom(dpy, "_BR8_START_OPEN", False);
    br8_metro_active = XInternAtom(dpy, "_BR8_METRO_ACTIVE", False);
    br8_panel_rev = XInternAtom(dpy, "_BR8_PANEL_REV", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    net_wm_state_hidden = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XSetErrorHandler(NULL);
    signal(SIGCHLD, SIG_IGN);
    update_root_geom();
    create_charms_windows();
    scan_existing();
    bump_panel();

    XSelectInput(dpy, root,
        SubstructureNotifyMask | PropertyChangeMask | StructureNotifyMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

    while (1) {
        if (!XPending(dpy)) {
            if (charms_visible)
                charms_tick_clock();
            struct timeval tv = { .tv_sec = 0, .tv_usec = charms_visible ? 500000 : 250000 };
            int xfd = ConnectionNumber(dpy);
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            select(xfd + 1, &fds, NULL, NULL, &tv);
            if (charms_visible)
                charms_tick_clock();
        }
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == MapNotify)
            handle_map(ev.xmap.window);
        else if (ev.type == UnmapNotify)
            handle_unmap(ev.xunmap.window);
        else if (ev.type == DestroyNotify)
            handle_destroy(ev.xdestroywindow.window);
        else if (ev.type == ConfigureNotify && ev.xconfigure.window == root) {
            update_root_geom();
            for (int i = 0; i < nmetros; i++) {
                if (metros[i].mapped)
                    apply_metro_geometry(&metros[i]);
            }
            if (charms_visible)
                layout_charms_windows();
        } else if (ev.type == PropertyNotify && ev.xproperty.atom == br8_metro)
            track_metro(ev.xproperty.window);
        else if (ev.type == ButtonPress) {
            if (ev.xbutton.window == charms_strip) {
                charms_handle_click(charms_btn_at((int)ev.xbutton.x, (int)ev.xbutton.y));
                continue;
            }
            if (ev.xbutton.window == root && ev.xbutton.y_root >= root_h - METRO_SWIPE_ZONE) {
                metro_swipe = 1;
                metro_swipe_y = ev.xbutton.y_root;
            }
        } else if (ev.type == ButtonRelease) {
            metro_swipe = 0;
        } else if (ev.type == MotionNotify) {
            if (metro_swipe && (ev.xmotion.state & Button1Mask))
                metro_check_swipe(&ev.xmotion);
            charms_pointer_update(ev.xmotion.x_root, ev.xmotion.y_root);
        } else if (ev.type == Expose) {
            if (ev.xexpose.window == charms_strip && ev.xexpose.count == 0)
                draw_charms_strip();
            else if (ev.xexpose.window == charms_clock && ev.xexpose.count == 0)
                draw_charms_clock();
        }
    }
    return 0;
}
