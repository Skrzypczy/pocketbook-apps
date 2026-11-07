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

static void copy_trim(const char *src, char *dst, size_t n)
{
    size_t b = 0;
    size_t e;
    if (!dst || n == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;
    while (src[b] == ' ' || src[b] == '\t')
        b++;
    e = strlen(src + b);
    while (e > 0 && (src[b + e - 1] == ' ' || src[b + e - 1] == '\t'))
        e--;
    if (e >= n)
        e = n - 1;
    memcpy(dst, src + b, e);
    dst[e] = '\0';
}

static void strip_calibre_id(char *s)
{
    size_t n;
    char *op;
    char *p;
    if (!s)
        return;
    n = strlen(s);
    if (n < 4 || s[n - 1] != ')')
        return;
    op = strrchr(s, '(');
    if (!op || op == s || op[-1] != ' ')
        return;
    for (p = op + 1; p < s + n - 1; p++) {
        if (*p < '0' || *p > '9')
            return;
    }
    if (p == op + 1)
        return;
    op[-1] = '\0';
}

static int generic_dir(const char *name)
{
    static const char *skip[] = {
        "Library", "_Categories", "Books", "SendToPb", "SendToPocketBook",
        "Send to PocketBook", "system", "applications", NULL
    };
    int i;
    if (!name || !name[0] || name[0] == '_' || name[0] == '.')
        return 1;
    for (i = 0; skip[i]; i++) {
        if (strcasecmp(name, skip[i]) == 0)
            return 1;
    }
    return 0;
}

static int looks_like_hash(const char *s)
{
    size_t n;
    size_t i;
    int digit = 0;
    if (!s)
        return 0;
    n = strlen(s);
    if (n < 8 || n > 48)
        return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9')
            digit = 1;
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-'))
            return 0;
    }
    return digit;
}

static int small_number(const char *s, int *out)
{
    int v = 0;
    const char *p;
    if (!s || !s[0])
        return 0;
    for (p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10 + (*p - '0');
        if (v > 200)
            return 0;
    }
    if (v < 1)
        return 0;
    if (out)
        *out = v;
    return 1;
}

/* Pull a volume number off the end of a name. "James Bond 01" and
 * "Casino Royale (James Bond #1)" both count. "Catch 22" does not. */
static int strip_volume(char *s, char *series, size_t series_n, int *index)
{
    char *op;
    char *sp;
    char inner[160];
    int words;
    int idx = 0;
    size_t n;
    if (!s || !s[0])
        return 0;
    n = strlen(s);
    if (n > 3 && s[n - 1] == ')') {
        op = strrchr(s, '(');
        if (op && op > s) {
            size_t ilen = (size_t)((s + n - 1) - (op + 1));
            if (ilen > 0 && ilen < sizeof(inner)) {
                memcpy(inner, op + 1, ilen);
                inner[ilen] = '\0';
                if (small_number(inner, &idx) || strip_volume(inner, series, series_n, &idx)) {
                    if (series && series_n && inner[0] && !small_number(inner, NULL))
                        copy_trim(inner, series, series_n);
                    if (op > s && op[-1] == ' ')
                        op--;
                    *op = '\0';
                    if (index && idx)
                        *index = idx;
                    return idx > 0;
                }
            }
        }
    }
    sp = strrchr(s, '#');
    if (sp && sp > s && small_number(sp + 1, &idx)) {
        char *cut = sp;
        while (cut > s && (cut[-1] == ' ' || cut[-1] == '('))
            cut--;
        *cut = '\0';
        if (series && series_n && s[0])
            copy_trim(s, series, series_n);
        if (index)
            *index = idx;
        s[0] = '\0';
        if (series && series_n && series[0])
            copy_trim(series, s, strlen(series) + 1);
        return 1;
    }
    sp = strrchr(s, ' ');
    if (!sp || !small_number(sp + 1, &idx))
        return 0;
    words = 1;
    {
        char *p;
        for (p = s; p < sp; p++) {
            if (*p == ' ')
                words++;
        }
    }
    /* "01" / "02" always, or a number after at least two words ("James Bond 1"). */
    if (sp[1] != '0' && words < 2)
        return 0;
    *sp = '\0';
    if (series && series_n)
        copy_trim(s, series, series_n);
    if (index)
        *index = idx;
    return 1;
}

