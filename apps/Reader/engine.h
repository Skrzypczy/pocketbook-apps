/*
 * Stable C API between Reader screens and layout engines.
 * Screens include only this header. Adapters implement the table.
 */
#ifndef SIMPLE_READER_ENGINE_H
#define SIMPLE_READER_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define ENGINE_POS_MAX     4096
#define ENGINE_TOC_MAX     512
#define ENGINE_TITLE_MAX   256

typedef struct engine_layout {
    int font_size;          /* points / crengine units */
    int margin;             /* pixels on each side */
    int line_spacing;       /* percent, 100 = normal */
    int night;              /* 0 day, 1 night */
    int publisher_styles;   /* 0 off, 1 on */
    const char *font_face;  /* may be NULL */
} engine_layout;

typedef struct engine_toc_item {
    char title[ENGINE_TITLE_MAX];
    char position[ENGINE_POS_MAX];
} engine_toc_item;

typedef struct reader_engine {
    int (*open)(const char *path, int width, int height);
    void (*close)(void);
    int (*set_layout)(const engine_layout *layout);
    int (*page_count)(void);
    int (*current_page)(void);          /* 1-based */
    int (*go_to)(int page);             /* 1-based */
    int (*next)(void);
    int (*prev)(void);
    int (*draw)(unsigned char *gray, int width, int height, int stride);
    int (*get_position)(char *buf, size_t buflen);
    int (*set_position)(const char *text);
    int (*contents)(engine_toc_item *items, int max_items);
    int (*is_reflowable)(void);         /* 1 = show font/margin settings */
    const char *(*name)(void);
} reader_engine;

/* Returns the engine table for path, or NULL if unsupported. */
const reader_engine *engine_for_path(const char *path);

/* Active engine helpers used by the UI. */
int engine_open_path(const char *path, int width, int height);
void engine_close(void);
const reader_engine *engine_active(void);
int engine_supports_extension(const char *path);

/* Cache / font init once per process (crengine). */
int engine_global_init(const char *cache_dir, const char *fonts_dir);
void engine_global_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SIMPLE_READER_ENGINE_H */
