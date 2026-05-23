/*
 * Backroot 8 OOBE — fullscreen metro first-run setup.
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
#include <sys/select.h>
#include <sys/wait.h>
#include <ctype.h>
#include <sys/stat.h>

#include "../../include/br8-metro.h"

#define COL_BG_R 30
#define COL_BG_G 46
#define COL_BG_B 76
#define COL_ACCENT_R 0
#define COL_ACCENT_G 120
#define COL_ACCENT_B 212
#define COL_FIELD_R 45
#define COL_FIELD_G 62
#define COL_FIELD_B 95
#define COL_TEXT_R 255
#define COL_TEXT_G 255
#define COL_TEXT_B 255
#define COL_MUTED_R 180
#define COL_MUTED_G 200
#define COL_MUTED_B 230

#define PAD 64
#define FIELD_W 420
#define FIELD_H 44
#define FIELD_GAP 20
#define BTN_W 200
#define BTN_H 48
#define USER_MAX 32
#define PASS_MAX 64

#define SETUP_SCRIPT "/usr/lib/backroot8/br8-oobe-setup.sh"

typedef enum {
    PHASE_ACCOUNT,
    PHASE_LOADING
} Phase;

typedef enum {
    FOCUS_USER,
    FOCUS_PASS,
    FOCUS_NONE
} FocusField;

static Display *dpy;
static int screen;
static Window root, win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static int win_w, win_h;
static XftFont *font_header, *font_sub, *font_label, *font_field, *font_btn;
static Phase phase = PHASE_ACCOUNT;
static FocusField focus = FOCUS_USER;
static char username[USER_MAX];
static char password[PASS_MAX];
static int continue_hover;
static int setup_pid;
static double loading_anim;

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
        return height - 12;
    return (height + font->ascent - font->descent) / 2;
}

static void xft_draw(Drawable draw, int x, int y, const char *text, int r, int g, int b,
        XftFont *font) {
    if (!font || !text)
        return;
    XftDraw *xd = XftDrawCreate(dpy, draw, visual, cmap);
    if (!xd)
        return;
    XftColor col;
    XRenderColor rc = render_rgb(r, g, b);
    if (XftColorAllocValue(dpy, visual, cmap, &rc, &col)) {
        XftDrawStringUtf8(xd, &col, font, x, y, (FcChar8 *)text, (int)strlen(text));
        XftColorFree(dpy, visual, cmap, &col);
    }
    XftDrawDestroy(xd);
}

static int text_width(XftFont *font, const char *text) {
    XGlyphInfo ext;
    if (!font || !text || !text[0])
        return 0;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)text, (int)strlen(text), &ext);
    return ext.xOff;
}

static XftFont *open_font(const char *const *names) {
    for (int i = 0; names[i]; i++) {
        XftFont *f = XftFontOpenName(dpy, screen, names[i]);
        if (f && f->ascent > 0)
            return f;
        if (f)
            XftFontClose(dpy, f);
    }
    return NULL;
}

static void open_fonts(void) {
    static const char *const header_names[] = {
        "Segoe UI Light-42:antialias=true",
        "Segoe UI-36:antialias=true",
        "DejaVu Sans-36",
        NULL
    };
    static const char *const sub_names[] = {
        "Segoe UI-20:antialias=true",
        "Segoe UI-18:antialias=true",
        "DejaVu Sans-18",
        NULL
    };
    static const char *const label_names[] = {
        "Segoe UI-14:antialias=true",
        "DejaVu Sans-14",
        NULL
    };
    static const char *const field_names[] = {
        "Segoe UI-16:antialias=true",
        "DejaVu Sans-16",
        NULL
    };
    static const char *const btn_names[] = {
        "Segoe UI Semibold-16:antialias=true",
        "Segoe UI-16:antialias=true",
        NULL
    };
    font_header = open_font(header_names);
    font_sub = open_font(sub_names);
    font_label = open_font(label_names);
    font_field = open_font(field_names);
    font_btn = open_font(btn_names);
}

static int oobe_pending(void) {
    return access("/etc/backroot8/oobe-pending", F_OK) == 0;
}

static int is_live_boot(void) {
    FILE *fp = fopen("/proc/cmdline", "r");
    char buf[4096];
    if (!fp)
        return 0;
    if (!fgets(buf, sizeof buf, fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return strstr(buf, "backroot8iso") != NULL;
}

static void draw_field(int x, int y, const char *label, const char *value, int masked,
        int active) {
    xft_draw(win, x, y - 8, label, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_label);
    y += 18;
    int border = active ? COL_ACCENT_R : 255;
    int border_g = active ? COL_ACCENT_G : 255;
    int border_b = active ? COL_ACCENT_B : 255;
    XSetForeground(dpy, gc, rgb(border, border_g, border_b));
    XDrawRectangle(dpy, win, gc, x, y, FIELD_W, FIELD_H);
    XSetForeground(dpy, gc, rgb(COL_FIELD_R, COL_FIELD_G, COL_FIELD_B));
    XFillRectangle(dpy, win, gc, x + 1, y + 1, FIELD_W - 2, FIELD_H - 2);

    char show[PASS_MAX + 4];
    if (masked) {
        size_t n = strlen(value);
        if (n >= sizeof show - 1)
            n = sizeof show - 2;
        memset(show, '*', n);
        show[n] = '\0';
    } else {
        snprintf(show, sizeof show, "%s", value);
    }
    if (!show[0] && active) {
        const char *hint = masked ? "Password" : "Username";
        xft_draw(win, x + 14, y + text_baseline(FIELD_H, font_field),
            hint, 120, 130, 150, font_field);
    } else {
        xft_draw(win, x + 14, y + text_baseline(FIELD_H, font_field),
            show, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_field);
    }
}

static void account_layout(int *fx, int *fy_user, int *fy_pass, int *btn_x, int *btn_y) {
    *fx = (win_w - FIELD_W) / 2;
    int header_h = font_header ? font_header->height : 48;
    int start_y = win_h / 4;
    *fy_user = start_y + header_h + 48;
    *fy_pass = *fy_user + FIELD_H + FIELD_GAP + 28;
    *btn_x = (win_w - BTN_W) / 2;
    *btn_y = *fy_pass + FIELD_H + 48;
}

static void draw_account(void) {
    const char *title = "Set up a computer account.";
    int tw = text_width(font_header, title);
    int y = win_h / 4;
    xft_draw(win, (win_w - tw) / 2, y + text_baseline(font_header ? font_header->height : 48, font_header),
        title, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);

    int fx, fy_user, fy_pass, btn_x, btn_y;
    account_layout(&fx, &fy_user, &fy_pass, &btn_x, &btn_y);
    draw_field(fx, fy_user, "Username", username, 0, focus == FOCUS_USER);
    draw_field(fx, fy_pass, "Password", password, 1, focus == FOCUS_PASS);

    int bg_r = continue_hover ? 255 : COL_ACCENT_R;
    int bg_g = continue_hover ? 255 : COL_ACCENT_G;
    int bg_b = continue_hover ? 255 : COL_ACCENT_B;
    int fg_r = continue_hover ? COL_BG_R : 255;
    int fg_g = continue_hover ? COL_BG_G : 255;
    int fg_b = continue_hover ? COL_BG_B : 255;
    XSetForeground(dpy, gc, rgb(bg_r, bg_g, bg_b));
    XFillRectangle(dpy, win, gc, btn_x, btn_y, BTN_W, BTN_H);
    const char *bl = "Continue";
    int blw = text_width(font_btn, bl);
    xft_draw(win, btn_x + (BTN_W - blw) / 2, btn_y + text_baseline(BTN_H, font_btn),
        bl, fg_r, fg_g, fg_b, font_btn);
}

static void draw_loading(void) {
    const char *main_text = "We are setting your computer up.";
    const char *sub_text = "This can take a while... But it will be worth it!";

    int mw = text_width(font_header, main_text);
    int sw = text_width(font_sub, sub_text);
    int cy = win_h / 2;

    xft_draw(win, (win_w - mw) / 2,
        cy + text_baseline(font_header ? font_header->height : 42, font_header),
        main_text, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);

    xft_draw(win, (win_w - sw) / 2,
        cy + (font_header ? font_header->height : 42) + 36,
        sub_text, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_sub);

    int dots = ((int)(loading_anim * 2.0)) % 4;
    char dotbuf[8] = "";
    for (int i = 0; i < dots; i++)
        strcat(dotbuf, ".");
    int dw = text_width(font_sub, dotbuf);
    xft_draw(win, (win_w - dw) / 2,
        cy + (font_header ? font_header->height : 42) + 72,
        dotbuf, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_sub);
}

static void draw_all(void) {
    XSetForeground(dpy, gc, rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);
    if (phase == PHASE_ACCOUNT)
        draw_account();
    else
        draw_loading();
}

static int valid_username(const char *s) {
    if (!s || !s[0] || strlen(s) < 2)
        return 0;
    if (!isalpha((unsigned char)s[0]))
        return 0;
    for (const char *p = s; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
            return 0;
    }
    return 1;
}

static void start_setup(void) {
    if (!valid_username(username) || strlen(password) < 4)
        return;

    phase = PHASE_LOADING;
    draw_all();

    char user_arg[USER_MAX + 16];
    char pass_file[128];
    snprintf(user_arg, sizeof user_arg, "BR8_USER=%s", username);
    snprintf(pass_file, sizeof pass_file, "/run/br8-oobe/pass");

    mkdir("/run/br8-oobe", 0700);
    FILE *fp = fopen(pass_file, "w");
    if (fp) {
        fputs(password, fp);
        fputc('\n', fp);
        fclose(fp);
        chmod(pass_file, 0600);
    }

    pid_t pid = fork();
    if (pid == 0) {
        putenv(user_arg);
        execl(SETUP_SCRIPT, SETUP_SCRIPT, username, pass_file, (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        setup_pid = (int)pid;
}

static char *active_field(void) {
    if (focus == FOCUS_USER)
        return username;
    if (focus == FOCUS_PASS)
        return password;
    return NULL;
}

static size_t active_max(void) {
    return focus == FOCUS_USER ? USER_MAX - 1 : PASS_MAX - 1;
}

static void handle_key(XKeyEvent *kev) {
    if (phase != PHASE_ACCOUNT)
        return;
    char *field = active_field();
    if (!field)
        return;

    size_t len = strlen(field);
    KeySym sym = XLookupKeysym(kev, 0);
    if (sym == XK_BackSpace || sym == XK_Delete) {
        if (len > 0)
            field[len - 1] = '\0';
    } else if (sym == XK_Tab || sym == XK_ISO_Left_Tab) {
        focus = (focus == FOCUS_USER) ? FOCUS_PASS : FOCUS_USER;
    } else if (sym == XK_Return || sym == XK_KP_Enter) {
        start_setup();
        return;
    } else {
        char buf[8];
        int n = XLookupString(kev, buf, sizeof buf, NULL, NULL);
        for (int i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (c < 32 || c == 127)
                continue;
            if (len < active_max()) {
                field[len++] = (char)c;
                field[len] = '\0';
            }
        }
    }
    draw_all();
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void handle_click(int x, int y) {
    if (phase != PHASE_ACCOUNT)
        return;
    int fx, fy_user, fy_pass, btn_x, btn_y;
    account_layout(&fx, &fy_user, &fy_pass, &btn_x, &btn_y);
    int field_y_user = fy_user + 18;
    int field_y_pass = fy_pass + 18;

    if (point_in_rect(x, y, fx, field_y_user, FIELD_W, FIELD_H))
        focus = FOCUS_USER;
    else if (point_in_rect(x, y, fx, field_y_pass, FIELD_W, FIELD_H))
        focus = FOCUS_PASS;
    else if (point_in_rect(x, y, btn_x, btn_y, BTN_W, BTN_H))
        start_setup();
    draw_all();
}

static void update_hover(int x, int y) {
    if (phase != PHASE_ACCOUNT)
        return;
    int fx, fy_user, fy_pass, btn_x, btn_y;
    account_layout(&fx, &fy_user, &fy_pass, &btn_x, &btn_y);
    (void)fx;
    (void)fy_user;
    (void)fy_pass;
    int h = point_in_rect(x, y, btn_x, btn_y, BTN_W, BTN_H);
    if (h != continue_hover) {
        continue_hover = h;
        draw_all();
    }
}

static void resize_window(int w, int h) {
    if (w < 640)
        w = 640;
    if (h < 480)
        h = 480;
    win_w = w;
    win_h = h;
    draw_all();
}

int main(void) {
    if (is_live_boot() || !oobe_pending())
        return 0;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-oobe: cannot open display\n");
        return 1;
    }

    XSetErrorHandler(NULL);
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    open_fonts();

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    win_w = ra.width > 0 ? ra.width : 1024;
    win_h = ra.height > 0 ? ra.height : 768;

    win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0,
        BlackPixel(dpy, screen), rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    br8_set_metro(dpy, win);
    XStoreName(dpy, win, "Backroot 8 Setup");

    gc = XCreateGC(dpy, win, 0, NULL);
    XSelectInput(dpy, win,
        ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask |
        PointerMotionMask);
    XMapRaised(dpy, win);
    draw_all();

    int xfd = ConnectionNumber(dpy);
    int running = 1;
    while (running) {
        struct timeval tv = { .tv_sec = 0, .tv_usec = phase == PHASE_LOADING ? 120000 : 200000 };
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        select(xfd + 1, &fds, NULL, NULL, &tv);

        if (phase == PHASE_LOADING) {
            loading_anim += 0.15;
            draw_all();
        }

        if (setup_pid > 0) {
            int st;
            if (waitpid(setup_pid, &st, WNOHANG) > 0) {
                setup_pid = 0;
                unlink("/run/br8-oobe/pass");
                running = 0;
            }
        }

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0)
                draw_all();
            else if (ev.type == ConfigureNotify)
                resize_window(ev.xconfigure.width, ev.xconfigure.height);
            else if (ev.type == KeyPress)
                handle_key(&ev.xkey);
            else if (ev.type == ButtonPress)
                handle_click((int)ev.xbutton.x, (int)ev.xbutton.y);
            else if (ev.type == MotionNotify)
                update_hover((int)ev.xmotion.x, (int)ev.xmotion.y);
        }
    }

    return 0;
}
