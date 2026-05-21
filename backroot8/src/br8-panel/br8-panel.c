/*
 * Backroot 8 panel - transparent taskbar with open-app icons
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/select.h>

#include "emblem.h"

#define PANEL_H 32
#define ALPHA 0.82
#define EMBLEM_DISPLAY 24
#define BRAND_W (EMBLEM_DISPLAY + 16)
#define ICON_SZ 22
#define ICON_PAD 4
#define TASK_GAP 6
#define MAX_TASKS 48

typedef struct {
    Window frame;
    Window client;
    Pixmap icon;
    int icon_w, icon_h;
    char label[64];
    int x;
    int w;
} TaskBtn;

static Display *dpy;
static int screen;
static Window panel, root;
static GC gc;
static XftFont *ui_font;
static Visual *visual;
static Colormap xft_cmap;
static int panel_w;
static Atom br8_frame, br8_client, br8_panel_rev, br8_activate, br8_start_menu;
static Atom net_wm_icon, net_wm_name, utf8_string;
static TaskBtn tasks[MAX_TASKS];
static int ntasks;
static unsigned long last_rev;
static Pixmap emblem_pm;
static int emblem_ready;

static unsigned long rgb_pixel(int r, int g, int b) {
    XColor c;
    c.red = (unsigned short)(r << 8);
    c.green = (unsigned short)(g << 8);
    c.blue = (unsigned short)(b << 8);
    c.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, xft_cmap, &c))
        return BlackPixel(dpy, screen);
    return c.pixel;
}

static void emblem_init(void) {
    int disp = EMBLEM_DISPLAY;
    int depth = DefaultDepth(dpy, screen);
    emblem_pm = XCreatePixmap(dpy, panel, disp, disp, depth);
    XImage *xi = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, disp, disp, 32, 0);
    if (!xi)
        return;
    xi->data = calloc((size_t)xi->bytes_per_line * disp, 1);
    if (!xi->data) {
        XDestroyImage(xi);
        return;
    }

    for (int dy = 0; dy < disp; dy++) {
        for (int dx = 0; dx < disp; dx++) {
            int sx = dx * EMBLEM_W / disp;
            int sy = dy * EMBLEM_H / disp;
            size_t idx = (size_t)(sy * EMBLEM_W + sx) * 3;
            unsigned long pix = rgb_pixel(emblem_rgb[idx], emblem_rgb[idx + 1], emblem_rgb[idx + 2]);
            XPutPixel(xi, dx, dy, pix);
        }
    }

    GC egc = XCreateGC(dpy, emblem_pm, 0, NULL);
    XPutImage(dpy, emblem_pm, egc, xi, 0, 0, 0, 0, disp, disp);
    XFreeGC(dpy, egc);
    XDestroyImage(xi);
    emblem_ready = 1;
}

static void draw_emblem(void) {
    if (!emblem_ready)
        return;
    int x0 = 8;
    int y0 = (PANEL_H - EMBLEM_DISPLAY) / 2;
    XCopyArea(dpy, emblem_pm, panel, gc, 0, 0, EMBLEM_DISPLAY, EMBLEM_DISPLAY, x0, y0);
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

static void xft_draw(Drawable draw, int x, int y, const char *text, int r, int g, int b) {
    if (!ui_font || !text || !text[0])
        return;
    XftDraw *xd = XftDrawCreate(dpy, draw, visual, xft_cmap);
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

static unsigned long rgba(int r, int g, int b, double a) {
    int R = (int)(r * a + 20 * (1 - a));
    int G = (int)(g * a + 22 * (1 - a));
    int B = (int)(b * a + 30 * (1 - a));
    XColor c;
    c.red = R << 8;
    c.green = G << 8;
    c.blue = B << 8;
    XAllocColor(dpy, DefaultColormap(dpy, screen), &c);
    return c.pixel;
}

static unsigned long class_color(const char *s) {
    unsigned h = 0;
    for (const char *p = s; p && *p; p++)
        h = h * 31 + (unsigned char)*p;
    int r = 60 + (h & 0x7f);
    int g = 60 + ((h >> 8) & 0x7f);
    int b = 60 + ((h >> 16) & 0x7f);
    return rgba(r, g, b, 1.0);
}

static void free_tasks(void) {
    for (int i = 0; i < ntasks; i++) {
        if (tasks[i].icon)
            XFreePixmap(dpy, tasks[i].icon);
        tasks[i].icon = 0;
    }
    ntasks = 0;
}

static Pixmap icon_from_net_wm(Window client) {
    /* Skip _NET_WM_ICON XPutImage path (depth mismatch on std VGA); use fallback badge */
    (void)client;
    return 0;
}

