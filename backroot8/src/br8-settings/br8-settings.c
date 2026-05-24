/*
 * br8-settings — Metro-style system settings (X11 + Xft)
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include "../../include/br8-metro.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "../br8-start/stb_image.h"

#define SIDEBAR_W 360
#define NAV_ITEM_H 48
#define SIDEBAR_PAD 24
#define CONTENT_PAD 48
#define HEADER_H 72
#define COMBO_H 40
#define POPUP_W 480
#define POPUP_VISIBLE 12
#define POPUP_ITEM_H 32
#define POPUP_H (POPUP_VISIBLE * POPUP_ITEM_H)
#define THUMB_W 200
#define THUMB_H 112
#define THUMB_GAP 16
#define PREVIEW_H 220
#define TOGGLE_W 56
#define TOGGLE_H 28
#define ROW_GAP 36
#define WALLPAPER_N 4

#define COL_SIDEBAR_R 81
#define COL_SIDEBAR_G 49
#define COL_SIDEBAR_B 169
#define COL_SIDEBAR_HI_R 120
#define COL_SIDEBAR_HI_G 80
#define COL_SIDEBAR_HI_B 200
#define COL_CONTENT_R 255
#define COL_CONTENT_G 255
#define COL_CONTENT_B 255
#define COL_TEXT_R 30
#define COL_TEXT_G 30
#define COL_TEXT_B 30
#define COL_MUTED_R 100
#define COL_MUTED_G 100
#define COL_MUTED_B 110
#define COL_ACCENT_R 112
#define COL_ACCENT_G 48
#define COL_ACCENT_B 160
#define COL_FIELD_R 240
#define COL_FIELD_G 240
#define COL_FIELD_B 245
#define COL_POPUP_R 63
#define COL_POPUP_G 39
#define COL_POPUP_B 133

typedef enum {
    SEC_PERSONALIZATION,
    SEC_DISPLAY,
    SEC_KEYBOARD,
    SEC_MOUSE,
    SEC_TIME,
    SEC_COUNT
} Section;

typedef struct {
    char **items;
    int n;
} StrList;

typedef struct {
    const char *language;
    const char *keymap;
} KeyboardLang;

static const KeyboardLang keyboard_languages[] = {
    { "English (US)", "us" },
    { "English (UK)", "gb" },
    { "German", "de" },
    { "French", "fr" },
    { "Spanish", "es" },
    { "Italian", "it" },
    { "Portuguese", "pt" },
    { "Portuguese (Brazil)", "br" },
    { "Dutch", "nl" },
    { "Danish", "dk" },
    { "Norwegian", "no" },
    { "Swedish", "se" },
    { "Finnish", "fi" },
    { "Polish", "pl" },
    { "Czech", "cz" },
    { "Hungarian", "hu" },
    { "Russian", "ru" },
    { "Ukrainian", "ua" },
    { "Turkish", "tr" },
    { "Greek", "gr" },
    { "Hebrew", "il" },
    { "Arabic", "ara" },
    { "Japanese", "jp" },
    { "Korean", "kr" },
    { "Chinese", "cn" },
    { NULL, NULL }
};

static const char *const SEC_LABELS[SEC_COUNT] = {
    "Personalization",
    "Display",
    "Keyboard",
    "Mouse",
    "Time & language"
};

static const char *const WALLPAPER_PATHS[WALLPAPER_N] = {
    "/usr/share/backgrounds/backroot8.jpg",
    "/usr/share/backroot8/oobe-wallpapers/wallpaper-1.jpg",
    "/usr/share/backroot8/oobe-wallpapers/wallpaper-2.jpg",
    "/usr/share/backroot8/oobe-wallpapers/wallpaper-3.jpg",
};

static const char *const WALLPAPER_LABELS[WALLPAPER_N] = {
    "Default",
    "Background 1",
    "Background 2",
    "Background 3"
};

static const char *const POINTER_SPEEDS[] = { "Slow", "Normal", "Fast" };
static const char *const POINTER_XSET[] = { "4/2 10", "2/1 4", "1/1 2" };

static Display *dpy;
static int screen;
static Window root, win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static int win_w, win_h;
static Drawable canvas;
static Pixmap back_pm;
static int back_pm_w, back_pm_h;
static XftFont *font_title, *font_nav, *font_header, *font_body, *font_label;
static Atom wm_protocols, wm_delete;

static Section section = SEC_PERSONALIZATION;
static int nav_hover = -1;
static StrList kb_list, tz_list, res_list;
static int kb_sel, tz_sel, res_sel, wallpaper_sel, pointer_sel;
static int natural_scroll;
static int popup_combo; /* 0 none, 1 kb, 2 tz, 3 res */
static int popup_scroll;
static Window popup;
static int popup_visible;
static Pixmap thumb_pm[WALLPAPER_N];
static int thumb_loaded[WALLPAPER_N];
static char config_dir[512];
static char wallpaper_config[512];

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
        return height - 10;
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
        "Cantarell Light-36:antialias=true",
        "DejaVu Sans-36",
        NULL
    };
    static const char *const nav_names[] = {
        "Segoe UI-16:antialias=true",
        "DejaVu Sans-16",
        NULL
    };
    static const char *const header_names[] = {
        "Segoe UI Light-32:antialias=true",
        "DejaVu Sans-28",
        NULL
    };
    static const char *const body_names[] = {
        "Segoe UI-14:antialias=true",
        "DejaVu Sans-14",
        NULL
    };
    static const char *const label_names[] = {
        "Segoe UI Semibold-13:antialias=true",
        "Segoe UI-13:antialias=true",
        NULL
    };
    font_title = open_font(title_names);
    font_nav = open_font(nav_names);
    font_header = open_font(header_names);
    font_body = open_font(body_names);
    font_label = open_font(label_names);
}

