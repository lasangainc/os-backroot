/*
 * PowerPDF — Metro-style PDF reader (Poppler + Cairo + X11 + Xft)
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <cairo/cairo-xlib.h>
#include <poppler.h>
#include <poppler-page.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <sys/select.h>

#include "../../include/br8-metro.h"

/* Wine-red Metro palette */
#define COL_BG_R 92
#define COL_BG_G 28
#define COL_BG_B 48
#define COL_BG2_R 72
#define COL_BG2_G 18
#define COL_BG2_B 38
#define COL_ACCENT_R 155
#define COL_ACCENT_G 52
#define COL_ACCENT_B 72
#define COL_ACCENT_HI_R 188
#define COL_ACCENT_HI_G 78
#define COL_ACCENT_HI_B 98
#define COL_TILE_R 122
#define COL_TILE_G 40
#define COL_TILE_B 58
#define COL_PAGE_BG_R 248
#define COL_PAGE_BG_G 246
#define COL_PAGE_BG_B 244
#define COL_TEXT_R 255
#define COL_TEXT_G 255
#define COL_TEXT_B 255
#define COL_MUTED_R 230
#define COL_MUTED_G 200
#define COL_MUTED_B 210

#define MIN_W 640
#define MIN_H 480
#define PAD 24
#define APPBAR_H 52
#define BTN_H 48
#define BTN_W 200
#define TILE_W 280
#define TILE_H 120

static Display *dpy;
static int screen;
static Window root, win;
static Visual *visual;
static Colormap cmap;
static int win_w, win_h;
static GC gc;
static XftFont *font_title;
static XftFont *font_sub;
static XftFont *font_btn;
static XftFont *font_bar;
static PopplerDocument *doc;
static int page_num;
static int page_count;
static Pixmap page_pix;
static int pix_w, pix_h;
static char status[256];
static char doc_path[1024];
static int open_btn_hover;
static int prev_btn_hover;
static int next_btn_hover;
static Atom wm_protocols;
static Atom wm_delete;

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

static void set_status(const char *msg) {
    snprintf(status, sizeof(status), "%s", msg ? msg : "");
}

static void free_page_pixmap(void) {
    if (page_pix) {
        XFreePixmap(dpy, page_pix);
        page_pix = 0;
    }
    pix_w = pix_h = 0;
}

