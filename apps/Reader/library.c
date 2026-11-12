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

/* True when name ends with " (digits)", the Calibre Title (id) folder. */
static int is_calibre_title_dir(const char *name)
{
    const char *op;
    const char *p;
    size_t n;
    if (!name || !name[0])
        return 0;
    n = strlen(name);
    if (n < 4 || name[n - 1] != ')')
        return 0;
    op = strrchr(name, '(');
    if (!op || op <= name || op[-1] != ' ')
        return 0;
    if (op + 1 >= name + n - 1)
        return 0;
    for (p = op + 1; p < name + n - 1; p++) {
        if (*p < '0' || *p > '9')
            return 0;
    }
    return 1;
}

void folder_author_from_path(const char *path, char *out, size_t out_n)
{
    char parent[160];
    char grand[160];
    char raw_parent[160];
    const char *slash;
    const char *start;
    size_t n;
    if (!out || out_n == 0)
        return;
    out[0] = '\0';
    if (!path || !path[0])
        return;
    dir_name_at(path, 0, parent, sizeof(parent));
    dir_name_at(path, 1, grand, sizeof(grand));
    /* Raw parent (with Calibre id) to detect Author/Title (id)/file. */
    raw_parent[0] = '\0';
    slash = strrchr(path, '/');
    if (slash && slash > path) {
        start = slash;
        while (start > path && start[-1] != '/')
            start--;
        n = (size_t)(slash - start);
        if (n >= sizeof(raw_parent))
            n = sizeof(raw_parent) - 1;
        memcpy(raw_parent, start, n);
        raw_parent[n] = '\0';
    }
    if (is_calibre_title_dir(raw_parent) && grand[0] && !generic_dir(grand)) {
        copy_trim(grand, out, out_n);
        return;
    }
    if (parent[0] && !generic_dir(parent))
        copy_trim(parent, out, out_n);
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

static void basename_no_ext(const char *path, char *out, size_t out_n)
{
    const char *slash;
    const char *base;
    const char *dot;
    size_t n;
    if (!out || out_n == 0)
        return;
    out[0] = '\0';
    if (!path || !path[0])
        return;
    slash = strrchr(path, '/');
    base = slash ? slash + 1 : path;
    dot = strrchr(base, '.');
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
    if (n >= out_n)
        n = out_n - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

/* Fill out from a Calibre metadata.opf sitting next to the book. */
static void apply_sidecar_opf(const char *path,
                              char *title, size_t title_n,
                              char *author, size_t author_n,
                              char *series, size_t series_n,
                              int *index, int *year)
{
    char opf_path[768];
    const char *slash;
    FILE *fp;
    char *buf;
    long sz;
    size_t nread;
    const char *p;

    if (!path || !path[0])
        return;
    slash = strrchr(path, '/');
    if (!slash)
        return;
    if ((size_t)(slash - path) + 1 + strlen("metadata.opf") >= sizeof(opf_path))
        return;
    snprintf(opf_path, sizeof(opf_path), "%.*smetadata.opf",
             (int)(slash - path + 1), path);
    fp = fopen(opf_path, "rb");
    if (!fp)
        return;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return;
    }
    sz = ftell(fp);
    if (sz <= 0 || sz > 256 * 1024) {
        fclose(fp);
        return;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return;
    }
    nread = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[nread] = '\0';

    /* <dc:title>...</dc:title> and the same for creator / date. */
    if (title && title_n && !title[0]) {
        p = strstr(buf, "<dc:title");
        if (!p)
            p = strstr(buf, "<title");
        if (p) {
            p = strchr(p, '>');
            if (p && p[1]) {
                const char *end = strchr(p + 1, '<');
                if (end && end > p + 1) {
                    size_t n = (size_t)(end - (p + 1));
                    if (n >= title_n)
                        n = title_n - 1;
                    memcpy(title, p + 1, n);
                    title[n] = '\0';
                }
            }
        }
    }
    if (author && author_n && !author[0]) {
        p = strstr(buf, "<dc:creator");
        if (!p)
            p = strstr(buf, "<creator");
        if (p) {
            p = strchr(p, '>');
            if (p && p[1]) {
                const char *end = strchr(p + 1, '<');
                if (end && end > p + 1) {
                    size_t n = (size_t)(end - (p + 1));
                    if (n >= author_n)
                        n = author_n - 1;
                    memcpy(author, p + 1, n);
                    author[n] = '\0';
                }
            }
        }
    }
    if (year && *year <= 0) {
        p = strstr(buf, "<dc:date");
        if (!p)
            p = strstr(buf, "<date");
        if (p) {
            p = strchr(p, '>');
            if (p && p[1] >= '0' && p[1] <= '9') {
                int y = 0;
                const char *q = p + 1;
                while (*q >= '0' && *q <= '9' && y < 100000)
                    y = y * 10 + (*q++ - '0');
                if (y >= 1000 && y <= 2100)
                    *year = y;
            }
        }
    }

    /* Calibre <meta name="calibre:series" content="..."/> */
    p = buf;
    while ((p = strstr(p, "<meta")) != NULL) {
        const char *tag_end = strchr(p, '>');
        char name[80];
        char content[160];
        const char *np;
        const char *cp;
        const char *nq;
        const char *cq;
        name[0] = content[0] = '\0';
        if (!tag_end)
            break;
        np = strstr(p, "name=\"");
        if (!np || np > tag_end)
            np = strstr(p, "name='");
        if (np && np < tag_end) {
            char quote = np[5];
            np += 6;
            nq = strchr(np, quote);
            if (nq && nq < tag_end) {
                size_t n = (size_t)(nq - np);
                if (n >= sizeof(name))
                    n = sizeof(name) - 1;
                memcpy(name, np, n);
                name[n] = '\0';
            }
        }
        cp = strstr(p, "content=\"");
        if (!cp || cp > tag_end)
            cp = strstr(p, "content='");
        if (cp && cp < tag_end) {
            char quote = cp[8];
            cp += 9;
            cq = strchr(cp, quote);
            if (cq && cq < tag_end) {
                size_t n = (size_t)(cq - cp);
                if (n >= sizeof(content))
                    n = sizeof(content) - 1;
                memcpy(content, cp, n);
                content[n] = '\0';
            }
        }
        if (name[0] && content[0]) {
            if ((strcasecmp(name, "calibre:series") == 0 ||
                 strcasecmp(name, "series") == 0) &&
                series && series_n && !series[0])
                snprintf(series, series_n, "%s", content);
            if ((strcasecmp(name, "calibre:series_index") == 0 ||
                 strcasecmp(name, "series_index") == 0) &&
                index && *index <= 0) {
                int v = (int)strtod(content, NULL);
                if (v > 0 && v <= 2000)
                    *index = v;
            }
        }
        p = tag_end + 1;
    }
    free(buf);
}

/* Only keep metadata for books on the recent list (or the open book). */
static int book_meta_want_store(const char *path)
{
    int i;
    if (!path || !path[0])
        return 0;
    if (g_app.open_path[0] && strcmp(g_app.open_path, path) == 0)
        return 1;
    if (g_app.cfg.last_book[0] && strcmp(g_app.cfg.last_book, path) == 0)
        return 1;
    for (i = 0; i < g_app.cfg.recent_count && i < CFG_RECENT_MAX; i++) {
        if (g_app.cfg.recent[i][0] && strcmp(g_app.cfg.recent[i], path) == 0)
            return 1;
    }
    return 0;
}

void book_info_from_path(const char *path,
                         char *title, size_t title_n,
                         char *author, size_t author_n,
                         char *series, size_t series_n,
                         int *index,
                         int *year)
{
    char base[256];
    bookinfo *info;
    book_meta cached;
    int is_hash;
    int need;
    if (year)
        *year = 0;
    book_names_from_path(path, title, title_n, author, author_n,
                         series, series_n, index);
    apply_sidecar_opf(path, title, title_n, author, author_n,
                      series, series_n, index, year);
    basename_no_ext(path, base, sizeof(base));
    is_hash = looks_like_hash(base);
    /* Sidecar already filled Calibre fields. GetBookInfo is only for a
     * hash or a still-empty title/author — never for every library file. */
    need = is_hash ||
           (title && title_n && !title[0]) ||
           (author && author_n && !author[0]);
    if (!need || !path || !path[0])
        return;

    /* Recent-sized disk cache: skip GetBookInfo when we already resolved
     * this path (same cap as the recent list). */
    if (book_meta_get(path, &cached)) {
        if (title && title_n && cached.title[0] &&
            (is_hash || !title[0] || looks_like_hash(title)))
            snprintf(title, title_n, "%s", cached.title);
        if (author && author_n && cached.author[0] &&
            (is_hash || !author[0]))
            snprintf(author, author_n, "%s", cached.author);
        if (series && series_n && cached.series[0] &&
            (is_hash || !series[0]))
            snprintf(series, series_n, "%s", cached.series);
        if (index && cached.index > 0 && (is_hash || *index <= 0))
            *index = cached.index;
        if (year && cached.year >= 1000 && *year <= 0)
            *year = cached.year;
        return;
    }

    info = GetBookInfo(path);
    if (!info)
        return;
    /* A hash often gets a junk folder author (for example "ext1"). Prefer
     * the metadata whenever the file name itself is a hash. */
    if (title && title_n && info->title && info->title[0] &&
        (is_hash || !title[0] || looks_like_hash(title)))
        snprintf(title, title_n, "%s", info->title);
    if (author && author_n && info->author && info->author[0] &&
        (is_hash || !author[0]))
        snprintf(author, author_n, "%s", info->author);
    if (series && series_n && info->series && info->series[0] &&
        (is_hash || !series[0]))
        snprintf(series, series_n, "%s", info->series);
    if (index && info->numinseries > 0 && (is_hash || *index <= 0))
        *index = info->numinseries;
    if (year && info->year >= 1000 && info->year <= 2100 && *year <= 0)
        *year = info->year;
    if (book_meta_want_store(path)) {
        book_meta_put(path,
                      title ? title : "",
                      author ? author : "",
                      series ? series : "",
                      index ? *index : 0,
                      year ? *year : 0);
    }
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
    DrawTextRect(0, 0, sw, ui_title_h(),
                 g_app.books_from_options ? "Books" : CFG_APP_NAME,
                 ALIGN_CENTER | VALIGN_MIDDLE);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);

    for (i = 0; i < nrows; i++) {
        int y = top + i * row_h;
        const char *text = "";
        if (y + row_h > sh - ui_action_h())
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
    DrawTextRect(m, sh - ui_action_h(), sw - 2 * m, ui_action_h(), "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

int home_hit(int x, int y)
{
    int sh = ui_sh();
    int row_h = ui_row_h();
    int top = ui_title_h();
    int rows[8];
    int nrows = home_build_rows(rows, 8);
    int idx;
    (void)x;
    if (y >= sh - ui_action_h()) {
        if (g_app.books_from_options) {
            g_app.books_from_options = 0;
            g_app.screen = SCREEN_SETTINGS;
            ui_draw();
        } else if (g_app.open_path[0] && engine_active()) {
            g_app.screen = SCREEN_READING;
            ui_draw();
        } else {
            CloseApp();
        }
        return 1;
    }
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

static int library_row_match(int idx)
{
    const char *f = g_app.library_filter;
    char hay[384];
    char needle[96];
    int i;
    if (idx < 0 || idx >= g_app.book_count)
        return 0;
    if (!f || !f[0] || g_app.browse_kind == BROWSE_RECENT)
        return 1;
    snprintf(hay, sizeof(hay), "%s %s", g_app.books[idx].title, g_app.books[idx].author);
    snprintf(needle, sizeof(needle), "%s", f);
    for (i = 0; hay[i]; i++)
        if (hay[i] >= 'A' && hay[i] <= 'Z')
            hay[i] = (char)(hay[i] - 'A' + 'a');
    for (i = 0; needle[i]; i++)
        if (needle[i] >= 'A' && needle[i] <= 'Z')
            needle[i] = (char)(needle[i] - 'A' + 'a');
    return strstr(hay, needle) != NULL;
}

static int library_match_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_app.book_count; i++)
        if (library_row_match(i))
            n++;
    return n;
}

static int library_index_at_match(int match_ord)
{
    int i, n = 0;
    for (i = 0; i < g_app.book_count; i++) {
        if (!library_row_match(i))
            continue;
        if (n == match_ord)
            return i;
        n++;
    }
    return -1;
}

static void library_find_keyboard(char *text)
{
    if (text)
        snprintf(g_app.library_filter, sizeof(g_app.library_filter), "%s", text);
    else
        g_app.library_filter[0] = '\0';
    g_app.book_scroll = 0;
    ui_draw();
}

static int library_visible(int sh, int top, int row_h, int *pager)
{
    int list_h = sh - top - ui_action_h();
    int vis = list_h / row_h;
    if (vis < 1)
        vis = 1;
    if (library_match_count() > vis) {
        list_h = sh - top - 2 * ui_action_h();
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
    DrawTextRect(m, ui_title_h(), sw / 3 - m, ui_row_h(), "Back",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(sw / 3, ui_title_h(), sw / 3, ui_row_h(),
                 g_app.browse_kind == BROWSE_RECENT ? "Refresh" : "Find",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(sw * 2 / 3, ui_title_h(), sw / 3 - m, ui_row_h(), "Refresh",
                 ALIGN_RIGHT | VALIGN_MIDDLE);

    if (g_app.book_count == 0) {
        DrawTextRect(20, sh / 2 - 20, sw - 40, 40,
                     g_app.browse_kind == BROWSE_RECENT ? "No recent books"
                                                        : "No books found",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        DrawTextRect(m, sh - ui_action_h(), sw - 2 * m, ui_action_h(), "Back",
                     ALIGN_CENTER | VALIGN_MIDDLE);
        return;
    }

    visible = library_visible(sh, top, row_h, &pager);
    max_scroll = library_match_count() - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (g_app.book_scroll < 0)
        g_app.book_scroll = 0;
    if (g_app.book_scroll > max_scroll)
        g_app.book_scroll = max_scroll;

    shown = 0;
    for (i = 0; i < visible; i++) {
        int idx = library_index_at_match(g_app.book_scroll + i);
        char line[384];
        int page = 0, pages = 0, pct = -1;
        if (idx < 0)
            break;
        y = top + shown * row_h;
        if (config_get_progress(g_app.books[idx].path, &page, &pages) == 0 && pages > 0)
            pct = (page * 100) / pages;
        if (g_app.books[idx].author[0] && pct >= 0)
            snprintf(line, sizeof(line), "%d%%  %s — %s",
                     pct, g_app.books[idx].title, g_app.books[idx].author);
        else if (pct >= 0)
            snprintf(line, sizeof(line), "%d%%  %s", pct, g_app.books[idx].title);
        else if (g_app.books[idx].author[0])
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
        int back_y = sh - 2 * ui_action_h();
        int total = library_match_count();
        snprintf(range, sizeof(range), "%d-%d / %d",
                 g_app.book_scroll + 1,
                 g_app.book_scroll + shown,
                 total);
        DrawTextRect(m, bar_y, sw / 3, ui_action_h(), "Previous",
                     ALIGN_LEFT | VALIGN_MIDDLE);
        DrawTextRect(sw / 3, bar_y, sw / 3, ui_action_h(), range,
                     ALIGN_CENTER | VALIGN_MIDDLE);
        DrawTextRect(sw - sw / 3, bar_y, sw / 3 - m, ui_action_h(), "Next",
                     ALIGN_RIGHT | VALIGN_MIDDLE);
        DrawTextRect(m, back_y, sw - 2 * m, ui_action_h(), "Back",
                     ALIGN_CENTER | VALIGN_MIDDLE);
    } else {
        DrawTextRect(m, sh - ui_action_h(), sw - 2 * m, ui_action_h(), "Back",
                     ALIGN_CENTER | VALIGN_MIDDLE);
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
        if (x < sw / 3) {
            ui_show_home();
            return 1;
        }
        if (x < sw * 2 / 3 && g_app.browse_kind != BROWSE_RECENT) {
            OpenKeyboard("Find in library", g_app.library_filter,
                         (int)sizeof(g_app.library_filter) - 1, KBD_NORMAL,
                         library_find_keyboard);
            return 1;
        }
        g_app.library_filter[0] = '\0';
        library_scan(g_app.browse_kind);
        ui_draw();
        return 1;
    }
    if (pager && y >= sh - ui_action_h()) {
        max_scroll = library_match_count() - visible;
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
    if (y >= sh - (pager ? 2 : 1) * ui_action_h()) {
        ui_show_home();
        return 1;
    }
    if (y < top)
        return 0;
    for (i = 0; i < visible; i++) {
        int idx = library_index_at_match(g_app.book_scroll + i);
        int yy = top + i * row_h;
        if (idx < 0)
            break;
        if (y >= yy && y < yy + row_h) {
            ui_open_book(g_app.books[idx].path);
            return 1;
        }
    }
    return 0;
}

void ui_open_next_in_series(void)
{
    char series[120], title[160], author[80];
    char best_path[512];
    int index = 0, year = 0;
    int i;
    int want;
    int best_index = 999999;

    best_path[0] = '\0';
    if (!g_app.open_path[0]) {
        ui_set_message("No book open");
        ui_draw();
        return;
    }
    book_info_from_path(g_app.open_path, title, sizeof(title), author, sizeof(author),
                        series, sizeof(series), &index, &year);
    if (!series[0] || index <= 0) {
        ui_set_message("No series info for this book");
        g_app.screen = SCREEN_SETTINGS;
        ui_draw();
        return;
    }
    want = index + 1;
    /* Prefer an already-scanned library list; otherwise scan Library. */
    if (g_app.book_count <= 0 || g_app.browse_kind != BROWSE_LIBRARY)
        library_scan(BROWSE_LIBRARY);
    for (i = 0; i < g_app.book_count; i++) {
        char s2[120], t2[160], a2[80];
        int idx2 = 0, y2 = 0;
        book_info_from_path(g_app.books[i].path, t2, sizeof(t2), a2, sizeof(a2),
                            s2, sizeof(s2), &idx2, &y2);
        if (!s2[0] || idx2 < want)
            continue;
        if (strcasecmp(s2, series) != 0)
            continue;
        if (idx2 < best_index) {
            best_index = idx2;
            snprintf(best_path, sizeof(best_path), "%s", g_app.books[i].path);
        }
    }
    if (!best_path[0]) {
        ui_set_message("Next volume not found");
        g_app.screen = SCREEN_SETTINGS;
        ui_draw();
        return;
    }
    ui_open_book(best_path);
}
