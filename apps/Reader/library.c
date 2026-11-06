#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void add_book(const char *path)
{
    book_entry *e;
    bookinfo *info;
    if (g_app.book_count >= CFG_BOOKS_MAX)
        return;
    e = &g_app.books[g_app.book_count];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    info = GetBookInfo(path);
    if (info && info->title && info->title[0])
        snprintf(e->title, sizeof(e->title), "%s", info->title);
    else {
        const char *slash = strrchr(path, '/');
        snprintf(e->title, sizeof(e->title), "%s", slash ? slash + 1 : path);
    }
    if (info && info->author && info->author[0])
        snprintf(e->author, sizeof(e->author), "%s", info->author);
    else
        e->author[0] = '\0';
    g_app.book_count++;
}

static void scan_dir(const char *dir, int depth)
{
    DIR *d;
    struct dirent *ent;
    if (depth > 6 || g_app.book_count >= CFG_BOOKS_MAX)
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
            scan_dir(path, depth + 1);
        else if (S_ISREG(st.st_mode) && is_book_name(ent->d_name))
            add_book(path);
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
    } else {
        dir = CFG_BOOKS_DIR;
        snprintf(g_app.browse_title, sizeof(g_app.browse_title), "Library");
    }

    scan_dir(dir, 0);
    g_app.library_ready = 1;
    return g_app.book_count;
}

void home_draw(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int row_h = 88;
    int top = 80;
    int y = top;
    const char *sendto = config_find_sendto_dir();

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    SetFont(g_app.font_msg, fg);
    DrawTextRect(0, 16, sw, 48, "Reader", ALIGN_CENTER | VALIGN_MIDDLE);
    SetFont(g_app.font_ui, fg);

    if (g_app.cfg.last_book[0] || g_app.last_book[0]) {
        DrawTextRect(32, y, sw - 64, row_h - 8, "Continue reading",
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
        y += row_h;
    }

    DrawTextRect(32, y, sw - 64, row_h - 8,
                 sendto ? "Send to PocketBook" : "Send to PocketBook (not found)",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
    y += row_h;

    DrawTextRect(32, y, sw - 64, row_h - 8, "Library (Books)",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
    y += row_h;

    DrawTextRect(32, y, sw - 64, row_h - 8, "Smart suggestion",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
}

int home_hit(int x, int y)
{
    int sh = ScreenHeight();
    int row_h = 88;
    int top = 80;
    int idx;
    int has_continue = (g_app.cfg.last_book[0] || g_app.last_book[0]) ? 1 : 0;
    (void)x;
    (void)sh;
    if (y < top)
        return 0;
    idx = (y - top) / row_h;
    if (!has_continue)
        idx += 1;

    if (has_continue && idx == 0) {
        const char *path = g_app.cfg.last_book[0] ? g_app.cfg.last_book : g_app.last_book;
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

void library_draw(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int row_h = 72;
    int top = 96;
    int i, y;
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int visible;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 8, sw, 36, g_app.browse_title,
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(16, 48, sw / 2 - 24, 40, "Home", ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(sw / 2, 48, sw / 2 - 16, 40, "Refresh",
                 ALIGN_RIGHT | VALIGN_MIDDLE);

    if (g_app.book_count == 0) {
        DrawTextRect(20, sh / 2 - 20, sw - 40, 40,
                     "No books found", ALIGN_CENTER | VALIGN_MIDDLE);
        return;
    }

    visible = (sh - top) / row_h;
    if (g_app.book_scroll < 0)
        g_app.book_scroll = 0;
    if (g_app.book_scroll > g_app.book_count - 1)
        g_app.book_scroll = g_app.book_count - 1;

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
        DrawTextRect(24, y, sw - 48, row_h - 8, line,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(24, y + row_h - 1, sw - 24, y + row_h - 1, fg);
    }
}

int library_hit(int x, int y)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int row_h = 72;
    int top = 96;
    int visible = (sh - top) / row_h;
    int i;

    if (y >= 48 && y < 96) {
        if (x < sw / 2) {
            ui_show_home();
            return 1;
        }
        library_scan(g_app.browse_kind);
        ui_draw();
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
    if (y > sh * 2 / 3 && g_app.book_scroll + visible < g_app.book_count) {
        g_app.book_scroll += visible > 1 ? visible - 1 : 1;
        ui_draw();
        return 1;
    }
    if (y < sh / 3 && g_app.book_scroll > 0) {
        g_app.book_scroll -= visible > 1 ? visible - 1 : 1;
        if (g_app.book_scroll < 0)
            g_app.book_scroll = 0;
        ui_draw();
        return 1;
    }
    return 0;
}
