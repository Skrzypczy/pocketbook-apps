/*
 * Stage-1 debug controls: debug.cfg, crash handlers, draw sentinel, mem probe.
 * debug.cfg is never rewritten by config_save — edit on the device over USB.
 */
#ifndef SIMPLE_READER_DEBUG_H
#define SIMPLE_READER_DEBUG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RENDER_TEXT = 0,   /* Inkview page_text path (baseline) */
    RENDER_AUTO = 1,   /* Prefer engine bitmap; fall back to text */
    RENDER_PATTERN = 2 /* Synthetic gray pattern; no crengine Draw */
} render_mode;

typedef struct reader_debug {
    render_mode render;
    int draw_probe;   /* 0..6 ladder; ignored for PATTERN */
    int crash_test;   /* 1 = deliberate SIGSEGV once after open */
    int force_text;   /* 1 = previous draw died (sentinel found) */
} reader_debug;

extern reader_debug g_debug;

void debug_init(void);
void debug_log_build_id(void);
void debug_log_mem(const char *tag);
void debug_install_handlers(void);
/* Host tests: apply one key=value into out. Returns 0 on success. */
int debug_parse_line_for_test(const char *line_in, reader_debug *out);

/* Draw-crash loop guard. path may be NULL. */
void debug_sentinel_begin(const char *path);
void debug_sentinel_clear(void);
int debug_sentinel_was_set(void); /* 1 if found at startup */
void debug_clear_draw_safe(void); /* allow auto Draw again after a crash */

/* Caps boot log like the main log. */
void boot_marker(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* SIMPLE_READER_DEBUG_H */
