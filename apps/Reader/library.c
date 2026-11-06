#include "ui.h"
#include "layout.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "inkview.h"

static int ends_with_ci(const char *name, const char *suf)
{
    size_t n = strlen(name);
    size_t s = strlen(suf);
    size_t i;
    if (n < s)
        return 0;
    for (i = 0; i < s; i++) {
        char a = name[n - s + i];
        char b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int is_book_name(const char *name)
{
    return ends_with_ci(name, ".epub") || ends_with_ci(name, ".fb2") ||
           ends_with_ci(name, ".fb2.zip") || ends_with_ci(name, ".txt") ||
           ends_with_ci(name, ".html") || ends_with_ci(name, ".htm") ||
           ends_with_ci(name, ".rtf") || ends_with_ci(name, ".pdf");
}

static void title_from_filename(const char *path, char *title, size_t title_n)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t n = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if (n >= title_n)
        n = title_n - 1;
    memcpy(title, base, n);
    title[n] = '\0';
}

static void add_book(const char *path, int fetch_info)
{
    book_entry *e;
    bookinfo *info = NULL;
    if (g_app.book_count >= CFG_BOOKS_MAX)
        return;
    e = &g_app.books[g_app.book_count];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    /* Library has thousands of files. Reading each epub here freezes the device.
     * Send-to names are hashes, so those few files still use GetBookInfo. */
    if (fetch_info)
        info = GetBookInfo(path);
    if (info && info->title && info->title[0])
        snprintf(e->title, sizeof(e->title), "%s", info->title);
    else
        title_from_filename(path, e->title, sizeof(e->title));
    if (info && info->author && info->author[0])
        snprintf(e->author, sizeof(e->author), "%s", info->author);
    else
        e->author[0] = '\0';
    g_app.book_count++;
}

static int dir_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int book_title_cmp(const void *a, const void *b)
{
    const book_entry *ea = (const book_entry *)a;
    const book_entry *eb = (const book_entry *)b;
    return strcasecmp(ea->title, eb->title);
}

static void scan_dir(const char *dir, int depth, int fetch_info)
{
    DIR *d;
    struct dirent *ent;
    if (depth > 8 || g_app.book_count >= CFG_BOOKS_MAX)
        return;
    d = opendir(dir);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL) {
        char path[768];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            scan_dir(path, depth + 1, fetch_info);
        else if (S_ISREG(st.st_mode) && is_book_name(ent->d_name))
            add_book(path, fetch_info);
        if (g_app.book_count >= CFG_BOOKS_MAX)
            break;
    }
    closedir(d);
}

int library_scan(browse_root root)
{
    const char *dir;

    if (!g_app.books) {
        g_app.books = (book_entry *)calloc(CFG_BOOKS_MAX, sizeof(book_entry));
        if (!g_app.books)
            return -1;
    }
    g_app.book_count = 0;
    g_app.book_scroll = 0;
    g_app.browse_kind = root;

    if (root == BROWSE_SENDTO) {
        dir = config_find_sendto_dir();
        snprintf(g_app.browse_title, sizeof(g_app.browse_title), "Send to PocketBook");
        if (!dir) {
            g_app.library_ready = 1;
            return 0;
        }
        scan_dir(dir, 0, 1);
    } else {
        /* This Era has no /mnt/ext1/Books. The shelf is /mnt/ext1/Library. */
        snprintf(g_app.browse_title, sizeof(g_app.browse_title), "Library");
        if (dir_exists(CFG_LIBRARY_DIR))
            scan_dir(CFG_LIBRARY_DIR, 0, 0);
        if (dir_exists(CFG_BOOKS_DIR))
            scan_dir(CFG_BOOKS_DIR, 0, 0);
    }

    if (g_app.book_count > 1)
        qsort(g_app.books, (size_t)g_app.book_count, sizeof(book_entry), book_title_cmp);
    g_app.library_ready = 1;
    return g_app.book_count;
}

void home_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int row_h = ui_row_h();
    int m = ui_margin();
    int top = ui_title_h();
    int y = top;
    const char *sendto = config_find_sendto_dir();
    (void)sh;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_msg)
        SetFont(g_app.font_msg, fg);
    DrawTextRect(0, 0, sw, ui_title_h(), "NaSkilla", ALIGN_CENTER | VALIGN_MIDDLE);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);

    if (g_app.cfg.last_book[0] || g_app.last_book[0]) {
        DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Continue reading",
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
        y += row_h;
    }

    DrawTextRect(m, y, sw - 2 * m, row_h - 8,
                 sendto ? "Send to PocketBook" : "Send to PocketBook (not found)",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    y += row_h;

    DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Library",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    y += row_h;

    DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Smart suggestion",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
}

