#include "bar.h"

#include <stdio.h>
#include <string.h>

#include "inkview.h"

void bar_compute(bar_layout *b, int screen_w, int screen_h, int height)
{
    int slot;
    memset(b, 0, sizeof(*b));
    b->screen_w = screen_w;
    b->screen_h = screen_h;
    b->height = height;
    if (screen_w <= 0 || height <= 0)
        return;
    slot = screen_w / 4;
    b->clock_x = 0;
    b->clock_w = slot;
    b->day_x = slot;
    b->day_w = slot;
    b->light_x = slot * 2;
    b->light_w = slot;
    b->pages_x = slot * 3;
    b->pages_w = screen_w - b->pages_x;
}

bar_hit bar_hit_test(const bar_layout *b, int x, int y)
{
    if (!b || y < 0 || y >= b->height || x < 0 || x >= b->screen_w)
        return BAR_HIT_NONE;
    if (x >= b->day_x && x < b->day_x + b->day_w)
        return BAR_HIT_DAYNIGHT;
    if (x >= b->light_x && x < b->light_x + b->light_w)
        return BAR_HIT_LIGHT;
    if (x >= b->pages_x && x < b->pages_x + b->pages_w)
        return BAR_HIT_PAGES;
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
             int bg)
{
    char pages_buf[48];
    const char *day_lbl = night ? "Night" : "Day";
    const char *light_lbl;

    if (!b)
        return -1;

    FillArea(0, 0, b->screen_w, b->height, bg);
    DrawLine(0, b->height - 1, b->screen_w, b->height - 1, fg);

    DrawTextRect(b->clock_x, 0, b->clock_w, b->height,
                 clock ? clock : "--:--", ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(b->day_x, 0, b->day_w, b->height,
                 day_lbl, ALIGN_CENTER | VALIGN_MIDDLE);

    if (light_available)
        light_lbl = light_on ? "Light on" : "Light off";
    else
        light_lbl = "";
    DrawTextRect(b->light_x, 0, b->light_w, b->height,
                 light_lbl, ALIGN_CENTER | VALIGN_MIDDLE);

    snprintf(pages_buf, sizeof(pages_buf), "%d / %d", page, pages);
    DrawTextRect(b->pages_x, 0, b->pages_w, b->height,
                 pages_buf, ALIGN_CENTER | VALIGN_MIDDLE);
    return 0;
}