static char *run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    char buf[512];
    char *out = NULL;
    size_t len = 0;

    if (!fp)
        return NULL;
    while (fgets(buf, sizeof buf, fp)) {
        size_t bl = strlen(buf);
        char *tmp = realloc(out, len + bl + 1);
        if (!tmp) {
            free(out);
            pclose(fp);
            return NULL;
        }
        out = tmp;
        memcpy(out + len, buf, bl);
        len += bl;
        out[len] = '\0';
    }
    pclose(fp);
    if (out) {
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' '))
            out[--len] = '\0';
    }
    return out;
}

static int run_cmd(const char *cmd) {
    return system(cmd);
}

static void strlist_free(StrList *sl) {
    if (!sl)
        return;
    for (int i = 0; i < sl->n; i++)
        free(sl->items[i]);
    free(sl->items);
    sl->items = NULL;
    sl->n = 0;
}

static void strlist_add(StrList *sl, const char *s) {
    char **next = realloc(sl->items, (size_t)(sl->n + 1) * sizeof(char *));
    if (!next)
        return;
    sl->items = next;
    sl->items[sl->n++] = strdup(s);
}

static int strlist_index(const StrList *sl, const char *value) {
    for (int i = 0; i < sl->n; i++) {
        if (strcmp(sl->items[i], value) == 0)
            return i;
    }
    return 0;
}

static void bump_panel_rev(void) {
    Atom rev_atom = XInternAtom(dpy, "_BR8_PANEL_REV", False);
    unsigned long rev = 1;
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;

    if (XGetWindowProperty(dpy, root, rev_atom, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        rev = data[0] + 1;
    if (data)
        XFree(data);
    XChangeProperty(dpy, root, rev_atom, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&rev, 1);
    XFlush(dpy);
}

static void init_config_paths(void) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        struct passwd *pw = getpwuid(getuid());
        home = (pw && pw->pw_dir) ? pw->pw_dir : "/root";
    }
    snprintf(config_dir, sizeof config_dir, "%s/.config/backroot8", home);
    snprintf(wallpaper_config, sizeof wallpaper_config, "%s/wallpaper.jpg", config_dir);
}

static int keyboard_lang_count(void) {
    int n = 0;
    while (keyboard_languages[n].language)
        n++;
    return n;
}

static const char *keyboard_keymap_for_index(int idx) {
    if (idx < 0 || idx >= keyboard_lang_count())
        return "us";
    return keyboard_languages[idx].keymap;
}

static int keyboard_index_for_keymap(const char *code) {
    if (!code || !code[0])
        return 0;
    for (int i = 0; keyboard_languages[i].language; i++) {
        if (strcmp(keyboard_languages[i].keymap, code) == 0)
            return i;
    }
    for (int i = 0; keyboard_languages[i].language; i++) {
        size_t len = strlen(keyboard_languages[i].keymap);
        if (strncmp(keyboard_languages[i].keymap, code, len) == 0 &&
            (code[len] == '\0' || code[len] == '-'))
            return i;
    }
    return 0;
}

static void load_keyboard_languages(void) {
    for (int i = 0; keyboard_languages[i].language; i++)
        strlist_add(&kb_list, keyboard_languages[i].language);
}

static void load_timezones(void) {
    FILE *fp = popen("timedatectl list-timezones 2>/dev/null", "r");
    char line[128];

    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            char *nl = strchr(line, '\n');
            if (nl)
                *nl = '\0';
            if (line[0])
                strlist_add(&tz_list, line);
        }
        pclose(fp);
    }
    if (tz_list.n == 0) {
        strlist_add(&tz_list, "UTC");
        strlist_add(&tz_list, "America/New_York");
        strlist_add(&tz_list, "Europe/London");
    }
}

