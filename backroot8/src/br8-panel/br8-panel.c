/*
 * Backroot 8 panel - transparent taskbar with open-app icons
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "emblem.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../br8-start/stb_image.h"

#define PANEL_H 40
#define ALPHA 0.82
#define EMBLEM_DISPLAY 28
#define BRAND_W (EMBLEM_DISPLAY + 16)
#define ICON_SZ 28
#define ICON_PAD 4
#define TASK_GAP 6
#define MAX_TASKS 48
#define MAX_DESKTOP_ICONS 256
#define ICON_MAX_DIM 512

typedef struct {
    char wm_class[128];
    char icon_name[128];
} DesktopIcon;

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
static Atom br8_frame, br8_client, br8_panel_rev, br8_activate, br8_start_open;
static Atom br8_metro_active;
static Atom net_wm_icon, net_wm_name, utf8_string;
static TaskBtn tasks[MAX_TASKS];
static int ntasks;
static unsigned long last_rev;
static int start_menu_open;
static Pixmap emblem_pm;
static int emblem_ready;
static DesktopIcon desktop_icons[MAX_DESKTOP_ICONS];
static int ndesktop_icons;

static Pixmap pixmap_from_rgba_scaled(const unsigned char *src, int sw, int sh, int dw, int dh);
static Pixmap icon_from_png_file(const char *path);
static Pixmap icon_from_file(const char *path);

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

static int ignore_xerror(Display *display, XErrorEvent *event) {
    (void)display;
    (void)event;
    return 0;
}

static int window_alive(Window w) {
    XWindowAttributes wa;
    return w && XGetWindowAttributes(dpy, w, &wa);
}

static int client_in_frame(Window frame, Window client) {
    Window root_ret, parent_ret;
    Window *kids = NULL;
    unsigned int nkids = 0;
    int found = 0;

    if (!XQueryTree(dpy, frame, &root_ret, &parent_ret, &kids, &nkids)) {
        if (kids)
            XFree(kids);
        return 0;
    }
    for (unsigned int i = 0; i < nkids; i++) {
        if (kids[i] == client) {
            found = 1;
            break;
        }
    }
    if (kids)
        XFree(kids);
    return found;
}

static void free_tasks(void) {
    for (int i = 0; i < ntasks; i++) {
        if (!tasks[i].icon)
            continue;
        int dup = 0;
        for (int j = 0; j < i; j++) {
            if (tasks[j].icon == tasks[i].icon) {
                dup = 1;
                break;
            }
        }
        if (!dup)
            XFreePixmap(dpy, tasks[i].icon);
        tasks[i].icon = 0;
    }
    ntasks = 0;
}

static void trim(char *s) {
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
    char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
}

static int parse_desktop_icon(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[512];
    char icon[128] = "";
    char startup[128] = "";
    int hidden = 0;
    int nodisplay = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Icon=", 5) == 0) {
            snprintf(icon, sizeof(icon), "%.*s", (int)(sizeof(icon) - 1), line + 5);
            trim(icon);
        } else if (strncmp(line, "StartupWMClass=", 17) == 0) {
            snprintf(startup, sizeof(startup), "%.*s", (int)(sizeof(startup) - 1), line + 17);
            trim(startup);
        } else if (strncmp(line, "Hidden=", 7) == 0) {
            hidden = (line[7] == 't' || line[7] == 'T' || line[7] == '1');
        } else if (strncmp(line, "NoDisplay=", 10) == 0) {
            nodisplay = (line[10] == 't' || line[10] == 'T' || line[10] == '1');
        }
    }
    fclose(f);
    if (hidden || nodisplay || !icon[0] || !startup[0] || ndesktop_icons >= MAX_DESKTOP_ICONS)
        return 0;
    DesktopIcon *d = &desktop_icons[ndesktop_icons++];
    snprintf(d->wm_class, sizeof(d->wm_class), "%s", startup);
    snprintf(d->icon_name, sizeof(d->icon_name), "%s", icon);
    return 1;
}

static void scan_desktop_icons(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *ent;
    char path[512];
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        size_t len = strlen(ent->d_name);
        if (len < 8 || strcmp(ent->d_name + len - 8, ".desktop") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        parse_desktop_icon(path);
    }
    closedir(d);
}

static void load_desktop_icon_map(void) {
    ndesktop_icons = 0;
    scan_desktop_icons("/usr/share/applications");
    scan_desktop_icons("/usr/local/share/applications");
}

static const char *icon_name_for_class(const char *res_class, const char *res_name) {
    for (int i = 0; i < ndesktop_icons; i++) {
        if (res_class && res_class[0] &&
                strcasecmp(desktop_icons[i].wm_class, res_class) == 0)
            return desktop_icons[i].icon_name;
        if (res_name && res_name[0] &&
                strcasecmp(desktop_icons[i].wm_class, res_name) == 0)
            return desktop_icons[i].icon_name;
    }
    return NULL;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int try_icon_path(const char *fmt, const char *name, char *out, size_t n) {
    snprintf(out, n, fmt, name);
    return file_exists(out);
}

static int resolve_icon_file(const char *icon_name, char *out, size_t n) {
    if (!icon_name || !icon_name[0])
        return 0;
    if (icon_name[0] == '/') {
        if (file_exists(icon_name)) {
            snprintf(out, n, "%s", icon_name);
            return 1;
        }
        return 0;
    }
    static const char *const paths[] = {
        "/usr/share/pixmaps/%s.png",
        "/usr/share/pixmaps/%s.xpm",
        "/usr/share/icons/hicolor/48x48/apps/%s.png",
        "/usr/share/icons/hicolor/32x32/apps/%s.png",
        "/usr/share/icons/hicolor/256x256/apps/%s.png",
        "/usr/share/icons/hicolor/scalable/apps/%s.svg",
        "/usr/share/icons/Adwaita/48x48/apps/%s.png",
        "/usr/share/icons/breeze/apps/48x48/%s.png",
        "/usr/share/icons/breeze/apps/32x32/%s.png",
        "/usr/share/icons/breeze/apps/48x48/%s.svg",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        if (try_icon_path(paths[i], icon_name, out, n))
            return 1;
    }
    return 0;
}

static int xpm_color(const char *spec, int *r, int *g, int *b, int *a) {
    if (!spec || strncmp(spec, "#", 1) != 0)
        return 0;
    unsigned long v = strtoul(spec + 1, NULL, 16);
    if (strlen(spec + 1) >= 6) {
        *r = (int)((v >> 16) & 0xff);
        *g = (int)((v >> 8) & 0xff);
        *b = (int)(v & 0xff);
        *a = 255;
        return 1;
    }
    return 0;
}

typedef struct {
    char key[8];
    int r, g, b, a;
} XpmColor;

static Pixmap icon_from_xpm_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[4096];
    int w = 0, h = 0, ncolors = 0, cpp = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "static char") || strstr(line, "char *"))
            continue;
        if (line[0] == '"') {
            char buf[64];
            if (sscanf(line, "\" %63[^\"]\"", buf) == 1 &&
                    sscanf(buf, "%d %d %d %d", &w, &h, &ncolors, &cpp) == 4)
                break;
        }
    }
    if (w <= 0 || h <= 0 || ncolors <= 0 || cpp <= 0 || cpp >= 8 || ncolors > 256) {
        fclose(f);
        return 0;
    }

    XpmColor colors[256];
    int got = 0;
    while (got < ncolors && fgets(line, sizeof(line), f)) {
        char *q1 = strchr(line, '"');
        if (!q1)
            continue;
        char *q2 = strchr(q1 + 1, '"');
        if (!q2 || (size_t)(q2 - q1 - 1) < (size_t)cpp)
            continue;
        memcpy(colors[got].key, q1 + 1, (size_t)cpp);
        colors[got].key[cpp] = '\0';
        char *c = strstr(line, "c ");
        if (!c)
            continue;
        c += 2;
        while (*c == ' ' || *c == '\t')
            c++;
        if (strncmp(c, "None", 4) == 0) {
            colors[got].r = colors[got].g = colors[got].b = 0;
            colors[got].a = 0;
        } else if (!xpm_color(c, &colors[got].r, &colors[got].g, &colors[got].b, &colors[got].a))
            continue;
        got++;
    }
    if (got < ncolors) {
        fclose(f);
        return 0;
    }

    unsigned char *rgba = calloc((size_t)w * (size_t)h, 4);
    if (!rgba) {
        fclose(f);
        return 0;
    }

    int row = 0;
    while (row < h && fgets(line, sizeof(line), f)) {
        char *q1 = strchr(line, '"');
        if (!q1)
            continue;
        char *q2 = strrchr(q1 + 1, '"');
        if (!q2)
            continue;
        const char *p = q1 + 1;
        if ((size_t)(q2 - p) < (size_t)(w * cpp))
            continue;
        for (int x = 0; x < w; x++) {
            char key[8];
            memcpy(key, p + (size_t)x * (size_t)cpp, (size_t)cpp);
            key[cpp] = '\0';
            int ci = 0;
            for (int k = 0; k < ncolors; k++) {
                if (memcmp(colors[k].key, key, (size_t)cpp) == 0) {
                    ci = k;
                    break;
                }
            }
            size_t off = (size_t)row * (size_t)w + (size_t)x;
            rgba[off * 4] = (unsigned char)colors[ci].r;
            rgba[off * 4 + 1] = (unsigned char)colors[ci].g;
            rgba[off * 4 + 2] = (unsigned char)colors[ci].b;
            rgba[off * 4 + 3] = (unsigned char)colors[ci].a;
        }
        row++;
    }
    fclose(f);
    if (row < h) {
        free(rgba);
        return 0;
    }

    Pixmap pm = pixmap_from_rgba_scaled(rgba, w, h, ICON_SZ, ICON_SZ);
    free(rgba);
    return pm;
}

static Pixmap icon_from_svg_file(const char *path) {
    char tmp[] = "/tmp/br8icXXXXXX.png";
    int fd = mkstemps(tmp, 4);
    if (fd < 0)
        return 0;
    close(fd);
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
        "rsvg-convert -w %d -h %d -o '%s' '%s' 2>/dev/null",
        ICON_SZ, ICON_SZ, tmp, path);
    if (system(cmd) != 0) {
        unlink(tmp);
        return 0;
    }
    Pixmap pm = icon_from_png_file(tmp);
    unlink(tmp);
    return pm;
}

static Pixmap icon_from_file(const char *path) {
    size_t len = strlen(path);
    if (len >= 4 && strcmp(path + len - 4, ".svg") == 0)
        return icon_from_svg_file(path);
    if (len >= 4 && strcmp(path + len - 4, ".xpm") == 0)
        return icon_from_xpm_file(path);
    return icon_from_png_file(path);
}

static Pixmap pixmap_from_rgba_scaled(const unsigned char *src, int sw, int sh,
        int dw, int dh) {
    int depth = DefaultDepth(dpy, screen);
    Pixmap pm = XCreatePixmap(dpy, panel, dw, dh, depth);
    XImage *xi = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, dw, dh, 32, 0);
    if (!xi) {
        XFreePixmap(dpy, pm);
        return 0;
    }
    xi->data = calloc((size_t)xi->bytes_per_line * (size_t)dh, 1);
    if (!xi->data) {
        XDestroyImage(xi);
        XFreePixmap(dpy, pm);
        return 0;
    }

    for (int dy = 0; dy < dh; dy++) {
        int sy = sh > 1 ? dy * sh / dh : 0;
        if (sy >= sh)
            sy = sh - 1;
        for (int dx = 0; dx < dw; dx++) {
            int sx = sw > 1 ? dx * sw / dw : 0;
            if (sx >= sw)
                sx = sw - 1;
            size_t idx = (size_t)(sy * sw + sx) * 4;
            int a = src[idx + 3];
            int r = src[idx];
            int g = src[idx + 1];
            int b = src[idx + 2];
            if (a < 255) {
                int bg = 45;
                r = (r * a + bg * (255 - a)) / 255;
                g = (g * a + bg * (255 - a)) / 255;
                b = (b * a + bg * (255 - a)) / 255;
            }
            XPutPixel(xi, dx, dy, rgb_pixel(r, g, b));
        }
    }

    GC pg = XCreateGC(dpy, pm, 0, NULL);
    XPutImage(dpy, pm, pg, xi, 0, 0, 0, 0, dw, dh);
    XFreeGC(dpy, pg);
    XDestroyImage(xi);
    return pm;
}

static Pixmap pixmap_from_argb_scaled(const unsigned long *src, int sw, int sh,
        int dw, int dh) {
    int depth = DefaultDepth(dpy, screen);
    Pixmap pm = XCreatePixmap(dpy, panel, dw, dh, depth);
    XImage *xi = XCreateImage(dpy, visual, depth, ZPixmap, 0, NULL, dw, dh, 32, 0);
    if (!xi) {
        XFreePixmap(dpy, pm);
        return 0;
    }
    xi->data = calloc((size_t)xi->bytes_per_line * (size_t)dh, 1);
    if (!xi->data) {
        XDestroyImage(xi);
        XFreePixmap(dpy, pm);
        return 0;
    }

    for (int dy = 0; dy < dh; dy++) {
        int sy = sh > 1 ? dy * sh / dh : 0;
        if (sy >= sh)
            sy = sh - 1;
        for (int dx = 0; dx < dw; dx++) {
            int sx = sw > 1 ? dx * sw / dw : 0;
            if (sx >= sw)
                sx = sw - 1;
            unsigned long argb = src[(size_t)sy * (size_t)sw + (size_t)sx];
            int a = (int)((argb >> 24) & 0xff);
            int r = (int)((argb >> 16) & 0xff);
            int g = (int)((argb >> 8) & 0xff);
            int b = (int)(argb & 0xff);
            if (a < 255) {
                int bg = 45;
                r = (r * a + bg * (255 - a)) / 255;
                g = (g * a + bg * (255 - a)) / 255;
                b = (b * a + bg * (255 - a)) / 255;
            }
            XPutPixel(xi, dx, dy, rgb_pixel(r, g, b));
        }
    }

    GC pg = XCreateGC(dpy, pm, 0, NULL);
    XPutImage(dpy, pm, pg, xi, 0, 0, 0, 0, dw, dh);
    XFreeGC(dpy, pg);
    XDestroyImage(xi);
    return pm;
}

static Pixmap icon_from_png_file(const char *path) {
    int iw = 0, ih = 0, comp = 0;
    unsigned char *data = stbi_load(path, &iw, &ih, &comp, 4);
    if (!data || iw <= 0 || ih <= 0)
        return 0;
    Pixmap pm = pixmap_from_rgba_scaled(data, iw, ih, ICON_SZ, ICON_SZ);
    stbi_image_free(data);
    return pm;
}

static Pixmap icon_from_desktop(Window client) {
    if (!window_alive(client))
        return 0;
    char res_class[128] = "";
    char res_name[128] = "";
    XClassHint hint;
    if (XGetClassHint(dpy, client, &hint)) {
        if (hint.res_class)
            strncpy(res_class, hint.res_class, sizeof(res_class) - 1);
        if (hint.res_name)
            strncpy(res_name, hint.res_name, sizeof(res_name) - 1);
        if (hint.res_name)
            XFree(hint.res_name);
        if (hint.res_class)
            XFree(hint.res_class);
    }

    const char *icon_name = icon_name_for_class(res_class, res_name);
    char path[512];
    if (icon_name && resolve_icon_file(icon_name, path, sizeof(path)))
        return icon_from_file(path);
    if (res_class[0] && resolve_icon_file(res_class, path, sizeof(path)))
        return icon_from_file(path);
    if (res_name[0] && resolve_icon_file(res_name, path, sizeof(path)))
        return icon_from_file(path);
    return 0;
}

static int icon_entry_fits(unsigned long off, int w, int h, unsigned long nitems) {
    if (w <= 0 || h <= 0 || w > ICON_MAX_DIM || h > ICON_MAX_DIM)
        return 0;
    unsigned long psz = (unsigned long)w * (unsigned long)h;
    unsigned long end = off + 2;
    if (end > nitems || psz > nitems - end)
        return 0;
    return 1;
}

static Pixmap icon_from_net_wm(Window client) {
    unsigned long *prop = NULL;
    Atom actual;
    int fmt;
    unsigned long nitems = 0, bytes = 0;

    if (!window_alive(client))
        return 0;

    if (XGetWindowProperty(dpy, client, net_wm_icon, 0, 1024 * 1024, False,
            XA_CARDINAL, &actual, &fmt, &nitems, &bytes,
            (unsigned char **)&prop) != Success || !prop || fmt != 32 || nitems < 2) {
        if (prop)
            XFree(prop);
        return 0;
    }

    int pick_w = 0, pick_h = 0;
    unsigned long pick_off = 0;
    int largest_w = 0, largest_h = 0;
    unsigned long largest_off = 0;

    for (unsigned long off = 0; off < nitems; ) {
        if (off + 1 >= nitems)
            break;
        int w = (int)prop[off];
        int h = (int)prop[off + 1];
        if (!icon_entry_fits(off, w, h, nitems))
            break;
        unsigned long psz = (unsigned long)w * (unsigned long)h;
        if (w >= ICON_SZ && h >= ICON_SZ) {
            if (!pick_w || w < pick_w || (w == pick_w && h < pick_h)) {
                pick_w = w;
                pick_h = h;
                pick_off = off;
            }
        }
        {
            unsigned long area = (unsigned long)w * (unsigned long)h;
            unsigned long best = (unsigned long)largest_w * (unsigned long)largest_h;
            if (area > best) {
                largest_w = w;
                largest_h = h;
                largest_off = off;
            }
        }
        off += 2 + psz;
    }

    if (!pick_w && largest_w > 0) {
        pick_w = largest_w;
        pick_h = largest_h;
        pick_off = largest_off;
    }
    if (!pick_w) {
        XFree(prop);
        return 0;
    }

    const unsigned long *pixels = prop + pick_off + 2;
    Pixmap pm = pixmap_from_argb_scaled(pixels, pick_w, pick_h, ICON_SZ, ICON_SZ);
    XFree(prop);
    return pm;
}

static Pixmap icon_fallback(Window client) {
    if (!window_alive(client))
        return 0;
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
    if (!pm)
        return 0;
    GC igc = XCreateGC(dpy, pm, 0, NULL);
    if (!igc) {
        XFreePixmap(dpy, pm);
        return 0;
    }
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
    if (!buf || n == 0)
        return;
    if (!window_alive(client)) {
        strncpy(buf, "App", n);
        return;
    }
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
        if (!window_alive(frame))
            continue;

        Window client = frame_client(frame);
        if (!client || !window_alive(client) || !client_in_frame(frame, client))
            continue;

        TaskBtn *t = &tasks[ntasks++];
        memset(t, 0, sizeof(*t));
        t->frame = frame;
        t->client = client;
        t->x = x;
        t->w = ICON_SZ + ICON_PAD * 2;
        x += t->w + TASK_GAP;

        get_client_label(client, t->label, sizeof(t->label));
        t->icon = icon_from_net_wm(client);
        if (!t->icon)
            t->icon = icon_from_desktop(client);
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

static void toggle_start_menu(void) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    unsigned long v = 1;
    if (XGetWindowProperty(dpy, root, br8_start_open, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        v = data[0] ? 0 : 1;
    if (data)
        XFree(data);
    XChangeProperty(dpy, root, br8_start_open, XA_CARDINAL, 32, PropModeReplace,
        (unsigned char *)&v, 1);
    XFlush(dpy);
}

static int emblem_clicked(int px) {
    return px >= 0 && px < BRAND_W + 8;
}

static TaskBtn *task_at(int px) {
    for (int i = 0; i < ntasks; i++)
        if (px >= tasks[i].x && px < tasks[i].x + tasks[i].w)
            return &tasks[i];
    return NULL;
}

static void activate_task(TaskBtn *t) {
    if (!t || !window_alive(t->frame))
        return;
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

static int read_start_open(void) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    int open = 0;
    if (XGetWindowProperty(dpy, root, br8_start_open, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        open = data[0] ? 1 : 0;
    if (data)
        XFree(data);
    return open;
}

static int read_metro_active(void) {
    Atom actual;
    int fmt;
    unsigned long n, bytes;
    unsigned long *data = NULL;
    int active = 0;
    if (XGetWindowProperty(dpy, root, br8_metro_active, 0, 8, False, XA_CARDINAL,
            &actual, &fmt, &n, &bytes, (unsigned char **)&data) == Success && data && n > 0)
        active = data[0] ? 1 : 0;
    if (data)
        XFree(data);
    return active;
}

static void sync_panel_visibility(void) {
    int hide = read_start_open() || read_metro_active();
    if (hide && !start_menu_open) {
        XUnmapWindow(dpy, panel);
        start_menu_open = 1;
    } else if (!hide && start_menu_open) {
        start_menu_open = 0;
        XMapRaised(dpy, panel);
        draw_panel();
    }
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

    XSetErrorHandler(ignore_xerror);

    br8_frame = XInternAtom(dpy, "_BR8_FRAME", False);
    br8_client = XInternAtom(dpy, "_BR8_CLIENT", False);
    br8_panel_rev = XInternAtom(dpy, "_BR8_PANEL_REV", False);
    br8_activate = XInternAtom(dpy, "_BR8_ACTIVATE", False);
    br8_start_open = XInternAtom(dpy, "_BR8_START_OPEN", False);
    br8_metro_active = XInternAtom(dpy, "_BR8_METRO_ACTIVE", False);
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
    load_desktop_icon_map();
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
            if (!start_menu_open)
                draw_panel();
        }
        sync_panel_visibility();

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0 && !start_menu_open)
                draw_panel();
            else if (ev.type == ConfigureNotify) {
                XGetWindowAttributes(dpy, root, &ra);
                XMoveResizeWindow(dpy, panel, 0, ra.height - PANEL_H, ra.width, PANEL_H);
                draw_panel();
            } else if (ev.type == ButtonPress && ev.xbutton.window == panel) {
                if (emblem_clicked(ev.xbutton.x))
                    toggle_start_menu();
                else {
                    TaskBtn *t = task_at(ev.xbutton.x);
                    if (t)
                        activate_task(t);
                }
            } else if (ev.type == PropertyNotify && ev.xproperty.window == root) {
                if (ev.xproperty.atom == br8_panel_rev) {
                    last_rev = read_panel_rev();
                    if (!start_menu_open)
                        draw_panel();
                } else if (ev.xproperty.atom == br8_start_open ||
                           ev.xproperty.atom == br8_metro_active) {
                    sync_panel_visibility();
                }
            }
        }
    }
    return 0;
}
