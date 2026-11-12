#include "ui.h"
#include "frontlight.h"
#include "layout.h"
#include "extensions.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inkview.h"

typedef enum {
    ROW_BOOKS = 0,
    ROW_DEFAULT_READER,
    ROW_FONT_FACE,
    ROW_FONT_SIZE,
    ROW_MARGINS,
    ROW_SPACING,
    ROW_PUBLISHER,
    ROW_GOTO,
    ROW_CONTENTS,
    ROW_BOOKMARK,
    ROW_BOOKMARKS,
    ROW_JUMP_BACK,
    ROW_FIND,
    ROW_FIND_NEXT,
    ROW_NEXT_SERIES,
    ROW_BRIGHTNESS,
    ROW_WARMTH,
    ROW_FULL_REFRESH,
    ROW_DAYNIGHT,
    ROW_BACK,
    ROW_COUNT
} settings_row;

static int row_visible(settings_row r)
{
    const reader_engine *eng = engine_active();
    int reflow = eng && eng->is_reflowable();
    switch (r) {
    case ROW_FONT_FACE:
    case ROW_FONT_SIZE:
    case ROW_MARGINS:
    case ROW_SPACING:
    case ROW_PUBLISHER:
        return reflow;
    case ROW_JUMP_BACK:
        return g_app.jump_back_valid;
    case ROW_BACK:
        return 0; /* pinned to the bottom bar */
    case ROW_FIND_NEXT:
        return g_app.find_query[0] != '\0';
    case ROW_BRIGHTNESS:
        return g_app.light_available;
    case ROW_WARMTH:
        return g_app.light_available && frontlight_get_warmth() != INT_MIN;
    default:
        return 1;
    }
}

