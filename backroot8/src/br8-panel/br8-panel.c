/*
 * Backroot 8 panel - simple transparent taskbar (no blur)
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PANEL_H 32
#define ALPHA 0.82

static Display *dpy;
static int screen;
static Window panel, root;
static GC gc;
static int panel_w;

static unsigned long rgba(int r, int g, int b, double a) {
    /* Solid blend on dark bg - no compositor blur */
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

static void draw_panel(void) {
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    panel_w = ra.width;
    XResizeWindow(dpy, panel, panel_w, PANEL_H);

    unsigned long bg = rgba(30, 32, 42, ALPHA);
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, panel, gc, 0, 0, panel_w, PANEL_H);

    XSetForeground(dpy, gc, rgba(120, 180, 255, 1.0));
    XDrawString(dpy, panel, gc, 12, 21, "Backroot 8", 10);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M", tm);
    int tw = XTextWidth(XLoadQueryFont(dpy, "fixed"), buf, strlen(buf));
    XSetForeground(dpy, gc, rgba(200, 200, 210, 1.0));
    XDrawString(dpy, panel, gc, panel_w - tw - 16, 21, buf, strlen(buf));
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-panel: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    panel_w = ra.width;

    panel = XCreateSimpleWindow(dpy, root, 0, ra.height - PANEL_H, panel_w, PANEL_H,
        0, 0, rgba(30, 32, 42, ALPHA));
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | StructureNotifyMask;
    XChangeWindowAttributes(dpy, panel, CWOverrideRedirect | CWEventMask, &attr);

    gc = XCreateGC(dpy, panel, 0, NULL);
    XMapRaised(dpy, panel);
    draw_panel();

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose && ev.xexpose.count == 0)
            draw_panel();
        else if (ev.type == ConfigureNotify) {
            XGetWindowAttributes(dpy, root, &ra);
            XMoveResizeWindow(dpy, panel, 0, ra.height - PANEL_H, ra.width, PANEL_H);
            draw_panel();
        }
    }
    return 0;
}
