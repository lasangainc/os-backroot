/*
 * Backroot 8 install wizard — metro-styled window on live USB/ISO boot.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

#define WIN_W 920
#define BANNER_PATH "/usr/share/backroot8/install-banner.png"
#define DISK_LIST_SCRIPT "/usr/lib/backroot8/br8-list-install-disks.sh"
#define WIN_H 640
#define PAD 28
#define BANNER_H 220
#define BTN_H 44
#define BTN_W 160
#define ROW_H 44
#define MAX_DISKS 24
#define TOS_LINES 8

#define COL_BG_R 0
#define COL_BG_G 120
#define COL_BG_B 212
#define COL_TILE_R 0
#define COL_TILE_G 90
#define COL_TILE_B 158
#define COL_ACCENT_R 255
#define COL_ACCENT_G 255
#define COL_ACCENT_B 255
#define COL_WARN_R 255
#define COL_WARN_G 200
#define COL_WARN_B 80
#define COL_TEXT_R 255
#define COL_TEXT_G 255
#define COL_TEXT_B 255
#define COL_MUTED_R 210
#define COL_MUTED_G 230
#define COL_MUTED_B 255

#define STATUS_PATH "/run/br8-install/status"
#define LOG_PATH "/run/br8-install/install.log"
#define INSTALL_SCRIPT "/usr/lib/backroot8/br8-install-to-disk.sh"
#define LOG_BTN_W 148
#define LOG_WIN_W 740
#define LOG_WIN_H 520
#define LOG_PAD 14
#define LOG_CLOSE_W 100
#define LOG_CLOSE_H 36
#define LOG_MAX_LINES 256
#define LOG_COLS 88

typedef enum {
    STEP_WELCOME,
    STEP_SETUP,
    STEP_INSTALLING,
    STEP_ALL_DONE
} Step;

typedef struct {
    char dev[64];
    char size[32];
    char model[128];
    char vendor[64];
    char line[384];
} DiskEntry;

static Display *dpy;
static int screen;
static Window root, win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static XftFont *font_title, *font_head, *font_body, *font_btn, *font_log;
static Window log_win;
static GC log_gc;
static int log_visible;
static int log_scroll;
static int log_follow_tail = 1;
static int log_btn_hover, log_close_hover;
static char log_lines[LOG_MAX_LINES][LOG_COLS];
static int n_log_lines;
static Step step = STEP_WELCOME;
static DiskEntry disks[MAX_DISKS];
static int n_disks;
static int disk_sel;
static int tos_scroll;
static int start_hover, install_hover, back_hover;
static int install_pid;
static int win_x, win_y;
static Pixmap banner_pm;
static int banner_w, banner_h;
static int banner_ready;
static time_t shutdown_at;

static const char *tos_placeholder =
    "Backroot 8 — Terms of Service (placeholder)\n\n"
    "This is placeholder legal text for the preview installer. "
    "By continuing you agree that this software is provided as-is "
    "without warranty. A full license will ship in a later release.\n\n"
    "Thank you for trying Backroot 8.";

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
    if (!font || !text || !text[0])
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
    static const char *const title_names[] = {
        "Segoe UI Light-36:antialias=true",
        "Segoe UI-32:antialias=true",
        "DejaVu Sans-32",
        NULL
    };
    static const char *const head_names[] = {
        "Segoe UI Semibold-18:antialias=true",
        "Segoe UI-18:antialias=true",
        "DejaVu Sans-18",
        NULL
    };
    static const char *const body_names[] = {
        "Segoe UI-12:antialias=true",
        "DejaVu Sans-12",
        NULL
    };
    static const char *const btn_names[] = {
        "Segoe UI Semibold-14:antialias=true",
        "Segoe UI-14:antialias=true",
        "DejaVu Sans-14",
        NULL
    };
    font_title = open_font(title_names);
    font_head = open_font(head_names);
    font_body = open_font(body_names);
    font_btn = open_font(btn_names);
    static const char *const log_names[] = {
        "DejaVu Sans Mono-11:antialias=true",
        "Consolas-11:antialias=true",
        "DejaVu Sans-11",
        NULL
    };
    font_log = open_font(log_names);
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

static void load_disks(void) {
    FILE *fp = popen(DISK_LIST_SCRIPT " 2>/dev/null", "r");
    char row[512];
    n_disks = 0;
    disk_sel = 0;

    if (!fp)
        return;
    while (fgets(row, sizeof row, fp) && n_disks < MAX_DISKS) {
        char *dev = row;
        char *size = strchr(row, '\t');
        if (!size)
            continue;
        *size++ = '\0';
        char *model = strchr(size, '\t');
        if (!model)
            continue;
        *model++ = '\0';
        char *vendor = strchr(model, '\t');
        if (vendor) {
            *vendor++ = '\0';
            vendor[strcspn(vendor, "\r\n")] = '\0';
        }
        size[strcspn(size, "\r\n")] = '\0';
        model[strcspn(model, "\r\n")] = '\0';
        dev[strcspn(dev, "\r\n")] = '\0';

        snprintf(disks[n_disks].dev, sizeof disks[n_disks].dev, "%s", dev);
        snprintf(disks[n_disks].size, sizeof disks[n_disks].size, "%s", size);
        snprintf(disks[n_disks].model, sizeof disks[n_disks].model, "%s", model);
        if (vendor)
            snprintf(disks[n_disks].vendor, sizeof disks[n_disks].vendor, "%s", vendor);
        else
            disks[n_disks].vendor[0] = '\0';

        if (disks[n_disks].vendor[0])
            snprintf(disks[n_disks].line, sizeof disks[n_disks].line,
                "%s %s — %s — %s", disks[n_disks].vendor, disks[n_disks].model,
                disks[n_disks].dev, disks[n_disks].size);
        else
            snprintf(disks[n_disks].line, sizeof disks[n_disks].line,
                "%s — %s — %s", disks[n_disks].model, disks[n_disks].dev, disks[n_disks].size);
        n_disks++;
    }
    pclose(fp);
    if (n_disks == 0) {
        snprintf(disks[0].dev, sizeof disks[0].dev, "/dev/sda");
        snprintf(disks[0].size, sizeof disks[0].size, "unknown");
        snprintf(disks[0].model, sizeof disks[0].model, "No disks detected");
        snprintf(disks[0].line, sizeof disks[0].line, "No disks detected — /dev/sda");
        n_disks = 1;
    }
}

static void load_banner(void) {
    int iw, ih, comp;
    unsigned char *img;
    int max_w = WIN_W - PAD * 2;
    int max_h = BANNER_H;

    if (banner_ready)
        return;
    img = stbi_load(BANNER_PATH, &iw, &ih, &comp, 4);
    if (!img || iw <= 0 || ih <= 0)
        return;

    double scale = (double)max_w / (double)iw;
    if ((double)max_h / (double)ih < scale)
        scale = (double)max_h / (double)ih;
    banner_w = (int)(iw * scale);
    banner_h = (int)(ih * scale);
    if (banner_w < 1)
        banner_w = 1;
    if (banner_h < 1)
        banner_h = 1;

    banner_pm = XCreatePixmap(dpy, win, banner_w, banner_h, DefaultDepth(dpy, screen));
    if (!banner_pm) {
        stbi_image_free(img);
        return;
    }
    XImage *xi = XCreateImage(dpy, visual, DefaultDepth(dpy, screen), ZPixmap, 0,
        NULL, (unsigned int)banner_w, (unsigned int)banner_h, 32, 0);
    if (!xi) {
        XFreePixmap(dpy, banner_pm);
        banner_pm = 0;
        stbi_image_free(img);
        return;
    }
    xi->data = (char *)malloc((size_t)banner_w * (size_t)banner_h * 4);
    if (!xi->data) {
        XDestroyImage(xi);
        XFreePixmap(dpy, banner_pm);
        banner_pm = 0;
        stbi_image_free(img);
        return;
    }
    for (int y = 0; y < banner_h; y++) {
        int sy = (int)((double)y / scale);
        if (sy >= ih)
            sy = ih - 1;
        for (int x = 0; x < banner_w; x++) {
            int sx = (int)((double)x / scale);
            if (sx >= iw)
                sx = iw - 1;
            int i = (sy * iw + sx) * 4;
            unsigned long pixel = ((unsigned long)img[i] << 16) |
                ((unsigned long)img[i + 1] << 8) | (unsigned long)img[i + 2];
            XPutPixel(xi, x, y, pixel);
        }
    }
    GC bgc = XCreateGC(dpy, banner_pm, 0, NULL);
    XPutImage(dpy, banner_pm, bgc, xi, 0, 0, 0, 0, (unsigned int)banner_w, (unsigned int)banner_h);
    XFreeGC(dpy, bgc);
    XDestroyImage(xi);
    stbi_image_free(img);
    banner_ready = 1;
}

static void draw_metro_btn_d(Drawable draw, GC g, int x, int y, int w, int h,
        const char *label, int hover, int inverse) {
    int bg_r, bg_g, bg_b, fg_r, fg_g, fg_b;
    if (inverse) {
        if (hover) {
            bg_r = COL_TILE_R;
            bg_g = COL_TILE_G;
            bg_b = COL_TILE_B;
            fg_r = fg_g = fg_b = 255;
        } else {
            bg_r = bg_g = bg_b = 255;
            fg_r = COL_BG_R;
            fg_g = COL_BG_G;
            fg_b = COL_BG_B;
        }
    } else {
        if (hover) {
            bg_r = 255;
            bg_g = 255;
            bg_b = 255;
            fg_r = COL_BG_R;
            fg_g = COL_BG_G;
            fg_b = COL_BG_B;
        } else {
            bg_r = COL_TILE_R;
            bg_g = COL_TILE_G;
            bg_b = COL_TILE_B;
            fg_r = fg_g = fg_b = 255;
        }
    }
    XSetForeground(dpy, g, rgb(bg_r, bg_g, bg_b));
    XFillRectangle(dpy, draw, g, x, y, w, h);
    int tw = text_width(font_btn, label);
    xft_draw(draw, x + (w - tw) / 2, y + text_baseline(h, font_btn),
        label, fg_r, fg_g, fg_b, font_btn);
}

static void draw_metro_btn(int x, int y, int w, int h, const char *label, int hover, int inverse) {
    draw_metro_btn_d(win, gc, x, y, w, h, label, hover, inverse);
}

static void draw_banner(int y0) {
    int x0 = PAD;
    int w = WIN_W - PAD * 2;

    load_banner();
    if (banner_ready && banner_pm) {
        int dx = x0 + (w - banner_w) / 2;
        int dy = y0 + (BANNER_H - banner_h) / 2;
        XSetForeground(dpy, gc, rgb(30, 50, 80));
        XFillRectangle(dpy, win, gc, x0, y0, w, BANNER_H);
        XCopyArea(dpy, banner_pm, win, gc, 0, 0, banner_w, banner_h, dx, dy);
        return;
    }

    XSetForeground(dpy, gc, rgb(40, 70, 110));
    XFillRectangle(dpy, win, gc, x0, y0, w, BANNER_H);
    const char *ph = "Backroot 8";
    int tw = text_width(font_head, ph);
    xft_draw(win, x0 + (w - tw) / 2, y0 + BANNER_H / 2,
        ph, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_head);
}

static void draw_welcome(void) {
    draw_banner(PAD);
    int y = PAD + BANNER_H + 36;
    xft_draw(win, PAD, y, "Install Backroot 8", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_title);
    y += font_title ? font_title->height + 16 : 48;
    xft_draw(win, PAD, y,
        "Set up Backroot on your PC from this live USB session.",
        COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_body);

    int bx = WIN_W - PAD - BTN_W;
    int by = WIN_H - PAD - BTN_H;
    draw_metro_btn(bx, by, BTN_W, BTN_H, "Start", start_hover, 1);
}

static void draw_tos_box(int x0, int y0, int w, int h) {
    XSetForeground(dpy, gc, rgb(20, 60, 100));
    XFillRectangle(dpy, win, gc, x0, y0, w, h);
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XDrawRectangle(dpy, win, gc, x0, y0, w - 1, h - 1);

    char buf[512];
    strncpy(buf, tos_placeholder, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    int line_y = y0 + 20 - tos_scroll;
    char *p = buf;
    while (*p && line_y < y0 + h - 8) {
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = '\0';
        if (line_y >= y0 + 8)
            xft_draw(win, x0 + 12, line_y, p, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_body);
        line_y += font_body ? font_body->height + 6 : 18;
        if (!nl)
            break;
        *nl = '\n';
        p = nl + 1;
    }
}

static void draw_setup(void) {
    int y = PAD;
    xft_draw(win, PAD, y + 20, "Install Backroot 8", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_head);
    y += 44;

    int tos_h = 140;
    draw_tos_box(PAD, y, WIN_W - PAD * 2, tos_h);
    y += tos_h + 12;

    xft_draw(win, PAD, y,
        "Warning: the selected drive will be completely nuked and Backroot will be installed on it.",
        COL_WARN_R, COL_WARN_G, COL_WARN_B, font_body);
    y += font_body ? font_body->height + 16 : 28;

    xft_draw(win, PAD, y, "Choose a drive:", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_body);
    y += 24;

    int list_h = n_disks * ROW_H;
    if (list_h > 160)
        list_h = 160;
    int x0 = PAD;
    int w = WIN_W - PAD * 2;
    XSetForeground(dpy, gc, rgb(20, 60, 100));
    XFillRectangle(dpy, win, gc, x0, y, w, list_h);

    for (int i = 0; i < n_disks; i++) {
        int ry = y + i * ROW_H;
        if (ry + ROW_H > y + list_h)
            break;
        if (i == disk_sel) {
            XSetForeground(dpy, gc, rgb(COL_TILE_R, COL_TILE_G, COL_TILE_B));
            XFillRectangle(dpy, win, gc, x0 + 2, ry + 2, w - 4, ROW_H - 4);
        }
        xft_draw(win, x0 + 12, ry + text_baseline(ROW_H, font_body),
            disks[i].line, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_body);
    }
    y += list_h + 20;

    draw_metro_btn(PAD, WIN_H - PAD - BTN_H, BTN_W, BTN_H, "Back", back_hover, 0);
    draw_metro_btn(WIN_W - PAD - BTN_W, WIN_H - PAD - BTN_H, BTN_W, BTN_H, "Install",
        install_hover, 1);
}

static void reload_log_lines(void) {
    FILE *fp = fopen(LOG_PATH, "r");
    char buf[65536];
    size_t len = 0;

    n_log_lines = 0;
    if (!fp) {
        snprintf(log_lines[0], LOG_COLS, "(waiting for install log…)");
        n_log_lines = 1;
        return;
    }
    if (fseek(fp, 0, SEEK_END) == 0) {
        long sz = ftell(fp);
        if (sz > (long)(sizeof buf - 1)) {
            fseek(fp, sz - (long)(sizeof buf - 1), SEEK_SET);
            len = fread(buf, 1, sizeof buf - 1, fp);
        } else {
            fseek(fp, 0, SEEK_SET);
            len = fread(buf, 1, sizeof buf - 1, fp);
        }
    } else {
        len = fread(buf, 1, sizeof buf - 1, fp);
    }
    buf[len] = '\0';
    fclose(fp);

    char *line = buf;
    while (*line && n_log_lines < LOG_MAX_LINES) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl++ = '\0';
        snprintf(log_lines[n_log_lines], LOG_COLS, "%.87s", line);
        n_log_lines++;
        if (!nl)
            break;
        line = nl;
    }
    if (n_log_lines == 0) {
        snprintf(log_lines[0], LOG_COLS, "(log empty)");
        n_log_lines = 1;
    }
}

static void draw_log_popup(void);
static void draw_all(void);

static void ensure_log_window(void) {
    XSetWindowAttributes attr;

    if (log_win)
        return;
    log_win = XCreateSimpleWindow(dpy, root, win_x + 50, win_y + 50, LOG_WIN_W, LOG_WIN_H, 2,
        rgb(255, 255, 255), rgb(24, 36, 56));
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
        PointerMotionMask;
    XChangeWindowAttributes(dpy, log_win, CWEventMask, &attr);
    XStoreName(dpy, log_win, "Install output");
    log_gc = XCreateGC(dpy, log_win, 0, NULL);
}

static void show_log_popup(void) {
    ensure_log_window();
    log_visible = 1;
    log_follow_tail = 1;
    log_scroll = 0;
    reload_log_lines();
    if (n_log_lines > 20)
        log_scroll = n_log_lines - 20;
    XMapRaised(dpy, log_win);
    draw_log_popup();
}

static void hide_log_popup(void) {
    if (!log_visible)
        return;
    log_visible = 0;
    XUnmapWindow(dpy, log_win);
    draw_all();
}

static void draw_log_popup(void) {
    int lh, visible_lines, text_h, y0, i, close_x, close_y;

    if (!log_visible || !log_win)
        return;
    reload_log_lines();

    XSetForeground(dpy, log_gc, rgb(24, 36, 56));
    XFillRectangle(dpy, log_win, log_gc, 0, 0, LOG_WIN_W, LOG_WIN_H);

    xft_draw(log_win, LOG_PAD, LOG_PAD + 18, "Install output",
        COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_head);

    lh = font_log ? font_log->height + 2 : 14;
    text_h = LOG_WIN_H - LOG_PAD * 2 - LOG_CLOSE_H - 36;
    visible_lines = text_h / lh;
    if (visible_lines < 4)
        visible_lines = 4;

    if (log_follow_tail && n_log_lines > visible_lines)
        log_scroll = n_log_lines - visible_lines;
    if (log_scroll < 0)
        log_scroll = 0;
    if (log_scroll > n_log_lines - 1)
        log_scroll = n_log_lines > visible_lines ? n_log_lines - visible_lines : 0;

    y0 = LOG_PAD + 36;
    XSetForeground(dpy, log_gc, rgb(12, 24, 40));
    XFillRectangle(dpy, log_win, log_gc, LOG_PAD, y0, LOG_WIN_W - LOG_PAD * 2, text_h);

    for (i = 0; i < visible_lines; i++) {
        int idx = log_scroll + i;
        if (idx >= n_log_lines)
            break;
        xft_draw(log_win, LOG_PAD + 8, y0 + 8 + (i + 1) * lh,
            log_lines[idx], 220, 230, 245, font_log);
    }

    close_x = LOG_WIN_W - LOG_PAD - LOG_CLOSE_W;
    close_y = LOG_WIN_H - LOG_PAD - LOG_CLOSE_H;
    draw_metro_btn_d(log_win, log_gc, close_x, close_y, LOG_CLOSE_W, LOG_CLOSE_H,
        "Close", log_close_hover, 1);
}

static void install_output_btn(int *bx, int *by) {
    *bx = PAD;
    *by = WIN_H - PAD - BTN_H;
}

static int read_install_status(int *pct, char *msg, size_t msgsz) {
    FILE *fp = fopen(STATUS_PATH, "r");
    char line[256];
    *pct = 0;
    if (msg && msgsz)
        msg[0] = '\0';
    if (!fp)
        return 0;
    while (fgets(line, sizeof line, fp)) {
        if (strncmp(line, "percent=", 8) == 0)
            *pct = atoi(line + 8);
        else if (strncmp(line, "message=", 8) == 0 && msg && msgsz) {
            size_t n = strlen(line + 8);
            while (n > 0 && (line[7 + n] == '\n' || line[7 + n] == '\r'))
                n--;
            snprintf(msg, msgsz, "%.*s", (int)n, line + 8);
        } else if (strncmp(line, "done=1", 6) == 0) {
            fclose(fp);
            return 2;
        } else if (strncmp(line, "error=", 6) == 0) {
            if (msg && msgsz)
                snprintf(msg, msgsz, "%s", line + 6);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 1;
}

static void draw_installing(void) {
    int pct = 0;
    char msg[256];
    int st = read_install_status(&pct, msg, sizeof msg);

    xft_draw(win, PAD, PAD + 30, "Installing Backroot 8…",
        COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_title);

    int bar_x = PAD;
    int bar_y = WIN_H / 2 - 20;
    int bar_w = WIN_W - PAD * 2;
    int bar_h = 28;
    XSetForeground(dpy, gc, rgb(20, 60, 100));
    XFillRectangle(dpy, win, gc, bar_x, bar_y, bar_w, bar_h);
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XFillRectangle(dpy, win, gc, bar_x + 2, bar_y + 2,
        (bar_w - 4) * pct / 100, bar_h - 4);

    if (!msg[0])
        snprintf(msg, sizeof msg, "Preparing installation…");
    xft_draw(win, PAD, bar_y + bar_h + 24, msg,
        COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_body);

    {
        int obx, oby;
        install_output_btn(&obx, &oby);
        draw_metro_btn(obx, oby, LOG_BTN_W, BTN_H, "View output", log_btn_hover, 0);
    }

    if (st == 2) {
        step = STEP_ALL_DONE;
        shutdown_at = time(NULL) + 10;
    } else if (st < 0) {
        char err[320];
        snprintf(err, sizeof err, "Installation failed: %s", msg[0] ? msg : "unknown error");
        xft_draw(win, PAD, bar_y + bar_h + 52, err, COL_WARN_R, COL_WARN_G, COL_WARN_B, font_body);
    }
}

static void draw_all_done(void) {
    int left = 10;
    if (shutdown_at > 0)
        left = (int)(shutdown_at - time(NULL));
    if (left < 0)
        left = 0;

    const char *title = "All done!";
    int tw = text_width(font_title, title);
    xft_draw(win, (WIN_W - tw) / 2, WIN_H / 2 - 50,
        title, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_title);

    char sub[128];
    snprintf(sub, sizeof sub,
        "Your computer will shut down in %d seconds", left);
    int sw = text_width(font_body, sub);
    xft_draw(win, (WIN_W - sw) / 2, WIN_H / 2 + 10,
        sub, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_body);
}

static void draw_all(void) {
    XSetForeground(dpy, gc, rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);

    switch (step) {
    case STEP_WELCOME:
        draw_welcome();
        break;
    case STEP_SETUP:
        draw_setup();
        break;
    case STEP_INSTALLING:
        draw_installing();
        break;
    case STEP_ALL_DONE:
        draw_all_done();
        break;
    }
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int welcome_start_btn(int *bx, int *by) {
    *bx = WIN_W - PAD - BTN_W;
    *by = WIN_H - PAD - BTN_H;
    return 1;
}

static int setup_disk_list_y(void) {
    int y = PAD + 44 + 140 + 12;
    y += font_body ? font_body->height + 16 : 28;
    y += 24;
    return y;
}

static int setup_disk_list_h(void) {
    int h = n_disks * ROW_H;
    return h > 160 ? 160 : h;
}

static void start_install(void) {
    if (n_disks <= 0 || disk_sel < 0 || disk_sel >= n_disks)
        return;

    mkdir("/run/br8-install", 0755);
    FILE *fp = fopen(STATUS_PATH, "w");
    if (fp) {
        fprintf(fp, "percent=0\nmessage=Starting…\n");
        fclose(fp);
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl(INSTALL_SCRIPT, INSTALL_SCRIPT, disks[disk_sel].dev, (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        install_pid = (int)pid;
    step = STEP_INSTALLING;
}

static void maybe_shutdown(void) {
    static int shutdown_done;
    if (shutdown_done || shutdown_at <= 0)
        return;
    if (time(NULL) < shutdown_at)
        return;
    shutdown_done = 1;
    if (fork() == 0) {
        execl("/usr/bin/systemctl", "systemctl", "poweroff", (char *)NULL);
        execl("/sbin/poweroff", "poweroff", (char *)NULL);
        _exit(1);
    }
}

static void handle_log_click(int x, int y) {
    int close_x = LOG_WIN_W - LOG_PAD - LOG_CLOSE_W;
    int close_y = LOG_WIN_H - LOG_PAD - LOG_CLOSE_H;
    if (point_in_rect(x, y, close_x, close_y, LOG_CLOSE_W, LOG_CLOSE_H))
        hide_log_popup();
}

static void handle_click(int x, int y) {
    if (step == STEP_INSTALLING) {
        int obx, oby;
        install_output_btn(&obx, &oby);
        if (point_in_rect(x, y, obx, oby, LOG_BTN_W, BTN_H)) {
            show_log_popup();
            return;
        }
    }
    if (step == STEP_WELCOME) {
        int bx, by;
        welcome_start_btn(&bx, &by);
        if (point_in_rect(x, y, bx, by, BTN_W, BTN_H))
            step = STEP_SETUP;
    } else if (step == STEP_SETUP) {
        if (point_in_rect(x, y, PAD, WIN_H - PAD - BTN_H, BTN_W, BTN_H)) {
            step = STEP_WELCOME;
            return;
        }
        if (point_in_rect(x, y, WIN_W - PAD - BTN_W, WIN_H - PAD - BTN_H, BTN_W, BTN_H)) {
            start_install();
            return;
        }
        int ly = setup_disk_list_y();
        int lh = setup_disk_list_h();
        if (point_in_rect(x, y, PAD, ly, WIN_W - PAD * 2, lh)) {
            int row = (y - ly) / ROW_H;
            if (row >= 0 && row < n_disks)
                disk_sel = row;
        }
    }
}

static void update_hover(int x, int y) {
    int ns = 0, ni = 0, nb = 0, lo = 0;
    if (step == STEP_WELCOME) {
        int bx, by;
        welcome_start_btn(&bx, &by);
        ns = point_in_rect(x, y, bx, by, BTN_W, BTN_H);
    } else if (step == STEP_SETUP) {
        nb = point_in_rect(x, y, PAD, WIN_H - PAD - BTN_H, BTN_W, BTN_H);
        ni = point_in_rect(x, y, WIN_W - PAD - BTN_W, WIN_H - PAD - BTN_H, BTN_W, BTN_H);
    } else if (step == STEP_INSTALLING) {
        int obx, oby;
        install_output_btn(&obx, &oby);
        lo = point_in_rect(x, y, obx, oby, LOG_BTN_W, BTN_H);
    }
    if (ns != start_hover || ni != install_hover || nb != back_hover || lo != log_btn_hover) {
        start_hover = ns;
        install_hover = ni;
        back_hover = nb;
        log_btn_hover = lo;
        draw_all();
    }
}

static void set_wm_name(void) {
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    const char *name = "Backroot 8 Setup";
    XChangeProperty(dpy, win, net_wm_name, utf8, 8, PropModeReplace,
        (unsigned char *)name, (int)strlen(name));
    XStoreName(dpy, win, name);
}

int main(void) {
    if (!is_live_boot())
        return 0;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-install: cannot open display\n");
        return 1;
    }

    XSetErrorHandler(NULL);
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    open_fonts();
    load_disks();

    XSetWindowAttributes attr;
    win = XCreateSimpleWindow(dpy, root, 60, 40, WIN_W, WIN_H, 0,
        BlackPixel(dpy, screen), rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
        PointerMotionMask;
    XChangeWindowAttributes(dpy, win, CWEventMask, &attr);

    XWMHints hints = { .flags = StateHint, .initial_state = NormalState };
    XSetWMHints(dpy, win, &hints);
    XClassHint ch = { .res_name = (char *)"br8-install", .res_class = (char *)"Br8Install" };
    XSetClassHint(dpy, win, &ch);
    set_wm_name();

    gc = XCreateGC(dpy, win, 0, NULL);
    XMapRaised(dpy, win);
    draw_all();

    int xfd = ConnectionNumber(dpy);
    while (1) {
        if (step == STEP_INSTALLING || step == STEP_ALL_DONE) {
            struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            select(xfd + 1, &fds, NULL, NULL, &tv);
            draw_all();
            if (log_visible)
                draw_log_popup();
            if (step == STEP_ALL_DONE)
                maybe_shutdown();
        } else {
            struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(xfd, &fds);
            select(xfd + 1, &fds, NULL, NULL, &tv);
        }

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) {
                if (ev.xexpose.window == win)
                    draw_all();
                else if (ev.xexpose.window == log_win && log_visible)
                    draw_log_popup();
            } else if (ev.type == ConfigureNotify && ev.xconfigure.window == win) {
                win_x = ev.xconfigure.x;
                win_y = ev.xconfigure.y;
                (void)win_x;
                (void)win_y;
            } else if (ev.type == MotionNotify) {
                if (ev.xmotion.window == win)
                    update_hover((int)ev.xmotion.x, (int)ev.xmotion.y);
                else if (ev.xmotion.window == log_win) {
                    int close_x = LOG_WIN_W - LOG_PAD - LOG_CLOSE_W;
                    int close_y = LOG_WIN_H - LOG_PAD - LOG_CLOSE_H;
                    int h = point_in_rect((int)ev.xmotion.x, (int)ev.xmotion.y,
                        close_x, close_y, LOG_CLOSE_W, LOG_CLOSE_H);
                    if (h != log_close_hover) {
                        log_close_hover = h;
                        draw_log_popup();
                    }
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.button == Button4) {
                    if (ev.xbutton.window == log_win && log_visible) {
                        log_follow_tail = 0;
                        log_scroll -= 3;
                        if (log_scroll < 0)
                            log_scroll = 0;
                        draw_log_popup();
                    } else if (ev.xbutton.window == win && step == STEP_SETUP)
                        tos_scroll = tos_scroll > 12 ? tos_scroll - 12 : 0;
                } else if (ev.xbutton.button == Button5) {
                    if (ev.xbutton.window == log_win && log_visible) {
                        log_follow_tail = 0;
                        log_scroll += 3;
                        draw_log_popup();
                    } else if (ev.xbutton.window == win && step == STEP_SETUP)
                        tos_scroll += 12;
                } else if (ev.xbutton.button == Button1) {
                    if (ev.xbutton.window == log_win)
                        handle_log_click((int)ev.xbutton.x, (int)ev.xbutton.y);
                    else if (ev.xbutton.window == win)
                        handle_click((int)ev.xbutton.x, (int)ev.xbutton.y);
                }
                draw_all();
            }
        }

        if (install_pid > 0) {
            int st;
            if (waitpid(install_pid, &st, WNOHANG) > 0)
                install_pid = 0;
        }
    }
    return 0;
}
