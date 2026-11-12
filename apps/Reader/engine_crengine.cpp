/*
 * crengine adapter — only translation unit that includes LVDocView headers.
 */
#include "engine.h"
#include "config.h"
#include "debug.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <unistd.h>

#include "crengine.h"
#include "lvdocview.h"
#include "lvdocviewprops.h"
#include "lvdrawbuf.h"
#include "lvfntman.h"
#include "lvmemman.h"
#include "hyphman.h"

static LVDocView *g_view = NULL;
static int g_width = 0;
static int g_height = 0;
static int g_inited = 0;
static std::string g_fonts_dir;

static lString8 u8(const char *s)
{
    return lString8(s ? s : "");
}

static lString32 u32(const char *s)
{
    return Utf8ToUnicode(u8(s));
}

static void register_fonts_in_dir(const char *dir)
{
    DIR *d;
    struct dirent *ent;
    if (!dir || !fontMan)
        return;
    d = opendir(dir);
    if (!d) {
        reader_log("crengine: cannot opendir %s", dir);
        return;
    }
    while ((ent = readdir(d)) != NULL) {
        size_t n;
        char path[768];
        if (!ent->d_name || ent->d_name[0] == '.')
            continue;
        n = strlen(ent->d_name);
        if (n < 5)
            continue;
        if (!(strcasecmp(ent->d_name + n - 4, ".ttf") == 0 ||
              strcasecmp(ent->d_name + n - 4, ".otf") == 0 ||
              strcasecmp(ent->d_name + n - 4, ".ttc") == 0))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (fontMan->RegisterFont(u8(path)))
            reader_log("crengine: registered %s", ent->d_name);
    }
    closedir(d);
}

static void cre_fatal_handler(int code, const char *text)
{
    reader_log("crFatalError code=%d \"%s\"", code, text ? text : "");
    boot_marker("naskilla: crFatalError");
    _exit(code != 0 ? code : 1);
}

static void apply_header_off(CRPropRef props)
{
    /* PROP_STATUS_LINE=1 means no header. Clear leftover SHOW_* flags too. */
    props->setInt(PROP_STATUS_LINE, 1);
    props->setInt(PROP_SHOW_TIME, 0);
    props->setInt(PROP_SHOW_BATTERY, 0);
    props->setInt(PROP_SHOW_TITLE, 0);
    props->setInt(PROP_SHOW_AUTHOR, 0);
    props->setInt(PROP_SHOW_PAGE_NUMBER, 0);
    props->setInt(PROP_SHOW_PAGE_COUNT, 0);
    props->setInt(PROP_SHOW_POS_PERCENT, 0);
    props->setInt(PROP_STATUS_CHAPTER_MARKS, 0);
}

/*
 * setStyleSheet() REPLACES the whole UA sheet (LVDocView starts from
 * crengine def_stylesheet). Keep those base rules, then append heading
 * scales. !important so publisher sheets that flatten titles do not win.
 * Apply ONLY before LoadDocument — post-load restyle hangs the device.
 */