static Pixmap icon_fallback(Window client) {
    char letter = '?';
    char label[64] = "App";
    XClassHint hint;
    if (XGetClassHint(dpy, client, &hint)) {
        if (hint.res_class && hint.res_class[0]) {
            letter = hint.res_class[0];
            strncpy(label, hint.res_class, sizeof(label) - 1);
        } else if (hint.res_name && hint.res_name[0]) {
            letter = hint.res_name[0];
            strncpy(label, hint.res_name, sizeof(label) - 1);
        }
        if (hint.res_name) XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
    }

    Pixmap pm = XCreatePixmap(dpy, panel, ICON_SZ, ICON_SZ, DefaultDepth(dpy, screen));
    GC igc = XCreateGC(dpy, pm, 0, NULL);
    XSetForeground(dpy, igc, class_color(label));
    XFillRectangle(dpy, pm, igc, 0, 0, ICON_SZ, ICON_SZ);
    XFreeGC(dpy, igc);
    char s[2] = { letter, 0 };
    if (ui_font) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)s, 1, &ext);
        xft_draw(pm, (ICON_SZ - ext.xOff) / 2, text_baseline(ICON_SZ), s, 255, 255, 255);
    }
    return pm;
}

static void get_client_label(Window client, char *buf, size_t n) {
    unsigned char *data = NULL;
    Atom type;
    int fmt;
    unsigned long items, bytes;

    if (XGetWindowProperty(dpy, client, net_wm_name, 0, 256, False,
            utf8_string, &type, &fmt, &items, &bytes, &data) == Success && data) {
        snprintf(buf, n, "%.*s", (int)(n - 1), (char *)data);
        XFree(data);
        return;
    }
    char *name = NULL;
    if (XFetchName(dpy, client, &name) && name) {
        strncpy(buf, name, n - 1);
        buf[n - 1] = '\0';
        XFree(name);
        return;
    }
    strncpy(buf, "App", n);
}

static int is_br8_frame(Window w) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    int ok = 0;
    if (XGetWindowProperty(dpy, w, br8_frame, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0 && data[0])
        ok = 1;
    if (data) XFree(data);
    return ok;
}

static Window frame_client(Window frame) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    Window *data = NULL;
    Window c = 0;
    if (XGetWindowProperty(dpy, frame, br8_client, 0, 8, False, XA_WINDOW,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        c = data[0];
    if (data) XFree(data);
    return c;
}

static void collect_tasks(void) {
    free_tasks();
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nch;
    if (!XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nch))
        return;

    int x = BRAND_W + 8;
    for (unsigned int i = 0; i < nch && ntasks < MAX_TASKS; i++) {
        if (children[i] == panel || !is_br8_frame(children[i]))
            continue;

        Window frame = children[i];
        Window client = frame_client(frame);
        if (!client)
            continue;

        TaskBtn *t = &tasks[ntasks++];
        t->frame = frame;
        t->client = client;
        t->x = x;
        t->w = ICON_SZ + ICON_PAD * 2;
        x += t->w + TASK_GAP;

        get_client_label(client, t->label, sizeof(t->label));
        t->icon = icon_from_net_wm(client);
        if (!t->icon)
            t->icon = icon_fallback(client);
        t->icon_w = ICON_SZ;
        t->icon_h = ICON_SZ;
    }
    if (children) XFree(children);
}

static void draw_panel(void) {
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    panel_w = ra.width;
    XResizeWindow(dpy, panel, panel_w, PANEL_H);

    unsigned long bg = rgba(30, 32, 42, ALPHA);
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, panel, gc, 0, 0, panel_w, PANEL_H);

    draw_emblem();

    collect_tasks();

    for (int i = 0; i < ntasks; i++) {
        int ix = tasks[i].x + ICON_PAD;
        int iy = (PANEL_H - ICON_SZ) / 2;
        XSetForeground(dpy, gc, rgba(55, 60, 75, 0.9));
        XFillRectangle(dpy, panel, gc, tasks[i].x, iy - 2, tasks[i].w, ICON_SZ + 4);
        if (tasks[i].icon)
            XCopyArea(dpy, tasks[i].icon, panel, gc, 0, 0, ICON_SZ, ICON_SZ, ix, iy);
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M", tm);
    if (ui_font) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)buf, (int)strlen(buf), &ext);
        xft_draw(panel, panel_w - ext.xOff - 16, text_baseline(PANEL_H), buf, 200, 200, 210);
    }

    XRaiseWindow(dpy, panel);
}