static void load_resolutions(void) {
    FILE *fp = popen("xrandr 2>/dev/null", "r");
    char line[256];
    int in_modes = 0;

    if (!fp) {
        strlist_add(&res_list, "1024x768");
        return;
    }
    while (fgets(line, sizeof line, fp)) {
        if (strstr(line, " connected")) {
            in_modes = 1;
            continue;
        }
        if (in_modes && line[0] == ' ') {
            char mode[64];
            if (sscanf(line, " %63s", mode) == 1 && strchr(mode, 'x'))
                strlist_add(&res_list, mode);
        } else if (line[0] != ' ' && line[0] != '\t' && line[0] != '\n') {
            in_modes = 0;
        }
    }
    pclose(fp);
    if (res_list.n == 0)
        strlist_add(&res_list, "1024x768");
}

static char *current_keymap(void) {
    char *out = run_command("localectl status 2>/dev/null | awk '/VC Keymap:/ {print $3}'");
    if (!out || !out[0]) {
        free(out);
        return strdup("us");
    }
    return out;
}

static char *current_timezone(void) {
    char *out = run_command("timedatectl show --property=Timezone --value 2>/dev/null");
    if (!out || !out[0]) {
        free(out);
        return strdup("UTC");
    }
    return out;
}

static char *current_resolution(void) {
    char *out = run_command(
        "xrandr --current 2>/dev/null | awk '/\\*/ {print $1; exit}'");
    if (!out || !out[0]) {
        free(out);
        return strdup("1024x768");
    }
    return out;
}

static int current_wallpaper_index(void) {
    if (access(wallpaper_config, R_OK) == 0) {
        for (int i = 0; i < WALLPAPER_N; i++) {
            if (strcmp(WALLPAPER_PATHS[i], wallpaper_config) == 0)
                return i;
        }
        return 0;
    }
    return 0;
}

static int read_natural_scroll(void) {
    char *out = run_command(
        "xinput list-props 'Virtual core pointer' 2>/dev/null | "
        "awk -F: '/Natural Scrolling Enabled/ {gsub(/[^0-9]/,\"\",$2); print $2; exit}'");
    int val = 0;
    if (out && out[0])
        val = atoi(out) != 0;
    free(out);
    return val;
}

static void apply_keymap(const char *code) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "localectl set-keymap %s", code);
    if (run_cmd(cmd) != 0)
        return;
    snprintf(cmd, sizeof cmd, "localectl set-x11-keymap %s", code);
    run_cmd(cmd);
    snprintf(cmd, sizeof cmd, "setxkbmap %s", code);
    run_cmd(cmd);
}

static void apply_timezone(const char *tz) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "timedatectl set-timezone %s", tz);
    run_cmd(cmd);
}

static void apply_resolution(const char *mode) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "xrandr -s %s 2>/dev/null", mode);
    run_cmd(cmd);
}

static void apply_wallpaper(int idx) {
    const char *src = WALLPAPER_PATHS[idx];
    char cmd[1024];

    if (idx < 0 || idx >= WALLPAPER_N || access(src, R_OK) != 0)
        return;
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", config_dir);
    run_cmd(cmd);
    snprintf(cmd, sizeof cmd, "cp '%s' '%s'", src, wallpaper_config);
    run_cmd(cmd);
    snprintf(cmd, sizeof cmd,
        "command -v feh >/dev/null && feh --bg-fill '%s'", wallpaper_config);
    run_cmd(cmd);
    bump_panel_rev();
    wallpaper_sel = idx;
}

static void apply_pointer_speed(int idx) {
    char cmd[128];
    if (idx < 0 || idx > 2)
        idx = 1;
    snprintf(cmd, sizeof cmd, "xset m %s 2>/dev/null", POINTER_XSET[idx]);
    run_cmd(cmd);
    pointer_sel = idx;
}

static void apply_natural_scroll(int on) {
    char cmd[512];
    snprintf(cmd, sizeof cmd,
        "prop=$(xinput list-props 'Virtual core pointer' 2>/dev/null | "
        "awk -F: '/Natural Scrolling Enabled/ {print $1; exit}'); "
        "[ -n \"$prop\" ] && xinput set-prop 'Virtual core pointer' \"$prop\" %d",
        on ? 1 : 0);
    run_cmd(cmd);
    natural_scroll = on;
}