static const char *k_ua_css =
    /* --- crengine def_stylesheet essentials --- */
    "image { text-align: center; text-indent: 0px }\n"
    "empty-line { height: 1em }\n"
    "sub { vertical-align: sub; font-size: 70% }\n"
    "sup { vertical-align: super; font-size: 70% }\n"
    "body > image, section > image { text-align: center; margin-before: 1em; margin-after: 1em }\n"
    "p > image { display: inline }\n"
    /* Footnote links only — do not shrink every <a> (chapter titles are often links). */
    "a { text-decoration: none }\n"
    "a[type=\"note\"] { vertical-align: super; font-size: 70%; text-decoration: none }\n"
    "p { margin-top: 0em; margin-bottom: 0em }\n"
    "text-author { font-weight: bold; font-style: italic; margin-left: 5% }\n"
    "epigraph { margin-left: 30%; margin-right: 4%; text-align: left; text-indent: 1px;"
    " font-style: italic; margin-top: 15px; margin-bottom: 25px }\n"
    "strong, b { font-weight: bold }\n"
    "emphasis, i { font-style: italic }\n"
    "body { text-align: justify; text-indent: 2em }\n"
    "cite { margin-left: 30%; margin-right: 4%; text-align: justify; text-indent: 0px;"
    " margin-top: 20px; margin-bottom: 20px }\n"
    "td, th { text-indent: 0px; font-size: 80%; text-align: left; padding: 2px }\n"
    "th { font-weight: bold }\n"
    "code, pre { display: block; white-space: pre; font-family: \"Courier New\", monospace }\n"
    /* --- heading / title overrides (EPUB h1–h6 + FB2 title + Calibre p.title*) --- */
    "h1 { font-size: 150% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.7em; margin-bottom: 0.5em }\n"
    "h2 { font-size: 140% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.7em; margin-bottom: 0.5em }\n"
    "h3 { font-size: 130% !important; font-weight: bold; text-indent: 0;"
    " hyphens: none; margin-top: 0.7em; margin-bottom: 0.5em }\n"
    "h4 { font-size: 120% !important; font-weight: bold; text-indent: 0;"
    " hyphens: none; margin-top: 0.5em; margin-bottom: 0.4em }\n"
    "h5 { font-size: 110% !important; font-weight: bold; text-indent: 0; hyphens: none }\n"
    "h6 { font-size: 100% !important; font-weight: bold; text-indent: 0; hyphens: none }\n"
    "title { text-align: center; text-indent: 0; font-size: 130% !important;"
    " font-weight: bold; hyphens: none; margin-top: 10px; margin-bottom: 10px;"
    " page-break-before: always; page-break-inside: avoid; page-break-after: avoid }\n"
    "subtitle { text-align: center; text-indent: 0; font-size: 120% !important;"
    " font-weight: bold; hyphens: none; margin-top: 10px; margin-bottom: 10px }\n"
    "p.title, p.title1 { font-size: 150% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.7em; margin-bottom: 0.5em }\n"
    "p.title2 { font-size: 140% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.7em; margin-bottom: 0.5em }\n"
    "p.title3 { font-size: 130% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.7em; margin-bottom: 0.5em }\n"
    "p.title4 { font-size: 120% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.5em; margin-bottom: 0.4em }\n"
    "p.title5 { font-size: 110% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none }\n"
    "p.subtitle { font-size: 120% !important; font-weight: bold; text-align: center;"
    " text-indent: 0; hyphens: none; margin-top: 0.3em; margin-bottom: 0.3em }\n"
    /* Inner spans/links often set absolute size; keep them at the heading size. */
    "h1 *, h2 *, h3 *, h4 *, h5 *, h6 *, title *, subtitle *,"
    " p.title *, p.title1 *, p.title2 *, p.title3 *, p.title4 *, p.title5 *, p.subtitle *"
    " { font-size: 1em !important; font-weight: inherit !important }\n";

static void apply_heading_stylesheet(void)
{
    if (!g_view)
        return;
    g_view->setStyleSheet(lString8(k_ua_css), false);
    reader_log("cre: UA+heading stylesheet set (pre-load)");
    boot_marker("naskilla: heading css");
}

extern "C" int engine_global_init(const char *cache_dir, const char *fonts_dir)
{
    if (g_inited)
        return 0;

    crSetFatalErrorHandler(cre_fatal_handler);

    reader_log("crengine: InitFontManager...");
    if (!InitFontManager(fonts_dir && fonts_dir[0] ? u8(fonts_dir) : lString8())) {
        reader_log("crengine: InitFontManager failed");
        return -1;
    }
    if (!fontMan) {
        reader_log("crengine: fontMan is NULL after init");
        return -1;
    }

    g_fonts_dir = fonts_dir ? fonts_dir : "";
    /* Register every TTF we can find — Draw() segfaults with zero usable fonts. */
    register_fonts_in_dir(fonts_dir);
    register_fonts_in_dir("/mnt/ext1/system/fonts");
    register_fonts_in_dir("/mnt/ext1/fonts");
    register_fonts_in_dir("/ebrmain/fonts");
    register_fonts_in_dir("/usr/share/fonts/ttf/liberation");
    register_fonts_in_dir("/usr/share/fonts/truetype/liberation");
    reader_log("crengine: font count after register = %d", fontMan->GetFontCount());

    (void)cache_dir;
    reader_log("crengine: doc cache disabled");
    reader_log("crengine: HyphMan skipped");

    g_inited = 1;
    reader_log("crengine: global init ok");
    return 0;
}

