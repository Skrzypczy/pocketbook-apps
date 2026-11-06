#include "refresh.h"

#include "inkview.h"

static int g_busy;

void refresh_init(void)
{
    g_busy = 0;
}

int refresh_busy(void)
{
    return g_busy;
}

void refresh_partial(int x, int y, int w, int h)
{
    if (g_busy)
        WaitForUpdateComplete();
    g_busy = 1;
    PartialUpdate(x, y, w, h);
    /* Partial updates are short; clear busy after wait so clock can proceed. */
    WaitForUpdateComplete();
    g_busy = 0;
}

void refresh_full(void)
{
    if (g_busy)
        WaitForUpdateComplete();
    g_busy = 1;
    FullUpdate();
    WaitForUpdateComplete();
    g_busy = 0;
}