static void load_thumb(int idx) {
    int iw, ih, comp;
    unsigned char *img;
    const char *path;

    if (idx < 0 || idx >= WALLPAPER_N || thumb_loaded[idx])
        return;
    path = WALLPAPER_PATHS[idx];
    if (access(path, R_OK) != 0)
        return;
    img = stbi_load(path, &iw, &ih, &comp, 4);
    if (!img || iw <= 0 || ih <= 0)
        return;

    thumb_pm[idx] = XCreatePixmap(dpy, win, THUMB_W, THUMB_H, DefaultDepth(dpy, screen));
    if (!thumb_pm[idx]) {
        stbi_image_free(img);
        return;
    }
    XSetForeground(dpy, gc, rgb(COL_FIELD_R, COL_FIELD_G, COL_FIELD_B));
    XFillRectangle(dpy, thumb_pm[idx], gc, 0, 0, THUMB_W, THUMB_H);

    double scale = (double)THUMB_W / (double)iw;
    if ((double)THUMB_H / (double)ih < scale)
        scale = (double)THUMB_H / (double)ih;
    int dw = (int)(iw * scale);
    int dh = (int)(ih * scale);
    if (dw < 1)
        dw = 1;
    if (dh < 1)
        dh = 1;

    XImage *xi = XCreateImage(dpy, visual, DefaultDepth(dpy, screen), ZPixmap, 0,
        NULL, (unsigned int)THUMB_W, (unsigned int)THUMB_H, 32, 0);
    if (!xi) {
        XFreePixmap(dpy, thumb_pm[idx]);
        thumb_pm[idx] = 0;
        stbi_image_free(img);
        return;
    }
    xi->data = (char *)calloc((size_t)THUMB_W * (size_t)THUMB_H, 4);
    if (!xi->data) {
        XDestroyImage(xi);
        XFreePixmap(dpy, thumb_pm[idx]);
        thumb_pm[idx] = 0;
        stbi_image_free(img);
        return;
    }
    int ox = (THUMB_W - dw) / 2;
    int oy = (THUMB_H - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int sy = (int)((double)y / scale);
        if (sy >= ih)
            sy = ih - 1;
        for (int x = 0; x < dw; x++) {
            int sx = (int)((double)x / scale);
            if (sx >= iw)
                sx = iw - 1;
            int i = (sy * iw + sx) * 4;
            unsigned long pixel = ((unsigned long)img[i] << 16) |
                ((unsigned long)img[i + 1] << 8) | (unsigned long)img[i + 2];
            XPutPixel(xi, ox + x, oy + y, pixel);
        }
    }
    GC tgc = XCreateGC(dpy, thumb_pm[idx], 0, NULL);
    XPutImage(dpy, thumb_pm[idx], tgc, xi, 0, 0, 0, 0, THUMB_W, THUMB_H);
    XFreeGC(dpy, tgc);
    free(xi->data);
    XDestroyImage(xi);
    stbi_image_free(img);
    thumb_loaded[idx] = 1;
}

static void ensure_back_buffer(void) {
    if (back_pm && back_pm_w == win_w && back_pm_h == win_h)
        return;
    if (back_pm)
        XFreePixmap(dpy, back_pm);
    back_pm_w = win_w;
    back_pm_h = win_h;
    back_pm = XCreatePixmap(dpy, win, (unsigned)back_pm_w, (unsigned)back_pm_h,
        DefaultDepth(dpy, screen));
    canvas = back_pm;
}

static void present_frame(void) {
    if (back_pm)
        XCopyArea(dpy, back_pm, win, gc, 0, 0, (unsigned)back_pm_w, (unsigned)back_pm_h, 0, 0);
}

static StrList *popup_list(void) {
    if (popup_combo == 1)
        return &kb_list;
    if (popup_combo == 2)
        return &tz_list;
    if (popup_combo == 3)
        return &res_list;
    return NULL;
}

static int popup_selected(void) {
    if (popup_combo == 1)
        return kb_sel;
    if (popup_combo == 2)
        return tz_sel;
    if (popup_combo == 3)
        return res_sel;
    return 0;
}

static void set_popup_selected(int idx) {
    if (popup_combo == 1)
        kb_sel = idx;
    else if (popup_combo == 2)
        tz_sel = idx;
    else if (popup_combo == 3)
        res_sel = idx;
}

static void hide_popup(void) {
    if (!popup_visible)
        return;
    XUnmapWindow(dpy, popup);
    popup_visible = 0;
    popup_combo = 0;
}

static void draw_popup(void) {
    StrList *sl = popup_list();
    int sel;
    int px = SIDEBAR_W + CONTENT_PAD;
    int py = HEADER_H + 120;

    if (!sl || !popup_visible)
        return;
    XMoveResizeWindow(dpy, popup, px, py, POPUP_W, POPUP_H);
    XSetForeground(dpy, gc, rgb(COL_POPUP_R, COL_POPUP_G, COL_POPUP_B));
    XFillRectangle(dpy, popup, gc, 0, 0, POPUP_W, POPUP_H);

    sel = popup_selected();
    for (int row = 0; row < POPUP_VISIBLE; row++) {
        int idx = popup_scroll + row;
        int y0 = row * POPUP_ITEM_H;
        if (idx >= sl->n)
            break;
        if (idx == sel) {
            XSetForeground(dpy, gc, rgb(COL_SIDEBAR_HI_R, COL_SIDEBAR_HI_G, COL_SIDEBAR_HI_B));
            XFillRectangle(dpy, popup, gc, 0, y0, POPUP_W, POPUP_ITEM_H);
        }
        xft_draw(popup, 12, y0 + text_baseline(POPUP_ITEM_H, font_body),
            sl->items[idx], 255, 255, 255, font_body);
    }
}