extern "C" int engine_list_faces(char out[][64], int max)
{
    int n = 0;
    if (!fontMan || !out || max <= 0)
        return 0;
    lString32Collection faces;
    fontMan->getFaceList(faces);
    for (int i = 0; i < faces.length() && n < max; i++) {
        lString8 utf = UnicodeToUtf8(faces[i]);
        snprintf(out[n], 64, "%s", utf.c_str());
        n++;
    }
    return n;
}

extern "C" void engine_global_shutdown(void)
{
    if (g_view) {
        delete g_view;
        g_view = NULL;
    }
    if (g_inited) {
        /* Cache/HyphMan were not started in the minimal init path. */
        ShutdownFontManager();
        g_inited = 0;
    }
}

static int cre_open(const char *path, int width, int height)
{
    reader_log("cre_open: start %s %dx%d", path ? path : "(null)", width, height);
    if (!g_inited)
        return -1;
    if (g_view) {
        delete g_view;
        g_view = NULL;
    }

    try {
        reader_log("cre_open: new LVDocView");
        g_view = new LVDocView(4, false); /* 4 bpp gray — lighter, PB-friendly */
        g_view->setViewMode(DVM_PAGES, 1);
        g_width = width;
        g_height = height;
        reader_log("cre_open: Resize");
        g_view->Resize(width, height);

        CRPropRef props = g_view->propsGetCurrent();
        apply_header_off(props);
        g_view->propsApply(props);
        g_view->setShowCover(false);
        apply_heading_stylesheet();

        boot_marker("naskilla: LoadDocument");
        reader_log("cre_open: LoadDocument...");
        if (!g_view->LoadDocument(path)) {
            reader_log("crengine: LoadDocument failed for %s", path);
            boot_marker("naskilla: LoadDocument FAIL");
            delete g_view;
            g_view = NULL;
            return -2;
        }
        reader_log("cre_open: LoadDocument ok, fonts=%d",
                   fontMan ? fontMan->GetFontCount() : -1);
        boot_marker("naskilla: LoadDocument ok");
        /* Prefer a face we know we registered. */
        g_view->setDefaultFontFace(u8("Bookerly"));
        g_view->setDefaultFontFace(u8("Liberation Sans"));
        g_view->setDefaultFontFace(u8("LiberationSans"));
        boot_marker("naskilla: Render");
        reader_log("cre_open: Render...");
        g_view->Render();
        reader_log("cre_open: Render ok pages=%d", g_view->getPageCount());
        boot_marker("naskilla: Render ok");
    } catch (...) {
        reader_log("cre_open: exception");
        if (g_view) {
            delete g_view;
            g_view = NULL;
        }
        return -3;
    }
    return 0;
}

static void cre_close(void)
{
    if (g_view) {
        delete g_view;
        g_view = NULL;
    }
}

static int cre_set_layout(const engine_layout *layout)
{
    if (!g_view || !layout)
        return -1;
    ldomXPointer bm = g_view->getBookmark();
    lString32 saved = bm.isNull() ? lString32() : bm.toString();

    if (layout->page_w > 0 && layout->page_h > 0) {
        g_width = layout->page_w;
        g_height = layout->page_h;
    }

    g_view->setFontSize(layout->font_size);
    if (layout->font_face && layout->font_face[0])
        g_view->setDefaultFontFace(u8(layout->font_face));

    int m = layout->margin;
    if (m < 16)
        m = 16;

    CRPropRef props = g_view->propsGetCurrent();
    apply_header_off(props);
    props->setInt(PROP_INTERLINE_SPACE, layout->line_spacing);
    props->setInt(PROP_EMBEDDED_STYLES, layout->publisher_styles ? 1 : 0);
    props->setInt(PROP_EMBEDDED_FONTS, layout->publisher_styles ? 1 : 0);
    /* propsApply re-applies PROP_PAGE_MARGIN_* and would wipe setPageMargins
     * if these stay at the crengine defaults (~8). Keep them in sync. */
    props->setInt(PROP_PAGE_MARGIN_LEFT, m);
    props->setInt(PROP_PAGE_MARGIN_RIGHT, m);
    props->setInt(PROP_PAGE_MARGIN_TOP, m);
    props->setInt(PROP_PAGE_MARGIN_BOTTOM, m);
    if (layout->night) {
        props->setColor(PROP_FONT_COLOR, 0xFFFFFF);
        props->setColor(PROP_BACKGROUND_COLOR, 0x000000);
    } else {
        props->setColor(PROP_FONT_COLOR, 0x000000);
        props->setColor(PROP_BACKGROUND_COLOR, 0xFFFFFF);
    }
    g_view->propsApply(props);
    g_view->setPageMargins(lvRect(m, m, m, m));
    /* Do not setStyleSheet here — restyle of an open book is too slow on-device. */
    g_view->Resize(g_width, g_height);
    g_view->Render();
    if (!saved.empty()) {
        ldomXPointer p = g_view->getDocument()->createXPointer(saved);
        if (!p.isNull())
            g_view->goToBookmark(p);
    }
    return 0;
}

