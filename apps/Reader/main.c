#include "ui.h"
#include "frontlight.h"
#include "refresh.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    int y, x;
    int w = g_app.buf_w;
    int h = g_app.buf_h;
    int fg_is_night = g_app.cfg.night;
    /* Stretch gray page into inkview framebuffer via FillArea of 1px is too slow;
     * use DrawBitmap-like path: DrawPixel loop is slow too. Use Stretch for rows
     * by packing into temporary and Draw. PocketBook: invert via color mapping. */
    for (y = 0; y < h; y++) {
        const unsigned char *row = g_app.page_buf + y * g_app.buf_stride;
        for (x = 0; x < w; ) {
            unsigned char v = row[x];
            int run = 1;
            int color;
            while (x + run < w && row[x + run] == v)
                run++;
            if (fg_is_night)
                color = (v < 128) ? WHITE : BLACK; /* already inverted by engine often */
            else
                color = (v < 128) ? BLACK : WHITE;
            /* Fine gray: map midtones */
            if (v > 64 && v < 192)
                color = (v < 128) ? DGRAY : LGRAY;
            FillArea(x, dst_y + y, run, 1, color);
            x += run;
        }
    }
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
    eng->set_layout(&layout);
}

void ui_save_position(void)
{
    const reader_engine *eng = engine_active();
    char pos[ENGINE_POS_MAX];
    if (!eng || !g_app.open_path[0])
        return;
    if (eng->get_position(pos, sizeof(pos)) == 0)
        config_set_position(g_app.open_path, pos);
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
    DrawTextRect(40, sh - 100, sw - 80, 48, "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
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

    bar_compute(&g_app.bar, sw, sh, CFG_BAR_HEIGHT);
    page_h = sh - CFG_BAR_HEIGHT;
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
    eng->draw(g_app.page_buf, sw, page_h, g_app.buf_stride);
    page = eng->current_page();
    pages = eng->page_count();

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    blit_gray_to_screen(CFG_BAR_HEIGHT);
    SetFont(g_app.font_bar, fg);
    update_clock_string();
    bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
             g_app.light_available, page, pages, fg, bg);

    if (full) {
        refresh_full();
        g_app.pages_since_full = 0;
    } else {
        refresh_partial(0, CFG_BAR_HEIGHT, sw, page_h);
        /* also refresh bar when page number changes */
        refresh_partial(0, 0, sw, CFG_BAR_HEIGHT);
        g_app.pages_since_full++;
        if (g_app.pages_since_full >= g_app.cfg.full_refresh) {
            refresh_full();
            g_app.pages_since_full = 0;
        }
    }
}

void ui_draw(void)
{
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
    library_scan(root);
    g_app.screen = SCREEN_BROWSE;
    ui_draw();
}

