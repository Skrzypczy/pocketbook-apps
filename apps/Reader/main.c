#define _POSIX_C_SOURCE 200809L
#include "ui.h"
#include "frontlight.h"
#include "frontlight_peek.h"
#include "refresh.h"
#include "layout.h"
#include "debug.h"
#include "extensions.h"
#include "keymap.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "inkview.h"

app_state g_app;

static void update_clock_string(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(g_app.clock, sizeof(g_app.clock), "%H:%M", &tm_now);
}

static int ensure_page_buffer(int w, int h)
{
    size_t need;
    if (w <= 0 || h <= 0)
        return -1;
    if (g_app.page_buf && g_app.buf_w == w && g_app.buf_h == h)
        return 0;
    free(g_app.page_buf);
    g_app.page_buf = NULL;
    g_app.buf_stride = w;
    need = (size_t)w * (size_t)h;
    g_app.page_buf = (unsigned char *)malloc(need);
    if (!g_app.page_buf) {
        ui_set_message("Out of memory");
        return -1;
    }
    memset(g_app.page_buf, 0xFF, need);
    g_app.buf_w = w;
    g_app.buf_h = h;
    return 0;
}

static void blit_gray_to_screen(int dst_y)
{
    icanvas *c;
    int y;

    if (!g_app.page_buf || g_app.buf_w <= 0 || g_app.buf_h <= 0)
        return;

    /* Prefer direct canvas copy when depths match — Stretch(IMAGE_GRAY8)
     * has crashed on some firmware after a successful cre_draw. */
    c = GetCanvas();
    if (c && c->addr && c->depth == 8 && c->width >= g_app.buf_w) {
        for (y = 0; y < g_app.buf_h; y++) {
            int dy = dst_y + y;
            if (dy < 0 || dy >= c->height)
                continue;
            memcpy(c->addr + dy * c->scanline,
                   g_app.page_buf + y * g_app.buf_stride,
                   (size_t)g_app.buf_w);
        }
        return;
    }

    Stretch(g_app.page_buf, IMAGE_GRAY8, g_app.buf_w, g_app.buf_h,
            g_app.buf_stride, 0, dst_y, g_app.buf_w, g_app.buf_h, 0);
}

void ui_apply_layout(void)
{
    const reader_engine *eng = engine_active();
    engine_layout layout;
    if (!eng)
        return;
    memset(&layout, 0, sizeof(layout));
    layout.font_size = g_app.cfg.font_size;
    layout.margin = g_app.cfg.margin;
    layout.line_spacing = g_app.cfg.line_spacing;
    layout.night = g_app.cfg.night;
    layout.publisher_styles = g_app.cfg.publisher_styles;
    layout.font_face = g_app.cfg.font_face;
    layout.page_w = g_app.buf_w;
    layout.page_h = g_app.buf_h;
    eng->set_layout(&layout);
}

static unsigned long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)ts.tv_nsec / 1000000UL;
}

static void update_bar_label(void)
{
    const reader_engine *eng = engine_active();
    int page, pages;
    long eta_sec = -1;

    g_app.bar_pages_label[0] = '\0';
    if (!eng)
        return;
    page = eng->current_page();
    pages = eng->page_count();
    if (g_app.turn_gap_count >= 3 && pages > page) {
        int i, sum = 0, n = g_app.turn_gap_count;
        for (i = 0; i < n; i++)
            sum += g_app.turn_gaps_ms[i];
        if (sum > 0)
            eta_sec = (long)(((long long)(pages - page) * sum / n) / 1000LL);
    }
    bar_format_progress(g_app.cfg.bar_progress, page, pages, eta_sec,
                        g_app.bar_pages_label, sizeof(g_app.bar_pages_label));
}

void ui_save_position(void)
{
    const reader_engine *eng = engine_active();
    char pos[ENGINE_POS_MAX];
    if (!eng || !g_app.open_path[0])
        return;
    if (eng->get_position(pos, sizeof(pos)) == 0)
        config_set_position(g_app.open_path, pos);
    config_set_progress(g_app.open_path, eng->current_page(), eng->page_count());
}

void ui_remember_jump(void)
{
    const reader_engine *eng = engine_active();
    if (!eng)
        return;
    if (eng->get_position(g_app.jump_back_pos, sizeof(g_app.jump_back_pos)) == 0) {
        g_app.jump_back_page = eng->current_page();
        g_app.jump_back_valid = 1;
    }
}

void ui_set_message(const char *msg)
{
    snprintf(g_app.message, sizeof(g_app.message), "%s", msg ? msg : "");
    g_app.screen = SCREEN_MESSAGE;
}