static int cre_page_count(void)
{
    return g_view ? g_view->getPageCount() : 0;
}

static int cre_current_page(void)
{
    if (!g_view)
        return 0;
    return g_view->getCurPage() + 1;
}

static int cre_go_to(int page)
{
    if (!g_view)
        return -1;
    int n = page - 1;
    if (n < 0)
        n = 0;
    if (n >= g_view->getPageCount())
        n = g_view->getPageCount() - 1;
    g_view->goToPage(n);
    return 0;
}

static int cre_next(void)
{
    if (!g_view)
        return -1;
    int cur = g_view->getCurPage();
    if (cur + 1 >= g_view->getPageCount())
        return -1;
    g_view->goToPage(cur + 1);
    return 0;
}

static int cre_prev(void)
{
    if (!g_view)
        return -1;
    int cur = g_view->getCurPage();
    if (cur <= 0)
        return -1;
    g_view->goToPage(cur - 1);
    return 0;
}

static void cre_log_draw_diag(const char *tag)
{
    int hdr = g_view ? g_view->getPageHeaderInfo() : -1;
    int hdr_h = g_view ? g_view->getPageHeaderHeight() : -1;
    int faces = fontMan ? fontMan->GetFontCount() : -1;
    int pages = g_view ? g_view->getPageCount() : -1;
    int cur = g_view ? g_view->getCurPage() + 1 : -1;
    reader_log("cre_draw %s hdrFlags=%d hdrH=%d infoFont=%s fonts=%d pages=%d cur=%d view=%dx%d",
               tag ? tag : "?",
               hdr, hdr_h,
               (g_view && !g_view->getInfoFont().isNull()) ? "ok" : "null",
               faces, pages, cur, g_width, g_height);
    if (fontMan) {
        lString32Collection list;
        fontMan->getFaceList(list);
        int n = list.length();
        if (n > 12)
            n = 12;
        for (int i = 0; i < n; i++) {
            lString8 utf = UnicodeToUtf8(list[i]);
            reader_log("cre_face[%d]=%s", i, utf.c_str());
        }
    }
}

static void cre_copy_gray(LVGrayDrawBuf &buf, unsigned char *gray, int width, int height, int stride)
{
    int row = buf.GetRowSize();
    int copy = row < width ? row : width;
    if (copy > stride)
        copy = stride;
    for (int y = 0; y < height; y++) {
        const lUInt8 *src = buf.GetScanLine(y);
        if (!src)
            break;
        memcpy(gray + (size_t)y * (size_t)stride, src, (size_t)copy);
    }
}