static int split_dash(char *s, char parts[][160], int max)
{
    int n = 0;
    char *p = s;
    while (p && n < max) {
        char *d = strstr(p, " - ");
        char piece[160];
        if (!d) {
            copy_trim(p, parts[n], 160);
            if (parts[n][0])
                n++;
            break;
        }
        if ((size_t)(d - p) >= sizeof(piece))
            memcpy(piece, p, sizeof(piece) - 1), piece[sizeof(piece) - 1] = '\0';
        else {
            memcpy(piece, p, (size_t)(d - p));
            piece[d - p] = '\0';
        }
        copy_trim(piece, parts[n], 160);
        if (parts[n][0])
            n++;
        p = d + 3;
    }
    return n;
}

static void dir_name_at(const char *path, int up, char *out, size_t outlen)
{
    const char *slash;
    const char *start;
    int i;
    size_t n;
    if (!out || outlen == 0)
        return;
    out[0] = '\0';
    slash = path ? strrchr(path, '/') : NULL;
    if (!slash)
        return;
    for (i = 0; i <= up; i++) {
        if (slash <= path)
            return;
        start = slash;
        while (start > path && start[-1] != '/')
            start--;
        if (i == up) {
            n = (size_t)(slash - start);
            if (n >= outlen)
                n = outlen - 1;
            memcpy(out, start, n);
            out[n] = '\0';
            strip_calibre_id(out);
            return;
        }
        if (start == path)
            return;
        slash = start - 1;
    }
}

void book_names_from_path(const char *path,
                          char *title, size_t title_n,
                          char *author, size_t author_n,
                          char *series, size_t series_n,
                          int *index)
{
    char base[256];
    char work[256];
    char parent[160];
    char grand[160];
    char parts[4][160];
    const char *slash;
    const char *dot;
    int nparts;
    int vol = 0;
    char series_tmp[160];
    if (title && title_n)
        title[0] = '\0';
    if (author && author_n)
        author[0] = '\0';
    if (series && series_n)
        series[0] = '\0';
    if (index)
        *index = 0;
    if (!path || !path[0])
        return;
    slash = strrchr(path, '/');
    slash = slash ? slash + 1 : path;
    dot = strrchr(slash, '.');
    if (dot && strcasecmp(dot, ".zip") == 0) {
        const char *dot2 = slash;
        const char *prev = NULL;
        while (dot2 < dot) {
            const char *f = strchr(dot2, '.');
            if (!f || f >= dot)
                break;
            prev = f;
            dot2 = f + 1;
        }
        if (prev)
            dot = prev;
    }
    {
        size_t n = dot && dot > slash ? (size_t)(dot - slash) : strlen(slash);
        if (n >= sizeof(base))
            n = sizeof(base) - 1;
        memcpy(base, slash, n);
        base[n] = '\0';
    }
    strip_calibre_id(base);
    dir_name_at(path, 0, parent, sizeof(parent));
    dir_name_at(path, 1, grand, sizeof(grand));

    if (looks_like_hash(base)) {
        if (title && title_n && parent[0] && !generic_dir(parent))
            copy_trim(parent, title, title_n);
        if (author && author_n && grand[0] && !generic_dir(grand))
            copy_trim(grand, author, author_n);
        return;
    }

    snprintf(work, sizeof(work), "%s", base);
    series_tmp[0] = '\0';
    nparts = split_dash(work, parts, 4);
    if (nparts >= 3 && (small_number(parts[1], &vol) ||
                        strip_volume(parts[1], series_tmp, sizeof(series_tmp), &vol))) {
        /* Author - 01 - Title   or   Author - Series 01 - Title */
        if (author && author_n)
            copy_trim(parts[0], author, author_n);
        if (title && title_n)
            copy_trim(parts[2], title, title_n);
        if (series && series_n) {
            if (series_tmp[0])
                copy_trim(series_tmp, series, series_n);
            else
                copy_trim(parts[0], series, series_n);
        }
        if (index)
            *index = vol;
    } else if (nparts >= 2 && small_number(parts[0], &vol)) {
        /* 01 - Title */
        if (title && title_n)
            copy_trim(parts[1], title, title_n);
        if (index)
            *index = vol;
    } else if (nparts >= 2 &&
               strip_volume(parts[0], series_tmp, sizeof(series_tmp), &vol) && vol) {
        /* Series 01 - Title */
        if (title && title_n)
            copy_trim(parts[1], title, title_n);
        if (series && series_n && series_tmp[0])
            copy_trim(series_tmp, series, series_n);
        if (index)
            *index = vol;
    } else if (nparts >= 2) {
        /* Title - Author. A parent folder that matches one side wins. */
        int left_author = parent[0] && strcasecmp(parent, parts[0]) == 0;
        int right_author = parent[0] && strcasecmp(parent, parts[1]) == 0;
        char left_series[160];
        int left_vol = 0;
        left_series[0] = '\0';
        snprintf(work, sizeof(work), "%s", parts[0]);
        if (strip_volume(work, left_series, sizeof(left_series), &left_vol) && left_vol) {
            if (title && title_n)
                copy_trim(work[0] ? work : parts[0], title, title_n);
            if (series && series_n && left_series[0])
                copy_trim(left_series, series, series_n);
            if (index)
                *index = left_vol;
            if (author && author_n)
                copy_trim(parts[1], author, author_n);
        } else if (left_author && !right_author) {
            if (author && author_n)
                copy_trim(parts[0], author, author_n);
            if (title && title_n)
                copy_trim(parts[1], title, title_n);
        } else {
            if (title && title_n)
                copy_trim(parts[0], title, title_n);
            if (author && author_n)
                copy_trim(parts[1], author, author_n);
        }
    } else {
        snprintf(work, sizeof(work), "%s", nparts == 1 ? parts[0] : base);
        if (strip_volume(work, series_tmp, sizeof(series_tmp), &vol) && vol) {
            if (title && title_n)
                copy_trim(work[0] ? work : base, title, title_n);
            if (series && series_n && series_tmp[0])
                copy_trim(series_tmp, series, series_n);
            if (index)
                *index = vol;
        } else if (title && title_n) {
            copy_trim(base, title, title_n);
        }
    }

    if (author && author_n && !author[0] && grand[0] && !generic_dir(grand) &&
        parent[0] && title && title[0] && strcasecmp(parent, title) == 0)
        copy_trim(grand, author, author_n);
    else if (author && author_n && !author[0] && parent[0] && !generic_dir(parent) &&
             (!title || !title[0] || strcasecmp(parent, title) != 0))
        copy_trim(parent, author, author_n);
}

