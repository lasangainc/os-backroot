/*
 * Backroot 8 metro app flag — set on the client window before mapping.
 * The window manager reads _BR8_METRO and runs the app borderless fullscreen.
 */
#ifndef BR8_METRO_H
#define BR8_METRO_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define BR8_METRO_ATOM "_BR8_METRO"

static inline Atom br8_metro_atom(Display *dpy) {
    return XInternAtom(dpy, BR8_METRO_ATOM, False);
}

static inline void br8_set_metro(Display *dpy, Window win) {
    Atom atom = br8_metro_atom(dpy);
    unsigned long one = 1;
    XChangeProperty(dpy, win, atom, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&one, 1);
}

static inline int br8_get_metro(Display *dpy, Window win) {
    Atom atom = br8_metro_atom(dpy);
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    int metro = 0;

    if (XGetWindowProperty(dpy, win, atom, 0, 1, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success &&
        data && n > 0 && data[0])
        metro = 1;
    if (data)
        XFree(data);
    return metro;
}

#endif