static void show_popup(int combo) {
    StrList *sl;
    int sel;

    hide_popup();
    popup_combo = combo;
    sl = popup_list();
    if (!sl || sl->n == 0)
        return;
    sel = popup_selected();
    popup_scroll = sel - POPUP_VISIBLE / 2;
    if (popup_scroll < 0)
        popup_scroll = 0;
    if (popup_scroll > sl->n - POPUP_VISIBLE)
        popup_scroll = sl->n - POPUP_VISIBLE;
    if (popup_scroll < 0)
        popup_scroll = 0;
    popup_visible = 1;
    draw_popup();
    XMapRaised(dpy, popup);
}

static void draw_combo(int x, int y, int w, const char *label, const char *value) {
    xft_draw(canvas, x, y, label, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_label);
    y += 22;
    XSetForeground(dpy, gc, rgb(200, 200, 210));
    XDrawRectangle(dpy, canvas, gc, x, y, w, COMBO_H);
    XSetForeground(dpy, gc, rgb(COL_FIELD_R, COL_FIELD_G, COL_FIELD_B));
    XFillRectangle(dpy, canvas, gc, x + 1, y + 1, w - 2, COMBO_H - 2);
    if (value && value[0])
        xft_draw(canvas, x + 12, y + text_baseline(COMBO_H, font_body),
            value, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_body);
    xft_draw(canvas, x + w - 20, y + text_baseline(COMBO_H, font_body),
        "v", COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_body);
}

static void draw_toggle(int x, int y, const char *label, int on) {
    xft_draw(canvas, x, y + text_baseline(TOGGLE_H, font_body),
        label, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_body);
    int tx = x + 320;
    int tr = on ? COL_ACCENT_R : 180;
    int tg = on ? COL_ACCENT_G : 180;
    int tb = on ? COL_ACCENT_B : 190;
    XSetForeground(dpy, gc, rgb(tr, tg, tb));
    XFillRectangle(dpy, canvas, gc, tx, y, TOGGLE_W, TOGGLE_H);
    int knob = on ? tx + TOGGLE_W - TOGGLE_H - 2 : tx + 2;
    XSetForeground(dpy, gc, rgb(255, 255, 255));
    XFillRectangle(dpy, canvas, gc, knob, y + 2, TOGGLE_H - 4, TOGGLE_H - 4);
}

static void draw_sidebar(void) {
    XSetForeground(dpy, gc, rgb(COL_SIDEBAR_R, COL_SIDEBAR_G, COL_SIDEBAR_B));
    XFillRectangle(dpy, canvas, gc, 0, 0, SIDEBAR_W, win_h);

    xft_draw(canvas, SIDEBAR_PAD, SIDEBAR_PAD + text_baseline(36, font_title),
        "Settings", 255, 255, 255, font_title);

    int y = HEADER_H;
    for (int i = 0; i < SEC_COUNT; i++) {
        int sel = (i == (int)section);
        int hover = (i == nav_hover);
        if (sel || hover) {
            XSetForeground(dpy, gc, rgb(COL_SIDEBAR_HI_R, COL_SIDEBAR_HI_G, COL_SIDEBAR_HI_B));
            XFillRectangle(dpy, canvas, gc, SIDEBAR_PAD / 2, y,
                SIDEBAR_W - SIDEBAR_PAD, NAV_ITEM_H);
        }
        xft_draw(canvas, SIDEBAR_PAD, y + text_baseline(NAV_ITEM_H, font_nav),
            SEC_LABELS[i], 255, 255, 255, font_nav);
        y += NAV_ITEM_H;
    }
}

static void draw_personalization(int cx, int cw) {
    int y = CONTENT_PAD;
    int preview_w = cw > 640 ? 640 : cw;

    xft_draw(canvas, cx, y, "Personalization", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);
    y += 48;
    xft_draw(canvas, cx, y, "Choose your desktop background",
        COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_body);
    y += 40;

    XSetForeground(dpy, gc, rgb(COL_FIELD_R, COL_FIELD_G, COL_FIELD_B));
    XFillRectangle(dpy, canvas, gc, cx, y, preview_w, PREVIEW_H);
    if (thumb_loaded[wallpaper_sel]) {
        int px = cx + (preview_w - THUMB_W) / 2;
        int py = y + (PREVIEW_H - THUMB_H) / 2;
        if (px < cx)
            px = cx;
        XCopyArea(dpy, thumb_pm[wallpaper_sel], canvas, gc,
            0, 0, THUMB_W, THUMB_H, px, py);
    }
    y += PREVIEW_H + 32;

    int tx = cx;
    for (int i = 0; i < WALLPAPER_N; i++) {
        if (!thumb_loaded[i])
            continue;
        int sel = (i == wallpaper_sel);
        XSetForeground(dpy, gc, rgb(sel ? COL_ACCENT_R : 200,
            sel ? COL_ACCENT_G : 200, sel ? COL_ACCENT_B : 210));
        XDrawRectangle(dpy, canvas, gc, tx - 2, y - 2, THUMB_W + 4, THUMB_H + 4);
        XCopyArea(dpy, thumb_pm[i], canvas, gc, 0, 0, THUMB_W, THUMB_H, tx, y);
        xft_draw(canvas, tx, y + THUMB_H + 18, WALLPAPER_LABELS[i],
            COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_label);
        tx += THUMB_W + THUMB_GAP;
    }

    int btn_y = y + THUMB_H + 48;
    XSetForeground(dpy, gc, rgb(COL_FIELD_R, COL_FIELD_G, COL_FIELD_B));
    XFillRectangle(dpy, canvas, gc, cx, btn_y, 100, 36);
    XSetForeground(dpy, gc, rgb(180, 180, 190));
    XDrawRectangle(dpy, canvas, gc, cx, btn_y, 100, 36);
    xft_draw(canvas, cx + 20, btn_y + text_baseline(36, font_body),
        "Browse", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_body);
}

