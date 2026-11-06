#include "ui.h"

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
    if (eng)
        g_app.toc_count = eng->contents(g_app.toc, ENGINE_TOC_MAX);
}

void contents_draw(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int row_h = 64;
    int top = 48;
    int i, y;
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int visible;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 8, sw, 36, "Contents", ALIGN_CENTER | VALIGN_MIDDLE);

    if (g_app.toc_count == 0) {
        DrawTextRect(20, sh / 2 - 20, sw - 40, 40,
                     "This book has no chapters", ALIGN_CENTER | VALIGN_MIDDLE);
        DrawTextRect(20, sh - 80, sw - 40, 48, "Back",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        return;
    }

    visible = (sh - top - 60) / row_h;
    for (i = 0; i < visible; i++) {
        int idx = g_app.toc_scroll + i;
        if (idx >= g_app.toc_count)
            break;
        y = top + i * row_h;
        DrawTextRect(24, y, sw - 48, row_h - 8, g_app.toc[idx].title,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(24, y + row_h - 1, sw - 24, y + row_h - 1, fg);
    }
    DrawTextRect(20, sh - 56, sw - 40, 48, "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

int contents_hit(int x, int y)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int row_h = 64;
    int top = 48;
    int visible;
    int i;

    (void)x;
    if (y >= sh - 56) {
        g_app.screen = SCREEN_SETTINGS;
        ui_draw();
        return 1;
    }
    if (g_app.toc_count == 0)
        return 1;

    visible = (sh - top - 60) / row_h;
    for (i = 0; i < visible; i++) {
        int idx = g_app.toc_scroll + i;
        int yy = top + i * row_h;
        if (idx >= g_app.toc_count)
            break;
        if (y >= yy && y < yy + row_h) {
            const reader_engine *eng = engine_active();
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
    (void)sw;
    return 0;
}
