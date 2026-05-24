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

#define COL_BG_R 70
#define COL_BG_G 29
#define COL_BG_B 96
#define COL_ACCENT_R 112
#define COL_ACCENT_G 48
#define COL_ACCENT_B 160
#define COL_FIELD_R 45
#define COL_FIELD_G 62
#define COL_FIELD_B 95
#define COL_TEXT_R 255
#define COL_TEXT_G 255
#define COL_TEXT_B 255
#define COL_MUTED_R 200
#define COL_MUTED_G 190
#define COL_MUTED_B 220

#define PAD 80
#define LABEL_W 200
#define FIELD_W 400
#define FIELD_H 40
#define ROW_GAP 28
#define BTN_W 140
#define BTN_H 44
#define USER_MAX 32
#define PASS_MAX 64

#define SETUP_SCRIPT "/usr/lib/backroot8/br8-oobe-setup.sh"
#define FINISH_LOGIN_SCRIPT "/usr/lib/backroot8/br8-oobe-finish-login.sh"

typedef enum {
    PHASE_ACCOUNT,
    PHASE_LOADING
} Phase;

typedef enum {
    FOCUS_USER,
    FOCUS_PASS,
    FOCUS_PASS2,
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
static char password2[PASS_MAX];
static int finish_hover;
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

static void draw_field_box(int fx, int fy, const char *value, const char *placeholder,
        int masked, int active) {
    int border_r = active ? COL_ACCENT_R : 200;
    int border_g = active ? COL_ACCENT_G : 200;
    int border_b = active ? COL_ACCENT_B : 210;
    XSetForeground(dpy, gc, rgb(border_r, border_g, border_b));
    XDrawRectangle(dpy, win, gc, fx, fy, FIELD_W, FIELD_H);
    XSetForeground(dpy, gc, rgb(COL_FIELD_R, COL_FIELD_G, COL_FIELD_B));
    XFillRectangle(dpy, win, gc, fx + 1, fy + 1, FIELD_W - 2, FIELD_H - 2);

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

    const char *draw_text = show;
    int tr = COL_TEXT_R, tg = COL_TEXT_G, tb = COL_TEXT_B;
    if (!show[0] && placeholder) {
        draw_text = placeholder;
        tr = 120;
        tg = 130;
        tb = 150;
    }
    if (draw_text[0])
        xft_draw(win, fx + 12, fy + text_baseline(FIELD_H, font_field),
            draw_text, tr, tg, tb, font_field);
}

static void draw_field_row(int row_y, const char *label, const char *value,
        const char *placeholder, int masked, int active) {
    int field_x = PAD + LABEL_W;
    int label_y = row_y + text_baseline(FIELD_H, font_label);
    xft_draw(win, PAD, label_y, label, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_label);
    draw_field_box(field_x, row_y, value, placeholder, masked, active);
}

static void account_layout(int *title_y, int *row_user, int *row_pass, int *row_pass2,
        int *btn_x, int *btn_y) {
    int header_h = font_header ? font_header->height : 48;
    int sub_h = font_sub ? font_sub->height : 22;
    *title_y = PAD + header_h;
    int form_top = *title_y + sub_h + 48;
    *row_user = form_top;
    *row_pass = form_top + FIELD_H + ROW_GAP;
    *row_pass2 = *row_pass + FIELD_H + ROW_GAP;
    *btn_x = win_w - PAD - BTN_W;
    *btn_y = win_h - PAD - BTN_H;
}

static void draw_account(void) {
    const char *title = "Create a computer account";
    const char *subtitle =
        "If you want a password, choose something that will be easy for you to "
        "remember but hard for others to guess.";

    int title_y, row_user, row_pass, row_pass2, btn_x, btn_y;
    account_layout(&title_y, &row_user, &row_pass, &row_pass2, &btn_x, &btn_y);

    int header_h = font_header ? font_header->height : 48;
    xft_draw(win, PAD, PAD + text_baseline(header_h, font_header),
        title, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);

    int sub_y = PAD + header_h + 20;
    xft_draw(win, PAD, sub_y + text_baseline(font_sub ? font_sub->height : 22, font_sub),
        subtitle, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_sub);

    draw_field_row(row_user, "User name", username, "Example: John", 0, focus == FOCUS_USER);
    draw_field_row(row_pass, "Password", password, NULL, 1, focus == FOCUS_PASS);
    draw_field_row(row_pass2, "Reenter password", password2, NULL, 1, focus == FOCUS_PASS2);

    int bg_r = finish_hover ? 255 : COL_ACCENT_R;
    int bg_g = finish_hover ? 255 : COL_ACCENT_G;
    int bg_b = finish_hover ? 255 : COL_ACCENT_B;
    int fg_r = finish_hover ? COL_BG_R : 255;
    int fg_g = finish_hover ? COL_BG_G : 255;
    int fg_b = finish_hover ? COL_BG_B : 255;
    XSetForeground(dpy, gc, rgb(bg_r, bg_g, bg_b));
    XFillRectangle(dpy, win, gc, btn_x, btn_y, BTN_W, BTN_H);
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XDrawRectangle(dpy, win, gc, btn_x, btn_y, BTN_W - 1, BTN_H - 1);
    const char *bl = "Finish";
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

static int passwords_ok(void) {
    if (strcmp(password, password2) != 0)
        return 0;
    if (!password[0])
        return 1;
    return strlen(password) >= 4;
}

static void start_setup(void) {
    if (!valid_username(username) || !passwords_ok())
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
    if (focus == FOCUS_PASS2)
        return password2;
    return NULL;
}

static size_t active_max(void) {
    return focus == FOCUS_USER ? USER_MAX - 1 : PASS_MAX - 1;
}

static void cycle_focus(int backward) {
    if (backward) {
        if (focus == FOCUS_PASS2)
            focus = FOCUS_PASS;
        else if (focus == FOCUS_PASS)
            focus = FOCUS_USER;
        else
            focus = FOCUS_PASS2;
    } else {
        if (focus == FOCUS_USER)
            focus = FOCUS_PASS;
        else if (focus == FOCUS_PASS)
            focus = FOCUS_PASS2;
        else
            focus = FOCUS_USER;
    }
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
    } else if (sym == XK_Tab) {
        cycle_focus(kev->state & ShiftMask);
    } else if (sym == XK_ISO_Left_Tab) {
        cycle_focus(1);
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
    int title_y, row_user, row_pass, row_pass2, btn_x, btn_y;
    account_layout(&title_y, &row_user, &row_pass, &row_pass2, &btn_x, &btn_y);
    int field_x = PAD + LABEL_W;

    if (point_in_rect(x, y, field_x, row_user, FIELD_W, FIELD_H))
        focus = FOCUS_USER;
    else if (point_in_rect(x, y, field_x, row_pass, FIELD_W, FIELD_H))
        focus = FOCUS_PASS;
    else if (point_in_rect(x, y, field_x, row_pass2, FIELD_W, FIELD_H))
        focus = FOCUS_PASS2;
    else if (point_in_rect(x, y, btn_x, btn_y, BTN_W, BTN_H))
        start_setup();
    draw_all();
}

static void update_hover(int x, int y) {
    if (phase != PHASE_ACCOUNT)
        return;
    int title_y, row_user, row_pass, row_pass2, btn_x, btn_y;
    account_layout(&title_y, &row_user, &row_pass, &row_pass2, &btn_x, &btn_y);
    (void)title_y;
    (void)row_user;
    (void)row_pass;
    (void)row_pass2;
    int h = point_in_rect(x, y, btn_x, btn_y, BTN_W, BTN_H);
    if (h != finish_hover) {
        finish_hover = h;
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

static void finish_login(void) {
    if (access(FINISH_LOGIN_SCRIPT, X_OK) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            execl(FINISH_LOGIN_SCRIPT, FINISH_LOGIN_SCRIPT, (char *)NULL);
            _exit(127);
        }
    }
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
                finish_login();
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
