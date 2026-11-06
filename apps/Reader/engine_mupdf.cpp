/*
 * MuPDF adapter for PDF — implements the same reader_engine table.
 */
#include "engine.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#include <mupdf/fitz.h>
}

static fz_context *g_ctx = NULL;
static fz_document *g_doc = NULL;
static fz_outline *g_outline = NULL;
static int g_page = 0;       /* 0-based */
static int g_pages = 0;
static int g_width = 0;
static int g_height = 0;
static int g_night = 0;

static void mupdf_drop_doc(void)
{
    if (g_outline && g_ctx) {
        fz_drop_outline(g_ctx, g_outline);
        g_outline = NULL;
    }
    if (g_doc && g_ctx) {
        fz_drop_document(g_ctx, g_doc);
        g_doc = NULL;
    }
    g_page = 0;
    g_pages = 0;
}

static int pdf_open(const char *path, int width, int height)
{
    if (!g_ctx) {
        g_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
        if (!g_ctx) {
            reader_log("mupdf: fz_new_context failed");
            return -1;
        }
        fz_register_document_handlers(g_ctx);
    }
    mupdf_drop_doc();
    g_width = width;
    g_height = height;

    fz_try(g_ctx) {
        g_doc = fz_open_document(g_ctx, path);
        g_pages = fz_count_pages(g_ctx, g_doc);
        g_outline = fz_load_outline(g_ctx, g_doc);
        g_page = 0;
    }
    fz_catch(g_ctx) {
        reader_log("mupdf: open failed for %s: %s", path, fz_caught_message(g_ctx));
        mupdf_drop_doc();
        return -2;
    }
    if (g_pages <= 0) {
        mupdf_drop_doc();
        return -2;
    }
    return 0;
}

static void pdf_close(void)
{
    mupdf_drop_doc();
}

static int pdf_set_layout(const engine_layout *layout)
{
    if (!layout)
        return -1;
    g_night = layout->night ? 1 : 0;
    return 0;
}

static int pdf_page_count(void) { return g_pages; }
static int pdf_current_page(void) { return g_pages > 0 ? g_page + 1 : 0; }

static int pdf_go_to(int page)
{
    if (!g_doc || g_pages <= 0)
        return -1;
    int n = page - 1;
    if (n < 0) n = 0;
    if (n >= g_pages) n = g_pages - 1;
    g_page = n;
    return 0;
}

static int pdf_next(void)
{
    if (!g_doc || g_page + 1 >= g_pages)
        return -1;
    g_page++;
    return 0;
}

static int pdf_prev(void)
{
    if (!g_doc || g_page <= 0)
        return -1;
    g_page--;
    return 0;
}

static int pdf_draw(unsigned char *gray, int width, int height, int stride)
{
    fz_page *page = NULL;
    fz_pixmap *pix = NULL;
    fz_matrix ctm;
    fz_rect bounds;
    float scale, sx, sy;
    int x, y, dx, dy;

    if (!g_ctx || !g_doc || !gray)
        return -1;
    g_width = width;
    g_height = height;
    memset(gray, g_night ? 0x00 : 0xFF, (size_t)stride * (size_t)height);

    fz_try(g_ctx) {
        page = fz_load_page(g_ctx, g_doc, g_page);
        bounds = fz_bound_page(g_ctx, page);
        sx = (float)width / (bounds.x1 - bounds.x0);
        sy = (float)height / (bounds.y1 - bounds.y0);
        scale = sx < sy ? sx : sy;
        ctm = fz_scale(scale, scale);
        pix = fz_new_pixmap_from_page(g_ctx, page, ctm, fz_device_gray(g_ctx), 0);
        dx = (width - pix->w) / 2;
        dy = (height - pix->h) / 2;
        for (y = 0; y < pix->h; y++) {
            int dy2 = y + dy;
            if (dy2 < 0 || dy2 >= height)
                continue;
            unsigned char *src = &pix->samples[y * pix->stride];
            unsigned char *dst = gray + dy2 * stride + dx;
            for (x = 0; x < pix->w && (x + dx) < width; x++) {
                unsigned char v = src[x];
                if (g_night)
                    v = (unsigned char)(255 - v);
                if (x + dx >= 0)
                    dst[x] = v;
            }
        }
    }
    fz_always(g_ctx) {
        if (pix) fz_drop_pixmap(g_ctx, pix);
        if (page) fz_drop_page(g_ctx, page);
    }
    fz_catch(g_ctx) {
        reader_log("mupdf: draw failed: %s", fz_caught_message(g_ctx));
        return -1;
    }
    return 0;
}

static int pdf_get_position(char *buf, size_t buflen)
{
    if (!buf || buflen == 0)
        return -1;
    snprintf(buf, buflen, "%d", g_page + 1);
    return 0;
}

static int pdf_set_position(const char *text)
{
    int page;
    if (!text || sscanf(text, "%d", &page) != 1)
        return -1;
    return pdf_go_to(page);
}

static void walk_outline(fz_outline *o, engine_toc_item *items, int *count, int max_items)
{
    for (; o && *count < max_items; o = o->next) {
        snprintf(items[*count].title, ENGINE_TITLE_MAX, "%s",
                 o->title ? o->title : "Section");
        if (o->page.page >= 0)
            snprintf(items[*count].position, ENGINE_POS_MAX, "%d", o->page.page + 1);
        else
            snprintf(items[*count].position, ENGINE_POS_MAX, "1");
        (*count)++;
        if (o->down)
            walk_outline(o->down, items, count, max_items);
    }
}

static int pdf_contents(engine_toc_item *items, int max_items)
{
    int count = 0;
    if (!items || max_items <= 0)
        return 0;
    if (g_outline)
        walk_outline(g_outline, items, &count, max_items);
    return count;
}

static int pdf_page_text(char *buf, size_t buflen)
{
    (void)buf;
    (void)buflen;
    return -1;
}

static int pdf_is_reflowable(void) { return 0; }
static const char *pdf_name(void) { return "mupdf"; }

extern const reader_engine engine_mupdf = {
    pdf_open,
    pdf_close,
    pdf_set_layout,
    pdf_page_count,
    pdf_current_page,
    pdf_go_to,
    pdf_next,
    pdf_prev,
    pdf_draw,
    pdf_page_text,
    pdf_get_position,
    pdf_set_position,
    pdf_contents,
    pdf_is_reflowable,
    pdf_name
};