static TaskBtn *task_at(int px) {
    for (int i = 0; i < ntasks; i++)
        if (px >= tasks[i].x && px < tasks[i].x + tasks[i].w)
            return &tasks[i];
    return NULL;
}

static unsigned long read_start_menu_rev(void) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    unsigned long rev = 0;

    if (XGetWindowProperty(dpy, root, br8_start_menu, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        rev = data[0];
    if (data)
        XFree(data);
    return rev;
}

static void toggle_start_menu(void) {
    unsigned long rev;
    int fd;
    char c = 't';

    rev = read_start_menu_rev() + 1;
    XChangeProperty(dpy, root, br8_start_menu, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&rev, 1);
    XFlush(dpy);

    fd = open("/tmp/br8-start-menu.ctl", O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        if (write(fd, &c, 1) == 1) {
            close(fd);
            return;
        }
        close(fd);
    }

    if (fork() == 0) {
        execl("/usr/local/bin/br8-start-menu", "br8-start-menu", "--toggle", NULL);
        _exit(1);
    }
}

static void activate_task(TaskBtn *t) {
    /* Tell WM to restore (property is reliable; ClientMessage often is not). */
    XChangeProperty(dpy, root, br8_activate, XA_WINDOW, 32, PropModeReplace,
        (unsigned char *)&t->frame, 1);
    /* Also map directly — is_our_chrome prevents duplicate title bars now. */
    XMapWindow(dpy, t->frame);
    XMapSubwindows(dpy, t->frame);
    XRaiseWindow(dpy, t->frame);
    XFlush(dpy);
}

static unsigned long read_panel_rev(void) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    unsigned long rev = 0;
    if (XGetWindowProperty(dpy, root, br8_panel_rev, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        rev = data[0];
    if (data) XFree(data);
    return rev;
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-panel: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
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

    XSetErrorHandler(NULL);

    br8_frame = XInternAtom(dpy, "_BR8_FRAME", False);
    br8_client = XInternAtom(dpy, "_BR8_CLIENT", False);
    br8_panel_rev = XInternAtom(dpy, "_BR8_PANEL_REV", False);
    br8_activate = XInternAtom(dpy, "_BR8_ACTIVATE", False);
    br8_start_menu = XInternAtom(dpy, "_BR8_START_MENU", False);
    net_wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    utf8_string = XInternAtom(dpy, "UTF8_STRING", False);

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    panel_w = ra.width;

    panel = XCreateSimpleWindow(dpy, root, 0, ra.height - PANEL_H, panel_w, PANEL_H,
        0, 0, rgba(30, 32, 42, ALPHA));
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | PropertyChangeMask;
    XChangeWindowAttributes(dpy, panel, CWOverrideRedirect | CWEventMask, &attr);
    XSelectInput(dpy, root, PropertyChangeMask);

    gc = XCreateGC(dpy, panel, 0, NULL);
    emblem_init();
    XMapRaised(dpy, panel);
    draw_panel();
    last_rev = read_panel_rev();

    int xfd = ConnectionNumber(dpy);
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        select(xfd + 1, &fds, NULL, NULL, &tv);

        unsigned long rev = read_panel_rev();
        if (rev != last_rev) {
            last_rev = rev;
            draw_panel();
        }

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0)
                draw_panel();
            else if (ev.type == ConfigureNotify) {
                XGetWindowAttributes(dpy, root, &ra);
                XMoveResizeWindow(dpy, panel, 0, ra.height - PANEL_H, ra.width, PANEL_H);
                draw_panel();
            } else if (ev.type == ButtonPress && ev.xbutton.window == panel) {
                if (ev.xbutton.x < BRAND_W)
                    toggle_start_menu();
                else {
                    TaskBtn *t = task_at(ev.xbutton.x);
                    if (t)
                        activate_task(t);
                }
            } else if (ev.type == PropertyNotify &&
                       (ev.xproperty.window == root && ev.xproperty.atom == br8_panel_rev)) {
                last_rev = read_panel_rev();
                draw_panel();
            }
        }
    }
    return 0;
}