static void draw_display(int cx, int cw) {
    int y = CONTENT_PAD;
    (void)cw;
    xft_draw(canvas, cx, y, "Display", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);
    y += 56;
    const char *cur = res_sel >= 0 && res_sel < res_list.n ? res_list.items[res_sel] : "";
    draw_combo(cx, y, 400, "Screen resolution", cur);
}

static void draw_keyboard(int cx, int cw) {
    int y = CONTENT_PAD;
    (void)cw;
    xft_draw(canvas, cx, y, "Keyboard", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);
    y += 56;
    const char *cur = kb_sel >= 0 && kb_sel < kb_list.n ? kb_list.items[kb_sel] : "";
    draw_combo(cx, y, 400, "Keyboard layout", cur);
}

static void draw_mouse(int cx, int cw) {
    int y = CONTENT_PAD;
    (void)cw;
    xft_draw(canvas, cx, y, "Mouse", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);
    y += 56;
    draw_combo(cx, y, 400, "Pointer speed",
        pointer_sel >= 0 && pointer_sel < 3 ? POINTER_SPEEDS[pointer_sel] : "Normal");
    y += ROW_GAP + COMBO_H;
    draw_toggle(cx, y, "Reverse scrolling direction (natural scroll)", natural_scroll);
}

static void draw_time(int cx, int cw) {
    int y = CONTENT_PAD;
    (void)cw;
    xft_draw(canvas, cx, y, "Time & language", COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_header);
    y += 56;
    const char *cur = tz_sel >= 0 && tz_sel < tz_list.n ? tz_list.items[tz_sel] : "";
    draw_combo(cx, y, 400, "Time zone", cur);
}

static void draw_content(void) {
    int cx = SIDEBAR_W + CONTENT_PAD;
    int cw = win_w - SIDEBAR_W - CONTENT_PAD * 2;

    XSetForeground(dpy, gc, rgb(COL_CONTENT_R, COL_CONTENT_G, COL_CONTENT_B));
    XFillRectangle(dpy, canvas, gc, SIDEBAR_W, 0, win_w - SIDEBAR_W, win_h);

    switch (section) {
    case SEC_PERSONALIZATION:
        draw_personalization(cx, cw);
        break;
    case SEC_DISPLAY:
        draw_display(cx, cw);
        break;
    case SEC_KEYBOARD:
        draw_keyboard(cx, cw);
        break;
    case SEC_MOUSE:
        draw_mouse(cx, cw);
        break;
    case SEC_TIME:
        draw_time(cx, cw);
        break;
    default:
        break;
    }
}

static void draw_all(void) {
    ensure_back_buffer();
    draw_sidebar();
    draw_content();
    present_frame();
    if (popup_visible)
        draw_popup();
}

static int nav_item_at(int x, int y) {
    if (x < SIDEBAR_PAD / 2 || x >= SIDEBAR_W)
        return -1;
    int y0 = HEADER_H;
    for (int i = 0; i < SEC_COUNT; i++) {
        if (y >= y0 && y < y0 + NAV_ITEM_H)
            return i;
        y0 += NAV_ITEM_H;
    }
    return -1;
}

