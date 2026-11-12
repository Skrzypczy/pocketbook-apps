#include "ui.h"
#include "layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inkview.h"

void contents_load(void)
{
    const reader_engine *eng = engine_active();
    if (!g_app.toc) {
        g_app.toc = (engine_toc_item *)calloc(ENGINE_TOC_MAX, sizeof(engine_toc_item));
        if (!g_app.toc) {
            g_app.toc_count = 0;
            return;
        }
    }
    g_app.toc_count = 0;
    g_app.toc_scroll = 0;
    g_app.toc_is_bookmarks = 0;
    if (eng)
        g_app.toc_count = eng->contents(g_app.toc, ENGINE_TOC_MAX);
}

void contents_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int m = ui_margin();
    int top = ui_title_h();
    int i, y;
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int visible;
    int action_h = ui_action_h();

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 0, sw, ui_title_h(),
                 g_app.toc_is_bookmarks ? "Bookmarks" : "Contents",
                 ALIGN_CENTER | VALIGN_MIDDLE);

    if (g_app.toc_count == 0) {
        DrawTextRect(m, sh / 2 - 20, sw - 2 * m, 40,
                     "This book has no chapters", ALIGN_CENTER | VALIGN_MIDDLE);
        DrawTextRect(m, sh - action_h, sw - 2 * m, action_h, "Back",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        return;
    }

    visible = (sh - top - action_h) / row_h;
    for (i = 0; i < visible; i++) {
        int idx = g_app.toc_scroll + i;
        if (idx >= g_app.toc_count)
            break;
        y = top + i * row_h;
        DrawTextRect(m, y, sw - 2 * m, row_h - 8, g_app.toc[idx].title,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    }
    DrawTextRect(m, sh - action_h, sw - 2 * m, action_h, "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

int contents_hit(int x, int y)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int top = ui_title_h();
    int action_h = ui_action_h();
    int visible;
    int i;

    (void)x;
    (void)sw;
    if (y >= sh - action_h) {
        g_app.screen = SCREEN_SETTINGS;
        ui_draw();
        return 1;
    }
    if (g_app.toc_count == 0)
        return 1;

    visible = (sh - top - action_h) / row_h;
    for (i = 0; i < visible; i++) {
        int idx = g_app.toc_scroll + i;
        int yy = top + i * row_h;
        if (idx >= g_app.toc_count)
            break;
        if (y >= yy && y < yy + row_h) {
            const reader_engine *eng = engine_active();
            ui_remember_jump();
            if (eng)
                eng->set_position(g_app.toc[idx].position);
            ui_save_position();
            g_app.screen = SCREEN_READING;
            ui_draw();
            return 1;
        }
    }
    if (y > sh * 2 / 3 && g_app.toc_scroll + visible < g_app.toc_count) {
        g_app.toc_scroll += visible > 1 ? visible - 1 : 1;
        ui_draw();
        return 1;
    }
    if (y < sh / 3 && g_app.toc_scroll > 0) {
        g_app.toc_scroll -= visible > 1 ? visible - 1 : 1;
        if (g_app.toc_scroll < 0)
            g_app.toc_scroll = 0;
        ui_draw();
        return 1;
    }
    return 0;
}
