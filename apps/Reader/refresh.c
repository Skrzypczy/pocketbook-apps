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
    /* Do not WaitForUpdateComplete — matching Chef/Vault; waiting here has
     * hung/killed long draws on Era. */
    g_busy = 1;
    PartialUpdate(x, y, w, h);
    g_busy = 0;
}

void refresh_full(void)
{
    g_busy = 1;
    FullUpdate();
    g_busy = 0;
}
