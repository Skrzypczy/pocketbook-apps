#include "frontlight_peek.h"

#include <limits.h>
#include <stdlib.h>

#ifndef FRONTLIGHT_PEEK_HOST_TEST
#include <inkview.h>
#endif

/* Present in libinkview; may be missing from some inkview.h copies. */
#ifndef FRONTLIGHT_PEEK_HOST_TEST
void SetFrontlightStateEx(int flstate, int temporary);
int GetFrontlightEnabled(void);
void SetFrontlightEnabled(int enabled);
#endif

static int g_peek_active;
static int g_saved_off;
static int g_peek_brightness;
static void (*g_on_end)(void);

#ifdef FRONTLIGHT_PEEK_HOST_TEST
static int g_host_state = INT_MIN;
static int g_host_enabled;
static int g_host_timer_armed;
static void (*g_host_timer_proc)(void);

static int GetFrontlightState(void)
{
    return g_host_state;
}

static int GetFrontlightEnabled(void)
{
    return g_host_enabled;
}

static void SetFrontlightEnabled(int enabled)
{
    g_host_enabled = enabled ? 1 : 0;
}

static void SetFrontlightStateEx(int flstate, int temporary)
{
    (void)temporary;
    g_host_state = flstate;
    if (flstate >= 0)
        g_host_enabled = 1;
}

static void SetHardTimer(const char *name, void (*tproc)(void), int ms)
{
    (void)name;
    (void)ms;
    g_host_timer_proc = tproc;
    g_host_timer_armed = 1;
}

static void ClearTimer(void (*tproc)(void))
{
    if (g_host_timer_proc == tproc) {
        g_host_timer_proc = NULL;
        g_host_timer_armed = 0;
    }
}

void frontlight_peek_host_reset(void)
{
    g_peek_active = 0;
    g_saved_off = 0;
    g_peek_brightness = 0;
    g_on_end = NULL;
    g_host_state = INT_MIN;
    g_host_enabled = 0;
    g_host_timer_armed = 0;
    g_host_timer_proc = NULL;
}

void frontlight_peek_host_set_state(int state)
{
    g_host_state = state;
}

void frontlight_peek_host_set_enabled(int enabled)
{
    g_host_enabled = enabled ? 1 : 0;
}

int frontlight_peek_host_state(void)
{
    return g_host_state;
}

int frontlight_peek_host_enabled(void)
{
    return g_host_enabled;
}

int frontlight_peek_host_timer_armed(void)
{
    return g_host_timer_armed;
}

void frontlight_peek_host_fire_timer(void)
{
    void (*proc)(void) = g_host_timer_proc;
    g_host_timer_proc = NULL;
    g_host_timer_armed = 0;
    if (proc)
        proc();
}
#endif

frontlight_peek_decision frontlight_peek_decide(int state, int enabled, int peek_active)
{
    frontlight_peek_decision d;
    int bright;
    int is_on;

    d.active = 0;
    d.saved_off = 0;
    d.brightness = 0;

    if (peek_active)
        return d;
    if (state == INT_MIN)
        return d;

    /* Firmware 5.19+: Enabled gate; older APIs leave enabled unknown (<0). */
    if (enabled > 0)
        is_on = (state >= 0);
    else if (enabled == 0)
        is_on = 0;
    else
        is_on = (state >= 0);

    if (is_on)
        return d;

    d.saved_off = (state < 0) ? state : -((state > 0) ? state : FRONTLIGHT_PEEK_FALLBACK_BRIGHT);
    bright = abs(state);
    if (bright < 1 || bright > 100)
        bright = FRONTLIGHT_PEEK_FALLBACK_BRIGHT;
    d.brightness = bright;
    d.active = 1;
    return d;
}

int frontlight_peek_should_restore(int live_state, int live_enabled, int peek_brightness)
{
    if (peek_brightness < 1)
        return 0;
    if (live_enabled == 0)
        return 0;
    return live_state == peek_brightness;
}

static void peek_timer(void)
{
    int live;
    int enabled;

    if (!g_peek_active)
        return;
    g_peek_active = 0;
    live = GetFrontlightState();
    enabled = GetFrontlightEnabled();
    if (frontlight_peek_should_restore(live, enabled, g_peek_brightness)) {
        SetFrontlightEnabled(0);
        SetFrontlightStateEx(g_saved_off, 1);
    }
    if (g_on_end)
        g_on_end();
}

void frontlight_peek_cancel(void)
{
    if (!g_peek_active)
        return;
    g_peek_active = 0;
    ClearTimer(peek_timer);
}

int frontlight_peek_active(void)
{
    return g_peek_active;
}

void frontlight_peek_set_on_end(void (*cb)(void))
{
    g_on_end = cb;
}

void frontlight_peek_on_show(void)
{
    frontlight_peek_decision d;
    int state = GetFrontlightState();
    int enabled = GetFrontlightEnabled();

    d = frontlight_peek_decide(state, enabled, g_peek_active);
    if (!d.active)
        return;

    g_saved_off = d.saved_off;
    g_peek_brightness = d.brightness;
    g_peek_active = 1;
    /* Era 5.19 needs Enabled=1; StateEx alone often leaves LEDs dark. */
    SetFrontlightEnabled(1);
    SetFrontlightStateEx(d.brightness, 1);
    SetHardTimer("frontlight_peek", peek_timer, FRONTLIGHT_PEEK_MS);
}