static void draw_message(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    SetFont(g_app.font_msg, fg);
    DrawTextRect(40, sh / 2 - 40, sw - 80, 80, g_app.message,
                 ALIGN_CENTER | VALIGN_MIDDLE);
    SetFont(g_app.font_ui, fg);
    DrawTextRect(40, sh - ui_action_h(), sw - 80, ui_action_h(), "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

static ifont *g_body_font;
static int g_body_size;
static int g_font_ui_px;
static int g_font_bar_px;
static int g_font_msg_px;

static int clamp_px(int sz, int lo, int hi)
{
    if (sz < lo) return lo;
    if (sz > hi) return hi;
    return sz;
}

static void reopen_font(ifont **slot, int *cached, int px, int aa)
{
    if (*slot && *cached == px)
        return;
    if (*slot) {
        CloseFont(*slot);
        *slot = NULL;
    }
    *slot = OpenFont("LiberationSans", px, aa);
    if (!*slot)
        *slot = OpenFont("LiberationSans", px > 20 ? 20 : px, aa);
    *cached = px;
}

void ui_apply_fonts(void)
{
    int body = g_app.cfg.font_size;
    int row = ui_row_h();
    int menu;
    int title;
    int bar;

    if (body < CFG_FONT_MIN)
        body = CFG_FONT_MIN;
    if (body > CFG_FONT_MAX)
        body = CFG_FONT_MAX;
    /* Same setting as the book, capped so one line still fits the menu row. */
    menu = body;
    if (menu > row - 20)
        menu = row - 20;
    menu = clamp_px(menu, 20, 64);
    title = menu + 4;
    if (title > row - 12)
        title = row - 12;
    title = clamp_px(title, 22, 72);
    /* Five labels share the bar, so this stays smaller than the menus. */
    bar = body * 2 / 3;
    if (bar > ui_bar_h() - 16)
        bar = ui_bar_h() - 16;
    bar = clamp_px(bar, 16, 26);
    reopen_font(&g_app.font_ui, &g_font_ui_px, menu, 0);
    reopen_font(&g_app.font_msg, &g_font_msg_px, title, 1);
    reopen_font(&g_app.font_bar, &g_font_bar_px, bar, 0);
}

static ifont *body_font(void)
{
    int sz = g_app.cfg.font_size;
    if (sz < CFG_FONT_MIN)
        sz = CFG_FONT_MIN;
    if (sz > CFG_FONT_MAX)
        sz = CFG_FONT_MAX;
    if (g_body_font && g_body_size == sz)
        return g_body_font;
    if (g_body_font) {
        CloseFont(g_body_font);
        g_body_font = NULL;
    }
    g_body_font = OpenFont("LiberationSans", sz, 0);
    if (!g_body_font)
        g_body_font = OpenFont("LiberationSans", sz > 24 ? 24 : sz, 0);
    g_body_size = sz;
    reader_log("body font size=%d handle=%p", sz, (void *)g_body_font);
    return g_body_font ? g_body_font : g_app.font_ui;
}

static void fill_test_pattern(unsigned char *buf, int w, int h, int stride)
{
    int x, y;
    if (!buf || w <= 0 || h <= 0)
        return;
    for (y = 0; y < h; y++) {
        unsigned char *row = buf + (size_t)y * (size_t)stride;
        for (x = 0; x < w; x++) {
            unsigned char v;
            if (y < 8 || y >= h - 8 || x < 8 || x >= w - 8)
                v = 0x00;
            else if (y < h / 4)
                v = (unsigned char)((x * 255) / (w > 1 ? w - 1 : 1));
            else if (y < h / 2)
                v = (x / 40) & 1 ? 0x00 : 0xFF;
            else if (y < (3 * h) / 4)
                v = (unsigned char)(((y - h / 2) * 255) / (h / 4 > 0 ? h / 4 : 1));
            else
                v = (x == y % w) ? 0x00 : 0xE0;
            row[x] = v;
        }
    }
}

static void draw_reading(int full)
{
    const reader_engine *eng = engine_active();
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int page_h;
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int page = 0, pages = 0;
    int draw_rc = -1;
    int use_bitmap = 0;
    int use_text = 0;
    icanvas *c;
    static int s_sentinel_armed;

    ui_apply_fonts();
    bar_compute(&g_app.bar, sw, sh, ui_bar_h());
    page_h = sh - ui_bar_h();
    if (!eng) {
        ui_set_message("No book open");
        draw_message();
        refresh_full();
        return;
    }
    if (ensure_page_buffer(sw, page_h) != 0) {
        draw_message();
        refresh_full();
        return;
    }

    c = GetCanvas();
    reader_log("draw_reading: canvas depth=%d scan=%d %dx%d render=%d probe=%d",
               c ? c->depth : -1, c ? c->scanline : -1,
               c ? c->width : -1, c ? c->height : -1,
               (int)g_debug.render, g_debug.draw_probe);

    if (g_debug.render == RENDER_PATTERN) {
        fill_test_pattern(g_app.page_buf, sw, page_h, g_app.buf_stride);
        draw_rc = 0;
        use_bitmap = 1;
        reader_log("draw_reading: synthetic pattern ready");
    } else {
        if (!s_sentinel_armed &&
            (g_debug.render == RENDER_AUTO && !g_debug.force_text)) {
            debug_sentinel_begin(g_app.open_path);
            s_sentinel_armed = 1;
        }
        boot_marker("naskilla: eng->draw");
        debug_log_mem("before_eng_draw");
        draw_rc = eng->draw(g_app.page_buf, sw, page_h, g_app.buf_stride);
        reader_log("draw_reading: eng->draw rc=%d", draw_rc);
        boot_marker("naskilla: eng->draw done");
        debug_log_mem("after_eng_draw");
        if (draw_rc == 0)
            use_bitmap = 1;
        if (s_sentinel_armed) {
            debug_sentinel_clear();
            s_sentinel_armed = 0;
            /* A completed Draw means it is safe to leave text-sticky mode. */
            if (draw_rc == 0 && g_debug.render == RENDER_AUTO)
                debug_clear_draw_safe();
        }
    }

    page = eng->current_page();
    pages = eng->page_count();

    boot_marker("naskilla: clear+blit");
    {
        static char page_text[24000];
        int m = ui_margin();
        page_text[0] = '\0';
        if (!use_bitmap && eng->page_text &&
            eng->page_text(page_text, sizeof(page_text)) == 0 && page_text[0])
            use_text = 1;

        ClearScreen();
        FillArea(0, 0, sw, sh, bg);
        if (use_bitmap)
            blit_gray_to_screen(ui_bar_h());
        boot_marker("naskilla: blit done");
        if (g_app.font_bar)
            SetFont(g_app.font_bar, fg);
        update_clock_string();
        update_bar_label();
        bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
                 g_app.light_available, page, pages, fg, bg,
                 g_app.bar_pages_label);
        if (use_text) {
            ifont *body = body_font();
            FillArea(0, ui_bar_h(), sw, page_h, bg);
            if (body)
                SetFont(body, fg);
            /* One crengine page only. Do not append the next page: Inkview
             * metrics are denser, so the block ends early, but page turns
             * must not repeat that text. Publisher layout does not affect
             * this fallback — it only toggles the book's own CSS. */
            DrawTextRect(m, ui_bar_h() + m / 2, sw - 2 * m, page_h - m,
                         page_text, ALIGN_LEFT | VALIGN_TOP);
        }
    }
    boot_marker("naskilla: bar done");

    if (full) {
        refresh_full();
        g_app.pages_since_full = 0;
    } else {
        refresh_partial(0, ui_bar_h(), sw, page_h);
        refresh_partial(0, 0, sw, ui_bar_h());
        g_app.pages_since_full++;
        if (g_app.pages_since_full >= g_app.cfg.full_refresh) {
            refresh_full();
            g_app.pages_since_full = 0;
        }
    }
    boot_marker("naskilla: refresh done");
}

void ui_draw(void)
{
    ui_apply_fonts();
    switch (g_app.screen) {
    case SCREEN_HOME:
        home_draw();
        refresh_full();
        break;
    case SCREEN_BROWSE:
        library_draw();
        refresh_full();
        break;
    case SCREEN_READING:
        draw_reading(1);
        break;
    case SCREEN_SETTINGS:
        settings_draw();
        refresh_full();
        break;
    case SCREEN_CONTENTS:
        contents_draw();
        refresh_full();
        break;
    case SCREEN_FONTS:
        fonts_draw();
        refresh_full();
        break;
    case SCREEN_MESSAGE:
        draw_message();
        refresh_full();
        break;
    case SCREEN_SUGGEST_ASK:
        suggest_ask_draw();
        refresh_full();
        break;
    case SCREEN_SUGGEST_RESULTS:
        suggest_results_draw();
        refresh_full();
        break;
    }
}

void ui_show_home(void)
{
    g_app.screen = SCREEN_HOME;
    ui_draw();
}

void ui_show_browse(browse_root root)
{
    if (root == BROWSE_LIBRARY) {
        int sw = ScreenWidth();
        int sh = ScreenHeight();
        int fg = g_app.cfg.night ? WHITE : BLACK;
        int bg = g_app.cfg.night ? BLACK : WHITE;
        ui_apply_fonts();
        ClearScreen();
        FillArea(0, 0, sw, sh, bg);
        if (g_app.font_msg)
            SetFont(g_app.font_msg, fg);
        DrawTextRect(0, sh / 2 - 20, sw, 40, "Scanning library",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        refresh_full();
    } else if (root == BROWSE_RECENT) {
        int sw = ScreenWidth();
        int sh = ScreenHeight();
        int fg = g_app.cfg.night ? WHITE : BLACK;
        int bg = g_app.cfg.night ? BLACK : WHITE;
        ui_apply_fonts();
        ClearScreen();
        FillArea(0, 0, sw, sh, bg);
        if (g_app.font_msg)
            SetFont(g_app.font_msg, fg);
        DrawTextRect(0, sh / 2 - 20, sw, 40, "Reading titles",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        refresh_partial(0, sh / 2 - 20, sw, 40);
    }
    library_scan(root);
    g_app.screen = SCREEN_BROWSE;
    ui_draw();
}

static void write_exact(const char *path, const char *text)
{
    FILE *fp;
    if (!path || !text)
        return;
    fp = fopen(path, "wb");
    if (!fp)
        return;
    fwrite(text, 1, strlen(text), fp);
    fclose(fp);
}

/* The desktop cover is the book recorded on this task, not the book
 * NaSkilla happens to have open. Same pair KOReader uses: /tmp/.current
 * plus SetSubtaskInfo. lastopen.txt is the recent-books list. */
static void notify_system_book(const char *path)
{
    bookinfo *info;
    ibitmap *cover;
    char label[192];
    if (!path || !path[0])
        return;
    info = GetBookInfo(path);
    if (info && info->title && info->title[0])
        snprintf(label, sizeof(label), "%s", info->title);
    else
        snprintf(label, sizeof(label), "%s", CFG_APP_NAME);

    write_exact(CURRENTBOOK, path);
    write_exact(CURRENTBOOK_SAVE, path);
    SetSubtaskInfo(GetCurrentTask(), 0, label, path);
    /* Only pin this book to NaSkilla when NaSkilla is the library default.
     * Otherwise opening a book here would override the user's choice. */
    if (extcfg_naskilla_is_default())
        SetFileHandler(path, CFG_APP_PATH);

    cover = GetBookCover(path, 600, 860);
    if (cover)
        free(cover);
    BookPreparing(path);
    AddLastOpen(path);
    FlushLastOpen();
    BookReady(path);
    reader_log("notify home cover %s", path);
}

void ui_open_book(const char *path)
{
    bookinfo *info;
    char pos[ENGINE_POS_MAX];
    char stored[512];
    size_t n;
    int sw, sh, page_h;
    int rc;

    /* Continue passes cfg.last_book. Copy first: snprintf into that same
     * buffer is undefined and was wiping the path before the engine opened. */
    while (path && (*path == ' ' || *path == '\t'))
        path++;
    if (!path || !path[0]) {
        reader_log("ui_open_book: empty path rejected");
        ui_set_message("No book selected");
        ui_draw();
        return;
    }
    snprintf(stored, sizeof(stored), "%s", path);
    n = strlen(stored);
    while (n > 0 && (stored[n - 1] == ' ' || stored[n - 1] == '\t' ||
                     stored[n - 1] == '\r' || stored[n - 1] == '\n')) {
        stored[--n] = '\0';
    }
    path = stored;
    if (!path[0]) {
        reader_log("ui_open_book: empty path rejected");
        ui_set_message("No book selected");
        ui_draw();
        return;
    }
    {
        FILE *probe = fopen(path, "rb");
        if (!probe) {
            reader_log("ui_open_book: cannot read [%s]", path);
            ui_set_message("File not found");
            ui_draw();
            return;
        }
        fclose(probe);
    }
    info = GetBookInfo(path);
    if (info && info->drm) {
        ui_set_message("This book is locked");
        ui_draw();
        return;
    }

    engine_close();
    snprintf(g_app.open_path, sizeof(g_app.open_path), "%s", path);
    snprintf(g_app.last_book, sizeof(g_app.last_book), "%s", path);
    snprintf(g_app.cfg.last_book, sizeof(g_app.cfg.last_book), "%s", path);
    config_remember_book(&g_app.cfg, path);
    if (info) {
        book_meta_put(path,
                      info->title ? info->title : "",
                      info->author ? info->author : "",
                      info->series ? info->series : "",
                      info->numinseries,
                      info->year);
    }
    config_save(&g_app.cfg);

    ui_apply_fonts();
    ClearScreen();
    if (g_app.font_msg)
        SetFont(g_app.font_msg, g_app.cfg.night ? WHITE : BLACK);
    DrawTextRect(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40, "Opening",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    refresh_partial(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40);

    PostponeTimedPoweroff();
    sw = ScreenWidth();
    sh = ScreenHeight();
    page_h = sh - ui_bar_h();
    if (ensure_page_buffer(sw, page_h) != 0) {
        ui_draw();
        return;
    }

    boot_marker("naskilla: before engine_global_init");
    /* Engines stay unloaded until a book is opened (home screen must not depend on them). */
    if (engine_global_init(CFG_CACHE_DIR, CFG_FONTS_DIR) != 0) {
        reader_log("engine_global_init failed");
        ui_set_message("Reader engine failed to start");
        ui_draw();
        return;
    }
    boot_marker("naskilla: after engine_global_init");

    reader_log("ui_open_book: opening %s %dx%d", path, sw, page_h);
    boot_marker("naskilla: before engine_open_path");
    rc = engine_open_path(path, sw, page_h);
    reader_log("ui_open_book: engine_open_path rc=%d", rc);
    boot_marker(rc == 0 ? "naskilla: engine_open_path ok" : "naskilla: engine_open_path FAIL");
    if (rc != 0) {
        reader_log("open failed %d for %s", rc, path);
        ui_set_message("Cannot open");
        ui_draw();
        return;
    }
    /* Bitmap Draw is skipped (OOM). Layout + position only call Render, which is safe. */
    {
        int book_sz = 0;
        if (config_get_book_font_size(path, &book_sz) == 0)
            g_app.cfg.font_size = book_sz;
    }
    g_app.jump_back_valid = 0;
    g_app.turn_gap_count = 0;
    g_app.turn_gap_i = 0;
    g_app.last_turn_ms = 0;
    boot_marker("naskilla: apply_layout");
    ui_apply_layout();
    if (config_get_position(path, pos, sizeof(pos)) == 0 && engine_active())
        engine_active()->set_position(pos);
    {
        const reader_engine *eng = engine_active();
        if (eng)
            g_app.goto_page_tmp = eng->current_page();
    }
    g_app.screen = SCREEN_READING;
    g_app.pages_since_full = 0;
    notify_system_book(path);
    if (g_debug.crash_test) {
        reader_log("crash_test: deliberate SIGSEGV");
        boot_marker("naskilla: crash_test");
        *(volatile int *)0 = 1;
    }
    keymap_reload();
    boot_marker("naskilla: before first draw");
    ui_draw();
    boot_marker("naskilla: first draw done");
}

static void note_page_turn(void)
{
    unsigned long t = now_ms();
    if (g_app.last_turn_ms > 0 && t > g_app.last_turn_ms) {
        int gap = (int)(t - g_app.last_turn_ms);
        if (gap > 200 && gap < 600000) {
            g_app.turn_gaps_ms[g_app.turn_gap_i % 8] = gap;
            g_app.turn_gap_i++;
            if (g_app.turn_gap_count < 8)
                g_app.turn_gap_count++;
        }
    }
    g_app.last_turn_ms = t;
}

static void turn_page(int next)
{
    const reader_engine *eng = engine_active();
    int rc;
    if (!eng || refresh_busy())
        return;
    rc = next ? eng->next() : eng->prev();
    if (rc != 0)
        return;
    note_page_turn();
    ui_save_position();
    draw_reading(0);
}

static void go_to_page_draw(int page)
{
    const reader_engine *eng = engine_active();
    int pages;
    if (!eng || refresh_busy())
        return;
    pages = eng->page_count();
    if (pages < 1)
        return;
    if (page < 1)
        page = 1;
    if (page > pages)
        page = pages;
    if (eng->go_to(page) != 0)
        return;
    note_page_turn();
    ui_save_position();
    draw_reading(0);
}

static void run_key_action(keymap_action act)
{
    const reader_engine *eng;
    int pages;

    if (act == KA_NONE)
        return;
    switch (act) {
    case KA_NEXT:
        turn_page(1);
        break;
    case KA_PREV:
        turn_page(0);
        break;
    case KA_NEXT10:
        eng = engine_active();
        if (!eng)
            return;
        go_to_page_draw(eng->current_page() + 10);
        break;
    case KA_PREV10:
        eng = engine_active();
        if (!eng)
            return;
        go_to_page_draw(eng->current_page() - 10);
        break;
    case KA_FIRST:
        go_to_page_draw(1);
        break;
    case KA_LAST:
        eng = engine_active();
        if (!eng)
            return;
        pages = eng->page_count();
        if (pages > 0)
            go_to_page_draw(pages);
        break;
    case KA_CONTENTS:
        ui_remember_jump();
        contents_load();
        g_app.screen = SCREEN_CONTENTS;
        ui_draw();
        break;
    default:
        break;
    }
}

static void toggle_night(void)
{
    g_app.cfg.night = !g_app.cfg.night;
    config_save(&g_app.cfg);
    ui_apply_layout();
    draw_reading(1);
}

static void refresh_light_button(void)
{
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    const reader_engine *eng = engine_active();

    if (g_app.screen != SCREEN_READING || !g_app.light_available)
        return;
    SetFont(g_app.font_bar, fg);
    update_bar_label();
    bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
             g_app.light_available,
             eng ? eng->current_page() : 0,
             eng ? eng->page_count() : 0, fg, bg,
             g_app.bar_pages_label);
    refresh_partial(g_app.bar.light_x, 0, g_app.bar.light_w, g_app.bar.height);
}

/* Sleep/keylock can turn the lamp off while our UI flag stays on. */
static void sync_light_ui_from_device(void)
{
    int on;

    if (!g_app.light_available || frontlight_peek_active())
        return;
    on = frontlight_is_on() ? 1 : 0;
    if (on == g_app.light_on)
        return;
    g_app.light_on = on;
    g_app.cfg.light_on = on;
    refresh_light_button();
}

/* Peek must not change committed intent; redraw label from g_app.light_on after restore. */
static void on_frontlight_peek_end(void)
{
    refresh_light_button();
}

static void toggle_light(void)
{
    if (!g_app.light_available || refresh_busy())
        return;
    /* Cancel first so the peek timer cannot turn the lamp off after this tap.
     * UI flag was synced from the real off state before the peek, so the first
     * tap still means "turn on" even while LEDs are temporarily lit. */
    frontlight_peek_cancel();
    g_app.light_on = !g_app.light_on;
    frontlight_set_on(g_app.light_on, g_app.cfg.brightness);
    g_app.cfg.light_on = g_app.light_on;
    config_save(&g_app.cfg);
    refresh_light_button();
}

void ui_handle_pointer(int x, int y)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();

    if (refresh_busy())
        return;
    if (x < 0 || y < 0 || x >= sw || y >= sh)
        return;

    switch (g_app.screen) {
    case SCREEN_HOME:
        home_hit(x, y);
        break;
    case SCREEN_BROWSE:
        library_hit(x, y);
        break;
    case SCREEN_MESSAGE:
        if (y > sh - ui_action_h())
            ui_show_home();
        break;
    case SCREEN_SETTINGS:
        settings_hit(x, y);
        break;
    case SCREEN_CONTENTS:
        contents_hit(x, y);
        break;
    case SCREEN_FONTS:
        fonts_hit(x, y);
        break;
    case SCREEN_SUGGEST_ASK:
        suggest_ask_hit(x, y);
        break;
    case SCREEN_SUGGEST_RESULTS:
        suggest_results_hit(x, y);
        break;
    case SCREEN_READING: {
        bar_hit hit = bar_hit_test(&g_app.bar, x, y);
        if (y < ui_bar_h()) {
            if (hit == BAR_HIT_DAYNIGHT)
                toggle_night();
            else if (hit == BAR_HIT_LIGHT)
                toggle_light();
            else if (hit == BAR_HIT_PAGES) {
                int fg = g_app.cfg.night ? WHITE : BLACK;
                int bg = g_app.cfg.night ? BLACK : WHITE;
                const reader_engine *eng = engine_active();

                g_app.cfg.bar_progress =
                    (g_app.cfg.bar_progress + 1) % BAR_PROGRESS_COUNT;
                config_save(&g_app.cfg);
                update_bar_label();
                SetFont(g_app.font_bar, fg);
                bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
                         g_app.light_available,
                         eng ? eng->current_page() : 0,
                         eng ? eng->page_count() : 0, fg, bg,
                         g_app.bar_pages_label);
                refresh_partial(g_app.bar.pages_x, 0, g_app.bar.pages_w,
                                g_app.bar.height);
            } else if (hit == BAR_HIT_OPTIONS) {
                const reader_engine *eng = engine_active();
                if (eng)
                    g_app.goto_page_tmp = eng->current_page();
                g_app.screen = SCREEN_SETTINGS;
                ui_draw();
            } else if (hit == BAR_HIT_SLEEP) {
                /* ForcingSleep() only shortens the idle timer. REQ_KEYLOCK
                 * is the system suspend KOReader uses (power-button sleep). */
                reader_log("bar: REQ_KEYLOCK");
                ui_save_position();
                config_save(&g_app.cfg);
                iv_sleepmode(1);
                SendGlobalRequest(REQ_KEYLOCK);
            }
            return;
        }
        if (x < sw / 3)
            turn_page(0);
        else
            turn_page(1);
        break;
    }
    }
}

static int g_key_held = -1;
static int g_key_ran_on_press;

static void handle_key_back(void)
{
    if (g_app.screen == SCREEN_READING) {
        ui_save_position();
        config_save(&g_app.cfg);
        engine_close();
        ui_show_home();
    } else if (g_app.screen == SCREEN_FONTS) {
        g_app.screen = SCREEN_SETTINGS;
        ui_draw();
    } else if (g_app.screen == SCREEN_SETTINGS ||
               g_app.screen == SCREEN_CONTENTS) {
        g_app.screen = SCREEN_READING;
        ui_draw();
    } else if (g_app.screen == SCREEN_BROWSE ||
               g_app.screen == SCREEN_MESSAGE ||
               g_app.screen == SCREEN_SUGGEST_ASK ||
               g_app.screen == SCREEN_SUGGEST_RESULTS) {
        ui_show_home();
    } else if (g_app.screen == SCREEN_HOME && g_app.books_from_options) {
        g_app.books_from_options = 0;
        g_app.screen = SCREEN_SETTINGS;
        ui_draw();
    } else {
        CloseApp();
    }
}

void ui_handle_key_event(int type, int key, int par2)
{
    keymap_action act;

    if (refresh_busy())
        return;

    if (type == EVT_KEYPRESS && key == KEY_BACK) {
        handle_key_back();
        return;
    }

    if (g_app.screen != SCREEN_READING)
        return;

    if (type == EVT_KEYPRESS) {
        if (keymap_has_long(key)) {
            reader_log("key: type=%d code=0x%x par2=%d -> act=defer",
                       type, key, par2);
            g_key_held = key;
            g_key_ran_on_press = 0;
        } else {
            act = keymap_lookup(key, 0);
            reader_log("key: type=%d code=0x%x par2=%d -> act=%d",
                       type, key, par2, (int)act);
            run_key_action(act);
            g_key_held = key;
            g_key_ran_on_press = 1;
        }
        return;
    }

    if (type == EVT_KEYRELEASE) {
        if (g_key_held == key && g_key_ran_on_press) {
            g_key_held = -1;
            g_key_ran_on_press = 0;
            return;
        }
        if (par2 == 0) {
            act = keymap_lookup(key, 0);
            reader_log("key: type=%d code=0x%x par2=%d -> act=%d",
                       type, key, par2, (int)act);
            run_key_action(act);
        } else {
            reader_log("key: type=%d code=0x%x par2=%d -> act=clear",
                       type, key, par2);
        }
        g_key_held = -1;
        g_key_ran_on_press = 0;
        return;
    }

    if (type == EVT_KEYREPEAT) {
        act = keymap_lookup(key, 1);
        if (par2 == 1 ||
            (par2 > 1 && keymap_action_repeatable(act))) {
            reader_log("key: type=%d code=0x%x par2=%d -> act=%d",
                       type, key, par2, (int)act);
            run_key_action(act);
        } else {
            reader_log("key: type=%d code=0x%x par2=%d -> act=skip",
                       type, key, par2);
        }
        g_key_held = -1;
        g_key_ran_on_press = 0;
    }
}

void ui_tick_clock(void)
{
    if (g_app.screen != SCREEN_READING || refresh_busy())
        return;
    update_clock_string();
    {
        int fg = g_app.cfg.night ? WHITE : BLACK;
        int bg = g_app.cfg.night ? BLACK : WHITE;
        const reader_engine *eng = engine_active();
        SetFont(g_app.font_bar, fg);
        update_bar_label();
        bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
                 g_app.light_available,
                 eng ? eng->current_page() : 0,
                 eng ? eng->page_count() : 0, fg, bg,
                 g_app.bar_pages_label);
        refresh_partial(g_app.bar.clock_x, 0, g_app.bar.clock_w, g_app.bar.height);
    }
}