static void row_label(settings_row r, char *buf, size_t n)
{
    switch (r) {
    case ROW_BOOKS:
        snprintf(buf, n, "Books");
        break;
    case ROW_DEFAULT_READER: {
        char who[EXTCFG_LABEL_MAX];
        extcfg_current_label(who, sizeof(who));
        snprintf(buf, n, "Open books with: %s", who);
        break;
    }
    case ROW_FONT_FACE:
        snprintf(buf, n, "Font: %s", g_app.cfg.font_face);
        break;
    case ROW_FONT_SIZE:
        snprintf(buf, n, "Font size: %d", g_app.cfg.font_size);
        break;
    case ROW_MARGINS:
        snprintf(buf, n, "Margins: %d", g_app.cfg.margin);
        break;
    case ROW_SPACING:
        snprintf(buf, n, "Line spacing: %d%%", g_app.cfg.line_spacing);
        break;
    case ROW_PUBLISHER:
        snprintf(buf, n, "Book styles: %s",
                 g_app.cfg.publisher_styles ? "On" : "Off");
        break;
    case ROW_GOTO:
        snprintf(buf, n, "Go to page: %d", g_app.goto_page_tmp);
        break;
    case ROW_CONTENTS:
        snprintf(buf, n, "Contents");
        break;
    case ROW_BOOKMARK:
        snprintf(buf, n, "Bookmark this page");
        break;
    case ROW_BOOKMARKS:
        snprintf(buf, n, "Bookmarks");
        break;
    case ROW_JUMP_BACK:
        snprintf(buf, n, "Back to page %d", g_app.jump_back_page);
        break;
    case ROW_FIND:
        snprintf(buf, n, "Find%s%s",
                 g_app.find_query[0] ? ": " : "",
                 g_app.find_query[0] ? g_app.find_query : "");
        break;
    case ROW_FIND_NEXT:
        snprintf(buf, n, "Find next");
        break;
    case ROW_NEXT_SERIES:
        snprintf(buf, n, "Next in series");
        break;
    case ROW_BRIGHTNESS:
        snprintf(buf, n, "Brightness: %d", g_app.cfg.brightness);
        break;
    case ROW_WARMTH:
        snprintf(buf, n, "Warmth: %d",
                 g_app.cfg.warmth_valid ? g_app.cfg.warmth : 0);
        break;
    case ROW_FULL_REFRESH:
        snprintf(buf, n, "Full refresh every: %d", g_app.cfg.full_refresh);
        break;
    case ROW_DAYNIGHT:
        snprintf(buf, n, "Theme: %s", g_app.cfg.night ? "Night" : "Day");
        break;
    case ROW_BACK:
        snprintf(buf, n, "Back to book");
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

void settings_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int m = ui_margin();
    int top = ui_title_h();
    int y = top;
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int r;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 0, sw, ui_title_h(), "Options", ALIGN_CENTER | VALIGN_MIDDLE);

    for (r = 0; r < ROW_COUNT; r++) {
        char label[128];
        if (!row_visible((settings_row)r))
            continue;
        if (y + row_h > sh - ui_action_h())
            break;
        row_label((settings_row)r, label, sizeof(label));
        DrawTextRect(m, y, sw - 2 * m - 200, row_h - 4, label,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        /* +/- zones for adjustable rows */
        if (r == ROW_FONT_SIZE || r == ROW_MARGINS || r == ROW_SPACING ||
            r == ROW_GOTO || r == ROW_BRIGHTNESS || r == ROW_WARMTH ||
            r == ROW_FULL_REFRESH) {
            DrawTextRect(sw - 200, y, 80, row_h - 4, "-",
                         ALIGN_CENTER | VALIGN_MIDDLE);
            DrawTextRect(sw - 100, y, 80, row_h - 4, "+",
                         ALIGN_CENTER | VALIGN_MIDDLE);
        }
        DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
        y += row_h;
    }
    DrawTextRect(m, sh - ui_action_h(), sw - 2 * m, ui_action_h(), "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

static void settings_find_keyboard(char *text)
{
    const reader_engine *eng;
    if (text)
        snprintf(g_app.find_query, sizeof(g_app.find_query), "%s", text);
    eng = engine_active();
    if (eng && eng->find && g_app.find_query[0]) {
        ui_remember_jump();
        if (eng->find(g_app.find_query, 0) == 0) {
            ui_save_position();
            g_app.screen = SCREEN_READING;
            ui_draw();
            return;
        }
    }
    g_app.screen = SCREEN_SETTINGS;
    ui_draw();
}

static int adjust_row(settings_row r, int dir)
{
    switch (r) {
    case ROW_FONT_SIZE:
        g_app.cfg.font_size += dir * 2;
        if (g_app.cfg.font_size < CFG_FONT_MIN) g_app.cfg.font_size = CFG_FONT_MIN;
        if (g_app.cfg.font_size > CFG_FONT_MAX) g_app.cfg.font_size = CFG_FONT_MAX;
        if (g_app.open_path[0])
            config_set_book_font_size(g_app.open_path, g_app.cfg.font_size);
        return 1;
    case ROW_MARGINS:
        g_app.cfg.margin += dir * 5;
        if (g_app.cfg.margin < CFG_MARGIN_MIN) g_app.cfg.margin = CFG_MARGIN_MIN;
        if (g_app.cfg.margin > CFG_MARGIN_MAX) g_app.cfg.margin = CFG_MARGIN_MAX;
        return 1;
    case ROW_SPACING:
        g_app.cfg.line_spacing += dir * 5;
        if (g_app.cfg.line_spacing < CFG_SPACING_MIN) g_app.cfg.line_spacing = CFG_SPACING_MIN;
        if (g_app.cfg.line_spacing > CFG_SPACING_MAX) g_app.cfg.line_spacing = CFG_SPACING_MAX;
        return 1;
    case ROW_GOTO: {
        const reader_engine *eng = engine_active();
        int maxp = eng ? eng->page_count() : 1;
        int step = (dir > 0 || dir < 0) ? (dir > 1 || dir < -1 ? 10 : 1) : 1;
        /* dir is -1 or +1 from side taps; long side uses 10 via x position below */
        g_app.goto_page_tmp += dir;
        if (g_app.goto_page_tmp < 1) g_app.goto_page_tmp = 1;
        if (g_app.goto_page_tmp > maxp) g_app.goto_page_tmp = maxp;
        (void)step;
        return 1;
    }
    case ROW_BRIGHTNESS:
        g_app.cfg.brightness += dir * 10;
        if (g_app.cfg.brightness < CFG_BRIGHT_MIN) g_app.cfg.brightness = CFG_BRIGHT_MIN;
        if (g_app.cfg.brightness > CFG_BRIGHT_MAX) g_app.cfg.brightness = CFG_BRIGHT_MAX;
        frontlight_set_brightness(g_app.cfg.brightness);
        return 1;
    case ROW_WARMTH:
        if (!g_app.cfg.warmth_valid) {
            int w = frontlight_get_warmth();
            g_app.cfg.warmth = (w < 0) ? 0 : w;
            g_app.cfg.warmth_valid = 1;
        }
        g_app.cfg.warmth += dir * 10;
        if (g_app.cfg.warmth < CFG_WARMTH_MIN) g_app.cfg.warmth = CFG_WARMTH_MIN;
        if (g_app.cfg.warmth > CFG_WARMTH_MAX) g_app.cfg.warmth = CFG_WARMTH_MAX;
        frontlight_set_warmth(g_app.cfg.warmth);
        return 1;
    case ROW_FULL_REFRESH:
        g_app.cfg.full_refresh += dir;
        if (g_app.cfg.full_refresh < CFG_REFRESH_MIN) g_app.cfg.full_refresh = CFG_REFRESH_MIN;
        if (g_app.cfg.full_refresh > CFG_REFRESH_MAX) g_app.cfg.full_refresh = CFG_REFRESH_MAX;
        return 1;
    default:
        return 0;
    }
}

int settings_hit(int x, int y)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int top = ui_title_h();
    int yy = top;
    int r;

    (void)sh;
    if (y >= sh - ui_action_h()) {
        config_save(&g_app.cfg);
        ui_apply_layout();
        if (g_app.open_path[0] && engine_active())
            g_app.screen = SCREEN_READING;
        else
            g_app.screen = SCREEN_HOME;
        ui_draw();
        return 1;
    }
    for (r = 0; r < ROW_COUNT; r++) {
        if (!row_visible((settings_row)r))
            continue;
        if (y >= yy && y < yy + row_h) {
            if (r == ROW_BOOKS) {
                ui_save_position();
                config_save(&g_app.cfg);
                g_app.books_from_options = 1;
                ui_show_home();
                return 1;
            }
            if (r == ROW_DEFAULT_READER) {
                if (extcfg_cycle_default() != 0) {
                    /* Still redraw so "unavailable" is visible if write failed. */
                }
                ui_draw();
                return 1;
            }
            if (r == ROW_BACK) {
                config_save(&g_app.cfg);
                ui_apply_layout();
                g_app.screen = SCREEN_READING;
                ui_draw();
                return 1;
            }
            if (r == ROW_CONTENTS) {
                ui_remember_jump();
                contents_load();
                g_app.screen = SCREEN_CONTENTS;
                ui_draw();
                return 1;
            }
            if (r == ROW_BOOKMARK) {
                const reader_engine *eng = engine_active();
                book_bookmark bm;
                memset(&bm, 0, sizeof(bm));
                if (eng && g_app.open_path[0] &&
                    eng->get_position(bm.pos, sizeof(bm.pos)) == 0) {
                    bm.page = eng->current_page();
                    if (eng->chapter_title)
                        eng->chapter_title(bm.title, sizeof(bm.title));
                    if (!bm.title[0])
                        snprintf(bm.title, sizeof(bm.title), "Page %d", bm.page);
                    config_bookmark_add(g_app.open_path, &bm);
                }
                ui_draw();
                return 1;
            }
            if (r == ROW_BOOKMARKS) {
                book_bookmark bms[CFG_BOOKMARKS_MAX];
                int n = config_bookmarks_load(g_app.open_path, bms, CFG_BOOKMARKS_MAX);
                int i;
                if (!g_app.toc)
                    g_app.toc = (engine_toc_item *)calloc(ENGINE_TOC_MAX, sizeof(engine_toc_item));
                g_app.toc_count = 0;
                g_app.toc_scroll = 0;
                g_app.toc_is_bookmarks = 1;
                for (i = 0; i < n && g_app.toc; i++) {
                    snprintf(g_app.toc[i].title, ENGINE_TITLE_MAX, "p.%d %s",
                             bms[i].page, bms[i].title);
                    snprintf(g_app.toc[i].position, ENGINE_POS_MAX, "%s", bms[i].pos);
                    g_app.toc_count++;
                }
                g_app.screen = SCREEN_CONTENTS;
                ui_draw();
                return 1;
            }
            if (r == ROW_JUMP_BACK && g_app.jump_back_valid) {
                const reader_engine *eng = engine_active();
                if (eng)
                    eng->set_position(g_app.jump_back_pos);
                g_app.jump_back_valid = 0;
                ui_save_position();
                g_app.screen = SCREEN_READING;
                ui_draw();
                return 1;
            }
            if (r == ROW_FIND) {
                OpenKeyboard("Find in book", g_app.find_query,
                             (int)sizeof(g_app.find_query) - 1, KBD_NORMAL,
                             settings_find_keyboard);
                return 1;
            }
            if (r == ROW_FIND_NEXT) {
                const reader_engine *eng = engine_active();
                if (eng && eng->find && g_app.find_query[0]) {
                    ui_remember_jump();
                    if (eng->find(g_app.find_query, 0) == 0) {
                        ui_save_position();
                        g_app.screen = SCREEN_READING;
                        ui_draw();
                        return 1;
                    }
                }
                ui_draw();
                return 1;
            }
            if (r == ROW_NEXT_SERIES) {
                ui_open_next_in_series();
                return 1;
            }
            if (r == ROW_FONT_FACE) {
                fonts_open();
                return 1;
            }
            if (r == ROW_PUBLISHER) {
                g_app.cfg.publisher_styles = !g_app.cfg.publisher_styles;
                ui_draw();
                return 1;
            }
            if (r == ROW_DAYNIGHT) {
                g_app.cfg.night = !g_app.cfg.night;
                ui_draw();
                return 1;
            }
            if (r == ROW_GOTO && x > sw / 2 - 40 && x < sw / 2 + 40) {
                const reader_engine *eng = engine_active();
                if (eng) {
                    ui_remember_jump();
                    eng->go_to(g_app.goto_page_tmp);
                    ui_save_position();
                }
                config_save(&g_app.cfg);
                ui_apply_layout();
                g_app.screen = SCREEN_READING;
                ui_draw();
                return 1;
            }
            /* +/- */
            if (x >= sw - 100) {
                int dir = 1;
                if (r == ROW_GOTO && x >= sw - 50)
                    g_app.goto_page_tmp += 9; /* will +1 in adjust → ~10 */
                adjust_row((settings_row)r, dir);
                ui_draw();
                return 1;
            }
            if (x >= sw - 200 && x < sw - 100) {
                int dir = -1;
                if (r == ROW_GOTO && x < sw - 150)
                    g_app.goto_page_tmp -= 9;
                adjust_row((settings_row)r, dir);
                ui_draw();
                return 1;
            }
            return 1;
        }
        yy += row_h;
    }
    return 0;
}