void ui_open_book(const char *path)
{
    bookinfo *info;
    char pos[ENGINE_POS_MAX];
    int sw, sh, page_h;
    int rc;

    if (!path || !path[0]) {
        ui_set_message("Missing file");
        ui_draw();
        return;
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
    config_save(&g_app.cfg);

    ClearScreen();
    SetFont(g_app.font_msg, g_app.cfg.night ? WHITE : BLACK);
    DrawTextRect(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40, "Opening",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    refresh_partial(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40);

    PostponeTimedPoweroff();
    sw = ScreenWidth();
    sh = ScreenHeight();
    page_h = sh - CFG_BAR_HEIGHT;
    if (ensure_page_buffer(sw, page_h) != 0) {
        ui_draw();
        return;
    }
    rc = engine_open_path(path, sw, page_h);
    if (rc != 0) {
        reader_log("open failed %d for %s", rc, path);
        ui_set_message("Cannot open");
        ui_draw();
        return;
    }
    ui_apply_layout();
    if (config_get_position(path, pos, sizeof(pos)) == 0)
        engine_active()->set_position(pos);
    {
        const reader_engine *eng = engine_active();
        if (eng)
            g_app.goto_page_tmp = eng->current_page();
    }
    g_app.screen = SCREEN_READING;
    g_app.pages_since_full = 0;
    ui_draw();
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
    ui_save_position();
    draw_reading(0);
}

static void toggle_night(void)
{
    g_app.cfg.night = !g_app.cfg.night;
    config_save(&g_app.cfg);
    ui_apply_layout();
    draw_reading(1);
}

static void toggle_light(void)
{
    if (!g_app.light_available || refresh_busy())
        return;
    g_app.light_on = !g_app.light_on;
    frontlight_set_on(g_app.light_on, g_app.cfg.brightness);
    g_app.cfg.light_on = g_app.light_on;
    config_save(&g_app.cfg);
    {
        int fg = g_app.cfg.night ? WHITE : BLACK;
        int bg = g_app.cfg.night ? BLACK : WHITE;
        const reader_engine *eng = engine_active();
        SetFont(g_app.font_bar, fg);
        bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
                 g_app.light_available,
                 eng ? eng->current_page() : 0,
                 eng ? eng->page_count() : 0, fg, bg);
        refresh_partial(g_app.bar.light_x, 0, g_app.bar.light_w, g_app.bar.height);
    }
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
        if (y > sh - 100)
            ui_show_home();
        break;
    case SCREEN_SETTINGS:
        settings_hit(x, y);
        break;
    case SCREEN_CONTENTS:
        contents_hit(x, y);
        break;
    case SCREEN_SUGGEST_ASK:
        suggest_ask_hit(x, y);
        break;
    case SCREEN_SUGGEST_RESULTS:
        suggest_results_hit(x, y);
        break;
    case SCREEN_READING: {
        bar_hit hit = bar_hit_test(&g_app.bar, x, y);
        if (y < CFG_BAR_HEIGHT) {
            if (hit == BAR_HIT_DAYNIGHT)
                toggle_night();
            else if (hit == BAR_HIT_LIGHT)
                toggle_light();
            else if (hit == BAR_HIT_PAGES) {
                const reader_engine *eng = engine_active();
                if (eng)
                    g_app.goto_page_tmp = eng->current_page();
                g_app.screen = SCREEN_SETTINGS;
                ui_draw();
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

void ui_handle_key(int key)
{
    if (refresh_busy())
        return;
    if (key == KEY_BACK) {
        if (g_app.screen == SCREEN_READING) {
            ui_save_position();
            config_save(&g_app.cfg);
            engine_close();
            ui_show_home();
        } else if (g_app.screen == SCREEN_SETTINGS ||
                   g_app.screen == SCREEN_CONTENTS) {
            g_app.screen = SCREEN_READING;
            ui_draw();
        } else if (g_app.screen == SCREEN_BROWSE ||
                   g_app.screen == SCREEN_MESSAGE ||
                   g_app.screen == SCREEN_SUGGEST_ASK ||
                   g_app.screen == SCREEN_SUGGEST_RESULTS) {
            ui_show_home();
        } else {
            CloseApp();
        }
        return;
    }
    if (g_app.screen == SCREEN_READING) {
        if (key == KEY_NEXT)
            turn_page(1);
        else if (key == KEY_PREV)
            turn_page(0);
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
        bar_draw(&g_app.bar, g_app.clock, g_app.cfg.night, g_app.light_on,
                 g_app.light_available,
                 eng ? eng->current_page() : 0,
                 eng ? eng->page_count() : 0, fg, bg);
        refresh_partial(g_app.bar.clock_x, 0, g_app.bar.clock_w, g_app.bar.height);
    }
}

void ui_on_show(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    bar_compute(&g_app.bar, sw, sh, CFG_BAR_HEIGHT);
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
}

void ui_on_orientation(void)
{
    const reader_engine *eng = engine_active();
    char pos[ENGINE_POS_MAX];
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int page_h = sh - CFG_BAR_HEIGHT;
    bar_compute(&g_app.bar, sw, sh, CFG_BAR_HEIGHT);
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
    config_load(&g_app.cfg);
    refresh_init();
    g_app.font_bar = OpenFont("LiberationSans", 22, 0);
    g_app.font_ui = OpenFont("LiberationSans", 28, 0);
    g_app.font_msg = OpenFont("LiberationSans", 36, 1);
    if (!g_app.font_bar) g_app.font_bar = OpenFont("LiberationSans", 20, 0);
    if (!g_app.font_ui) g_app.font_ui = OpenFont("LiberationSans", 24, 0);
    if (!g_app.font_msg) g_app.font_msg = OpenFont("LiberationSans", 28, 0);

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
    }

    engine_global_init(CFG_CACHE_DIR, CFG_FONTS_DIR);
    if (g_app.cfg.last_book[0])
        snprintf(g_app.last_book, sizeof(g_app.last_book), "%s", g_app.cfg.last_book);
    bar_compute(&g_app.bar, ScreenWidth(), ScreenHeight(), CFG_BAR_HEIGHT);
    update_clock_string();
    SetHardTimer("clock", ui_tick_clock, 60000);
}

void ui_shutdown(void)
{
    ui_save_position();
    config_save(&g_app.cfg);
    engine_close();
    engine_global_shutdown();
    free(g_app.page_buf);
    g_app.page_buf = NULL;
    free(g_app.books);
    g_app.books = NULL;
    free(g_app.toc);
    g_app.toc = NULL;
    if (g_app.font_bar) CloseFont(g_app.font_bar);
    if (g_app.font_ui) CloseFont(g_app.font_ui);
    if (g_app.font_msg) CloseFont(g_app.font_msg);
}

static char g_launch_path[512];

static int main_handler(int type, int par1, int par2)
{
    switch (type) {
    case EVT_INIT:
        SetPanelType(0);
        ui_init();
        if (g_launch_path[0])
            ui_open_book(g_launch_path);
        else
            ui_show_home();
        break;
    case EVT_SHOW:
        ui_on_show();
        break;
    case EVT_HIDE:
        ui_on_hide();
        break;
    case EVT_POINTERUP:
        ui_handle_pointer(par1, par2);
        break;
    case EVT_KEYPRESS:
        ui_handle_key(par1);
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
    g_launch_path[0] = '\0';
    if (argc > 1 && argv[1] && argv[1][0])
        snprintf(g_launch_path, sizeof(g_launch_path), "%s", argv[1]);
    InkViewMain(main_handler);
    return 0;
}
