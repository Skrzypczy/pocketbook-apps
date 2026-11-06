/*
 * Host unit tests: clamp, bar hits (Era + landscape), atomic rename.
 * gcc -DHOST_TEST -I. -I.. test_host.c ../bar.c -o test_host
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <stdio.h>
#else
#include <unistd.h>
#endif

#include "bar.h"

int ScreenWidth(void) { return 1264; }
int ScreenHeight(void) { return 1680; }
void FillArea(int x, int y, int w, int h, int c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
void DrawLine(int x1, int y1, int x2, int y2, int c)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)c; }
void DrawTextRect(int x, int y, int w, int h, const char *s, int f)
{ (void)x; (void)y; (void)w; (void)h; (void)s; (void)f; }

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void test_bar_hits(void)
{
    bar_layout b;
    bar_compute(&b, 1264, 1680, 64);
    assert(bar_hit_test(&b, 10, 10) == BAR_HIT_NONE);
    assert(bar_hit_test(&b, 400, 10) == BAR_HIT_DAYNIGHT);
    assert(bar_hit_test(&b, 700, 10) == BAR_HIT_LIGHT);
    assert(bar_hit_test(&b, 800, 10) == BAR_HIT_BOOKS);
    assert(bar_hit_test(&b, 1100, 10) == BAR_HIT_PAGES);
    assert(bar_hit_test(&b, 100, 100) == BAR_HIT_NONE);
    assert(bar_hit_test(&b, -1, 10) == BAR_HIT_NONE);

    bar_compute(&b, 1680, 1264, 64);
    assert(bar_hit_test(&b, 500, 10) == BAR_HIT_DAYNIGHT);
    assert(b.pages_x + b.pages_w == 1680);
    printf("bar hits OK\n");
}

static void test_clamp(void)
{
    assert(clamp_int(5, 16, 72) == 16);
    assert(clamp_int(100, 16, 72) == 72);
    assert(clamp_int(32, 16, 72) == 32);
    printf("clamp OK\n");
}

static void test_atomic_rename(void)
{
    const char *tmp = "host_cfg.tmp";
    const char *dst = "host_cfg.cfg";
    FILE *f = fopen(tmp, "w");
    assert(f);
    fputs("font_size=32\npos./mnt/ext1/Books/a.epub=xpointer\n", f);
    fclose(f);
    remove(dst);
    assert(rename(tmp, dst) == 0);
    f = fopen(dst, "r");
    assert(f);
    {
        char line[256];
        int saw_pos = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "pos.", 4) == 0)
                saw_pos = 1;
        }
        assert(saw_pos);
    }
    fclose(f);
    remove(dst);
    printf("atomic rename OK\n");
}

int main(void)
{
    test_clamp();
    test_bar_hits();
    test_atomic_rename();
    printf("all host tests passed\n");
    return 0;
}