void ui_on_show(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    bar_compute(&g_app.bar, sw, sh, ui_bar_h());
    if (g_app.screen == SCREEN_READING && engine_active() && g_app.page_buf) {
        /* Redraw cached buffer without reloading. */
        draw_reading(1);
    } else {
        ui_draw();
    }
}

void ui_on_hide(void)
{
    ui_save_position();
    config_save(&g_app.cfg);
    /* Home is about to draw. Record the open book again so its cover is current. */
    if (g_app.open_path[0])
        notify_system_book(g_app.open_path);
}

void ui_on_orientation(void)
{
    const reader_engine *eng = engine_active();
    char pos[ENGINE_POS_MAX];
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int page_h = sh - ui_bar_h();
    bar_compute(&g_app.bar, sw, sh, ui_bar_h());
    if (!eng)
        return;
    if (eng->get_position(pos, sizeof(pos)) != 0)
        pos[0] = '\0';
    if (ensure_page_buffer(sw, page_h) != 0)
        return;
    /* Re-open size via set_layout/render path */
    ui_apply_layout();
    if (pos[0])
        eng->set_position(pos);
    if (g_app.screen == SCREEN_READING)
        draw_reading(1);
}

void ui_init(void)
{
    memset(&g_app, 0, sizeof(g_app));
    reader_log("ui_init: start");
    debug_init();
    debug_install_handlers();
    debug_log_build_id();
    debug_log_mem("ui_init");
    config_load(&g_app.cfg);
    refresh_init();
    ui_apply_fonts();
    if (!g_app.font_bar || !g_app.font_ui || !g_app.font_msg)
        reader_log("ui_init: OpenFont returned NULL (bar=%p ui=%p msg=%p)",
                   (void *)g_app.font_bar, (void *)g_app.font_ui, (void *)g_app.font_msg);

    g_app.light_available = frontlight_available();
    if (g_app.light_available) {
        g_app.light_on = frontlight_is_on();
        if (g_app.cfg.brightness < CFG_BRIGHT_MIN)
            g_app.cfg.brightness = CFG_DEFAULT_BRIGHT;
        /* Do not force lamp state at launch. */
        {
            int w = frontlight_get_warmth();
            if (w != INT_MIN && !g_app.cfg.warmth_valid) {
                g_app.cfg.warmth = w;
                g_app.cfg.warmth_valid = 1;
            }
        }
        frontlight_peek_set_on_end(on_frontlight_peek_end);
    }

    /* Do not init crengine/MuPDF here — that was crashing before any UI paint. */
    if (g_app.cfg.last_book[0])
        snprintf(g_app.last_book, sizeof(g_app.last_book), "%s", g_app.cfg.last_book);
    bar_compute(&g_app.bar, ScreenWidth(), ScreenHeight(), ui_bar_h());
    update_clock_string();
    SetHardTimer("clock", ui_tick_clock, 60000);
    g_app.screen = SCREEN_HOME;
    reader_log("ui_init: ready screen=%d sw=%d sh=%d", g_app.screen,
               ScreenWidth(), ScreenHeight());
}

