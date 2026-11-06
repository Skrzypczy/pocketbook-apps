#ifndef SIMPLE_READER_CONFIG_H
#define SIMPLE_READER_CONFIG_H

#include "engine.h"

#define CFG_PATH            "/mnt/ext1/.naskilla.cfg"
#define CFG_CACHE_DIR       "/mnt/ext1/system/state/naskilla/cr3cache"
#define CFG_STATE_DIR       "/mnt/ext1/system/state/naskilla"
#define CFG_LOG_PATH        "/mnt/ext1/system/state/naskilla.log"
#define CFG_BOOT_LOG        "/mnt/ext1/naskilla_boot.log"
#define CFG_FONTS_DIR       "/mnt/ext1/system/fonts"
#define CFG_BOOKS_DIR       "/mnt/ext1/Books"
#define CFG_LIBRARY_DIR     "/mnt/ext1/Library"
#define CFG_SENDTO_DIR1     "/mnt/ext1/SendToPocketBook"
#define CFG_SENDTO_DIR2     "/mnt/ext1/SendToPb"
#define CFG_SENDTO_DIR3     "/mnt/ext1/Send to PocketBook"
#define CFG_API_KEY_FILE    "/mnt/ext1/.ai_api_key"
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
} reader_config;

void config_set_defaults(reader_config *cfg);
int config_load(reader_config *cfg);
int config_save(const reader_config *cfg);
int config_get_position(const char *path, char *buf, size_t buflen);
int config_set_position(const char *path, const char *pos);
const char *config_find_sendto_dir(void);

#ifdef __cplusplus
extern "C" {
#endif
void reader_log(const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#endif /* SIMPLE_READER_CONFIG_H */
