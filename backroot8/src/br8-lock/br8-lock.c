/*
 * Backroot 8 lock screen — wallpaper + clock, swipe up for password login.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <ctype.h>
#include <time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "../br8-start/stb_image.h"

#define WALLPAPER_DEFAULT "/usr/share/backgrounds/backroot8.jpg"
#define USER_PIC "/usr/share/backroot8/default-user.png"
#define CHKPASS "/usr/libexec/br8-chkpass"
#define SHOW_START_FLAG "/run/br8-lock/show-start"

#define COL_BG_R 70
#define COL_BG_G 29
#define COL_BG_B 96
#define COL_ACCENT_R 112
#define COL_ACCENT_G 48
#define COL_ACCENT_B 160

#define PASS_MAX 128
#define DRAG_UNLOCK_PX 72
#define CLOCK_PAD 48
#define LOGIN_AVATAR 120
#define FIELD_W 420
#define FIELD_H 36
#define SUBMIT_SZ 36
#define BACK_BTN 44
#define CARET_BLINK_MS 530

typedef enum { PHASE_LOCK, PHASE_LOGIN } Phase;

static Display *dpy;
static int screen;
static Window root, win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static int win_w, win_h;
static XftFont *font_clock, *font_date, *font_name, *font_field;
static Phase phase = PHASE_LOCK;
static Pixmap wallpaper_pm;
static int wallpaper_ready;
static Pixmap user_pic_pm;
static int user_pic_ready;
static char user_name[64];
static char user_display_name[64];
static char password[PASS_MAX];
static int caret_on;
static double caret_ms;
static int drag_active;
static int drag_start_y;
static int slide_offset;
static int submit_hover;
static int back_hover;
static int login_err;
static double login_err_ms;
static time_t clock_last;
static Drawable canvas;

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

static int text_baseline(int box_h, XftFont *font) {
    if (!font)
        return box_h / 2;
    return (box_h + font->ascent - font->descent) / 2;
}

static int text_width(XftFont *font, const char *s) {
    if (!font || !s)
        return 0;
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)s, (int)strlen(s), &ext);
    return ext.xOff;
}

static void xft_draw(Drawable dst, int x, int y, const char *s, int r, int g, int b, XftFont *font) {
    if (!font || !s || !s[0])
        return;
    XftDraw *xd = XftDrawCreate(dpy, dst, visual, cmap);
    if (!xd)
        return;
    XRenderColor rc;
    rc.red = (unsigned short)(r * 257);
    rc.green = (unsigned short)(g * 257);
    rc.blue = (unsigned short)(b * 257);
    rc.alpha = 0xffff;
    XftColor col;
    if (XftColorAllocValue(dpy, visual, cmap, &rc, &col)) {
        XftDrawStringUtf8(xd, &col, font, x, y, (FcChar8 *)s, (int)strlen(s));
        XftColorFree(dpy, visual, cmap, &col);
    }
    XftDrawDestroy(xd);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static const char *user_home_dir(void) {
    const char *home = getenv("HOME");
    struct passwd *pw = getpwuid(getuid());
    if ((!home || !home[0]) && pw && pw->pw_dir && pw->pw_dir[0])
        home = pw->pw_dir;
    if (!home || !home[0])
        home = "/root";
    return home;
}

static void init_user_name(void) {
    const char *u = getenv("USER");
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name && pw->pw_name[0])
        u = pw->pw_name;
    if (!u || !u[0])
        u = "root";
    snprintf(user_name, sizeof user_name, "%s", u);
    snprintf(user_display_name, sizeof user_display_name, "%s", u);
    if (user_display_name[0])
        user_display_name[0] = (char)toupper((unsigned char)user_display_name[0]);
}

static Pixmap scale_cover(unsigned char *src, int sw, int sh, int dw, int dh) {
    int depth = DefaultDepth(dpy, screen);
    Pixmap pm = XCreatePixmap(dpy, win, dw, dh, depth);
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
    double scale = (double)dw / (double)sw;
    if ((double)dh / (double)sh > scale)
        scale = (double)dh / (double)sh;
    int crop_w = (int)(dw / scale);
    int crop_h = (int)(dh / scale);
    if (crop_w > sw)
        crop_w = sw;
    if (crop_h > sh)
        crop_h = sh;
    int off_x = (sw - crop_w) / 2;
    int off_y = (sh - crop_h) / 2;

    for (int dy = 0; dy < dh; dy++) {
        int sy = off_y + (crop_h > 0 ? dy * crop_h / dh : 0);
        for (int dx = 0; dx < dw; dx++) {
            int sx = off_x + (crop_w > 0 ? dx * crop_w / dw : 0);
            size_t idx = (size_t)(sy * sw + sx) * 3;
            XPutPixel(xi, dx, dy, rgb(src[idx], src[idx + 1], src[idx + 2]));
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
    char path[512];
    snprintf(path, sizeof path, "%s/.config/backroot8/wallpaper.jpg", user_home_dir());
    if (access(path, R_OK) != 0)
        snprintf(path, sizeof path, "%s", WALLPAPER_DEFAULT);
    int iw = 0, ih = 0, comp = 0;
    unsigned char *data = stbi_load(path, &iw, &ih, &comp, 3);
    if (!data || iw <= 0 || ih <= 0)
        return;
    wallpaper_pm = scale_cover(data, iw, ih, win_w, win_h);
    stbi_image_free(data);
    wallpaper_ready = wallpaper_pm != 0;
}

static Pixmap scale_avatar(unsigned char *src, int sw, int sh, int sz) {
    int depth = DefaultDepth(dpy, screen);
    Pixmap pm = XCreatePixmap(dpy, win, sz, sz, depth);
    XImage *xi = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, sz, sz, 32, 0);
    if (!xi) {
        XFreePixmap(dpy, pm);
        return 0;
    }
    xi->data = calloc((size_t)xi->bytes_per_line * sz, 1);
    if (!xi->data) {
        XDestroyImage(xi);
        XFreePixmap(dpy, pm);
        return 0;
    }
    for (int dy = 0; dy < sz; dy++) {
        int sy = sh > 0 ? dy * sh / sz : 0;
        for (int dx = 0; dx < sz; dx++) {
            int sx = sw > 0 ? dx * sw / sz : 0;
            size_t idx = (size_t)(sy * sw + sx) * 3;
            XPutPixel(xi, dx, dy, rgb(src[idx], src[idx + 1], src[idx + 2]));
        }
    }
    GC pg = XCreateGC(dpy, pm, 0, NULL);
    XPutImage(dpy, pm, pg, xi, 0, 0, 0, 0, sz, sz);
    XFreeGC(dpy, pg);
    XDestroyImage(xi);
    return pm;
}

static void load_user_picture(void) {
    if (user_pic_pm) {
        XFreePixmap(dpy, user_pic_pm);
        user_pic_pm = 0;
    }
    user_pic_ready = 0;
    int iw = 0, ih = 0, comp = 0;
    unsigned char *data = stbi_load(USER_PIC, &iw, &ih, &comp, 3);
    if (!data || iw <= 0 || ih <= 0)
        return;
    user_pic_pm = scale_avatar(data, iw, ih, LOGIN_AVATAR);
    stbi_image_free(data);
    user_pic_ready = user_pic_pm != 0;
}

static void open_fonts(void) {
    static const char *const clock_names[] = {
        "Segoe UI Light-72:antialias=true",
        "Segoe UI-64:antialias=true",
        "sans-serif-64",
        NULL
    };
    static const char *const date_names[] = {
        "Segoe UI Light-22:antialias=true",
        "Segoe UI-20:antialias=true",
        "sans-serif-20",
        NULL
    };
    static const char *const name_names[] = {
        "Segoe UI-28:antialias=true",
        "sans-serif-28",
        NULL
    };
    static const char *const field_names[] = {
        "Segoe UI-14:antialias=true",
        "sans-serif-14",
        NULL
    };
    for (int i = 0; clock_names[i]; i++) {
        font_clock = XftFontOpenName(dpy, screen, clock_names[i]);
        if (font_clock && font_clock->ascent > 0)
            break;
        if (font_clock) {
            XftFontClose(dpy, font_clock);
            font_clock = NULL;
        }
    }
    for (int i = 0; date_names[i]; i++) {
        font_date = XftFontOpenName(dpy, screen, date_names[i]);
        if (font_date && font_date->ascent > 0)
            break;
        if (font_date) {
            XftFontClose(dpy, font_date);
            font_date = NULL;
        }
    }
    for (int i = 0; name_names[i]; i++) {
        font_name = XftFontOpenName(dpy, screen, name_names[i]);
        if (font_name && font_name->ascent > 0)
            break;
        if (font_name) {
            XftFontClose(dpy, font_name);
            font_name = NULL;
        }
    }
    for (int i = 0; field_names[i]; i++) {
        font_field = XftFontOpenName(dpy, screen, field_names[i]);
        if (font_field && font_field->ascent > 0)
            break;
        if (font_field) {
            XftFontClose(dpy, font_field);
            font_field = NULL;
        }
    }
}

static void draw_lock_clock(void) {
    time_t now = time(NULL);
    if (now == clock_last && !drag_active)
        return;
    clock_last = now;
    struct tm tm;
    localtime_r(&now, &tm);
    char time_buf[32];
    char date_buf[64];
    strftime(time_buf, sizeof time_buf, "%-I:%M", &tm);
    strftime(date_buf, sizeof date_buf, "%A, %B %-d", &tm);

    int tw = text_width(font_clock, time_buf);
    int dw = text_width(font_date, date_buf);
    int block_w = tw > dw ? tw : dw;
    int cx = win_w - CLOCK_PAD - block_w;
    int ty = win_h - CLOCK_PAD - (font_date ? font_date->height + 12 : 28);
    if (font_clock)
        ty -= font_clock->ascent + font_clock->descent;
    if (ty < CLOCK_PAD)
        ty = CLOCK_PAD;

    xft_draw(canvas, cx, ty + (font_clock ? font_clock->ascent : 48),
        time_buf, 255, 255, 255, font_clock);
    xft_draw(canvas, cx, win_h - CLOCK_PAD - (font_date ? font_date->descent : 8),
        date_buf, 255, 255, 255, font_date);
}

static void draw_lock_phase(void) {
    int off = slide_offset;
    if (wallpaper_ready)
        XCopyArea(dpy, wallpaper_pm, canvas, gc, 0, 0, win_w, win_h, 0, -off);
    else {
        XSetForeground(dpy, gc, rgb(30, 30, 40));
        XFillRectangle(dpy, canvas, gc, 0, -off, win_w, win_h);
    }
    draw_lock_clock();
    if (off > 0) {
        XSetForeground(dpy, gc, rgb(COL_BG_R, COL_BG_G, COL_BG_B));
        XFillRectangle(dpy, canvas, gc, 0, win_h - off, win_w, off);
    }
}

static void draw_arrow(int x, int y, int sz, int left) {
    int cx = x + sz / 2;
    int cy = y + sz / 2;
    int arm = sz / 4;
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    if (left) {
        XDrawLine(dpy, canvas, gc, cx + arm / 2, cy - arm, cx - arm / 2, cy);
        XDrawLine(dpy, canvas, gc, cx - arm / 2, cy, cx + arm / 2, cy + arm);
    } else {
        XDrawLine(dpy, canvas, gc, cx - arm / 2, cy - arm, cx + arm / 2, cy);
        XDrawLine(dpy, canvas, gc, cx + arm / 2, cy, cx - arm / 2, cy + arm);
    }
}

static void draw_login_field(int fx, int fy) {
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XFillRectangle(dpy, canvas, gc, fx, fy, FIELD_W, FIELD_H);

    char show[PASS_MAX + 4];
    size_t n = strlen(password);
    if (n >= sizeof show - 1)
        n = sizeof show - 2;
    memset(show, 0, sizeof show);
    for (size_t i = 0; i < n; i++)
        show[i] = '*';

    const char *draw_text = show;
    int tr = 40, tg = 40, tb = 50;
    if (!show[0]) {
        draw_text = "Password";
        tr = 140;
        tg = 140;
        tb = 150;
    }
    int text_x = fx + 12;
    if (draw_text[0])
        xft_draw(canvas, text_x, fy + text_baseline(FIELD_H, font_field),
            draw_text, tr, tg, tb, font_field);

    if (caret_on) {
        int caret_x = text_x;
        if (show[0])
            caret_x += text_width(font_field, show);
        XSetForeground(dpy, gc, rgb(40, 40, 50));
        XDrawLine(dpy, canvas, gc, caret_x, fy + 8, caret_x, fy + FIELD_H - 8);
    }

    int sx = fx + FIELD_W + 8;
    int sy = fy;
    int bg_r = submit_hover ? 255 : COL_ACCENT_R;
    int bg_g = submit_hover ? 255 : COL_ACCENT_G;
    int bg_b = submit_hover ? 255 : COL_ACCENT_B;
    XSetForeground(dpy, gc, rgb(bg_r, bg_g, bg_b));
    XFillRectangle(dpy, canvas, gc, sx, sy, SUBMIT_SZ, SUBMIT_SZ);
    draw_arrow(sx, sy, SUBMIT_SZ, 0);
}

static void login_layout(int *bx, int *by, int *ax, int *ay, int *nx, int *ny, int *fx, int *fy) {
    int cx = win_w / 2;
    *ax = cx - LOGIN_AVATAR / 2 - 80;
    *ay = win_h / 2 - LOGIN_AVATAR / 2;
    *nx = *ax + LOGIN_AVATAR + 24;
    *ny = *ay + LOGIN_AVATAR / 2 - (font_name ? font_name->ascent : 14);
    *fx = *nx;
    *fy = *ay + LOGIN_AVATAR / 2 + 16;
    *bx = 48;
    *by = win_h / 2 - BACK_BTN / 2;
}

static void draw_login_phase(void) {
    XSetForeground(dpy, gc, rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    XFillRectangle(dpy, canvas, gc, 0, 0, win_w, win_h);

    int bx, by, ax, ay, nx, ny, fx, fy;
    login_layout(&bx, &by, &ax, &ay, &nx, &ny, &fx, &fy);

    int ring = back_hover ? 255 : 200;
    XSetForeground(dpy, gc, rgb(ring, ring, ring));
    XDrawArc(dpy, canvas, gc, bx, by, BACK_BTN, BACK_BTN, 0, 360 * 64);
    draw_arrow(bx, by, BACK_BTN, 1);

    if (user_pic_ready)
        XCopyArea(dpy, user_pic_pm, canvas, gc, 0, 0, LOGIN_AVATAR, LOGIN_AVATAR, ax, ay);
    else {
        XSetForeground(dpy, gc, rgb(70, 110, 170));
        XFillRectangle(dpy, canvas, gc, ax, ay, LOGIN_AVATAR, LOGIN_AVATAR);
        char letter[2] = { user_display_name[0] ? user_display_name[0] : '?', 0 };
        xft_draw(canvas, ax + LOGIN_AVATAR / 2 - 8, ay + LOGIN_AVATAR / 2 + 8,
            letter, 255, 255, 255, font_name);
    }

    xft_draw(canvas, nx, ny + (font_name ? font_name->ascent : 20),
        user_display_name, 255, 255, 255, font_name);
    draw_login_field(fx, fy);

    if (login_err && (now_ms() - login_err_ms) < 3000.0) {
        xft_draw(canvas, fx, fy + FIELD_H + 12,
            "Incorrect password. Try again.", 255, 180, 180, font_field);
    }
}

static void redraw(void) {
    if (phase == PHASE_LOCK)
        draw_lock_phase();
    else
        draw_login_phase();
    XCopyArea(dpy, canvas, win, gc, 0, 0, win_w, win_h, 0, 0);
}

static int verify_password(void) {
    if (access(CHKPASS, X_OK) != 0)
        return 0;
    pid_t pid = fork();
    if (pid < 0)
        return 0;
    if (pid == 0) {
        execl(CHKPASS, CHKPASS, user_name, password, (char *)NULL);
        _exit(1);
    }
    int st = 1;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static void unlock_success(void) {
    mkdir("/run/br8-lock", 0755);
    FILE *fp = fopen(SHOW_START_FLAG, "w");
    if (fp)
        fclose(fp);
    XUngrabKeyboard(dpy, CurrentTime);
    XUnmapWindow(dpy, win);
    XFlush(dpy);
    exit(0);
}

static void try_login(void) {
    if (!password[0])
        return;
    if (verify_password())
        unlock_success();
    login_err = 1;
    login_err_ms = now_ms();
    password[0] = '\0';
    redraw();
}

static void reveal_login(void) {
    phase = PHASE_LOGIN;
    slide_offset = 0;
    drag_active = 0;
    password[0] = '\0';
    login_err = 0;
    redraw();
}

static void back_to_lock(void) {
    phase = PHASE_LOCK;
    slide_offset = 0;
    password[0] = '\0';
    login_err = 0;
    redraw();
}

static int login_back_hit(int x, int y) {
    int bx, by, ax, ay, nx, ny, fx, fy;
    (void)ax;
    (void)ay;
    (void)nx;
    (void)ny;
    (void)fx;
    (void)fy;
    login_layout(&bx, &by, &ax, &ay, &nx, &ny, &fx, &fy);
    return x >= bx && x < bx + BACK_BTN && y >= by && y < by + BACK_BTN;
}

static int login_submit_hit(int x, int y) {
    int bx, by, ax, ay, nx, ny, fx, fy;
    (void)bx;
    (void)by;
    (void)ax;
    (void)ay;
    (void)nx;
    (void)ny;
    login_layout(&bx, &by, &ax, &ay, &nx, &ny, &fx, &fy);
    int sx = fx + FIELD_W + 8;
    return x >= sx && x < sx + SUBMIT_SZ && y >= fy && y < fy + SUBMIT_SZ;
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-lock: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    XSetErrorHandler(NULL);

    init_user_name();
    open_fonts();

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    win_w = ra.width;
    win_h = ra.height;

    win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0, 0, BlackPixel(dpy, screen));
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
        ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    XChangeWindowAttributes(dpy, win, CWOverrideRedirect | CWEventMask, &attr);
    gc = XCreateGC(dpy, win, 0, NULL);
    canvas = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, screen));

    load_wallpaper();
    load_user_picture();
    XMapRaised(dpy, win);
    XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    caret_ms = now_ms();
    redraw();

    int xfd = ConnectionNumber(dpy);
    while (1) {
        double t = now_ms();
        if ((int)((t - caret_ms) / CARET_BLINK_MS) % 2 == 0)
            caret_on = 1;
        else
            caret_on = 0;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 80000 };
        select(xfd + 1, &fds, NULL, NULL, &tv);

        if (phase == PHASE_LOGIN || phase == PHASE_LOCK)
            redraw();

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == ConfigureNotify && ev.xconfigure.window == root) {
                XWindowAttributes na;
                XGetWindowAttributes(dpy, root, &na);
                if (na.width != win_w || na.height != win_h) {
                    win_w = na.width;
                    win_h = na.height;
                    XMoveResizeWindow(dpy, win, 0, 0, win_w, win_h);
                    XFreePixmap(dpy, canvas);
                    canvas = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, screen));
                    load_wallpaper();
                    redraw();
                }
            } else if (ev.type == ButtonPress && ev.xbutton.window == win) {
                if (phase == PHASE_LOCK) {
                    drag_active = 1;
                    drag_start_y = ev.xbutton.y;
                } else if (phase == PHASE_LOGIN) {
                    if (login_back_hit(ev.xbutton.x, ev.xbutton.y))
                        back_to_lock();
                    else if (login_submit_hit(ev.xbutton.x, ev.xbutton.y))
                        try_login();
                }
            } else if (ev.type == MotionNotify && ev.xmotion.window == win && phase == PHASE_LOCK) {
                if (drag_active) {
                    int dy = drag_start_y - ev.xmotion.y;
                    if (dy < 0)
                        dy = 0;
                    if (dy > win_h)
                        dy = win_h;
                    slide_offset = dy;
                    if (dy >= DRAG_UNLOCK_PX)
                        reveal_login();
                }
            } else if (ev.type == ButtonRelease && ev.xbutton.window == win) {
                if (phase == PHASE_LOCK && drag_active) {
                    if (slide_offset >= DRAG_UNLOCK_PX)
                        reveal_login();
                    else {
                        slide_offset = 0;
                        redraw();
                    }
                    drag_active = 0;
                }
            } else if (ev.type == KeyPress && ev.xkey.window == win && phase == PHASE_LOGIN) {
                char buf[32];
                KeySym sym;
                int n = XLookupString(&ev.xkey, buf, sizeof buf, &sym, NULL);
                if (sym == XK_Return || sym == XK_KP_Enter)
                    try_login();
                else if (sym == XK_BackSpace) {
                    size_t len = strlen(password);
                    if (len > 0)
                        password[len - 1] = '\0';
                } else if (sym == XK_Escape)
                    back_to_lock();
                else if (n == 1 && isprint((unsigned char)buf[0])) {
                    size_t len = strlen(password);
                    if (len + 1 < sizeof password) {
                        password[len] = buf[0];
                        password[len + 1] = '\0';
                    }
                }
            }
        }
    }
    return 0;
}