void ui_shutdown(void)
{
    ui_save_position();
    config_save(&g_app.cfg);
    engine_close();
    engine_global_shutdown();
    keymap_unload();
    free(g_app.page_buf);
    g_app.page_buf = NULL;
    free(g_app.books);
    g_app.books = NULL;
    free(g_app.toc);
    g_app.toc = NULL;
    if (g_body_font) {
        CloseFont(g_body_font);
        g_body_font = NULL;
    }
    if (g_app.font_bar) CloseFont(g_app.font_bar);
    if (g_app.font_ui) CloseFont(g_app.font_ui);
    if (g_app.font_msg) CloseFont(g_app.font_msg);
}

static char g_launch_path[512];
static int g_opened_launch;
static int g_resumed_last;

static int last_book_readable(const char **out_path)
{
    const char *path = g_app.cfg.last_book[0] ? g_app.cfg.last_book : g_app.last_book;
    FILE *fp;
    if (out_path)
        *out_path = NULL;
    if (!path || !path[0])
        return 0;
    fp = fopen(path, "rb");
    if (!fp)
        return 0;
    fclose(fp);
    if (out_path)
        *out_path = path;
    return 1;
}

static int main_handler(int type, int par1, int par2)
{
    switch (type) {
    case EVT_INIT:
        boot_marker("naskilla: EVT_INIT");
        /* Match working apps: only init here. Never FullUpdate/Wait in INIT. */
        SetPanelType(0);
        ui_init();
        boot_marker("naskilla: ui_init done");
        break;
    case EVT_SHOW:
        boot_marker("naskilla: EVT_SHOW");
        reader_log("evt SHOW fl=%d on=%d", GetFrontlightState(), frontlight_is_on());
        sync_light_ui_from_device();
        frontlight_peek_on_show();
        keymap_reload();
        if (g_launch_path[0] && !g_opened_launch) {
            g_opened_launch = 1;
            g_resumed_last = 1;
            ui_open_book(g_launch_path);
        } else if (!g_resumed_last) {
            const char *resume = NULL;
            g_resumed_last = 1;
            if (last_book_readable(&resume))
                ui_open_book(resume);
            else
                ui_on_show();
        } else {
            ui_on_show();
        }
        boot_marker("naskilla: EVT_SHOW done");
        break;
    case EVT_FOREGROUND:
        /* Keylock sleep/wake often resumes here without a new EVT_SHOW. */
        reader_log("evt FOREGROUND fl=%d on=%d", GetFrontlightState(), frontlight_is_on());
        sync_light_ui_from_device();
        frontlight_peek_on_show();
        keymap_reload();
        break;
    case EVT_HIDE:
        ui_on_hide();
        break;
    case EVT_BACKGROUND:
        reader_log("evt BACKGROUND");
        break;
    case EVT_POINTERUP:
        ui_handle_pointer(par1, par2);
        break;
    case EVT_KEYPRESS:
    case EVT_KEYRELEASE:
    case EVT_KEYREPEAT:
        ui_handle_key_event(type, par1, par2);
        break;
    case EVT_NEXTPAGE:
    case EVT_PREVPAGE:
        reader_log("key: page-evt type=%d (ignored)", type);
        break;
    case EVT_ORIENTATION:
        ui_on_orientation();
        break;
    case EVT_EXIT:
        ui_shutdown();
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    /* If this never appears on disk, the dynamic loader died before main
     * (missing .so) or a C++ static constructor aborted. */
    boot_marker("naskilla: main entered");

    g_launch_path[0] = '\0';
    g_opened_launch = 0;
    g_resumed_last = 0;
    if (argc > 1 && argv[1] && argv[1][0])
        snprintf(g_launch_path, sizeof(g_launch_path), "%s", argv[1]);
    reader_log("boot: InkViewMain path=%s", g_launch_path[0] ? g_launch_path : "(none)");
    boot_marker("naskilla: calling InkViewMain");
    InkViewMain(main_handler);
    boot_marker("naskilla: InkViewMain returned");
    return 0;
}