int home_hit(int x, int y)
{
    int row_h = ui_row_h();
    int top = ui_title_h();
    int idx;
    int has_continue = (g_app.cfg.last_book[0] || g_app.last_book[0]) ? 1 : 0;
    (void)x;
    if (y < top)
        return 0;
    idx = (y - top) / row_h;
    if (!has_continue)
        idx += 1;

    if (has_continue && idx == 0) {
        const char *path = g_app.cfg.last_book[0] ? g_app.cfg.last_book : g_app.last_book;
        if (!path || !path[0]) {
            ui_set_message("No book to continue");
            ui_draw();
            return 1;
        }
        ui_open_book(path);
        return 1;
    }
    if (idx == 1) {
        if (!config_find_sendto_dir()) {
            ui_set_message("Send-to folder not found");
            ui_draw();
            return 1;
        }
        ui_show_browse(BROWSE_SENDTO);
        return 1;
    }
    if (idx == 2) {
        ui_show_browse(BROWSE_LIBRARY);
        return 1;
    }
    if (idx == 3) {
        g_app.screen = SCREEN_SUGGEST_ASK;
        ui_draw();
        return 1;
    }
    return 0;
}

static int library_visible(int sh, int top, int row_h, int *pager)
{
    int list_h = sh - top;
    int vis = list_h / row_h;
    if (vis < 1)
        vis = 1;
    /* A bottom Previous / Next bar only when the list is longer than one screen.
     * Taps on book rows were eating the old scroll zones, so paging never ran. */
    if (g_app.book_count > vis) {
        list_h = sh - top - ui_action_h();
        vis = list_h / row_h;
        if (vis < 1)
            vis = 1;
        if (pager)
            *pager = 1;
    } else if (pager) {
        *pager = 0;
    }
    return vis;
}

void library_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int m = ui_margin();
    int top = ui_title_h() + ui_row_h();
    int i, y;
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int visible;
    int pager = 0;
    int max_scroll;
    int shown;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 0, sw, ui_title_h(), g_app.browse_title,
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(m, ui_title_h(), sw / 2 - m, ui_row_h(), "Home",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(sw / 2, ui_title_h(), sw / 2 - m, ui_row_h(), "Refresh",
                 ALIGN_RIGHT | VALIGN_MIDDLE);

    if (g_app.book_count == 0) {
        DrawTextRect(20, sh / 2 - 20, sw - 40, 40,
                     "No books found", ALIGN_CENTER | VALIGN_MIDDLE);
        return;
    }

    visible = library_visible(sh, top, row_h, &pager);
    max_scroll = g_app.book_count - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (g_app.book_scroll < 0)
        g_app.book_scroll = 0;
    if (g_app.book_scroll > max_scroll)
        g_app.book_scroll = max_scroll;

    shown = 0;
    for (i = 0; i < visible; i++) {
        int idx = g_app.book_scroll + i;
        char line[384];
        if (idx >= g_app.book_count)
            break;
        y = top + i * row_h;
        if (g_app.books[idx].author[0])
            snprintf(line, sizeof(line), "%s — %s",
                     g_app.books[idx].title, g_app.books[idx].author);
        else
            snprintf(line, sizeof(line), "%s", g_app.books[idx].title);
        DrawTextRect(m, y, sw - 2 * m, row_h - 8, line,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
        shown++;
    }

    if (pager) {
        char range[64];
        int bar_y = sh - ui_action_h();
        snprintf(range, sizeof(range), "%d-%d / %d",
                 g_app.book_scroll + 1,
                 g_app.book_scroll + shown,
                 g_app.book_count);
        DrawTextRect(m, bar_y, sw / 3, ui_action_h(), "Previous",
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawTextRect(sw / 3, bar_y, sw / 3, ui_action_h(), range,
                     ALIGN_CENTER | VALIGN_MIDDLE);
        DrawTextRect(sw - sw / 3, bar_y, sw / 3 - m, ui_action_h(), "Next",
                     ALIGN_RIGHT | VALIGN_MIDDLE);
    }
}

int library_hit(int x, int y)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int top = ui_title_h() + ui_row_h();
    int nav_y0 = ui_title_h();
    int nav_y1 = top;
    int pager = 0;
    int visible = library_visible(sh, top, row_h, &pager);
    int max_scroll;
    int i;

    if (y >= nav_y0 && y < nav_y1) {
        if (x < sw / 2) {
            ui_show_home();
            return 1;
        }
        library_scan(g_app.browse_kind);
        ui_draw();
        return 1;
    }
    if (pager && y >= sh - ui_action_h()) {
        max_scroll = g_app.book_count - visible;
        if (max_scroll < 0)
            max_scroll = 0;
        if (x < sw / 3 && g_app.book_scroll > 0) {
            g_app.book_scroll -= visible;
            if (g_app.book_scroll < 0)
                g_app.book_scroll = 0;
            ui_draw();
        } else if (x >= sw - sw / 3 && g_app.book_scroll < max_scroll) {
            g_app.book_scroll += visible;
            if (g_app.book_scroll > max_scroll)
                g_app.book_scroll = max_scroll;
            ui_draw();
        }
        return 1;
    }
    if (y < top)
        return 0;
    for (i = 0; i < visible; i++) {
        int idx = g_app.book_scroll + i;
        int yy = top + i * row_h;
        if (idx >= g_app.book_count)
            break;
        if (y >= yy && y < yy + row_h) {
            ui_open_book(g_app.books[idx].path);
            return 1;
        }
    }
    return 0;
}