static void filename_label(const char *path, char *title, size_t title_n)
{
    const char *slash = strrchr(path ? path : "", '/');
    const char *base = slash ? slash + 1 : (path ? path : "");
    const char *dot = strrchr(base, '.');
    size_t n;
    if (dot && strcasecmp(dot, ".zip") == 0) {
        const char *earlier = base;
        const char *p;
        while ((p = strchr(earlier, '.')) != NULL && p < dot) {
            earlier = p + 1;
            if (p > base)
                dot = p;
        }
    }
    n = (dot && dot > base) ? (size_t)(dot - base) : strlen(base);
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
    /* The list shows the name on disk. Parsing it into title and author
     * was hiding the filename the user set. */
    filename_label(path, e->title, sizeof(e->title));
    if (looks_like_hash(e->title)) {
        char parsed[160];
        char author[80];
        parsed[0] = author[0] = '\0';
        if (fetch_info)
            info = GetBookInfo(path);
        if (info && info->title && info->title[0])
            snprintf(e->title, sizeof(e->title), "%s", info->title);
        if (info && info->author && info->author[0])
            snprintf(e->author, sizeof(e->author), "%s", info->author);
        if (looks_like_hash(e->title) || !e->title[0]) {
            book_names_from_path(path, parsed, sizeof(parsed), author, sizeof(author),
                                 NULL, 0, NULL);
            if (parsed[0] && !looks_like_hash(parsed))
                snprintf(e->title, sizeof(e->title), "%s", parsed);
            if (!e->author[0] && author[0])
                snprintf(e->author, sizeof(e->author), "%s", author);
        }
    }
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

static void add_recent_book(const char *path)
{
    book_entry *e;
    bookinfo *info;
    FILE *fp;
    if (!path || !path[0] || g_app.book_count >= CFG_BOOKS_MAX)
        return;
    fp = fopen(path, "rb");
    if (!fp)
        return;
    fclose(fp);
    e = &g_app.books[g_app.book_count];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    info = GetBookInfo(path);
    if (info && info->title && info->title[0])
        snprintf(e->title, sizeof(e->title), "%s", info->title);
    if (info && info->author && info->author[0])
        snprintf(e->author, sizeof(e->author), "%s", info->author);
    if (!e->title[0] || !e->author[0]) {
        char title[160];
        char author[80];
        book_names_from_path(path, title, sizeof(title), author, sizeof(author),
                             NULL, 0, NULL);
        if (!e->title[0] && title[0] && !looks_like_hash(title))
            snprintf(e->title, sizeof(e->title), "%s", title);
        if (!e->author[0] && author[0])
            snprintf(e->author, sizeof(e->author), "%s", author);
    }
    if (!e->title[0])
        snprintf(e->title, sizeof(e->title), "Untitled");
    g_app.book_count++;
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

    if (root == BROWSE_RECENT) {
        int i;
        snprintf(g_app.browse_title, sizeof(g_app.browse_title), "Recent");
        for (i = 0; i < g_app.cfg.recent_count && i < CFG_RECENT_MAX; i++)
            add_recent_book(g_app.cfg.recent[i]);
    } else if (root == BROWSE_SENDTO) {
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

    if (root != BROWSE_RECENT && g_app.book_count > 1)
        qsort(g_app.books, (size_t)g_app.book_count, sizeof(book_entry), book_title_cmp);
    g_app.library_ready = 1;
    return g_app.book_count;
}

enum {
    HOME_CONTINUE = 1,
    HOME_SEND,
    HOME_LIBRARY,
    HOME_SUGGEST,
    HOME_RECENT
};

static int home_build_rows(int *rows, int cap)
{
    int n = 0;
    if (n < cap && (g_app.cfg.last_book[0] || g_app.last_book[0]))
        rows[n++] = HOME_CONTINUE;
    if (n < cap)
        rows[n++] = HOME_SEND;
    if (n < cap)
        rows[n++] = HOME_LIBRARY;
    if (n < cap)
        rows[n++] = HOME_RECENT;
    if (n < cap)
        rows[n++] = HOME_SUGGEST;
    return n;
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
    int rows[8];
    int nrows = home_build_rows(rows, 8);
    int i;
    const char *sendto = config_find_sendto_dir();

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_msg)
        SetFont(g_app.font_msg, fg);
    DrawTextRect(0, 0, sw, ui_title_h(), "NaSkilla", ALIGN_CENTER | VALIGN_MIDDLE);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);

    for (i = 0; i < nrows; i++) {
        int y = top + i * row_h;
        const char *text = "";
        if (y + row_h > sh)
            break;
        if (rows[i] == HOME_CONTINUE)
            text = "Continue reading";
        else if (rows[i] == HOME_SEND)
            text = sendto ? "Send to PocketBook" : "Send to PocketBook (not found)";
        else if (rows[i] == HOME_LIBRARY)
            text = "Library";
        else if (rows[i] == HOME_RECENT)
            text = "Recent";
        else if (rows[i] == HOME_SUGGEST)
            text = "Smart suggestion";
        DrawTextRect(m, y, sw - 2 * m, row_h - 8, text, ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    }
}

int home_hit(int x, int y)
{
    int row_h = ui_row_h();
    int top = ui_title_h();
    int rows[8];
    int nrows = home_build_rows(rows, 8);
    int idx;
    (void)x;
    if (y < top)
        return 0;
    idx = (y - top) / row_h;
    if (idx < 0 || idx >= nrows)
        return 0;
    if (rows[idx] == HOME_CONTINUE) {
        const char *path = g_app.cfg.last_book[0] ? g_app.cfg.last_book : g_app.last_book;
        if (!path || !path[0]) {
            ui_set_message("No book to continue");
            ui_draw();
            return 1;
        }
        ui_open_book(path);
        return 1;
    }
    if (rows[idx] == HOME_SEND) {
        if (!config_find_sendto_dir()) {
            ui_set_message("Send-to folder not found");
            ui_draw();
            return 1;
        }
        ui_show_browse(BROWSE_SENDTO);
        return 1;
    }
    if (rows[idx] == HOME_LIBRARY) {
        ui_show_browse(BROWSE_LIBRARY);
        return 1;
    }
    if (rows[idx] == HOME_RECENT) {
        ui_show_browse(BROWSE_RECENT);
        return 1;
    }
    if (rows[idx] == HOME_SUGGEST) {
        g_app.screen = SCREEN_SUGGEST_ASK;
        ui_draw();
        return 1;
    }
    return 1;
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
                     g_app.browse_kind == BROWSE_RECENT ? "No recent books"
                                                        : "No books found",
                     ALIGN_CENTER | VALIGN_MIDDLE);
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
