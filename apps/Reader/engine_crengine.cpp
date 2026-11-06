/*
 * crengine adapter — only translation unit that includes LVDocView headers.
 */
#include "engine.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

extern "C" int engine_global_init(const char *cache_dir, const char *fonts_dir)
{
    if (g_inited)
        return 0;
    if (!InitFontManager(lString8())) {
        reader_log("crengine: InitFontManager failed");
        return -1;
    }
    g_fonts_dir = fonts_dir ? fonts_dir : "";
    if (fonts_dir && fonts_dir[0]) {
        char path[512];
        const char *faces[] = {
            "Bookerly-Regular.ttf", "Bookerly-Bold.ttf", "Bookerly-Italic.ttf",
            "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf", NULL
        };
        for (int i = 0; faces[i]; i++) {
            snprintf(path, sizeof(path), "%s/%s", fonts_dir, faces[i]);
            fontMan->RegisterFont(u8(path));
        }
    }
    /* Also register common system fonts. */
    fontMan->RegisterFont(u8("/usr/share/fonts/ttf/liberation/LiberationSans-Regular.ttf"));
    fontMan->RegisterFont(u8("/usr/share/fonts/ttf/liberation/LiberationSans-Bold.ttf"));

    if (cache_dir && cache_dir[0]) {
        /* 64 MB disk cache; storage factor applied via CRE defaults. */
        ldomDocCache::init(u32(cache_dir), 64 * 1024 * 1024);
    }

    HyphMan::initDictionaries(u32("/mnt/ext1/system/share/crengine/hyph"), true);
    g_inited = 1;
    return 0;
}

extern "C" void engine_global_shutdown(void)
{
    if (g_view) {
        delete g_view;
        g_view = NULL;
    }
    if (g_inited) {
        ldomDocCache::close();
        HyphMan::uninit();
        ShutdownFontManager();
        g_inited = 0;
    }
}

static int cre_open(const char *path, int width, int height)
{
    if (!g_inited)
        return -1;
    if (g_view) {
        delete g_view;
        g_view = NULL;
    }
    g_view = new LVDocView(8, false); /* 8 bpp gray */
    g_view->setViewMode(DVM_PAGES, 1);
    g_width = width;
    g_height = height;
    g_view->Resize(width, height);

    CRPropRef props = g_view->propsGetCurrent();
    props->setInt(PROP_STATUS_LINE, 0);
    props->setInt(PROP_SHOW_TIME, 0);
    props->setInt(PROP_SHOW_BATTERY, 0);
    props->setInt(PROP_SHOW_TITLE, 0);
    props->setInt(PROP_SHOW_PAGE_NUMBER, 0);
    props->setInt(PROP_SHOW_PAGE_COUNT, 0);
    g_view->propsApply(props);

    if (!g_view->LoadDocument(path)) {
        reader_log("crengine: LoadDocument failed for %s", path);
        delete g_view;
        g_view = NULL;
        return -2;
    }

    /* Hyphenation from book language. */
    lString32 lang = g_view->getLanguage();
    if (lang.empty())
        lang = lString32("en");
    {
        lString32 id = lString32("English_US.pattern");
        if (lang == lString32("pl") || lang.startsWith(lString32("pl-")))
            id = lString32("Polish.pattern");
        HyphMan::activateDictionary(id);
    }

    g_view->Render();
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
    if (!g_view || !gray)
        return -1;
    if (width != g_width || height != g_height) {
        g_width = width;
        g_height = height;
        g_view->Resize(width, height);
        g_view->Render();
    }
    LVGrayDrawBuf buf(width, height, 8);
    g_view->Draw(buf, false);
    for (int y = 0; y < height; y++) {
        lUInt8 *src = buf.GetScanLine(y);
        unsigned char *dst = gray + y * stride;
        if (src)
            memcpy(dst, src, (size_t)width);
        else
            memset(dst, 0xFF, (size_t)width);
    }
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
    cre_get_position,
    cre_set_position,
    cre_contents,
    cre_is_reflowable,
    cre_name
};
