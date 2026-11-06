/*
 * crengine adapter — only translation unit that includes LVDocView headers.
 */
#include "engine.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>
#include <dirent.h>

#include "crengine.h"
#include "lvdocview.h"
#include "lvdocviewprops.h"
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

extern "C" int engine_global_init(const char *cache_dir, const char *fonts_dir)
{
    if (g_inited)
        return 0;

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
        props->setInt(PROP_STATUS_LINE, 0);
        props->setInt(PROP_SHOW_TIME, 0);
        props->setInt(PROP_SHOW_BATTERY, 0);
        props->setInt(PROP_SHOW_TITLE, 0);
        props->setInt(PROP_SHOW_PAGE_NUMBER, 0);
        props->setInt(PROP_SHOW_PAGE_COUNT, 0);
        g_view->propsApply(props);

        reader_log("cre_open: LoadDocument...");
        if (!g_view->LoadDocument(path)) {
            reader_log("crengine: LoadDocument failed for %s", path);
            delete g_view;
            g_view = NULL;
            return -2;
        }
        reader_log("cre_open: LoadDocument ok, fonts=%d",
                   fontMan ? fontMan->GetFontCount() : -1);
        /* Prefer a face we know we registered. */
        g_view->setDefaultFontFace(u8("Bookerly"));
        g_view->setDefaultFontFace(u8("Liberation Sans"));
        g_view->setDefaultFontFace(u8("LiberationSans"));
        reader_log("cre_open: Render...");
        g_view->Render();
        reader_log("cre_open: Render ok pages=%d", g_view->getPageCount());
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

    g_view->setFontSize(layout->font_size);
    if (layout->font_face && layout->font_face[0])
        g_view->setDefaultFontFace(u8(layout->font_face));

    int m = layout->margin;
    g_view->setPageMargins(lvRect(m, m, m, m));

    CRPropRef props = g_view->propsGetCurrent();
    props->setInt(PROP_INTERLINE_SPACE, layout->line_spacing);
    props->setInt(PROP_EMBEDDED_STYLES, layout->publisher_styles ? 1 : 0);
    props->setInt(PROP_EMBEDDED_FONTS, layout->publisher_styles ? 1 : 0);
    if (layout->night) {
        props->setColor(PROP_FONT_COLOR, 0xFFFFFF);
        props->setColor(PROP_BACKGROUND_COLOR, 0x000000);
    } else {
        props->setColor(PROP_FONT_COLOR, 0x000000);
        props->setColor(PROP_BACKGROUND_COLOR, 0xFFFFFF);
    }
    g_view->propsApply(props);
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

static int cre_draw(unsigned char *gray, int width, int height, int stride)
{
    /* TEMP: full-page LVDocView::Draw hard-kills the process on Era
     * (log stops at "calling Draw"; SIGSEGV handler never runs → likely
     * SIGKILL/OOM). Until a safe blit path exists, skip bitmap Draw and
     * let the UI show page number via inkview text. Pagination still works. */
    if (!g_view || !gray)
        return -1;
    reader_log("cre_draw: SKIP bitmap Draw %dx%d fonts=%d pages=%d cur=%d",
               width, height,
               fontMan ? fontMan->GetFontCount() : -1,
               g_view->getPageCount(), g_view->getCurPage() + 1);
    memset(gray, 0xFF, (size_t)stride * (size_t)height);
    return 0;
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
    cre_get_position,
    cre_set_position,
    cre_contents,
    cre_is_reflowable,
    cre_name
};
