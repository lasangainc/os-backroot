/*
 * Backroot 8 window manager - minimal X11 WM
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define TITLE_H 28
#define BTN_W 22
#define BTN_H 18
#define BTN_PAD 4
#define CLOSE_EXTRA_W 6

typedef struct {
    Window frame;
    Window title;
    Window client;
    Window btn_min;
    Window btn_max;
    Window btn_close;
    int x, y, w, h;
    int mapped;
    int maximized;
    int saved_x, saved_y, saved_w, saved_h;
} Client;

static Display *dpy;
static int screen;
static Window root;
static Colormap cmap;
static GC gc_title, gc_btn, gc_close_bg, gc_close_border, gc_close_x;
static Atom wm_protocols, wm_delete, wm_state, net_wm_state;
static Client clients[256];
static int nclients;
static int dragging;
static int drag_x, drag_y;
static Client *drag_client;
static Window menu_win;
static int menu_visible;

static unsigned long rgb(int r, int g, int b) {
    XColor c;
    c.red = r << 8;
    c.green = g << 8;
    c.blue = b << 8;
    if (!XAllocColor(dpy, cmap, &c))
        return BlackPixel(dpy, screen);
    return c.pixel;
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

static Client *find_by_frame(Window frame) {
    for (int i = 0; i < nclients; i++)
        if (clients[i].frame == frame)
            return &clients[i];
    return NULL;
}

static void draw_close_button(Client *c) {
    XClearWindow(dpy, c->btn_close);
    int w = BTN_W + CLOSE_EXTRA_W;
    int h = BTN_H;
    int pad = 2;
    /* red square border */
    XSetForeground(dpy, gc_close_border, rgb(200, 40, 40));
    XDrawRectangle(dpy, c->btn_close, gc_close_border, pad, pad, w - 2 * pad - 1, h - 2 * pad - 1);
    /* elongated X */
    XSetForeground(dpy, gc_close_x, rgb(220, 220, 220));
    int cx = w / 2;
    int cy = h / 2;
    int dx = 5;
    int dy = 2;
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx, cy - dy, cx + dx, cy + dy);
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx, cy + dy, cx + dx, cy - dy);
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx + 1, cy - dy, cx + dx + 1, cy + dy);
    XDrawLine(dpy, c->btn_close, gc_close_x, cx - dx + 1, cy + dy, cx + dx + 1, cy - dy);
}

static void draw_title(Client *c, const char *name) {
    XSetForeground(dpy, gc_title, rgb(45, 48, 58));
    XFillRectangle(dpy, c->title, gc_title, 0, 0, c->w, TITLE_H);
    XSetForeground(dpy, gc_btn, rgb(230, 230, 235));
    if (name && name[0])
        XDrawString(dpy, c->title, gc_btn, 8, 18, name, strlen(name));
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
    /* xterm: standard OSS X11 terminal (reliable in minimal Arch images) */
    execl("/usr/bin/xterm", "xterm",
        "-fa", "Monospace", "-fs", "11",
        "-bg", "#1a1a22", "-fg", "#e8e8ec",
        "-title", "root@Backroot8",
        "-e", "/bin/bash", "-l",
        NULL);
    /* fallback: xfce4-terminal if installed and libraries match */
    execl("/usr/bin/xfce4-terminal", "xfce4-terminal",
        "--working-directory=/",
        "--title=root@Backroot8",
        NULL);
    _exit(1);
}

static void hide_menu(void) {
    if (menu_visible) {
        XUnmapWindow(dpy, menu_win);
        menu_visible = 0;
    }
}

static void show_menu(int x, int y) {
    hide_menu();
    XMoveWindow(dpy, menu_win, x, y);
    XMapRaised(dpy, menu_win);
    menu_visible = 1;
}

static void create_menu(void) {
    menu_win = XCreateSimpleWindow(dpy, root, 0, 0, 220, 36, 1,
        rgb(80, 80, 90), rgb(35, 38, 48));
    XSelectInput(dpy, menu_win, ExposureMask | ButtonPressMask);
    XMapWindow(dpy, menu_win);
    XUnmapWindow(dpy, menu_win);
    menu_visible = 0;
}

static void layout_client(Client *c) {
    int btn_total = 3 * BTN_W + CLOSE_EXTRA_W + 4 * BTN_PAD;
    int tx = c->w - btn_total;
    XMoveResizeWindow(dpy, c->title, 0, 0, c->w, TITLE_H);
    XMoveResizeWindow(dpy, c->client, 0, TITLE_H, c->w, c->h - TITLE_H);
    XMoveResizeWindow(dpy, c->btn_min, tx, (TITLE_H - BTN_H) / 2, BTN_W, BTN_H);
    tx += BTN_W + BTN_PAD;
    XMoveResizeWindow(dpy, c->btn_max, tx, (TITLE_H - BTN_H) / 2, BTN_W, BTN_H);
    tx += BTN_W + BTN_PAD;
    XMoveResizeWindow(dpy, c->btn_close, tx, (TITLE_H - BTN_H) / 2, BTN_W + CLOSE_EXTRA_W, BTN_H);
    draw_title(c, "Window");
    draw_close_button(c);
}

