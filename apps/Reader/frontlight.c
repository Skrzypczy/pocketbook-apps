#include "frontlight.h"

#include <limits.h>

#include "inkview.h"

/* Present in libinkview on SDK 5.19+, missing from some inkview.h copies. */
int GetFrontlightEnabled(void);
void SetFrontlightEnabled(int enabled);

int frontlight_available(void)
{
    int st = GetFrontlightState();
    return st != INT_MIN;
}

int frontlight_is_on(void)
{
    if (!frontlight_available())
        return 0;
    /* Prefer Enabled API when present; fall back to state sign. */
    {
        int en = GetFrontlightEnabled();
        if (en >= 0)
            return en > 0;
    }
    return GetFrontlightState() >= 0;
}

int frontlight_get_brightness(void)
{
    int st = GetFrontlightState();
    if (st == INT_MIN || st < 0)
        return 0;
    if (st < 1)
        return 1;
    if (st > 100)
        return 100;
    return st;
}

int frontlight_get_warmth(void)
{
    return GetFrontlightColor();
}

int frontlight_set_on(int on, int brightness_1_100)
{
    if (!frontlight_available())
        return -1;
    if (!on) {
        SetFrontlightEnabled(0);
        return 0;
    }
    if (brightness_1_100 < 1)
        brightness_1_100 = 1;
    if (brightness_1_100 > 100)
        brightness_1_100 = 100;
    SetFrontlightEnabled(1);
    SetFrontlightState(brightness_1_100);
    return 0;
}

int frontlight_set_brightness(int brightness_1_100)
{
    if (!frontlight_available())
        return -1;
    if (brightness_1_100 < 1)
        brightness_1_100 = 1;
    if (brightness_1_100 > 100)
        brightness_1_100 = 100;
    if (frontlight_is_on())
        SetFrontlightState(brightness_1_100);
    return 0;
}

int frontlight_set_warmth(int warmth_0_100)
{
    int cur = GetFrontlightColor();
    if (cur == INT_MIN)
        return -1;
    if (warmth_0_100 < 0)
        warmth_0_100 = 0;
    if (warmth_0_100 > 100)
        warmth_0_100 = 100;
    SetFrontlightColor(warmth_0_100);
    return 0;
}
