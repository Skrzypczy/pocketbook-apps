#ifndef SIMPLE_READER_CONFIG_H
#define SIMPLE_READER_CONFIG_H

#include "engine.h"

#define CFG_PATH            "/mnt/ext1/.naskilla.cfg"
#define CFG_BOOKMETA_PATH   "/mnt/ext1/system/state/naskilla/bookmeta.cfg"
#define CFG_CACHE_DIR       "/mnt/ext1/system/state/naskilla/cr3cache"
#define CFG_STATE_DIR       "/mnt/ext1/system/state/naskilla"
#define CFG_LOG_PATH        "/mnt/ext1/system/state/naskilla.log"
#define CFG_BOOT_LOG        "/mnt/ext1/naskilla_boot.log"
#define CFG_DEBUG_PATH      "/mnt/ext1/system/state/naskilla/debug.cfg"
#define CFG_SENTINEL_PATH   "/mnt/ext1/system/state/naskilla/draw_inflight"
/* Sticky after a Draw crash until the user deletes it or draw succeeds. */
#define CFG_DRAW_SAFE_PATH  "/mnt/ext1/system/state/naskilla/draw_safe_text"
#define CFG_FONTS_DIR       "/mnt/ext1/system/fonts"
#define CFG_BOOKS_DIR       "/mnt/ext1/Books"
#define CFG_LIBRARY_DIR     "/mnt/ext1/Library"
#define CFG_SENDTO_DIR1     "/mnt/ext1/SendToPocketBook"
#define CFG_SENDTO_DIR2     "/mnt/ext1/SendToPb"
#define CFG_SENDTO_DIR3     "/mnt/ext1/Send to PocketBook"
#define CFG_API_KEY_FILE    "/mnt/ext1/.ai_api_key"
#define CFG_APP_NAME        "NaSkilla Reader"
#define CFG_APP_PATH        "/mnt/ext1/applications/NaSkilla Reader.app"
#define CFG_LOG_MAX         (64 * 1024)

#define CFG_FONT_FACE_MAX   64
#define CFG_DEFAULT_FACE    "Bookerly"
#define CFG_DEFAULT_SIZE    32
#define CFG_DEFAULT_MARGIN  40
#define CFG_DEFAULT_SPACING 120
#define CFG_DEFAULT_REFRESH 8
#define CFG_DEFAULT_BRIGHT  30

#define CFG_FONT_MIN        16
#define CFG_FONT_MAX        72
#define CFG_MARGIN_MIN      10
#define CFG_MARGIN_MAX      120
#define CFG_SPACING_MIN     80
#define CFG_SPACING_MAX     200
#define CFG_REFRESH_MIN     1
#define CFG_REFRESH_MAX     30
#define CFG_BRIGHT_MIN      1
#define CFG_BRIGHT_MAX      100
#define CFG_WARMTH_MIN      0
#define CFG_WARMTH_MAX      100

#define CFG_BOOKS_MAX       8000
#define CFG_RECENT_MAX      10
/* Prefer ui_bar_h() from layout.h at runtime; keep a fallback constant. */
#define CFG_BAR_HEIGHT      64

typedef struct reader_config {
    char font_face[CFG_FONT_FACE_MAX];
    int font_size;
    int margin;
    int line_spacing;
    int night;
    int publisher_styles;
    int full_refresh;
    int brightness;
    int light_on;       /* -1 = leave device alone at launch until user toggles */
    int warmth;         /* -1 = unread from device */
    int warmth_valid;
    char last_book[512];
    char recent[CFG_RECENT_MAX][512];
    int recent_count;
} reader_config;

void config_set_defaults(reader_config *cfg);
int config_load(reader_config *cfg);
int config_save(const reader_config *cfg);
void config_remember_book(reader_config *cfg, const char *path);
int config_get_position(const char *path, char *buf, size_t buflen);
int config_set_position(const char *path, const char *pos);
/* Generic per-book keys: prog.<path>, fsize.<path>, bmk.<path>.N */
int config_get_book_kv(const char *prefix, const char *path, char *buf, size_t buflen);
int config_set_book_kv(const char *prefix, const char *path, const char *val);
int config_get_progress(const char *path, int *page, int *pages);
int config_set_progress(const char *path, int page, int pages);
int config_get_book_font_size(const char *path, int *size_out);
int config_set_book_font_size(const char *path, int size);

#define CFG_BOOKMARKS_MAX 20
typedef struct book_bookmark {
    char pos[ENGINE_POS_MAX];
    char title[96];
    int page;
} book_bookmark;
int config_bookmarks_load(const char *path, book_bookmark *out, int max);
int config_bookmark_add(const char *path, const book_bookmark *bm);
const char *config_find_sendto_dir(void);

/* Cached GetBookInfo / sidecar fields for recent books only.
 * Cap is CFG_RECENT_MAX; oldest entries are dropped. */
typedef struct book_meta {
    char path[512];
    char title[160];
    char author[80];
    char series[120];
    int index;
    int year;
} book_meta;

void book_meta_load(void);
void book_meta_save(void);
/* 1 if found; copies into out. Touches MRU order. */
int book_meta_get(const char *path, book_meta *out);
/* Insert or refresh; keeps at most CFG_RECENT_MAX (MRU). */
void book_meta_put(const char *path, const char *title, const char *author,
                   const char *series, int index, int year);
/* Drop entries whose path is not in the recent list. */
void book_meta_prune_recent(const reader_config *cfg);

#ifdef __cplusplus
extern "C" {
#endif
void reader_log(const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#endif /* SIMPLE_READER_CONFIG_H */