static int cre_draw_into(unsigned char *gray, int width, int height, int stride, int wrap)
{
    clock_t t0 = clock();
    debug_log_mem("before_Draw");
    reader_log("cre_draw: calling Draw wrap=%d %dx%d stride=%d", wrap, width, height, stride);
    /* Boot marker + fsync so a hard kill still leaves a breadcrumb. */
    boot_marker(wrap ? "naskilla: Draw wrap begin" : "naskilla: Draw scratch begin");
    try {
        if (wrap) {
            /* Must use autoResize=false — Resize on a wrapped buffer reallocates. */
            LVGrayDrawBuf buf(width, height, 8, gray);
            if (buf.GetRowSize() != stride) {
                reader_log("cre_draw: stride mismatch row=%d stride=%d", buf.GetRowSize(), stride);
                return -1;
            }
            g_view->Draw(buf, false);
        } else {
            LVGrayDrawBuf scratch(width, height, 8);
            g_view->Draw(scratch, false);
            cre_copy_gray(scratch, gray, width, height, stride);
        }
    } catch (...) {
        reader_log("cre_draw: Draw exception");
        boot_marker("naskilla: Draw exception");
        debug_log_mem("after_Draw_exc");
        return -1;
    }
    boot_marker("naskilla: Draw end");
    {
        double ms = (double)(clock() - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
        reader_log("cre_draw: Draw ok %.1fms wrap=%d", ms, wrap);
    }
    debug_log_mem("after_Draw");
    return 0;
}

static int cre_draw(unsigned char *gray, int width, int height, int stride)
{
    int probe;
    int saved_page;

    if (!g_view || !gray || width <= 0 || height <= 0 || stride < width)
        return -1;

    /* Orientation / buffer mismatch — refuse rather than overflow a wrap. */
    if (width != g_width || height != g_height) {
        reader_log("cre_draw: size mismatch req=%dx%d view=%dx%d",
                   width, height, g_width, g_height);
        return -1;
    }

    if (g_debug.render == RENDER_TEXT || g_debug.force_text) {
        reader_log("cre_draw: text mode skip bitmap");
        return -1;
    }
    if (g_debug.render == RENDER_PATTERN) {
        reader_log("cre_draw: pattern mode — UI owns the buffer");
        return -1;
    }

    probe = g_debug.draw_probe;
    cre_log_draw_diag(probe <= 1 ? "probe_diag" : "probe_draw");

    if (probe <= 0) {
        reader_log("cre_draw: probe 0 — no Draw");
        return -1;
    }
    if (probe == 1) {
        reader_log("cre_draw: probe 1 — header-off check only hdr=%d h=%d",
                   g_view->getPageHeaderInfo(), g_view->getPageHeaderHeight());
        return -1;
    }

    saved_page = g_view->getCurPage();
    if (probe == 5) {
        /* Cover / first page (images). */
        g_view->setShowCover(true);
        g_view->goToPage(0);
    } else if (probe >= 2 && probe <= 4) {
        /* Mid-book text page — avoid cover. */
        int pc = g_view->getPageCount();
        int mid = pc > 4 ? pc / 5 : (pc > 1 ? 1 : 0);
        if (mid != saved_page)
            g_view->goToPage(mid);
    }

    {
        /*
         * Wrap (LVGrayDrawBuf over page_buf) crashes hard on PB Era / FW 6.10
         * inside Draw — boot log stops at "Draw wrap begin", draw_inflight
         * left behind. Probe 6 therefore uses the same scratch+copy path as
         * probe 3/4. Keep wrap=1 only via cre_draw_into for experiments.
         */
        int wrap = 0;
        int copy_out = (probe >= 3) ? 1 : 0;
        int rc;

        if (!copy_out) {
            /* Probe 2: Draw into scratch, discard — prove Draw alone. */
            try {
                debug_log_mem("before_Draw_scratch");
                reader_log("cre_draw: probe 2 Draw scratch only");
                LVGrayDrawBuf scratch(width, height, 8);
                g_view->Draw(scratch, false);
                reader_log("cre_draw: probe 2 Draw ok");
                debug_log_mem("after_Draw_scratch");
            } catch (...) {
                reader_log("cre_draw: probe 2 exception");
                g_view->goToPage(saved_page);
                return -1;
            }
            g_view->goToPage(saved_page);
            return -1;
        }

        rc = cre_draw_into(gray, width, height, stride, wrap);
        /* Probe 2–4 jumped to a mid-book page to avoid the cover; restore. */
        if (probe >= 2 && probe <= 4)
            g_view->goToPage(saved_page);
        if (probe == 5 && rc != 0)
            g_view->setShowCover(false);
        return rc;
    }
}

static int cre_get_position(char *buf, size_t buflen)
{
    if (!g_view || !buf || buflen == 0)
        return -1;
    ldomXPointer bm = g_view->getBookmark();
    if (bm.isNull()) {
        buf[0] = '\0';
        return -1;
    }
    lString8 utf = UnicodeToUtf8(bm.toString());
    snprintf(buf, buflen, "%s", utf.c_str());
    return 0;
}

static int cre_set_position(const char *text)
{
    if (!g_view || !text || !text[0])
        return -1;
    ldomXPointer p = g_view->getDocument()->createXPointer(u32(text));
    if (p.isNull())
        return -1;
    g_view->goToBookmark(p);
    return 0;
}

static void walk_toc(LVTocItem *item, engine_toc_item *out, int *count, int max_items)
{
    if (!item || *count >= max_items)
        return;
    for (int i = 0; i < item->getChildCount(); i++) {
        LVTocItem *ch = item->getChild(i);
        if (!ch)
            continue;
        if (*count >= max_items)
            break;
        lString8 title = UnicodeToUtf8(ch->getName());
        lString8 path = UnicodeToUtf8(ch->getPath());
        snprintf(out[*count].title, ENGINE_TITLE_MAX, "%s", title.c_str());
        snprintf(out[*count].position, ENGINE_POS_MAX, "%s", path.c_str());
        (*count)++;
        walk_toc(ch, out, count, max_items);
    }
}

static int cre_contents(engine_toc_item *items, int max_items)
{
    if (!g_view || !items || max_items <= 0)
        return 0;
    LVTocItem *root = g_view->getToc();
    int count = 0;
    if (root)
        walk_toc(root, items, &count, max_items);
    return count;
}

static int cre_page_text_at(int page, char *buf, size_t buflen)
{
    if (!g_view || !buf || buflen < 2 || page < 1)
        return -1;
    buf[0] = '\0';
    try {
        int idx = page - 1; /* crengine page index is 0-based */
        int pc = g_view->getPageCount();
        if (idx < 0 || idx >= pc)
            return -1;
        lString32 text = g_view->getPageText(true, idx);
        if (text.empty())
            return -1;
        lString8 utf = UnicodeToUtf8(text);
        snprintf(buf, buflen, "%s", utf.c_str());
        return 0;
    } catch (...) {
        reader_log("cre_page_text_at: exception");
        return -1;
    }
}

static int cre_page_text(char *buf, size_t buflen)
{
    if (!g_view || !buf || buflen < 2)
        return -1;
    buf[0] = '\0';
    try {
        lString32 text = g_view->getPageText(true, -1);
        if (text.empty()) {
            reader_log("cre_page_text: empty");
            return -1;
        }
        lString8 utf = UnicodeToUtf8(text);
        snprintf(buf, buflen, "%s", utf.c_str());
        reader_log("cre_page_text: %d bytes", (int)strlen(buf));
        return 0;
    } catch (...) {
        reader_log("cre_page_text: exception");
        return -1;
    }
}

static int cre_is_reflowable(void) { return 1; }
static const char *cre_name(void) { return "crengine"; }

static int cre_find(const char *query, int reverse)
{
    ldomXRangeList ranges;
    lString32 pattern;
    if (!g_view || !g_view->getDocument() || !query || !query[0])
        return -1;
    pattern = u32(query);
    ranges.clear();
    if (!g_view->getDocument()->findText(pattern, true, reverse != 0,
                                         -1, -1, ranges, 1, -1))
        return -1;
    if (ranges.length() <= 0 || !ranges[0])
        return -1;
    g_view->goToBookmark(ranges[0]->getStart());
    return 0;
}

static int cre_chapter_title(char *buf, size_t buflen)
{
    LVTocItem *root;
    LVTocItem *best = NULL;
    int cur;
    int i;
    if (!g_view || !buf || buflen == 0)
        return -1;
    buf[0] = '\0';
    root = g_view->getToc();
    if (!root)
        return -1;
    cur = g_view->getCurPage();
    /* Flatten: walk children of root; pick last item whose page <= cur. */
    for (i = 0; i < root->getChildCount(); i++) {
        LVTocItem *ch = root->getChild(i);
        if (!ch)
            continue;
        if (ch->getPage() <= cur)
            best = ch;
        /* one level of nesting */
        for (int j = 0; j < ch->getChildCount(); j++) {
            LVTocItem *g = ch->getChild(j);
            if (g && g->getPage() <= cur)
                best = g;
        }
    }
    if (!best)
        return -1;
    {
        lString8 title = UnicodeToUtf8(best->getName());
        snprintf(buf, buflen, "%s", title.c_str());
    }
    return 0;
}

extern const reader_engine engine_crengine = {
    cre_open,
    cre_close,
    cre_set_layout,
    cre_page_count,
    cre_current_page,
    cre_go_to,
    cre_next,
    cre_prev,
    cre_draw,
    cre_page_text,
    cre_page_text_at,
    cre_get_position,
    cre_set_position,
    cre_contents,
    cre_is_reflowable,
    cre_name,
    cre_find,
    cre_chapter_title
};