static void map_client(Client *c) {
    XMapWindow(dpy, c->frame);
    XMapSubwindows(dpy, c->frame);
    c->mapped = 1;
}

static void unmap_client(Client *c) {
    XUnmapWindow(dpy, c->frame);
    c->mapped = 0;
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
        c->w = c->saved_w;
        c->h = c->saved_h;
        XMoveResizeWindow(dpy, c->frame, c->saved_x, c->saved_y, c->w, c->h);
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
        c->h = ra.height - 32;
        XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
    }
    layout_client(c);
}

static void add_client(Window w) {
    if (nclients >= 256)
        return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa))
        return;
    if (wa.override_redirect)
        return;

    Client *c = &clients[nclients++];
    memset(c, 0, sizeof(*c));
    c->client = w;
    c->w = wa.width < 400 ? 640 : wa.width;
    c->h = (wa.height < 300 ? 400 : wa.height) + TITLE_H;
    c->x = 80 + nclients * 24;
    c->y = 60 + nclients * 24;

    c->frame = XCreateSimpleWindow(dpy, root, c->x, c->y, c->w, c->h, 2,
        rgb(70, 75, 90), rgb(28, 30, 38));
    c->title = XCreateSimpleWindow(dpy, c->frame, 0, 0, c->w, TITLE_H, 0, 0, 0);
    c->btn_min = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W, BTN_H, 1,
        rgb(100, 105, 120), rgb(55, 58, 70));
    c->btn_max = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W, BTN_H, 1,
        rgb(100, 105, 120), rgb(55, 58, 70));
    c->btn_close = XCreateSimpleWindow(dpy, c->frame, 0, 0, BTN_W + CLOSE_EXTRA_W, BTN_H, 0, 0, 0);

    XSelectInput(dpy, c->frame, SubstructureRedirectMask | SubstructureNotifyMask);
    XSelectInput(dpy, c->title, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
    XSelectInput(dpy, c->btn_min, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->btn_max, ButtonPressMask | ExposureMask);
    XSelectInput(dpy, c->btn_close, ButtonPressMask | ExposureMask);

    XReparentWindow(dpy, w, c->frame, 0, TITLE_H);
    layout_client(c);
    map_client(c);

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
}

static void handle_map_request(XMapRequestEvent *e) {
    for (int i = 0; i < nclients; i++)
        if (clients[i].client == e->window)
            return;
    add_client(e->window);
}

static void handle_destroy(XDestroyWindowEvent *e) {
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

    gc_title = XCreateGC(dpy, root, 0, NULL);
    gc_btn = XCreateGC(dpy, root, 0, NULL);
    gc_close_bg = XCreateGC(dpy, root, 0, NULL);
    gc_close_border = XCreateGC(dpy, root, 0, NULL);
    gc_close_x = XCreateGC(dpy, root, 0, NULL);

    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wm_state = XInternAtom(dpy, "WM_STATE", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);

    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);
    XSetErrorHandler(NULL);
    signal(SIGCHLD, SIG_IGN);

    create_menu();

    Cursor cur = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cur);

    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        if (ev.type == MapRequest)
            handle_map_request(&ev.xmaprequest);
        else if (ev.type == DestroyNotify)
            handle_destroy(&ev.xdestroywindow);
        else if (ev.type == ButtonPress) {
            /* Menu click must be handled before hide_menu() */
            if (menu_visible && ev.xbutton.window == menu_win) {
                hide_menu();
                spawn_terminal_root();
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
            if (ev.xbutton.window == c->btn_close) {
                close_client(c);
            } else if (ev.xbutton.window == c->btn_min) {
                unmap_client(c);
            } else if (ev.xbutton.window == c->btn_max) {
                maximize_client(c);
            } else if (ev.xbutton.window == c->title && ev.xbutton.button == Button1) {
                dragging = 1;
                drag_client = c;
                drag_x = ev.xbutton.x;
                drag_y = ev.xbutton.y;
            }
        } else if (ev.type == ButtonRelease && dragging) {
            dragging = 0;
            drag_client = NULL;
        } else if (ev.type == MotionNotify && dragging && drag_client) {
            drag_client->x = ev.xmotion.x_root - drag_x;
            drag_client->y = ev.xmotion.y_root - drag_y;
            XMoveWindow(dpy, drag_client->frame, drag_client->x, drag_client->y);
        } else if (ev.type == Expose) {
            if (ev.xexpose.window == menu_win && ev.xexpose.count == 0) {
                XSetForeground(dpy, gc_btn, rgb(230, 230, 235));
                XDrawString(dpy, menu_win, gc_btn, 12, 22,
                    "New terminal at root", 22);
            } else {
                Client *c = find_client(ev.xexpose.window);
                if (c) {
                    if (ev.xexpose.window == c->title)
                        draw_title(c, "Window");
                    else if (ev.xexpose.window == c->btn_close)
                        draw_close_button(c);
                }
            }
        }
    }
    return 0;
}
