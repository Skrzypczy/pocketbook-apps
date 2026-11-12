/*
 * Host unit tests for temporary frontlight peek decisions.
 * gcc -DFRONTLIGHT_PEEK_HOST_TEST -I. test_frontlight_peek.c frontlight_peek.c -o test_frontlight_peek
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>

#define FRONTLIGHT_PEEK_HOST_TEST 1
#include "frontlight_peek.h"

static void test_decide_skip_unavailable(void)
{
    frontlight_peek_decision d = frontlight_peek_decide(INT_MIN, 0, 0);
    assert(d.active == 0);
}

static void test_decide_skip_already_on(void)
{
    frontlight_peek_decision d = frontlight_peek_decide(40, 1, 0);
    assert(d.active == 0);
    d = frontlight_peek_decide(0, 1, 0);
    assert(d.active == 0);
    /* Unknown enabled: use state sign. */
    d = frontlight_peek_decide(40, -1, 0);
    assert(d.active == 0);
}

static void test_decide_when_disabled(void)
{
    /* Enabled=0 with positive remembered brightness → still peek.
     * UI must treat this as off (not on) — same gate as frontlight_is_on. */
    frontlight_peek_decision d = frontlight_peek_decide(40, 0, 0);
    assert(d.active == 1);
    assert(d.brightness == 40);
    assert(d.saved_off == -40);
}

static int g_end_calls;

static void on_end_count(void)
{
    g_end_calls++;
}

static void test_on_end_after_timer_not_cancel(void)
{
    g_end_calls = 0;
    frontlight_peek_host_reset();
    frontlight_peek_set_on_end(on_end_count);
    frontlight_peek_host_set_state(40);
    frontlight_peek_host_set_enabled(0);
    frontlight_peek_on_show();
    assert(frontlight_peek_host_enabled() == 1);
    frontlight_peek_host_fire_timer();
    assert(g_end_calls == 1);
    assert(frontlight_peek_host_enabled() == 0);

    g_end_calls = 0;
    frontlight_peek_host_reset();
    frontlight_peek_set_on_end(on_end_count);
    frontlight_peek_host_set_state(-40);
    frontlight_peek_host_set_enabled(0);
    frontlight_peek_on_show();
    frontlight_peek_cancel();
    frontlight_peek_host_fire_timer();
    assert(g_end_calls == 0);
}

static void test_decide_skip_when_peek_active(void)
{
    frontlight_peek_decision d = frontlight_peek_decide(-40, 0, 1);
    assert(d.active == 0);
}

static void test_decide_peek_with_abs_brightness(void)
{
    frontlight_peek_decision d = frontlight_peek_decide(-55, 0, 0);
    assert(d.active == 1);
    assert(d.saved_off == -55);
    assert(d.brightness == 55);
}

static void test_decide_peek_fallback_brightness(void)
{
    frontlight_peek_decision d = frontlight_peek_decide(-200, 0, 0);
    assert(d.active == 1);
    assert(d.saved_off == -200);
    assert(d.brightness == FRONTLIGHT_PEEK_FALLBACK_BRIGHT);

    d = frontlight_peek_decide(-1, 0, 0);
    assert(d.active == 1);
    assert(d.brightness == 1);
}

static void test_should_restore(void)
{
    assert(frontlight_peek_should_restore(40, 1, 40) == 1);
    assert(frontlight_peek_should_restore(50, 1, 40) == 0);
    assert(frontlight_peek_should_restore(40, 0, 40) == 0);
    assert(frontlight_peek_should_restore(-40, 1, 40) == 0);
    assert(frontlight_peek_should_restore(INT_MIN, 1, 40) == 0);
}

static void test_cancel_clears_pending_restore(void)
{
    frontlight_peek_host_reset();
    frontlight_peek_host_set_state(-40);
    frontlight_peek_host_set_enabled(0);
    frontlight_peek_on_show();
    assert(frontlight_peek_host_state() == 40);
    assert(frontlight_peek_host_enabled() == 1);
    assert(frontlight_peek_host_timer_armed() == 1);
    frontlight_peek_cancel();
    assert(frontlight_peek_host_timer_armed() == 0);
    frontlight_peek_host_fire_timer();
    assert(frontlight_peek_host_state() == 40);
    assert(frontlight_peek_host_enabled() == 1);
}

static void test_timer_restores_off(void)
{
    frontlight_peek_host_reset();
    frontlight_peek_host_set_state(-40);
    frontlight_peek_host_set_enabled(0);
    frontlight_peek_on_show();
    assert(frontlight_peek_host_state() == 40);
    assert(frontlight_peek_host_enabled() == 1);
    frontlight_peek_host_fire_timer();
    assert(frontlight_peek_host_enabled() == 0);
    assert(frontlight_peek_host_state() == -40);
}

int main(void)
{
    test_decide_skip_unavailable();
    test_decide_skip_already_on();
    test_decide_when_disabled();
    test_decide_skip_when_peek_active();
    test_decide_peek_with_abs_brightness();
    test_decide_peek_fallback_brightness();
    test_should_restore();
    test_cancel_clears_pending_restore();
    test_timer_restores_off();
    test_on_end_after_timer_not_cancel();
    printf("frontlight_peek host tests passed\n");
    return 0;
}
