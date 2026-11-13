#ifndef SIMPLE_READER_BAR_H
#define SIMPLE_READER_BAR_H

#include <stddef.h>

typedef enum {
    BAR_HIT_NONE = 0,
    BAR_HIT_DAYNIGHT,
    BAR_HIT_LIGHT,
    BAR_HIT_OPTIONS,
    BAR_HIT_PAGES,
    BAR_HIT_SLEEP
} bar_hit;

/* Progress slot modes (also stored as cfg.bar_progress). */
#define BAR_PROGRESS_PAGES   0
#define BAR_PROGRESS_PERCENT 1
#define BAR_PROGRESS_ETA     2
#define BAR_PROGRESS_COUNT   3

typedef struct bar_layout {
    int screen_w;
    int screen_h;
    int height;
    int clock_x, clock_w;
    int day_x, day_w;
    int light_x, light_w;
    int options_x, options_w;
    int pages_x, pages_w;
    int sleep_x, sleep_w;
} bar_layout;

void bar_compute(bar_layout *b, int screen_w, int screen_h, int height);
bar_hit bar_hit_test(const bar_layout *b, int x, int y);

/* mode 0 pages, 1 percent, 2 ETA. eta_sec < 0 means unknown. */
void bar_format_progress(int mode, int page, int pages, long eta_sec,
                         char *out, size_t out_n);

/* Caller must SetFont before calling. */
/* pages_label overrides "page / pages" when non-NULL. */
int bar_draw(const bar_layout *b,
             const char *clock,
             int night,
             int light_on,
             int light_available,
             int page,
             int pages,
             int fg,
             int bg,
             const char *pages_label);

#endif /* SIMPLE_READER_BAR_H */
