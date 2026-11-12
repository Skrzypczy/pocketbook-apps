#ifndef FRONTLIGHT_PEEK_H
#define FRONTLIGHT_PEEK_H

#define FRONTLIGHT_PEEK_MS 5000
#define FRONTLIGHT_PEEK_FALLBACK_BRIGHT 40

typedef struct {
    int active;     /* 1 = start a temporary on */
    int saved_off;  /* state to restore (usually negative) */
    int brightness; /* 1..100 temporary on level */
} frontlight_peek_decision;

/*
 * Pure decision.
 * state = GetFrontlightState(); enabled = GetFrontlightEnabled() (0/1; treat <0 as unknown);
 * peek_active if timer already running.
 */
frontlight_peek_decision frontlight_peek_decide(int state, int enabled, int peek_active);

/* Restore off only when still enabled at the temporary brightness. */
int frontlight_peek_should_restore(int live_state, int live_enabled, int peek_brightness);

void frontlight_peek_on_show(void);
void frontlight_peek_cancel(void);
int frontlight_peek_active(void);
/* Optional: called after peek timer restores (or skips restore). Not called on cancel. */
void frontlight_peek_set_on_end(void (*cb)(void));

#ifdef FRONTLIGHT_PEEK_HOST_TEST
void frontlight_peek_host_reset(void);
void frontlight_peek_host_set_state(int state);
void frontlight_peek_host_set_enabled(int enabled);
int frontlight_peek_host_state(void);
int frontlight_peek_host_enabled(void);
int frontlight_peek_host_timer_armed(void);
void frontlight_peek_host_fire_timer(void);
#endif

#endif
