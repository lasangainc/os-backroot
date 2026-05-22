/*
 * PowerPDF — basic metro PDF reader (Poppler + Cairo + X11)
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <poppler.h>
#include <poppler-page.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/select.h>

#include "../../include/br8-metro.h"

#define MIN_W 640
#define MIN_H 480
#define PAD 16
#define BAR_H 36

static Display *dpy;
static int screen;
static Window root, win;
static Visual *visual;
static Colormap cmap;
static int win_w, win_h;
static PopplerDocument *doc;
static int page_num;
static int page_count;
static Pixmap page_pix;
static int pix_w, pix_h;
static GC gc;
static char status[256];
static char doc_path[1024];

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

static void set_status(const char *msg) {
    snprintf(status, sizeof(status), "%s", msg ? msg : "");
}

static void free_page_pixmap(void) {
    if (page_pix) {
        XFreePixmap(dpy, page_pix);
        page_pix = 0;
    }
    pix_w = pix_h = 0;
}

static int render_current_page(void) {
    if (!doc)
        return 0;

    PopplerPage *page = poppler_document_get_page(doc, page_num);
    if (!page)
        return 0;

    double pw, ph;
    poppler_page_get_size(page, &pw, &ph);
    int area_w = win_w - PAD * 2;
    int area_h = win_h - PAD * 2 - BAR_H;
    if (area_w < 64)
        area_w = 64;
    if (area_h < 64)
        area_h = 64;

    double scale = area_w / pw;
    if (ph * scale > area_h)
        scale = area_h / ph;

    pix_w = (int)(pw * scale);
    pix_h = (int)(ph * scale);
    if (pix_w < 1)
        pix_w = 1;
    if (pix_h < 1)
        pix_h = 1;

    free_page_pixmap();
    page_pix = XCreatePixmap(dpy, win, pix_w, pix_h,
        (unsigned int)DefaultDepth(dpy, screen));

    cairo_surface_t *surface = cairo_xlib_surface_create(dpy, page_pix, visual,
        pix_w, pix_h);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    poppler_page_render(page, cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(page);

    const char *base = doc_path[0] ? strrchr(doc_path, '/') : NULL;
    base = base ? base + 1 : (doc_path[0] ? doc_path : "document");
    snprintf(status, sizeof(status), "%.80s — page %d / %d",
        base, page_num + 1, page_count);
    return 1;
}

static int load_document(const char *path) {
    if (!path || !path[0])
        return 0;

    GError *err = NULL;
    char uri[1200];
    char abs[PATH_MAX];
    const char *use = path;
    if (strncmp(path, "file://", 7) != 0 && realpath(path, abs))
        use = abs;
    if (strncmp(use, "file://", 7) == 0)
        snprintf(uri, sizeof(uri), "%s", use);
    else
        snprintf(uri, sizeof(uri), "file://%s", use);

    if (doc) {
        g_object_unref(doc);
        doc = NULL;
    }

    doc = poppler_document_new_from_file(uri, NULL, &err);
    if (!doc) {
        set_status(err && err->message ? err->message : "Failed to open PDF");
        if (err)
            g_error_free(err);
        return 0;
    }

    strncpy(doc_path, path, sizeof(doc_path) - 1);
    doc_path[sizeof(doc_path) - 1] = '\0';
    page_count = poppler_document_get_n_pages(doc);
    page_num = 0;
    return render_current_page();
}

static void draw_window(void) {
    XSetForeground(dpy, gc, rgb(30, 32, 42));
    XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);

    XSetForeground(dpy, gc, rgb(220, 220, 230));
    int ty = win_h - BAR_H + 10;
    if (status[0])
        XDrawString(dpy, win, gc, PAD, ty, status, (int)strlen(status));

    if (!page_pix) {
        const char *hint = "PowerPDF — usage: powerpdf file.pdf";
        XDrawString(dpy, win, gc, PAD, win_h / 2, hint, (int)strlen(hint));
        return;
    }

    int x = (win_w - pix_w) / 2;
    int y = PAD + (win_h - BAR_H - PAD - pix_h) / 2;
    if (y < PAD)
        y = PAD;
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XFillRectangle(dpy, win, gc, x - 2, y - 2, pix_w + 4, pix_h + 4);
    XCopyArea(dpy, page_pix, win, gc, 0, 0, pix_w, pix_h, x, y);
}

static void change_page(int delta) {
    if (!doc || page_count <= 0)
        return;
    int next = page_num + delta;
    if (next < 0 || next >= page_count)
        return;
    page_num = next;
    render_current_page();
    draw_window();
}

static void resize_window(int w, int h) {
    if (w < MIN_W)
        w = MIN_W;
    if (h < MIN_H)
        h = MIN_H;
    win_w = w;
    win_h = h;
    XResizeWindow(dpy, win, win_w, win_h);
    if (doc)
        render_current_page();
    draw_window();
}

int main(int argc, char **argv) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "powerpdf: cannot open display\n");
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    win_w = ra.width > 0 ? ra.width : 1024;
    win_h = ra.height > 0 ? ra.height : 768;

    win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0,
        BlackPixel(dpy, screen), rgb(30, 32, 42));
    XStoreName(dpy, win, "PowerPDF");
    {
        XClassHint ch;
        ch.res_name = (char *)"PowerPDF";
        ch.res_class = (char *)"PowerPDF";
        XSetClassHint(dpy, win, &ch);
    }

    br8_set_metro(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);
    XSelectInput(dpy, win,
        ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask);

    XMapRaised(dpy, win);

    if (argc > 1)
        load_document(argv[1]);
    else
        set_status("PowerPDF — pass a PDF path on the command line");

    draw_window();

    while (1) {
        if (!XPending(dpy)) {
            struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
            int xfd = ConnectionNumber(dpy);
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            select(xfd + 1, &fds, NULL, NULL, &tv);
        }

        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.count == 0)
            draw_window();
        else if (ev.type == ConfigureNotify && ev.xconfigure.window == win)
            resize_window(ev.xconfigure.width, ev.xconfigure.height);
        else if (ev.type == KeyPress) {
            KeySym sym = XLookupKeysym(&ev.xkey, 0);
            if (sym == XK_Escape)
                break;
            if (sym == XK_Page_Down || sym == XK_Right || sym == XK_Down)
                change_page(1);
            else if (sym == XK_Page_Up || sym == XK_Left || sym == XK_Up)
                change_page(-1);
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == 4)
                change_page(-1);
            else if (ev.xbutton.button == 5)
                change_page(1);
        }
    }

    free_page_pixmap();
    if (doc)
        g_object_unref(doc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