static int combo_rect(int cx, int cy, int w, int h, int *ox, int *oy, int *ow, int *oh) {
    *ox = cx;
    *oy = cy + 22;
    *ow = w;
    *oh = COMBO_H;
    (void)h;
    return 1;
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void on_kb_selected(int idx) {
    kb_sel = idx;
    apply_keymap(keyboard_keymap_for_index(idx));
}

static void on_tz_selected(int idx) {
    tz_sel = idx;
    if (idx >= 0 && idx < tz_list.n)
        apply_timezone(tz_list.items[idx]);
}

static void on_res_selected(int idx) {
    res_sel = idx;
    if (idx >= 0 && idx < res_list.n)
        apply_resolution(res_list.items[idx]);
}

static void browse_wallpaper(void) {
    char cmd[512];
    snprintf(cmd, sizeof cmd,
        "zenity --file-selection --title='Choose wallpaper' "
        "--file-filter='Images | *.jpg *.jpeg *.png *.bmp' 2>/dev/null");
    FILE *fp = popen(cmd, "r");
    char path[1024];
    if (!fp)
        return;
    if (!fgets(path, sizeof path, fp)) {
        pclose(fp);
        return;
    }
    pclose(fp);
    char *nl = strchr(path, '\n');
    if (nl)
        *nl = '\0';
    if (!path[0] || access(path, R_OK) != 0)
        return;
    snprintf(cmd, sizeof cmd, "mkdir -p '%s' && cp '%s' '%s'",
        config_dir, path, wallpaper_config);
    run_cmd(cmd);
    snprintf(cmd, sizeof cmd, "feh --bg-fill '%s' 2>/dev/null", wallpaper_config);
    run_cmd(cmd);
    bump_panel_rev();
}

static void handle_content_click(int x, int y) {
    int cx = SIDEBAR_W + CONTENT_PAD;

    if (section == SEC_PERSONALIZATION) {
        int thumb_y = CONTENT_PAD + 48 + 40 + PREVIEW_H + 32;
        int tx = cx;
        for (int i = 0; i < WALLPAPER_N; i++) {
            if (!thumb_loaded[i])
                continue;
            if (point_in_rect(x, y, tx, thumb_y, THUMB_W, THUMB_H)) {
                apply_wallpaper(i);
                draw_all();
                return;
            }
            tx += THUMB_W + THUMB_GAP;
        }
        int btn_y = thumb_y + THUMB_H + 48;
        if (point_in_rect(x, y, cx, btn_y, 100, 36)) {
            browse_wallpaper();
            draw_all();
        }
        return;
    }

    if (section == SEC_DISPLAY) {
        int cy = CONTENT_PAD + 56;
        int ox, oy, ow, oh;
        combo_rect(cx, cy, 400, 0, &ox, &oy, &ow, &oh);
        if (point_in_rect(x, y, ox, oy, ow, oh))
            show_popup(3);
        return;
    }

    if (section == SEC_KEYBOARD) {
        int cy = CONTENT_PAD + 56;
        int ox, oy, ow, oh;
        combo_rect(cx, cy, 400, 0, &ox, &oy, &ow, &oh);
        if (point_in_rect(x, y, ox, oy, ow, oh))
            show_popup(1);
        return;
    }

    if (section == SEC_MOUSE) {
        int cy = CONTENT_PAD + 56;
        int ox, oy, ow, oh;
        combo_rect(cx, cy, 400, 0, &ox, &oy, &ow, &oh);
        if (point_in_rect(x, y, ox, oy, ow, oh)) {
            pointer_sel = (pointer_sel + 1) % 3;
            apply_pointer_speed(pointer_sel);
            draw_all();
            return;
        }
        int ty = cy + ROW_GAP + COMBO_H;
        if (point_in_rect(x, y, cx + 320, ty, TOGGLE_W, TOGGLE_H)) {
            apply_natural_scroll(!natural_scroll);
            draw_all();
        }
        return;
    }

    if (section == SEC_TIME) {
        int cy = CONTENT_PAD + 56;
        int ox, oy, ow, oh;
        combo_rect(cx, cy, 400, 0, &ox, &oy, &ow, &oh);
        if (point_in_rect(x, y, ox, oy, ow, oh))
            show_popup(2);
    }
}

static void popup_scroll_by(int delta) {
    StrList *sl = popup_list();
    if (!sl)
        return;
    popup_scroll += delta;
    if (popup_scroll < 0)
        popup_scroll = 0;
    if (popup_scroll > sl->n - POPUP_VISIBLE)
        popup_scroll = sl->n - POPUP_VISIBLE;
    if (popup_scroll < 0)
        popup_scroll = 0;
    draw_popup();
}

static int popup_item_at(int py) {
    int row = py / POPUP_ITEM_H;
    if (row < 0 || row >= POPUP_VISIBLE)
        return -1;
    return popup_scroll + row;
}

static void setup_wm_delete(void) {
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
}

static int handle_client_message(const XClientMessageEvent *cm) {
    if (cm->message_type == wm_protocols && (Atom)cm->data.l[0] == wm_delete)
        return 1;
    return 0;
}

static void resize_window(int w, int h) {
    if (w < 800)
        w = 800;
    if (h < 600)
        h = 600;
    win_w = w;
    win_h = h;
    if (back_pm) {
        XFreePixmap(dpy, back_pm);
        back_pm = 0;
    }
    draw_all();
}

int main(void) {
    XSetWindowAttributes attr;
    char *cur_kb, *cur_tz, *cur_res;

    XSetErrorHandler(NULL);
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-settings: cannot open display\n");
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    init_config_paths();
    open_fonts();

    load_keyboard_languages();
    load_timezones();
    load_resolutions();

    cur_kb = current_keymap();
    cur_tz = current_timezone();
    cur_res = current_resolution();
    kb_sel = keyboard_index_for_keymap(cur_kb);
    tz_sel = strlist_index(&tz_list, cur_tz);
    res_sel = strlist_index(&res_list, cur_res);
    wallpaper_sel = current_wallpaper_index();
    pointer_sel = 1;
    natural_scroll = read_natural_scroll();
    free(cur_kb);
    free(cur_tz);
    free(cur_res);

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    win_w = ra.width > 0 ? ra.width : 1024;
    win_h = ra.height > 0 ? ra.height : 768;

    win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0,
        BlackPixel(dpy, screen), rgb(COL_SIDEBAR_R, COL_SIDEBAR_G, COL_SIDEBAR_B));
    XStoreName(dpy, win, "Settings");
    {
        XClassHint ch;
        ch.res_name = (char *)"br8-settings";
        ch.res_class = (char *)"Br8Settings";
        XSetClassHint(dpy, win, &ch);
    }
    br8_set_metro(dpy, win);
    setup_wm_delete();

    gc = XCreateGC(dpy, win, 0, NULL);
    XSelectInput(dpy, win,
        ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask |
        PointerMotionMask | LeaveWindowMask);

    popup = XCreateSimpleWindow(dpy, root, SIDEBAR_W + CONTENT_PAD, HEADER_H + 120,
        POPUP_W, POPUP_H, 1, rgb(255, 255, 255), rgb(COL_POPUP_R, COL_POPUP_G, COL_POPUP_B));
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
    XChangeWindowAttributes(dpy, popup, CWOverrideRedirect | CWEventMask, &attr);
    XUnmapWindow(dpy, popup);

    for (int i = 0; i < WALLPAPER_N; i++)
        load_thumb(i);

    draw_all();
    XFlush(dpy);
    XMapRaised(dpy, win);

    int running = 1;
    while (running) {
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

        if (ev.type == Expose && ev.xexpose.count == 0) {
            if (ev.xexpose.window == win)
                draw_all();
            else if (ev.xexpose.window == popup)
                draw_popup();
        } else if (ev.type == ConfigureNotify && ev.xconfigure.window == win) {
            resize_window(ev.xconfigure.width, ev.xconfigure.height);
        } else if (ev.type == ClientMessage && handle_client_message(&ev.xclient))
            running = 0;
        else if (ev.type == KeyPress && XLookupKeysym(&ev.xkey, 0) == XK_Escape)
            running = 0;
        else if (ev.type == MotionNotify && ev.xmotion.window == win) {
            int nh = nav_item_at((int)ev.xmotion.x, (int)ev.xmotion.y);
            if (nh != nav_hover) {
                nav_hover = nh;
                draw_all();
            }
        } else if (ev.type == LeaveNotify) {
            if (nav_hover != -1) {
                nav_hover = -1;
                draw_all();
            }
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == Button4 && ev.xbutton.window == popup)
                popup_scroll_by(-1);
            else if (ev.xbutton.button == Button5 && ev.xbutton.window == popup)
                popup_scroll_by(1);
            else if (ev.xbutton.window == popup) {
                StrList *sl = popup_list();
                int idx = popup_item_at((int)ev.xbutton.y);
                if (sl && idx >= 0 && idx < sl->n) {
                    set_popup_selected(idx);
                    if (popup_combo == 1)
                        on_kb_selected(idx);
                    else if (popup_combo == 2)
                        on_tz_selected(idx);
                    else if (popup_combo == 3)
                        on_res_selected(idx);
                    hide_popup();
                    draw_all();
                }
            } else if (ev.xbutton.window == win) {
                int ni = nav_item_at((int)ev.xbutton.x, (int)ev.xbutton.y);
                if (ni >= 0) {
                    hide_popup();
                    section = (Section)ni;
                    draw_all();
                } else if (popup_visible) {
                    hide_popup();
                    draw_all();
                } else {
                    handle_content_click((int)ev.xbutton.x, (int)ev.xbutton.y);
                }
            } else if (popup_visible) {
                hide_popup();
                draw_all();
            }
        }
    }

    hide_popup();
    for (int i = 0; i < WALLPAPER_N; i++) {
        if (thumb_pm[i])
            XFreePixmap(dpy, thumb_pm[i]);
    }
    if (back_pm)
        XFreePixmap(dpy, back_pm);
    strlist_free(&kb_list);
    strlist_free(&tz_list);
    strlist_free(&res_list);
    if (font_title)
        XftFontClose(dpy, font_title);
    if (font_nav)
        XftFontClose(dpy, font_nav);
    if (font_header)
        XftFontClose(dpy, font_header);
    if (font_body)
        XftFontClose(dpy, font_body);
    if (font_label)
        XftFontClose(dpy, font_label);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
