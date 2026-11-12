#include "ui.h"
#include "layout.h"

#include <stdio.h>
#include <string.h>

#include "inkview.h"

#define FONTS_MAX 128

static char s_faces[FONTS_MAX][64];
static int s_count;
static int s_scroll;

static int fonts_visible(void)
{
    int sh = ui_sh();
    int top = ui_title_h();
    int action_h = ui_action_h();
    int row_h = ui_row_h();
    int v = (sh - top - action_h) / row_h;
    return v > 0 ? v : 1;
}

void fonts_open(void)
{
    int i;
    int cur = -1;
    int visible;

    s_count = engine_list_faces(s_faces, FONTS_MAX);
    s_scroll = 0;
    for (i = 0; i < s_count; i++) {
        if (strcmp(s_faces[i], g_app.cfg.font_face) == 0) {
            cur = i;
            break;
        }
    }
    visible = fonts_visible();
    if (cur >= 0) {
        s_scroll = (cur / visible) * visible;
        if (s_scroll >= s_count)
            s_scroll = 0;
    }
    g_app.screen = SCREEN_FONTS;
    ui_draw();
}

void fonts_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int m = ui_margin();
    int top = ui_title_h();
    int action_h = ui_action_h();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int visible = fonts_visible();
    int pages;
    int page;
    int i, y;
    char title[64];

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);

    pages = s_count > 0 ? (s_count + visible - 1) / visible : 1;
    page = s_count > 0 ? (s_scroll / visible) + 1 : 1;
    if (page > pages)
        page = pages;
    snprintf(title, sizeof(title), "Fonts (page %d/%d)", page, pages);
    DrawTextRect(0, 0, sw, ui_title_h(), title, ALIGN_CENTER | VALIGN_MIDDLE);

    if (s_count == 0) {
        DrawTextRect(m, sh / 2 - 20, sw - 2 * m, 40,
                     "No fonts found", ALIGN_CENTER | VALIGN_MIDDLE);
        DrawTextRect(m, sh - action_h, sw - 2 * m, action_h, "Back",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        return;
    }

    for (i = 0; i < visible; i++) {
        int idx = s_scroll + i;
        char label[72];
        if (idx >= s_count)
            break;
        y = top + i * row_h;
        if (strcmp(s_faces[idx], g_app.cfg.font_face) == 0)
            snprintf(label, sizeof(label), "> %s", s_faces[idx]);
        else
            snprintf(label, sizeof(label), "  %s", s_faces[idx]);
        DrawTextRect(m, y, sw - 2 * m, row_h - 8, label,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    }

    DrawTextRect(0, sh - action_h, sw / 3, action_h, "Up",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(sw / 3, sh - action_h, sw / 3, action_h, "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect((sw * 2) / 3, sh - action_h, sw / 3, action_h, "Down",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

int fonts_hit(int x, int y)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int top = ui_title_h();
    int action_h = ui_action_h();
    int visible = fonts_visible();
    int i;

    if (y >= sh - action_h) {
        if (s_count == 0 || (x >= sw / 3 && x < (sw * 2) / 3)) {
            g_app.screen = SCREEN_SETTINGS;
            ui_draw();
            return 1;
        }
        if (x < sw / 3) {
            if (s_scroll > 0) {
                s_scroll -= visible;
                if (s_scroll < 0)
                    s_scroll = 0;
                ui_draw();
            }
            return 1;
        }
        if (s_scroll + visible < s_count) {
            s_scroll += visible;
            ui_draw();
        }
        return 1;
    }

    if (s_count == 0)
        return 1;

    for (i = 0; i < visible; i++) {
        int idx = s_scroll + i;
        int yy = top + i * row_h;
        if (idx >= s_count)
            break;
        if (y >= yy && y < yy + row_h) {
            snprintf(g_app.cfg.font_face, sizeof(g_app.cfg.font_face),
                     "%s", s_faces[idx]);
            g_app.screen = SCREEN_SETTINGS;
            ui_draw();
            return 1;
        }
    }
    return 0;
}
