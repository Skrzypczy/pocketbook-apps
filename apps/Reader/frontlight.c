#include "frontlight.h"
#include "config.h"

#include <limits.h>

#include "inkview.h"

/* Present in libinkview on SDK 5.19+, missing from some inkview.h copies. */
int GetFrontlightEnabled(void);
void SetFrontlightEnabled(int enabled);

/* Era SMARTlight. Not in the public header, exported by libinkview. */
int hw_get_frontlight_color(void);
void hw_set_frontlight_color(int color);
void hw_set_frontlight_color_auto_enabled(int enable);

int frontlight_available(void)
{
    int st = GetFrontlightState();
    return st != INT_MIN;
}

int frontlight_is_on(void)
{
    int st;
    if (!frontlight_available())
        return 0;
    /* Prefer state sign — avoids relying on Enabled quirks during early boot. */
    st = GetFrontlightState();
    if (st == INT_MIN)
        return 0;
    return st >= 0;
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
    int c = hw_get_frontlight_color();
    if (c == INT_MIN)
        c = GetFrontlightColor();
    return c;
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
    iconfig *cfg;
    if (warmth_0_100 < 0)
        warmth_0_100 = 0;
    if (warmth_0_100 > 100)
        warmth_0_100 = 100;
    /* The timed SMARTlight profile was putting the color back (25 at this
     * hour). Turn that off, then set the LEDs and the system memory. */
    hw_set_frontlight_color_auto_enabled(0);
    hw_set_frontlight_color(warmth_0_100);
    SetFrontlightColor(warmth_0_100);
    cfg = GetGlobalConfig();
    if (cfg) {
        WriteInt(cfg, "FrontLightLastColor", warmth_0_100);
        SaveConfig(cfg);
        NotifyConfigChanged();
    }
    reader_log("warmth set %d", warmth_0_100);
    return 0;
}
