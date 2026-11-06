#include "ui.h"
#include "frontlight.h"
#include "layout.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "inkview.h"

typedef enum {
    ROW_FONT_FACE = 0,
    ROW_FONT_SIZE,
    ROW_MARGINS,
    ROW_SPACING,
    ROW_PUBLISHER,
    ROW_GOTO,
    ROW_CONTENTS,
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
        snprintf(buf, n, "Publisher layout: %s",
                 g_app.cfg.publisher_styles ? "On" : "Off");
        break;
    case ROW_GOTO:
        snprintf(buf, n, "Go to page: %d", g_app.goto_page_tmp);
        break;
    case ROW_CONTENTS:
        snprintf(buf, n, "Contents");
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
    DrawTextRect(0, 0, sw, ui_title_h(), "Settings", ALIGN_CENTER | VALIGN_MIDDLE);

    for (r = 0; r < ROW_COUNT; r++) {
        char label[128];
        if (!row_visible((settings_row)r))
            continue;
        if (y + row_h > sh)
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
}

static void cycle_font_face(void)
{
    if (strcmp(g_app.cfg.font_face, "Bookerly") == 0)
        snprintf(g_app.cfg.font_face, sizeof(g_app.cfg.font_face), "LiberationSans");
    else
        snprintf(g_app.cfg.font_face, sizeof(g_app.cfg.font_face), "Bookerly");
}

static int adjust_row(settings_row r, int dir)
{
    switch (r) {
    case ROW_FONT_SIZE:
        g_app.cfg.font_size += dir * 2;
        if (g_app.cfg.font_size < CFG_FONT_MIN) g_app.cfg.font_size = CFG_FONT_MIN;
        if (g_app.cfg.font_size > CFG_FONT_MAX) g_app.cfg.font_size = CFG_FONT_MAX;
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
    for (r = 0; r < ROW_COUNT; r++) {
        if (!row_visible((settings_row)r))
            continue;
        if (y >= yy && y < yy + row_h) {
            if (r == ROW_BACK) {
                config_save(&g_app.cfg);
                ui_apply_layout();
                g_app.screen = SCREEN_READING;
                ui_draw();
                return 1;
            }
            if (r == ROW_CONTENTS) {
                contents_load();
                g_app.screen = SCREEN_CONTENTS;
                ui_draw();
                return 1;
            }
            if (r == ROW_FONT_FACE) {
                cycle_font_face();
                ui_draw();
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
                /* center tap applies go-to */
                const reader_engine *eng = engine_active();
                if (eng) {
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
