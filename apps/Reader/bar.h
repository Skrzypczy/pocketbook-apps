#ifndef SIMPLE_READER_BAR_H
#define SIMPLE_READER_BAR_H

typedef enum {
    BAR_HIT_NONE = 0,
    BAR_HIT_DAYNIGHT,
    BAR_HIT_LIGHT,
    BAR_HIT_BOOKS,
    BAR_HIT_PAGES
} bar_hit;

typedef struct bar_layout {
    int screen_w;
    int screen_h;
    int height;
    int clock_x, clock_w;
    int day_x, day_w;
    int light_x, light_w;
    int books_x, books_w;
    int pages_x, pages_w;
} bar_layout;

void bar_compute(bar_layout *b, int screen_w, int screen_h, int height);
bar_hit bar_hit_test(const bar_layout *b, int x, int y);

/* Caller must SetFont before calling. */
int bar_draw(const bar_layout *b,
             const char *clock,
             int night,
             int light_on,
             int light_available,
             int page,
             int pages,
             int fg,
             int bg);

#endif /* SIMPLE_READER_BAR_H */