static int render_current_page(void) {
    if (!doc)
        return 0;

    PopplerPage *page = poppler_document_get_page(doc, page_num);
    if (!page)
        return 0;

    double pw, ph;
    poppler_page_get_size(page, &pw, &ph);
    int area_w = win_w - PAD * 2;
    int area_h = win_h - APPBAR_H - PAD * 2;
    if (area_w < 64)
        area_w = 64;
    if (area_h < 64)
        area_h = 64;

    double scale = area_w / pw;
    if (ph * scale > area_h)
        scale = area_h / ph;

    pix_w = (int)(pw * scale);
    pix_h = (int)(ph * scale);
    if (pix_w < 1)
        pix_w = 1;
    if (pix_h < 1)
        pix_h = 1;

    free_page_pixmap();
    page_pix = XCreatePixmap(dpy, win, pix_w, pix_h,
        (unsigned int)DefaultDepth(dpy, screen));

    cairo_surface_t *surface = cairo_xlib_surface_create(dpy, page_pix, visual,
        pix_w, pix_h);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    poppler_page_render(page, cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(page);

    const char *base = doc_path[0] ? strrchr(doc_path, '/') : NULL;
    base = base ? base + 1 : (doc_path[0] ? doc_path : "document");
    snprintf(status, sizeof(status), "%.72s  ·  page %d of %d",
        base, page_num + 1, page_count);
    return 1;
}

static int load_document(const char *path) {
    if (!path || !path[0])
        return 0;

    GError *err = NULL;
    char uri[1200];
    char abs[PATH_MAX];
    const char *use = path;

    if (strncmp(path, "file://", 7) != 0 && realpath(path, abs))
        use = abs;
    if (strncmp(use, "file://", 7) == 0)
        snprintf(uri, sizeof(uri), "%s", use);
    else
        snprintf(uri, sizeof(uri), "file://%s", use);

    if (doc) {
        g_object_unref(doc);
        doc = NULL;
    }

    doc = poppler_document_new_from_file(uri, NULL, &err);
    if (!doc) {
        set_status(err && err->message ? err->message : "Could not open PDF");
        if (err)
            g_error_free(err);
        return 0;
    }

    strncpy(doc_path, path, sizeof(doc_path) - 1);
    doc_path[sizeof(doc_path) - 1] = '\0';
    page_count = poppler_document_get_n_pages(doc);
    page_num = 0;
    return render_current_page();
}

static int scan_pdf_in_dir(const char *dir, char *out, size_t outsz) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        size_t n = strlen(ent->d_name);
        if (n < 5)
            continue;
        if (strcasecmp(ent->d_name + n - 4, ".pdf") != 0)
            continue;
        snprintf(out, outsz, "%s/%s", dir, ent->d_name);
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

static int pick_pdf_fallback(char *out, size_t outsz) {
    const char *dirs[] = { "/root", "/root/Documents", "/usr/share/doc", NULL };
    for (int i = 0; dirs[i]; i++) {
        if (scan_pdf_in_dir(dirs[i], out, outsz))
            return 1;
    }
    return 0;
}

static int open_pdf_dialog(void) {
    char path[PATH_MAX];
    FILE *fp = popen("zenity --file-selection --title='Open PDF' "
        "--file-filter='PDF files (*.pdf)|*.pdf' 2>/dev/null", "r");
    if (fp) {
        path[0] = '\0';
        if (fgets(path, sizeof(path), fp)) {
            size_t n = strlen(path);
            while (n > 0 && (path[n - 1] == '\n' || path[n - 1] == '\r'))
                path[--n] = '\0';
        }
        pclose(fp);
        if (path[0])
            return load_document(path);
    }

    if (pick_pdf_fallback(path, sizeof(path)))
        return load_document(path);

    set_status("No PDF selected — add files under /root or install zenity");
    return 0;
}

static void draw_metro_button(int x, int y, int w, int h, const char *label, int hover) {
    int r = hover ? COL_ACCENT_HI_R : COL_ACCENT_R;
    int g = hover ? COL_ACCENT_HI_G : COL_ACCENT_G;
    int b = hover ? COL_ACCENT_HI_B : COL_ACCENT_B;
    XSetForeground(dpy, gc, rgb(r, g, b));
    XFillRectangle(dpy, win, gc, x, y, w, h);
    if (hover) {
        XSetForeground(dpy, gc, rgb(COL_TEXT_R, COL_TEXT_G, COL_TEXT_B));
        XDrawRectangle(dpy, win, gc, x, y, w - 1, h - 1);
    }
    int tw = text_width(font_btn, label);
    int tx = x + (w - tw) / 2;
    int ty = text_baseline(h, font_btn);
    xft_draw(win, tx, y + ty, label, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_btn);
}

static void draw_welcome(void) {
    int y = win_h / 5;
    const char *title = "PowerPDF";
    const char *sub = "Metro PDF reader for Backroot 8";

    xft_draw(win, PAD * 2, y, title, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_title);
    y += font_title ? font_title->height + 12 : 48;
    xft_draw(win, PAD * 2, y, sub, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_sub);

    int tx = (win_w - TILE_W) / 2;
    int ty = win_h / 2 - TILE_H / 2;
    int tr = open_btn_hover ? COL_TILE_R + 20 : COL_TILE_R;
    int tg = open_btn_hover ? COL_TILE_G + 12 : COL_TILE_G;
    int tb = open_btn_hover ? COL_TILE_B + 14 : COL_TILE_B;
    XSetForeground(dpy, gc, rgb(tr, tg, tb));
    XFillRectangle(dpy, win, gc, tx, ty, TILE_W, TILE_H);
    if (open_btn_hover) {
        XSetForeground(dpy, gc, rgb(COL_TEXT_R, COL_TEXT_G, COL_TEXT_B));
        XDrawRectangle(dpy, win, gc, tx, ty, TILE_W - 1, TILE_H - 1);
    }

    const char *tile_label = "Open PDF";
    int lw = text_width(font_btn, tile_label);
    xft_draw(win, tx + (TILE_W - lw) / 2,
        ty + text_baseline(TILE_H, font_btn) + 8,
        tile_label, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, font_btn);

    const char *hint = "Browse for a document, or place PDFs in /root";
    int hw = text_width(font_sub, hint);
    xft_draw(win, (win_w - hw) / 2, ty + TILE_H + 28,
        hint, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_sub);

    if (status[0]) {
        int sw = text_width(font_sub, status);
        xft_draw(win, (win_w - sw) / 2, win_h - PAD - 20,
            status, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_sub);
    }
}

static void draw_appbar(void) {
    int bar_y = win_h - APPBAR_H;
    XSetForeground(dpy, gc, rgb(COL_BG2_R, COL_BG2_G, COL_BG2_B));
    XFillRectangle(dpy, win, gc, 0, bar_y, win_w, APPBAR_H);

    int bx = PAD;
    draw_metro_button(bx, bar_y + (APPBAR_H - BTN_H) / 2, BTN_W, BTN_H, "Open PDF",
        open_btn_hover);

    if (doc && page_count > 0) {
        int nx = win_w - PAD - 88;
        int px = nx - 96;
        int by = bar_y + (APPBAR_H - 36) / 2;
        draw_metro_button(px, by, 88, 36, "Prev", prev_btn_hover);
        draw_metro_button(nx, by, 88, 36, "Next", next_btn_hover);
    }

    if (status[0] && font_bar) {
        int sx = PAD + BTN_W + 20;
        xft_draw(win, sx, bar_y + text_baseline(APPBAR_H, font_bar),
            status, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, font_bar);
    }
}

static void draw_viewer(void) {
    int content_h = win_h - APPBAR_H;
    XSetForeground(dpy, gc, rgb(COL_PAGE_BG_R, COL_PAGE_BG_G, COL_PAGE_BG_B));
    XFillRectangle(dpy, win, gc, 0, 0, win_w, content_h);

    if (!page_pix) {
        xft_draw(win, PAD * 2, content_h / 2,
            "Could not render page", COL_BG_R, COL_BG_G, COL_BG_B, font_sub);
        draw_appbar();
        return;
    }

    int x = (win_w - pix_w) / 2;
    int y = PAD + (content_h - PAD * 2 - pix_h) / 2;
    if (y < PAD)
        y = PAD;
    XSetForeground(dpy, gc, rgb(200, 198, 196));
    XFillRectangle(dpy, win, gc, x - 3, y - 3, pix_w + 6, pix_h + 6);
    XCopyArea(dpy, page_pix, win, gc, 0, 0, pix_w, pix_h, x, y);
    draw_appbar();
}

static void draw_window(void) {
    XSetForeground(dpy, gc, rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);

    if (!doc)
        draw_welcome();
    else
        draw_viewer();
}

static int open_btn_contains(int x, int y) {
    if (doc) {
        int bar_y = win_h - APPBAR_H;
        int bx = PAD;
        int by = bar_y + (APPBAR_H - BTN_H) / 2;
        return x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H;
    }
    int tx = (win_w - TILE_W) / 2;
    int ty = win_h / 2 - TILE_H / 2;
    return x >= tx && x < tx + TILE_W && y >= ty && y < ty + TILE_H;
}

static int prev_btn_contains(int x, int y) {
    if (!doc || page_count <= 0)
        return 0;
    int bar_y = win_h - APPBAR_H;
    int nx = win_w - PAD - 88;
    int px = nx - 96;
    int by = bar_y + (APPBAR_H - 36) / 2;
    return x >= px && x < px + 88 && y >= by && y < by + 36;
}

static int next_btn_contains(int x, int y) {
    if (!doc || page_count <= 0)
        return 0;
    int bar_y = win_h - APPBAR_H;
    int nx = win_w - PAD - 88;
    int by = bar_y + (APPBAR_H - 36) / 2;
    return x >= nx && x < nx + 88 && y >= by && y < by + 36;
}

static void update_hover(int x, int y) {
    int ob = open_btn_contains(x, y);
    int pb = prev_btn_contains(x, y);
    int nb = next_btn_contains(x, y);
    if (ob != open_btn_hover || pb != prev_btn_hover || nb != next_btn_hover) {
        open_btn_hover = ob;
        prev_btn_hover = pb;
        next_btn_hover = nb;
        draw_window();
    }
}

static void change_page(int delta) {
    if (!doc || page_count <= 0)
        return;
    int next = page_num + delta;
    if (next < 0 || next >= page_count)
        return;
    page_num = next;
    render_current_page();
    draw_window();
}

static void handle_click(int x, int y) {
    if (open_btn_contains(x, y)) {
        open_pdf_dialog();
        draw_window();
        return;
    }
    if (prev_btn_contains(x, y))
        change_page(-1);
    else if (next_btn_contains(x, y))
        change_page(1);
}

static void resize_window(int w, int h) {
    if (w < MIN_W)
        w = MIN_W;
    if (h < MIN_H)
        h = MIN_H;
    win_w = w;
    win_h = h;
    XResizeWindow(dpy, win, win_w, win_h);
    if (doc)
        render_current_page();
    draw_window();
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

static void setup_wm_delete(void) {
    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    Atom protos[] = { wm_delete };
    XSetWMProtocols(dpy, win, protos, 1);
}

static int handle_client_message(XClientMessageEvent *ev) {
    if (ev->message_type == wm_protocols && (Atom)ev->data.l[0] == wm_delete)
        return 1;
    return 0;
}

int main(int argc, char **argv) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "powerpdf: cannot open display\n");
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);

    XWindowAttributes ra;
    XGetWindowAttributes(dpy, root, &ra);
    win_w = ra.width > 0 ? ra.width : 1024;
    win_h = ra.height > 0 ? ra.height : 768;

    static const char *const title_fonts[] = {
        "Segoe UI-42:weight=light",
        "Segoe UI-36:weight=light",
        "DejaVu Sans-36",
        NULL
    };
    static const char *const sub_fonts[] = {
        "Segoe UI-16",
        "DejaVu Sans-16",
        NULL
    };
    static const char *const btn_fonts[] = {
        "Segoe UI-18:bold",
        "Segoe UI-18",
        "DejaVu Sans-18:bold",
        NULL
    };
    static const char *const bar_fonts[] = {
        "Segoe UI-11",
        "DejaVu Sans-11",
        NULL
    };

    font_title = open_font(title_fonts);
    font_sub = open_font(sub_fonts);
    font_btn = open_font(btn_fonts);
    font_bar = open_font(bar_fonts);

    win = XCreateSimpleWindow(dpy, root, 0, 0, win_w, win_h, 0,
        BlackPixel(dpy, screen), rgb(COL_BG_R, COL_BG_G, COL_BG_B));
    XStoreName(dpy, win, "PowerPDF");
    {
        XClassHint ch;
        ch.res_name = (char *)"PowerPDF";
        ch.res_class = (char *)"PowerPDF";
        XSetClassHint(dpy, win, &ch);
    }

    br8_set_metro(dpy, win);
    setup_wm_delete();

    gc = XCreateGC(dpy, win, 0, NULL);
    XSelectInput(dpy, win,
        ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask |
        PointerMotionMask | LeaveWindowMask);

    XMapRaised(dpy, win);

    if (argc > 1)
        load_document(argv[1]);
    else
        set_status("");

    draw_window();
    br8_signal_metro_ready(dpy);

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
        if (ev.type == Expose && ev.xexpose.count == 0)
            draw_window();
        else if (ev.type == ConfigureNotify && ev.xconfigure.window == win)
            resize_window(ev.xconfigure.width, ev.xconfigure.height);
        else if (ev.type == ClientMessage && handle_client_message(&ev.xclient))
            running = 0;
        else if (ev.type == KeyPress) {
            KeySym sym = XLookupKeysym(&ev.xkey, 0);
            if (sym == XK_Escape)
                running = 0;
            else if (sym == XK_o || sym == XK_O)
                open_pdf_dialog();
            else if (sym == XK_Page_Down || sym == XK_Right || sym == XK_Down)
                change_page(1);
            else if (sym == XK_Page_Up || sym == XK_Left || sym == XK_Up)
                change_page(-1);
            draw_window();
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == Button1)
                handle_click((int)ev.xbutton.x, (int)ev.xbutton.y);
            else if (ev.xbutton.button == 4)
                change_page(-1);
            else if (ev.xbutton.button == 5)
                change_page(1);
        } else if (ev.type == MotionNotify)
            update_hover((int)ev.xmotion.x, (int)ev.xmotion.y);
        else if (ev.type == LeaveNotify)
            update_hover(-1, -1);
    }

    free_page_pixmap();
    if (doc)
        g_object_unref(doc);
    if (font_title)
        XftFontClose(dpy, font_title);
    if (font_sub)
        XftFontClose(dpy, font_sub);
    if (font_btn)
        XftFontClose(dpy, font_btn);
    if (font_bar)
        XftFontClose(dpy, font_bar);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
