#ifndef SIMPLE_READER_UI_H
#define SIMPLE_READER_UI_H

#include "config.h"
#include "bar.h"
#include "engine.h"
#include "inkview.h"

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_BROWSE,
    SCREEN_READING,
    SCREEN_SETTINGS,
    SCREEN_CONTENTS,
    SCREEN_FONTS,
    SCREEN_MESSAGE,
    SCREEN_SUGGEST_ASK,
    SCREEN_SUGGEST_RESULTS
} app_screen;

typedef enum {
    BROWSE_SENDTO = 0,
    BROWSE_LIBRARY = 1,
    BROWSE_RECENT = 2
} browse_root;

typedef struct book_entry {
    char path[512];
    char title[256];
    char author[128];
} book_entry;

typedef struct suggest_hit {
    char title[256];
    char author[128];
    char reason[256];
    char path[512]; /* matched file, may be empty */
    char series[120];
    int index;
} suggest_hit;

typedef struct app_state {
    app_screen screen;
    reader_config cfg;
    bar_layout bar;
    char open_path[512];
    char last_book[512];
    char message[256];
    char clock[16];
    book_entry *books;
    int book_count;
    int book_scroll;
    int library_ready;
    browse_root browse_kind;
    char browse_title[64];
    int light_available;
    int light_on;
    int pages_since_full;
    unsigned char *page_buf;
    int buf_w;
    int buf_h;
    int buf_stride;
    ifont *font_bar;
    ifont *font_ui;
    ifont *font_msg;
    engine_toc_item *toc;
    int toc_count;
    int toc_scroll;
    int toc_is_bookmarks; /* 1 = bookmarks list in contents screen */
    int settings_scroll;
    int goto_page_tmp;
    suggest_hit suggestions[5];
    int suggest_count;
    char suggest_status[128];
    /* Jump-back: position before Contents / Go to page. */
    char jump_back_pos[ENGINE_POS_MAX];
    int jump_back_page;
    int jump_back_valid;
    /* 1 = books menu opened from Options; Back returns there, not exit. */
    int books_from_options;
    char find_query[96];
    char library_filter[96];
    /* Pace estimate: last few page-turn gaps in ms. */
    int turn_gaps_ms[8];
    int turn_gap_count;
    int turn_gap_i;
    unsigned long last_turn_ms;
    char bar_pages_label[96];
} app_state;

extern app_state g_app;

void ui_init(void);
void ui_shutdown(void);
void ui_set_message(const char *msg);
void ui_show_home(void);
void ui_show_browse(browse_root root);
void ui_open_book(const char *path);
void ui_open_next_in_series(void);
void ui_draw(void);
void ui_handle_pointer(int x, int y);
void ui_handle_key(int key);
void ui_on_show(void);
void ui_on_hide(void);
void ui_on_orientation(void);
void ui_tick_clock(void);
void ui_save_position(void);
void ui_remember_jump(void);
void ui_apply_layout(void);
void ui_apply_fonts(void);

int library_scan(browse_root root);
void library_draw(void);
int library_hit(int x, int y);
void home_draw(void);
int home_hit(int x, int y);
/* Title, author, and series come from the file name (or the library
 * folder names when the file itself is a hash). Any out pointer may be NULL. */
void book_names_from_path(const char *path,
                          char *title, size_t title_n,
                          char *author, size_t author_n,
                          char *series, size_t series_n,
                          int *index);
/* Calibre Author folder (or parent when there is no Title (id) folder).
 * Empty for SendToPb and other generic parents. No file I/O. */
void folder_author_from_path(const char *path, char *out, size_t out_n);
/* File name, then sidecar metadata.opf, then GetBookInfo for a hash or
 * an empty field. year may be NULL. Never frees the SDK bookinfo. */
void book_info_from_path(const char *path,
                         char *title, size_t title_n,
                         char *author, size_t author_n,
                         char *series, size_t series_n,
                         int *index,
                         int *year);

void settings_draw(void);
int settings_hit(int x, int y);

void contents_load(void);
void contents_draw(void);
int contents_hit(int x, int y);

void fonts_open(void);
void fonts_draw(void);
int fonts_hit(int x, int y);

void suggest_ask_draw(void);
int suggest_ask_hit(int x, int y);
void suggest_results_draw(void);
int suggest_results_hit(int x, int y);
void suggest_run(int mode); /* 0 series, 1 theme, 2 something else, 3 suggest next */

#endif
