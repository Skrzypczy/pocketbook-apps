#include "bar.h"

#include <stdio.h>
#include <string.h>

#include "inkview.h"

void bar_format_progress(int mode, int page, int pages, long eta_sec,
                         char *out, size_t out_n)
{
    long mins;

    if (!out || out_n == 0)
        return;
    out[0] = '\0';

    if (mode == BAR_PROGRESS_PERCENT) {
        int pct = (pages > 0) ? (page * 100) / pages : 0;
        snprintf(out, out_n, "%d%%", pct);
        return;
    }
    if (mode == BAR_PROGRESS_ETA) {
        if (eta_sec < 0) {
            snprintf(out, out_n, "~--");
            return;
        }
        if (eta_sec < 60) {
            snprintf(out, out_n, "<1m");
            return;
        }
        /* Round up to whole minutes. */
        mins = (eta_sec + 59) / 60;
        if (mins < 60) {
            snprintf(out, out_n, "~%ldm", mins);
            return;
        }
        snprintf(out, out_n, "~%ldh%02ldm", mins / 60, mins % 60);
        return;
    }
    /* Default: pages */
    snprintf(out, out_n, "%d/%d", page, pages);
}

void bar_compute(bar_layout *b, int screen_w, int screen_h, int height)
{
    int unit;
    memset(b, 0, sizeof(*b));
    b->screen_w = screen_w;
    b->screen_h = screen_h;
    b->height = height;
    if (screen_w <= 0 || height <= 0)
        return;
    /* Six equal columns: time | pages | Options | Night | Light | Sleep */
    unit = screen_w / 6;
    b->clock_x = 0;
    b->clock_w = unit;
    b->pages_x = unit;
    b->pages_w = unit;
    b->options_x = unit * 2;
    b->options_w = unit;
    b->day_x = unit * 3;
    b->day_w = unit;
    b->light_x = unit * 4;
    b->light_w = unit;
    b->sleep_x = unit * 5;
    b->sleep_w = screen_w - b->sleep_x;
}

bar_hit bar_hit_test(const bar_layout *b, int x, int y)
{
    if (!b || y < 0 || y >= b->height || x < 0 || x >= b->screen_w)
        return BAR_HIT_NONE;
    if (x >= b->options_x && x < b->options_x + b->options_w)
        return BAR_HIT_OPTIONS;
    if (x >= b->pages_x && x < b->pages_x + b->pages_w)
        return BAR_HIT_PAGES;
    if (x >= b->day_x && x < b->day_x + b->day_w)
        return BAR_HIT_DAYNIGHT;
    if (x >= b->light_x && x < b->light_x + b->light_w)
        return BAR_HIT_LIGHT;
    if (x >= b->sleep_x && x < b->sleep_x + b->sleep_w)
        return BAR_HIT_SLEEP;
    return BAR_HIT_NONE;
}

int bar_draw(const bar_layout *b,
             const char *clock,
             int night,
             int light_on,
             int light_available,
             int page,
             int pages,
             int fg,
             int bg,
             const char *pages_label)
{
    char pages_buf[96];
    const char *day_lbl = night ? "Night" : "Day";
    const char *light_lbl;
    const char *shown;

    if (!b)
        return -1;

    FillArea(0, 0, b->screen_w, b->height, bg);
    DrawLine(0, b->height - 1, b->screen_w, b->height - 1, fg);

    DrawTextRect(b->clock_x, 0, b->clock_w, b->height,
                 clock ? clock : "--:--", ALIGN_CENTER | VALIGN_MIDDLE);

    if (pages_label && pages_label[0])
        shown = pages_label;
    else {
        snprintf(pages_buf, sizeof(pages_buf), "%d/%d", page, pages);
        shown = pages_buf;
    }
    DrawTextRect(b->pages_x, 0, b->pages_w, b->height,
                 shown, ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(b->options_x, 0, b->options_w, b->height,
                 "Options", ALIGN_CENTER | VALIGN_MIDDLE);

    DrawTextRect(b->day_x, 0, b->day_w, b->height,
                 day_lbl, ALIGN_CENTER | VALIGN_MIDDLE);

    if (light_available)
        light_lbl = light_on ? "Light on" : "Light off";
    else
        light_lbl = "";
    DrawTextRect(b->light_x, 0, b->light_w, b->height,
                 light_lbl, ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(b->sleep_x, 0, b->sleep_w, b->height,
                 "Sleep", ALIGN_CENTER | VALIGN_MIDDLE);
    return 0;
}
