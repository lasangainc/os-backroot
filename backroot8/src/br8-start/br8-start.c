/*
 * Backroot 8 start menu - Windows 8-style Home / Apps launcher
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/select.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

#define PANEL_H 32
#define TILE 150
#define TILE_GAP 10
#define MARGIN 48
#define HEADER_H 72
#define APP_ROW_H 56
#define APP_ICON 48
#define APP_COL_W 240
#define APP_GAP 8
#define MAX_APPS 256
#define MAX_HOME_TILES 24
#define WALLPAPER "/usr/share/backgrounds/backroot8.jpg"
#define CONFIG_DIR "/root/.config/backroot8"
#define CONFIG_PATH CONFIG_DIR "/start_layout"
#define OPEN_SLIDE_X 100
#define OPEN_TILE_DUR_MS 170.0
#define OPEN_TILE_STAGGER_MS 48.0
#define HOME_HEADER_DELAY_MS 110.0
#define HOME_HEADER_SLIDE_MS 220.0
#define SMOOTH_TAU_MS 40.0
#define DRAG_THRESHOLD 6
#define DRAG_SCALE 0.88
#define TICK_MS 8
#define COLOR_CACHE_MAX 512
#define CTX_W 200
#define CTX_H 36

typedef enum {
    ACT_TERMINAL,
    ACT_DOLPHIN,
    ACT_HELLO,
    ACT_DESKTOP,
    ACT_EXEC
} Action;

typedef struct {
    char id[48];
    char label[64];
    int x, y, w, h;
    int layout_x, layout_y, layout_w, layout_h;
    int anim_x, anim_y, anim_w, anim_h;
    int cr, cg, cb;
    char letter;
    Action act;
    char exec_cmd[512];
    int is_wide;
    int pinned;
    unsigned long fill_pixel;
} Tile;

typedef struct {
    char name[128];
    char exec_cmd[512];
    int cr, cg, cb;
    char letter;
} AppEntry;

static Display *dpy;
static int screen;
static Window root, start_win;
static GC gc;
static Visual *visual;
static Colormap cmap;
static XftFont *ui_font, *header_font;
static int win_w, win_h, root_h;
static Atom br8_start_open;
static int visible;
static int scroll_y;
static int content_h;
static int home_h;
static int apps_cols;
static Pixmap wallpaper_pm;
static int wallpaper_ready;
static Tile home_tiles[MAX_HOME_TILES];
static int n_home_tiles;
static int tile_order[MAX_HOME_TILES];
static int n_tile_order;
static AppEntry apps[MAX_APPS];
static int n_apps;

static double open_anim;
static struct timespec open_anim_start;
static int open_start_x[MAX_HOME_TILES];
static int open_stagger_rank[MAX_HOME_TILES];
static int home_header_x;
static int layout_animating;
static struct timespec last_tick;

static Pixmap buf_pm;
static Pixmap static_pm;
static int buf_w, buf_h;
static int static_valid;
static int static_scroll;
static GC buf_gc;

static struct {
    int r, g, b;
    unsigned long pix;
} color_cache[COLOR_CACHE_MAX];
static int n_color_cache;
static XftDraw *xft_dc;
static XftColor xft_white;
static int xft_white_ok;
static unsigned long pix_bg, pix_line, pix_ctx_bg, pix_ctx_border;

static int frame_dirty;

static int dragging;
static int drag_tile_idx;
static int drag_pointer_x, drag_pointer_y;
static int drag_offset_x, drag_offset_y;
static int drag_hover_slot;
static int btn1_down;
static int btn1_tile_idx;
static int btn1_x, btn1_y;

static int ctx_visible;
static int ctx_x, ctx_y;
static int ctx_app_idx;
static int ctx_is_tile;

static void draw_all(void);
static void mark_dirty(void);
static void layout_home(void);
static void save_layout(void);
static void load_layout(void);
static void invalidate_static(void);
static void free_buffers(void);
static void ensure_buffers(void);
static void rebuild_static_layer(void);
static void present_buffer(void);
static XRenderColor render_rgb(int r, int g, int b);
static unsigned long rgb(int r, int g, int b);

static XRenderColor render_rgb(int r, int g, int b) {
    XRenderColor c;
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    c.alpha = 0xffff;
    return c;
}

static unsigned long rgb(int r, int g, int b) {
    for (int i = 0; i < n_color_cache; i++) {
        if (color_cache[i].r == r && color_cache[i].g == g && color_cache[i].b == b)
            return color_cache[i].pix;
    }
    XColor c;
    c.red = (unsigned short)(r << 8);
    c.green = (unsigned short)(g << 8);
    c.blue = (unsigned short)(b << 8);
    c.flags = DoRed | DoGreen | DoBlue;
    unsigned long pix = BlackPixel(dpy, screen);
    if (XAllocColor(dpy, cmap, &c))
        pix = c.pixel;
    if (n_color_cache < COLOR_CACHE_MAX) {
        color_cache[n_color_cache].r = r;
        color_cache[n_color_cache].g = g;
        color_cache[n_color_cache].b = b;
        color_cache[n_color_cache].pix = pix;
        n_color_cache++;
    }
    return pix;
}

static void init_palette(void) {
    pix_bg = rgb(30, 46, 76);
    pix_line = rgb(100, 170, 220);
    pix_ctx_bg = rgb(45, 55, 72);
    pix_ctx_border = rgb(120, 140, 170);
    XRenderColor rc = render_rgb(255, 255, 255);
    xft_white_ok = XftColorAllocValue(dpy, visual, cmap, &rc, &xft_white);
}

static void mark_dirty(void) {
    frame_dirty = 1;
}

static void invalidate_static(void) {
    static_valid = 0;
}

static void free_buffers(void) {
    if (buf_pm) {
        XFreePixmap(dpy, buf_pm);
        buf_pm = 0;
    }
    if (static_pm) {
        XFreePixmap(dpy, static_pm);
        static_pm = 0;
    }
    if (buf_gc) {
        XFreeGC(dpy, buf_gc);
        buf_gc = 0;
    }
    buf_w = buf_h = 0;
    static_valid = 0;
}

static void ensure_buffers(void) {
    if (win_w < 1 || win_h < 1)
        return;
    if (buf_pm && buf_w == win_w && buf_h == win_h)
        return;
    free_buffers();
    int depth = DefaultDepth(dpy, screen);
    buf_pm = XCreatePixmap(dpy, start_win, win_w, win_h, depth);
    static_pm = XCreatePixmap(dpy, start_win, win_w, win_h, depth);
    buf_gc = XCreateGC(dpy, buf_pm, 0, NULL);
    buf_w = win_w;
    buf_h = win_h;
    static_valid = 0;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static double ease_out_cubic(double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    double u = 1.0 - t;
    return 1.0 - u * u * u;
}

static double open_timeline_ms(void) {
    int n = n_tile_order > 0 ? n_tile_order : 1;
    double tiles = (n - 1) * OPEN_TILE_STAGGER_MS + OPEN_TILE_DUR_MS;
    return tiles + HOME_HEADER_DELAY_MS + HOME_HEADER_SLIDE_MS;
}

static void build_open_stagger(void) {
    int order[MAX_HOME_TILES];
    for (int s = 0; s < n_tile_order; s++)
        order[s] = tile_order[s];
    for (int i = 0; i < n_tile_order - 1; i++) {
        for (int j = i + 1; j < n_tile_order; j++) {
            Tile *a = &home_tiles[order[i]];
            Tile *b = &home_tiles[order[j]];
            int ra = a->layout_x + a->layout_w;
            int rb = b->layout_x + b->layout_w;
            if (rb > ra || (rb == ra && b->layout_y < a->layout_y)) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }
    for (int i = 0; i < n_home_tiles; i++)
        open_stagger_rank[i] = n_tile_order;
    for (int rank = 0; rank < n_tile_order; rank++)
        open_stagger_rank[order[rank]] = rank;
}

static int lerp_i(int a, int b, double t) {
    return (int)(a + (b - a) * t + 0.5);
}

static int text_baseline(int height, XftFont *font) {
    if (!font)
        return height - 8;
    return (height + font->ascent - font->descent) / 2;
}

static Drawable draw_dst;

static void xft_draw(Drawable draw, XftFont *font, int x, int y,
        const char *text, int r, int g, int b) {
    if (!font || !text || !text[0])
        return;
    if (!xft_dc || draw != draw_dst) {
        if (xft_dc)
            XftDrawDestroy(xft_dc);
        xft_dc = XftDrawCreate(dpy, draw, visual, cmap);
        draw_dst = draw;
    }
    XftColor col;
    const XftColor *use = &xft_white;
    if (r != 255 || g != 255 || b != 255) {
        XRenderColor rc = render_rgb(r, g, b);
        if (!XftColorAllocValue(dpy, visual, cmap, &rc, &col))
            return;
        use = &col;
    } else if (!xft_white_ok) {
        XRenderColor rc = render_rgb(255, 255, 255);
        if (!XftColorAllocValue(dpy, visual, cmap, &rc, &col))
            return;
        use = &col;
    }
    XftDrawStringUtf8(xft_dc, use, font, x, y,
        (FcChar8 *)text, (int)strlen(text));
    if (use != &xft_white)
        XftColorFree(dpy, visual, cmap, &col);
}

static void app_color(AppEntry *a) {
    unsigned h = 0;
    for (const char *p = a->name; p && *p; p++)
        h = h * 31 + (unsigned char)*p;
    a->cr = 60 + (h & 0x7f);
    a->cg = 60 + ((h >> 8) & 0x7f);
    a->cb = 60 + ((h >> 16) & 0x7f);
    a->letter = a->name[0] ? a->name[0] : '?';
    if (a->letter >= 'a' && a->letter <= 'z')
        a->letter = (char)(a->letter - 'a' + 'A');
}

static int read_open(void) {
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

static void set_open(int open) {
    unsigned long v = open ? 1 : 0;
    XChangeProperty(dpy, root, br8_start_open, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&v, 1);
    XFlush(dpy);
}

static void hide_menu(void) {
    if (!visible)
        return;
    dragging = 0;
    drag_tile_idx = -1;
    ctx_visible = 0;
    open_anim = 0.0;
    layout_animating = 0;
    frame_dirty = 0;
    XUnmapWindow(dpy, start_win);
    visible = 0;
    scroll_y = 0;
    invalidate_static();
    set_open(0);
}

static void begin_open_animation(void) {
    open_anim = 0.0;
    home_header_x = -OPEN_SLIDE_X;
    clock_gettime(CLOCK_MONOTONIC, &open_anim_start);
    clock_gettime(CLOCK_MONOTONIC, &last_tick);
    layout_home();
    build_open_stagger();
    for (int i = 0; i < n_home_tiles; i++) {
        Tile *t = &home_tiles[i];
        open_start_x[i] = t->layout_x + OPEN_SLIDE_X;
        t->anim_x = open_start_x[i];
        t->anim_y = t->layout_y;
        t->anim_w = t->layout_w;
        t->anim_h = t->layout_h;
    }
    layout_animating = 0;
    invalidate_static();
    mark_dirty();
}

static void show_menu(void) {
    if (visible)
        return;
    scroll_y = 0;
    visible = 1;
    ctx_visible = 0;
    XMapRaised(dpy, start_win);
    begin_open_animation();
    draw_all();
}

static void spawn_terminal(void) {
    pid_t pid = fork();
    if (pid != 0)
        return;
    if (chdir("/") != 0)
        _exit(1);
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    setenv("TERM", "xterm-256color", 1);
    execl("/usr/bin/xterm", "xterm",
        "-fa", "Monospace", "-fs", "11",
        "-bg", "#1a1a22", "-fg", "#e8e8ec",
        "-title", "root@Backroot8",
        "-e", "/bin/bash", "-l",
        NULL);
    execl("/usr/bin/xfce4-terminal", "xfce4-terminal",
        "--working-directory=/", "--title=root@Backroot8", NULL);
    _exit(1);
}

static void spawn_dolphin(void) {
    pid_t pid = fork();
    if (pid != 0)
        return;
    if (chdir("/") != 0)
        _exit(1);
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/usr/bin/dolphin", "dolphin", "/", NULL);
    _exit(1);
}

static void spawn_hello(void) {
    pid_t pid = fork();
    if (pid != 0)
        return;
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/usr/local/bin/backroot-hello", "backroot-hello", NULL);
    _exit(1);
}

static void spawn_exec(const char *cmd) {
    if (!cmd || !cmd[0])
        return;
    pid_t pid = fork();
    if (pid != 0)
        return;
    setenv("HOME", "/root", 1);
    setenv("DISPLAY", ":0", 1);
    execl("/bin/sh", "sh", "-c", cmd, NULL);
    _exit(1);
}

static void launch_action(Action act, const char *exec_cmd) {
    hide_menu();
    switch (act) {
    case ACT_TERMINAL: spawn_terminal(); break;
    case ACT_DOLPHIN: spawn_dolphin(); break;
    case ACT_HELLO: spawn_hello(); break;
    case ACT_DESKTOP: break;
    case ACT_EXEC: spawn_exec(exec_cmd); break;
    }
}

static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

static void strip_exec_field_codes(char *cmd) {
    char out[512];
    size_t o = 0;
    for (size_t i = 0; cmd[i] && o + 1 < sizeof(out); i++) {
        if (cmd[i] == '%') {
            char c = cmd[i + 1];
            if (c && strchr("fFuUdDnNickvm", c))
                i++;
            continue;
        }
        out[o++] = cmd[i];
    }
    out[o] = '\0';
    trim(out);
    snprintf(cmd, 512, "%s", out);
}

static int parse_desktop(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[512];
    char name[128] = "";
    char exec[512] = "";
    int hidden = 0;
    int nodisplay = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Name=", 5) == 0) {
            snprintf(name, sizeof(name), "%.*s",
                (int)(sizeof(name) - 1), line + 5);
            trim(name);
        } else if (strncmp(line, "Exec=", 5) == 0) {
            snprintf(exec, sizeof(exec), "%s", line + 5);
            trim(exec);
        } else if (strncmp(line, "Hidden=", 7) == 0) {
            hidden = (line[7] == 't' || line[7] == 'T' || line[7] == '1');
        } else if (strncmp(line, "NoDisplay=", 10) == 0) {
            nodisplay = (line[10] == 't' || line[10] == 'T' || line[10] == '1');
        }
    }
    fclose(f);
    if (hidden || nodisplay || !name[0] || !exec[0])
        return 0;
    strip_exec_field_codes(exec);
    if (!exec[0])
        return 0;
    for (int i = 0; i < n_apps; i++) {
        if (strcmp(apps[i].name, name) == 0)
            return 0;
    }
    if (n_apps >= MAX_APPS)
        return 0;
    AppEntry *a = &apps[n_apps++];
    snprintf(a->name, sizeof(a->name), "%s", name);
    snprintf(a->exec_cmd, sizeof(a->exec_cmd), "%s", exec);
    app_color(a);
    return 1;
}

static int cmp_app(const void *a, const void *b) {
    return strcasecmp(((const AppEntry *)a)->name, ((const AppEntry *)b)->name);
}

static void scan_desktop_dir(const char *dir) {
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
        parse_desktop(path);
    }
    closedir(d);
}

static void load_apps(void) {
    n_apps = 0;
    scan_desktop_dir("/usr/share/applications");
    scan_desktop_dir("/usr/local/share/applications");
    qsort(apps, (size_t)n_apps, sizeof(AppEntry), cmp_app);
}

static int tile_index_by_id(const char *id) {
    for (int i = 0; i < n_home_tiles; i++) {
        if (strcmp(home_tiles[i].id, id) == 0)
            return i;
    }
    return -1;
}

static void add_tile(const char *id, const char *label,
        int cr, int cg, int cb, char letter, Action act,
        const char *exec, int is_wide, int pinned) {
    if (n_home_tiles >= MAX_HOME_TILES)
        return;
    Tile *t = &home_tiles[n_home_tiles++];
    snprintf(t->id, sizeof(t->id), "%s", id);
    strncpy(t->label, label, sizeof(t->label) - 1);
    t->cr = cr;
    t->cg = cg;
    t->cb = cb;
    t->letter = letter;
    t->act = act;
    t->is_wide = is_wide;
    t->pinned = pinned;
    t->fill_pixel = rgb(cr, cg, cb);
    if (exec)
        strncpy(t->exec_cmd, exec, sizeof(t->exec_cmd) - 1);
    else
        t->exec_cmd[0] = '\0';
}

static void init_default_tiles(void) {
    n_home_tiles = 0;
    add_tile("dolphin", "Dolphin", 0, 120, 215, 'D', ACT_DOLPHIN, NULL, 0, 0);
    add_tile("terminal", "Terminal", 16, 124, 16, 'T', ACT_TERMINAL, NULL, 0, 0);
    add_tile("hello", "Backroot Hello", 92, 45, 145, 'B', ACT_HELLO, NULL, 0, 0);
    add_tile("desktop", "Desktop", 0, 0, 0, ' ', ACT_DESKTOP, NULL, 1, 0);
}

static void reset_tile_order(void) {
    n_tile_order = n_home_tiles;
    for (int i = 0; i < n_home_tiles; i++)
        tile_order[i] = i;
}

static void ensure_config_dir(void) {
    mkdir("/root/.config", 0755);
    mkdir(CONFIG_DIR, 0755);
}

static void save_layout(void) {
    ensure_config_dir();
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f)
        return;
    fprintf(f, "order=");
    for (int i = 0; i < n_tile_order; i++) {
        if (i > 0)
            fputc(',', f);
        fputs(home_tiles[tile_order[i]].id, f);
    }
    fputc('\n', f);
    fclose(f);
}

static void apply_order_list(const char *list) {
    char buf[1024];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    n_tile_order = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        trim(tok);
        if (!tok[0])
            continue;
        int idx = tile_index_by_id(tok);
        if (idx < 0)
            continue;
        int used = 0;
        for (int i = 0; i < n_tile_order; i++) {
            if (tile_order[i] == idx) {
                used = 1;
                break;
            }
        }
        if (used)
            continue;
        tile_order[n_tile_order++] = idx;
    }
    for (int i = 0; i < n_home_tiles; i++) {
        int found = 0;
        for (int j = 0; j < n_tile_order; j++) {
            if (tile_order[j] == i) {
                found = 1;
                break;
            }
        }
        if (!found && n_tile_order < MAX_HOME_TILES)
            tile_order[n_tile_order++] = i;
    }
}

static void load_layout(void) {
    init_default_tiles();
    reset_tile_order();
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f)
        return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "order=", 6) == 0) {
            char *val = line + 6;
            size_t n = strlen(val);
            while (n > 0 && (val[n - 1] == '\n' || val[n - 1] == '\r'))
                val[--n] = '\0';
            apply_order_list(val);
        }
    }
    fclose(f);
}

static int home_tile_top(void) {
    if (header_font)
        return MARGIN + header_font->ascent + header_font->descent + 24;
    return HEADER_H;
}

typedef struct {
    int cols;
    int cell_w;
    int cell_h;
    int wide_w;
    int x0;
    int y0;
} TileGrid;

static void grid_metrics(TileGrid *gr) {
    gr->y0 = home_tile_top();
    gr->x0 = MARGIN;
    gr->cell_h = TILE;
    int usable = win_w - 2 * MARGIN;
    if (usable < TILE * 2 + TILE_GAP)
        usable = TILE * 2 + TILE_GAP;
    gr->cols = (usable + TILE_GAP) / (TILE + TILE_GAP);
    if (gr->cols < 2)
        gr->cols = 2;
    gr->cell_w = (usable - (gr->cols - 1) * TILE_GAP) / gr->cols;
    if (gr->cell_w < 48)
        gr->cell_w = TILE;
    gr->wide_w = gr->cell_w * 2 + TILE_GAP;
    if (gr->wide_w > usable)
        gr->wide_w = usable;
}

static void compute_tile_layout(const int *order, int n_order) {
    TileGrid gr;
    grid_metrics(&gr);
    int col = 0;
    int y = gr.y0;

    for (int s = 0; s < n_order; s++) {
        Tile *t = &home_tiles[order[s]];
        if (t->is_wide) {
            if (col > 0) {
                y += gr.cell_h + TILE_GAP;
                col = 0;
            }
            t->layout_x = gr.x0;
            t->layout_y = y;
            t->layout_w = gr.wide_w;
            t->layout_h = gr.cell_h;
            y += gr.cell_h + TILE_GAP;
            col = 0;
        } else {
            if (col >= gr.cols) {
                y += gr.cell_h + TILE_GAP;
                col = 0;
            }
            t->layout_x = gr.x0 + col * (gr.cell_w + TILE_GAP);
            t->layout_y = y;
            t->layout_w = gr.cell_w;
            t->layout_h = gr.cell_h;
            col++;
            if (col >= gr.cols) {
                y += gr.cell_h + TILE_GAP;
                col = 0;
            }
        }
        t->x = t->layout_x;
        t->y = t->layout_y;
        t->w = t->layout_w;
        t->h = t->layout_h;
    }
    if (col > 0)
        y += gr.cell_h + TILE_GAP;
    home_h = y;
}

static void layout_home(void) {
    compute_tile_layout(tile_order, n_tile_order);
}

static int apps_section_h(void) {
    if (n_apps == 0)
        return HEADER_H + 40;
    apps_cols = (win_w - MARGIN * 2) / APP_COL_W;
    if (apps_cols < 1)
        apps_cols = 1;
    int rows = (n_apps + apps_cols - 1) / apps_cols;
    return HEADER_H + rows * (APP_ROW_H + APP_GAP) + MARGIN;
}

static void layout_content(void) {
    layout_home();
    content_h = home_h + apps_section_h();
    int max_scroll = content_h - win_h;
    if (max_scroll < 0)
        max_scroll = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    if (scroll_y < 0)
        scroll_y = 0;
}

static int animating_now(void) {
    if (open_anim < 1.0)
        return 1;
    if (layout_animating)
        return 1;
    if (dragging)
        return 1;
    if (frame_dirty)
        return 1;
    return 0;
}

static int smooth_step(int cur, int target, double k) {
    if (cur == target)
        return cur;
    return cur + (int)((target - cur) * k + (target > cur ? 0.5 : -0.5));
}

static void smooth_tiles(double dt_ms) {
    double k = 1.0 - exp(-dt_ms / SMOOTH_TAU_MS);
    if (k < 0.08)
        k = 0.08;
    if (k > 1.0)
        k = 1.0;
    int moving = 0;
    for (int i = 0; i < n_home_tiles; i++) {
        Tile *t = &home_tiles[i];
        if (dragging && i == drag_tile_idx)
            continue;
        int nx = smooth_step(t->anim_x, t->layout_x, k);
        int ny = smooth_step(t->anim_y, t->layout_y, k);
        int nw = smooth_step(t->anim_w, t->layout_w, k);
        int nh = smooth_step(t->anim_h, t->layout_h, k);
        if (nx != t->layout_x || ny != t->layout_y ||
            nw != t->layout_w || nh != t->layout_h)
            moving = 1;
        t->anim_x = nx;
        t->anim_y = ny;
        t->anim_w = nw;
        t->anim_h = nh;
    }
    if (!moving)
        layout_animating = 0;
    else
        layout_animating = 1;
}

static void tick_animations(void) {
    if (!visible)
        return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double dt = (now.tv_sec - last_tick.tv_sec) * 1000.0 +
        (now.tv_nsec - last_tick.tv_nsec) / 1000000.0;
    if (dt < 0.0)
        dt = 0.0;
    if (dt > 50.0)
        dt = 50.0;
    last_tick = now;

    if (open_anim < 1.0) {
        double elapsed = now_ms() - (open_anim_start.tv_sec * 1000.0 +
            open_anim_start.tv_nsec / 1000000.0);
        double total = open_timeline_ms();
        open_anim = elapsed / total;
        if (open_anim > 1.0)
            open_anim = 1.0;

        for (int i = 0; i < n_home_tiles; i++) {
            Tile *t = &home_tiles[i];
            int rank = open_stagger_rank[i];
            if (rank >= n_tile_order)
                rank = n_tile_order - 1;
            double tile_el = elapsed - rank * OPEN_TILE_STAGGER_MS;
            double te = tile_el / OPEN_TILE_DUR_MS;
            if (te < 0.0)
                te = 0.0;
            if (te > 1.0)
                te = 1.0;
            double e = ease_out_cubic(te);
            t->anim_x = lerp_i(open_start_x[i], t->layout_x, e);
            t->anim_y = t->layout_y;
            t->anim_w = lerp_i((int)(t->layout_w * 0.88), t->layout_w, e);
            t->anim_h = lerp_i((int)(t->layout_h * 0.88), t->layout_h, e);
        }

        double header_el = elapsed - HOME_HEADER_DELAY_MS;
        if (header_el < 0.0)
            home_header_x = -OPEN_SLIDE_X - 40;
        else {
            double he = header_el / HOME_HEADER_SLIDE_MS;
            if (he > 1.0)
                he = 1.0;
            home_header_x = lerp_i(-OPEN_SLIDE_X, MARGIN, ease_out_cubic(he));
        }
        mark_dirty();
    } else if (layout_animating) {
        smooth_tiles(dt);
        mark_dirty();
    }

    if (frame_dirty) {
        draw_all();
        frame_dirty = 0;
    }
}

static void start_layout_animation(void) {
    layout_animating = 1;
    mark_dirty();
}

static int slot_at_content_xy_order(const int *order, int n_order, int cx, int cy) {
    TileGrid gr;
    grid_metrics(&gr);
    int col = 0;
    int y = gr.y0;
    int slot = 0;
    int last_slot = 0;

    for (int s = 0; s < n_order; s++) {
        Tile *t = &home_tiles[order[s]];
        int tx, ty, tw, th;
        if (t->is_wide) {
            if (col > 0) {
                y += gr.cell_h + TILE_GAP;
                col = 0;
            }
            tx = gr.x0;
            ty = y;
            tw = gr.wide_w;
            th = gr.cell_h;
            y += gr.cell_h + TILE_GAP;
            col = 0;
        } else {
            if (col >= gr.cols) {
                y += gr.cell_h + TILE_GAP;
                col = 0;
            }
            tx = gr.x0 + col * (gr.cell_w + TILE_GAP);
            ty = y;
            tw = gr.cell_w;
            th = gr.cell_h;
            col++;
            if (col >= gr.cols) {
                y += gr.cell_h + TILE_GAP;
                col = 0;
            }
        }
        last_slot = slot;
        if (cx >= tx && cx < tx + tw && cy >= ty && cy < ty + th)
            return slot;
        slot++;
    }
    if (slot > 0)
        return last_slot;
    return 0;
}

static int drag_from_slot(void) {
    for (int i = 0; i < n_tile_order; i++) {
        if (tile_order[i] == drag_tile_idx)
            return i;
    }
    return -1;
}

static int compact_slot_to_order(int compact_slot, int from_slot, int n) {
    int idx = compact_slot;
    if (idx >= from_slot)
        idx++;
    if (idx < 0)
        idx = 0;
    if (idx >= n)
        idx = n - 1;
    return idx;
}

static int slot_at_content_xy(int cx, int cy) {
    if (dragging && drag_tile_idx >= 0) {
        int from_slot = drag_from_slot();
        if (from_slot < 0)
            return 0;
        int compact[MAX_HOME_TILES];
        int n = 0;
        for (int i = 0; i < n_tile_order; i++) {
            if (tile_order[i] != drag_tile_idx)
                compact[n++] = tile_order[i];
        }
        int hit = slot_at_content_xy_order(compact, n, cx, cy);
        if (hit < 0)
            hit = 0;
        if (hit >= n)
            hit = n > 0 ? n - 1 : 0;
        return compact_slot_to_order(hit, from_slot, n_tile_order);
    }
    int hit = slot_at_content_xy_order(tile_order, n_tile_order, cx, cy);
    if (hit < 0)
        return 0;
    if (hit >= n_tile_order)
        return n_tile_order - 1;
    return hit;
}

static void move_slot(int *order, int n, int from, int to) {
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    int val = order[from];
    if (from < to) {
        for (int i = from; i < to; i++)
            order[i] = order[i + 1];
    } else {
        for (int i = from; i > to; i--)
            order[i] = order[i - 1];
    }
    order[to] = val;
}

static void reorder_tile(int from_slot, int to_slot) {
    if (from_slot < 0 || from_slot >= n_tile_order ||
        to_slot < 0 || to_slot >= n_tile_order ||
        from_slot == to_slot)
        return;
    move_slot(tile_order, n_tile_order, from_slot, to_slot);
    layout_home();
    invalidate_static();
    start_layout_animation();
    save_layout();
}

static void preview_order_with_drag(int hover_slot) {
    if (drag_tile_idx < 0 || hover_slot < 0 || hover_slot >= n_tile_order)
        return;
    int from_slot = -1;
    for (int i = 0; i < n_tile_order; i++) {
        if (tile_order[i] == drag_tile_idx) {
            from_slot = i;
            break;
        }
    }
    if (from_slot < 0 || from_slot == hover_slot) {
        layout_home();
        return;
    }
    int preview[MAX_HOME_TILES];
    memcpy(preview, tile_order, (size_t)n_tile_order * sizeof(int));
    move_slot(preview, n_tile_order, from_slot, hover_slot);
    compute_tile_layout(preview, n_tile_order);
}

static Pixmap scale_image_to_pixmap(unsigned char *src, int sw, int sh, int dw, int dh) {
    int depth = DefaultDepth(dpy, screen);
    Pixmap pm = XCreatePixmap(dpy, start_win, dw, dh, depth);
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
    for (int dy = 0; dy < dh; dy++) {
        int sy = sh > 0 ? dy * sh / dh : 0;
        for (int dx = 0; dx < dw; dx++) {
            int sx = sw > 0 ? dx * sw / dw : 0;
            size_t idx = (size_t)(sy * sw + sx) * 3;
            unsigned long pix = rgb(src[idx], src[idx + 1], src[idx + 2]);
            XPutPixel(xi, dx, dy, pix);
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
    int iw = 0, ih = 0, comp = 0;
    unsigned char *data = stbi_load(WALLPAPER, &iw, &ih, &comp, 3);
    if (!data || iw <= 0 || ih <= 0)
        return;
    TileGrid gr;
    grid_metrics(&gr);
    int tw = gr.wide_w > 0 ? gr.wide_w : (TILE * 2 + TILE_GAP);
    int th = gr.cell_h;
    wallpaper_pm = scale_image_to_pixmap(data, iw, ih, tw, th);
    stbi_image_free(data);
    wallpaper_ready = wallpaper_pm != 0;
}

static void draw_bg(Drawable dst, GC g) {
    XSetForeground(dpy, g, pix_bg);
    XFillRectangle(dpy, dst, g, 0, 0, win_w, win_h);
}

static void draw_tile_glyph(Drawable dst, GC g, const Tile *t, int sx, int sy, int sw, int sh) {
    if (sw < 1 || sh < 1)
        return;
    if (t->act == ACT_DESKTOP && wallpaper_ready) {
        int pw = t->w > 0 ? t->w : sw;
        int ph = t->h > 0 ? t->h : sh;
        int cw = sw < pw ? sw : pw;
        int ch = sh < ph ? sh : ph;
        XSetForeground(dpy, g, pix_bg);
        XFillRectangle(dpy, dst, g, sx, sy, sw, sh);
        XCopyArea(dpy, wallpaper_pm, dst, g, 0, 0, cw, ch, sx, sy);
        return;
    }
    XSetForeground(dpy, g, t->fill_pixel);
    XFillRectangle(dpy, dst, g, sx, sy, sw, sh);

    if (t->act == ACT_TERMINAL) {
        xft_draw(dst, ui_font, sx + sw / 2 - 18, sy + sh / 2 + 6,
            ">_", 255, 255, 255);
    } else if (t->letter) {
        char s[2] = { t->letter, 0 };
        if (ui_font) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)s, 1, &ext);
            xft_draw(dst, ui_font,
                sx + (sw - ext.xOff) / 2,
                sy + text_baseline(sh, ui_font),
                s, 255, 255, 255);
        }
    }
}

static void draw_tile_label(Drawable dst, const Tile *t, int sx, int sy, int sh) {
    xft_draw(dst, ui_font, sx + 8, sy + sh - 10, t->label, 255, 255, 255);
}

static void tile_screen_rect(const Tile *t, int ti, int dragging_now,
        int *sx, int *sy, int *sw, int *sh) {
    int ax, ay, aw, ah;
    if (dragging_now && ti == drag_tile_idx) {
        aw = (int)(t->layout_w * DRAG_SCALE);
        ah = (int)(t->layout_h * DRAG_SCALE);
        if (aw < 24)
            aw = 24;
        if (ah < 24)
            ah = 24;
        ax = drag_pointer_x - drag_offset_x - aw / 2;
        ay = drag_pointer_y - drag_offset_y - ah / 2;
    } else {
        ax = t->anim_x;
        ay = t->anim_y;
        aw = t->anim_w;
        ah = t->anim_h;
        if (aw < 1)
            aw = t->layout_w > 0 ? t->layout_w : TILE;
        if (ah < 1)
            ah = t->layout_h > 0 ? t->layout_h : TILE;
    }
    *sx = ax;
    *sy = ay - scroll_y;
    *sw = aw;
    *sh = ah;
}

static void clear_home_region(Drawable dst, GC g) {
    XSetForeground(dpy, g, pix_bg);
    int top = 0;
    int bot = home_h - scroll_y + TILE_GAP;
    for (int s = 0; s < n_tile_order; s++) {
        int ti = tile_order[s];
        const Tile *t = &home_tiles[ti];
        int sx, sy, sw, sh;
        tile_screen_rect(t, ti, dragging, &sx, &sy, &sw, &sh);
        int b = sy + sh + TILE_GAP;
        if (b > bot)
            bot = b;
    }
    if (bot > win_h)
        bot = win_h;
    if (bot > top)
        XFillRectangle(dpy, dst, g, 0, top, win_w, bot - top);
}

static void draw_home_section(Drawable dst, GC g) {
    if (header_font && home_header_x > -OPEN_SLIDE_X - 80)
        xft_draw(dst, header_font, home_header_x, MARGIN + header_font->ascent,
            "Home", 255, 255, 255);

    for (int pass = 0; pass < 2; pass++) {
        for (int s = 0; s < n_tile_order; s++) {
            int ti = tile_order[s];
            int is_drag = (dragging && ti == drag_tile_idx);
            if (pass == 0 && is_drag)
                continue;
            if (pass == 1 && !is_drag)
                continue;

            const Tile *t = &home_tiles[ti];
            int sx, sy, sw, sh;
            tile_screen_rect(t, ti, dragging, &sx, &sy, &sw, &sh);
            if (sy + sh < 0 || sy > win_h)
                continue;
            draw_tile_glyph(dst, g, t, sx, sy, sw, sh);
            draw_tile_label(dst, t, sx, sy, sh);
        }
    }
}

static void draw_apps_section(Drawable dst, GC g) {
    int base_y = home_h - scroll_y;
    if (base_y > win_h)
        return;

    if (header_font)
        xft_draw(dst, header_font, MARGIN, base_y + header_font->ascent,
            "Apps", 255, 255, 255);

    XSetForeground(dpy, g, pix_line);
    int line_y = base_y + HEADER_H - 8;
    if (line_y >= 0 && line_y < win_h)
        XDrawLine(dpy, dst, g, MARGIN, line_y, win_w - MARGIN, line_y);

    if (apps_cols < 1)
        apps_cols = 1;

    for (int i = 0; i < n_apps; i++) {
        int col = i % apps_cols;
        int row = i / apps_cols;
        int ax = MARGIN + col * APP_COL_W;
        int ay = base_y + HEADER_H + row * (APP_ROW_H + APP_GAP);
        if (ay + APP_ROW_H < 0 || ay > win_h)
            continue;

        const AppEntry *a = &apps[i];
        XSetForeground(dpy, g, rgb(a->cr, a->cg, a->cb));
        XFillRectangle(dpy, dst, g, ax, ay, APP_ICON, APP_ICON);
        char s[2] = { a->letter, 0 };
        if (ui_font) {
            XGlyphInfo ext;
            XftTextExtentsUtf8(dpy, ui_font, (FcChar8 *)s, 1, &ext);
            xft_draw(dst, ui_font,
                ax + (APP_ICON - ext.xOff) / 2,
                ay + text_baseline(APP_ICON, ui_font),
                s, 255, 255, 255);
        }
        xft_draw(dst, ui_font, ax + APP_ICON + 12,
            ay + text_baseline(APP_ROW_H, ui_font), a->name, 255, 255, 255);
    }
}

static void draw_context_menu(Drawable dst, GC g) {
    if (!ctx_visible)
        return;
    int mx = ctx_x;
    int my = ctx_y;
    if (my + CTX_H > win_h)
        my = win_h - CTX_H - 4;
    if (mx + CTX_W > win_w)
        mx = win_w - CTX_W - 4;

    XSetForeground(dpy, g, pix_ctx_bg);
    XFillRectangle(dpy, dst, g, mx, my, CTX_W, CTX_H);
    XSetForeground(dpy, g, pix_ctx_border);
    XDrawRectangle(dpy, dst, g, mx, my, CTX_W - 1, CTX_H - 1);
    xft_draw(dst, ui_font, mx + 12, my + text_baseline(CTX_H, ui_font),
        "Pin to Start", 255, 255, 255);
}

static void rebuild_static_layer(void) {
    ensure_buffers();
    if (!static_pm)
        return;
    layout_content();
    draw_bg(static_pm, buf_gc);
    draw_apps_section(static_pm, buf_gc);
    static_valid = 1;
    static_scroll = scroll_y;
}

static void present_buffer(void) {
    if (!buf_pm)
        return;
    XCopyArea(dpy, buf_pm, start_win, gc, 0, 0, win_w, win_h, 0, 0);
    XRaiseWindow(dpy, start_win);
    XFlush(dpy);
}

static void draw_all(void) {
    if (!visible)
        return;

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    if (ra.width != win_w || ra.height != root_h) {
        root_h = ra.height;
        win_w = ra.width;
        win_h = root_h;
        XMoveResizeWindow(dpy, start_win, 0, 0, win_w, win_h);
        invalidate_static();
        free_buffers();
        load_wallpaper();
    }

    ensure_buffers();
    if (!buf_pm)
        return;

    if (dragging)
        preview_order_with_drag(drag_hover_slot);
    else
        layout_content();

    int fast = (open_anim < 1.0 || layout_animating || dragging) &&
        static_valid && static_scroll == scroll_y;

    if (!fast || !static_valid || static_scroll != scroll_y)
        rebuild_static_layer();

    if (fast) {
        XCopyArea(dpy, static_pm, buf_pm, buf_gc, 0, 0, win_w, win_h, 0, 0);
        clear_home_region(buf_pm, buf_gc);
        draw_home_section(buf_pm, buf_gc);
        draw_context_menu(buf_pm, buf_gc);
    } else {
        XCopyArea(dpy, static_pm, buf_pm, buf_gc, 0, 0, win_w, win_h, 0, 0);
        draw_home_section(buf_pm, buf_gc);
        draw_context_menu(buf_pm, buf_gc);
        if (!dragging && !layout_animating && open_anim >= 1.0) {
            home_header_x = MARGIN;
            for (int i = 0; i < n_home_tiles; i++) {
                Tile *t = &home_tiles[i];
                t->anim_x = t->layout_x;
                t->anim_y = t->layout_y;
                t->anim_w = t->layout_w;
                t->anim_h = t->layout_h;
            }
        }
    }

    present_buffer();
}

static int tile_index_at_content(int cx, int cy) {
    if (cy < home_tile_top() - TILE)
        return -1;
    for (int s = 0; s < n_tile_order; s++) {
        int ti = tile_order[s];
        Tile *t = &home_tiles[ti];
        int y = (dragging && ti == drag_tile_idx) ? t->layout_y : t->anim_y;
        int x = (dragging && ti == drag_tile_idx) ? t->layout_x : t->anim_x;
        int w = (dragging && ti == drag_tile_idx) ? t->layout_w : t->anim_w;
        int h = (dragging && ti == drag_tile_idx) ? t->layout_h : t->anim_h;
        if (cx >= x && cx < x + w && cy >= y && cy < y + h)
            return ti;
    }
    return -1;
}

static AppEntry *app_at_content(int cx, int cy) {
    int base_y = home_h;
    if (cy < base_y + HEADER_H)
        return NULL;
    if (apps_cols < 1)
        apps_cols = 1;
    int rel_y = cy - base_y - HEADER_H;
    int row = rel_y / (APP_ROW_H + APP_GAP);
    int in_row = rel_y % (APP_ROW_H + APP_GAP);
    if (in_row >= APP_ROW_H)
        return NULL;
    int col = (cx - MARGIN) / APP_COL_W;
    if (col < 0 || col >= apps_cols)
        return NULL;
    if (cx - MARGIN - col * APP_COL_W > APP_COL_W - 20)
        return NULL;
    int idx = row * apps_cols + col;
    if (idx < 0 || idx >= n_apps)
        return NULL;
    return &apps[idx];
}

static int app_index_at_content(int cx, int cy) {
    int base_y = home_h;
    if (cy < base_y + HEADER_H)
        return -1;
    if (apps_cols < 1)
        apps_cols = 1;
    int rel_y = cy - base_y - HEADER_H;
    int row = rel_y / (APP_ROW_H + APP_GAP);
    int in_row = rel_y % (APP_ROW_H + APP_GAP);
    if (in_row >= APP_ROW_H)
        return -1;
    int col = (cx - MARGIN) / APP_COL_W;
    if (col < 0 || col >= apps_cols)
        return -1;
    if (cx - MARGIN - col * APP_COL_W > APP_COL_W - 20)
        return -1;
    int idx = row * apps_cols + col;
    if (idx < 0 || idx >= n_apps)
        return -1;
    return idx;
}

static int emblem_zone_click(int x, int y) {
    return x >= 0 && x < 48 && y >= win_h - PANEL_H;
}

static int app_already_pinned(const char *name) {
    for (int i = 0; i < n_home_tiles; i++) {
        if (home_tiles[i].pinned && strcmp(home_tiles[i].label, name) == 0)
            return 1;
    }
    return 0;
}

static void pin_app(int app_idx) {
    if (app_idx < 0 || app_idx >= n_apps)
        return;
    AppEntry *a = &apps[app_idx];
    if (app_already_pinned(a->name))
        return;
    char id[48];
    snprintf(id, sizeof(id), "pin_%s", a->name);
    for (const char *p = id + 4; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_')
            *(char *)p = '_';
    }
    if (tile_index_by_id(id) >= 0)
        return;
    add_tile(id, a->name, a->cr, a->cg, a->cb, a->letter,
        ACT_EXEC, a->exec_cmd, 0, 1);
    int idx = n_home_tiles - 1;
    if (n_tile_order < MAX_HOME_TILES)
        tile_order[n_tile_order++] = idx;
    Tile *t = &home_tiles[idx];
    layout_home();
    open_start_x[idx] = t->layout_x + OPEN_SLIDE_X;
    t->anim_x = open_start_x[idx];
    t->anim_y = t->layout_y;
    t->anim_w = t->layout_w;
    t->anim_h = t->layout_h;
    invalidate_static();
    start_layout_animation();
    save_layout();
    mark_dirty();
    draw_all();
}

static void show_pin_menu(int x, int y, int app_idx, int is_tile) {
    ctx_visible = 1;
    ctx_x = x;
    ctx_y = y;
    ctx_app_idx = app_idx;
    ctx_is_tile = is_tile;
    draw_all();
}

static int ctx_menu_hit(int x, int y) {
    if (!ctx_visible)
        return 0;
    int mx = ctx_x;
    int my = ctx_y;
    if (my + CTX_H > win_h)
        my = win_h - CTX_H - 4;
    if (mx + CTX_W > win_w)
        mx = win_w - CTX_W - 4;
    return x >= mx && x < mx + CTX_W && y >= my && y < my + CTX_H;
}

static void handle_click(int x, int y) {
    if (ctx_visible) {
        if (ctx_menu_hit(x, y) && ctx_app_idx >= 0 && !ctx_is_tile) {
            pin_app(ctx_app_idx);
        }
        ctx_visible = 0;
        draw_all();
        return;
    }

    if (emblem_zone_click(x, y)) {
        hide_menu();
        return;
    }
    int cy = y + scroll_y;
    if (!dragging) {
        int ti = tile_index_at_content(x, cy);
        if (ti >= 0) {
            Tile *t = &home_tiles[ti];
            launch_action(t->act, t->exec_cmd);
            return;
        }
        AppEntry *a = app_at_content(x, cy);
        if (a)
            launch_action(ACT_EXEC, a->exec_cmd);
    }
}

static void handle_scroll(int dir) {
    layout_content();
    int step = APP_ROW_H + APP_GAP;
    scroll_y += dir * step;
    layout_content();
    invalidate_static();
    mark_dirty();
    draw_all();
}

static void resize_to_root(void) {
    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    root_h = ra.height;
    win_w = ra.width;
    win_h = root_h;
    XMoveResizeWindow(dpy, start_win, 0, 0, win_w, win_h);
    invalidate_static();
    free_buffers();
    if (visible)
        draw_all();
}

static void open_fonts(void) {
    static const char *const ui_names[] = {
        "Segoe UI-10:antialias=true",
        "sans-serif-10:antialias=true",
        "DejaVu Sans-10",
        NULL
    };
    static const char *const hdr_names[] = {
        "Segoe UI Light-42:antialias=true",
        "Segoe UI-36:antialias=true",
        "sans-serif-36:antialias=true",
        "DejaVu Sans-36",
        NULL
    };
    for (int i = 0; ui_names[i]; i++) {
        ui_font = XftFontOpenName(dpy, screen, ui_names[i]);
        if (ui_font && ui_font->ascent > 0)
            break;
        if (ui_font) {
            XftFontClose(dpy, ui_font);
            ui_font = NULL;
        }
    }
    for (int i = 0; hdr_names[i]; i++) {
        header_font = XftFontOpenName(dpy, screen, hdr_names[i]);
        if (header_font && header_font->ascent > 0)
            break;
        if (header_font) {
            XftFontClose(dpy, header_font);
            header_font = NULL;
        }
    }
}

static void start_drag(int tile_idx, int px, int py) {
    dragging = 1;
    drag_tile_idx = tile_idx;
    drag_pointer_x = px;
    drag_pointer_y = py;
    Tile *t = &home_tiles[tile_idx];
    int cx = px;
    int cy = py + scroll_y;
    int lw = t->layout_w > 0 ? t->layout_w : TILE;
    int lh = t->layout_h > 0 ? t->layout_h : TILE;
    drag_offset_x = cx - (t->layout_x + lw / 2);
    drag_offset_y = cy - (t->layout_y + lh / 2);
    for (int i = 0; i < n_tile_order; i++) {
        if (tile_order[i] == tile_idx) {
            drag_hover_slot = i;
            break;
        }
    }
    XGrabPointer(dpy, start_win, False,
        ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, start_win, None, CurrentTime);
}

static void end_drag(int px, int py) {
    if (!dragging)
        return;
    XUngrabPointer(dpy, CurrentTime);
    int cy = py + scroll_y;
    int hover = slot_at_content_xy(px, cy);
    int from_slot = -1;
    for (int i = 0; i < n_tile_order; i++) {
        if (tile_order[i] == drag_tile_idx) {
            from_slot = i;
            break;
        }
    }
    if (from_slot >= 0 && hover >= 0 && hover != from_slot)
        reorder_tile(from_slot, hover);
    else {
        layout_home();
        invalidate_static();
    }
    dragging = 0;
    drag_tile_idx = -1;
    for (int i = 0; i < n_home_tiles; i++) {
        Tile *t = &home_tiles[i];
        t->anim_x = t->layout_x;
        t->anim_y = t->layout_y;
        t->anim_w = t->layout_w;
        t->anim_h = t->layout_h;
    }
    layout_animating = 0;
    mark_dirty();
}

static void update_drag(int px, int py) {
    if (!dragging)
        return;
    drag_pointer_x = px;
    drag_pointer_y = py;
    int cy = py + scroll_y;
    int hover = slot_at_content_xy(px, cy);
    if (hover < 0)
        hover = drag_hover_slot;
    if (hover != drag_hover_slot) {
        drag_hover_slot = hover;
        preview_order_with_drag(hover);
        start_layout_animation();
    }
    mark_dirty();
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "br8-start: cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    XSetErrorHandler(NULL);

    drag_tile_idx = -1;
    dragging = 0;
    open_anim = 1.0;

    br8_start_open = XInternAtom(dpy, "_BR8_START_OPEN", False);
    set_open(0);

    open_fonts();
    init_palette();
    load_apps();
    load_layout();

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    root_h = ra.height;
    win_w = ra.width;
    win_h = root_h - PANEL_H;

    start_win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0, 0, pix_bg);
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask |
        ButtonReleaseMask | PointerMotionMask | KeyPressMask;
    XChangeWindowAttributes(dpy, start_win, CWOverrideRedirect | CWEventMask, &attr);
    gc = XCreateGC(dpy, start_win, 0, NULL);
    load_wallpaper();
    clock_gettime(CLOCK_MONOTONIC, &last_tick);
    XUnmapWindow(dpy, start_win);
    visible = 0;

    XSelectInput(dpy, root, PropertyChangeMask | StructureNotifyMask);

    int xfd = ConnectionNumber(dpy);
    while (1) {
        int tick = visible && (animating_now() || frame_dirty);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv;
        if (tick) {
            tv.tv_sec = 0;
            tv.tv_usec = TICK_MS * 1000;
        } else {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
        }
        select(xfd + 1, &fds, NULL, NULL, &tv);

        if (tick)
            tick_animations();

        int want = read_open();
        if (want && !visible)
            show_menu();
        else if (!want && visible)
            hide_menu();

        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0 && visible)
                draw_all();
            else if (ev.type == ConfigureNotify && ev.xconfigure.window == root)
                resize_to_root();
            else if (ev.type == PropertyNotify &&
                     ev.xproperty.window == root &&
                     ev.xproperty.atom == br8_start_open) {
                want = read_open();
                if (want && !visible)
                    show_menu();
                else if (!want && visible)
                    hide_menu();
            } else if (ev.type == ButtonPress && ev.xbutton.window == start_win && visible) {
                if (ev.xbutton.button == Button4)
                    handle_scroll(-1);
                else if (ev.xbutton.button == Button5)
                    handle_scroll(1);
                else if (ev.xbutton.button == Button3) {
                    int cy = ev.xbutton.y + scroll_y;
                    int aidx = app_index_at_content(ev.xbutton.x, cy);
                    if (aidx >= 0 && !app_already_pinned(apps[aidx].name))
                        show_pin_menu(ev.xbutton.x, ev.xbutton.y, aidx, 0);
                } else if (ev.xbutton.button == Button1) {
                    if (ctx_visible) {
                        handle_click(ev.xbutton.x, ev.xbutton.y);
                    } else {
                        int cy = ev.xbutton.y + scroll_y;
                        btn1_tile_idx = tile_index_at_content(ev.xbutton.x, cy);
                        btn1_down = 1;
                        btn1_x = ev.xbutton.x;
                        btn1_y = ev.xbutton.y;
                    }
                }
            } else if (ev.type == MotionNotify && ev.xmotion.window == start_win && visible) {
                if (btn1_down && btn1_tile_idx >= 0 && !dragging) {
                    int dx = ev.xmotion.x - btn1_x;
                    int dy = ev.xmotion.y - btn1_y;
                    if (dx * dx + dy * dy >= DRAG_THRESHOLD * DRAG_THRESHOLD)
                        start_drag(btn1_tile_idx, ev.xmotion.x, ev.xmotion.y);
                }
                if (dragging)
                    update_drag(ev.xmotion.x, ev.xmotion.y);
            } else if (ev.type == ButtonRelease && ev.xbutton.window == start_win &&
                       visible && ev.xbutton.button == Button1) {
                if (dragging)
                    end_drag(ev.xbutton.x, ev.xbutton.y);
                else if (btn1_down && btn1_tile_idx < 0)
                    handle_click(ev.xbutton.x, ev.xbutton.y);
                else if (btn1_down && btn1_tile_idx >= 0) {
                    int cy = ev.xbutton.y + scroll_y;
                    int ti = tile_index_at_content(ev.xbutton.x, cy);
                    if (ti == btn1_tile_idx)
                        handle_click(ev.xbutton.x, ev.xbutton.y);
                }
                btn1_down = 0;
                btn1_tile_idx = -1;
            } else if (ev.type == KeyPress && ev.xkey.window == start_win && visible) {
                KeySym sym = XLookupKeysym(&ev.xkey, 0);
                if (sym == XK_Escape) {
                    if (ctx_visible) {
                        ctx_visible = 0;
                        draw_all();
                    } else {
                        hide_menu();
                    }
                }
            }
        }
    }
    return 0;
}
