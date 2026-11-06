#include "ui.h"
#include "layout.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

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
        if (a != b)
            return 0;
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

static void parent_dir(const char *path, char *out, size_t outlen)
{
    const char *slash;
    size_t n;
    if (!out || outlen == 0)
        return;
    out[0] = '\0';
    if (!path)
        return;
    slash = strrchr(path, '/');
    if (!slash || slash == path)
        return;
    n = (size_t)(slash - path);
    if (n >= outlen)
        n = outlen - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

static int already_suggested(const char *path)
{
    int i;
    for (i = 0; i < g_app.suggest_count; i++) {
        if (strcmp(g_app.suggestions[i].path, path) == 0)
            return 1;
    }
    return 0;
}

static void add_suggestion(const char *path, const char *reason)
{
    suggest_hit *h;
    char base[256];
    const char *slash;
    const char *dot;
    const char *sep;
    size_t n;
    if (!path || !path[0] || g_app.suggest_count >= 5)
        return;
    if (g_app.open_path[0] && strcmp(path, g_app.open_path) == 0)
        return;
    if (already_suggested(path))
        return;
    h = &g_app.suggestions[g_app.suggest_count];
    memset(h, 0, sizeof(*h));
    snprintf(h->path, sizeof(h->path), "%s", path);
    if (reason)
        snprintf(h->reason, sizeof(h->reason), "%s", reason);
    slash = strrchr(path, '/');
    slash = slash ? slash + 1 : path;
    dot = strrchr(slash, '.');
    n = dot && dot > slash ? (size_t)(dot - slash) : strlen(slash);
    if (n >= sizeof(base))
        n = sizeof(base) - 1;
    memcpy(base, slash, n);
    base[n] = '\0';
    sep = strstr(base, " - ");
    if (sep) {
        size_t tlen = (size_t)(sep - base);
        if (tlen >= sizeof(h->title))
            tlen = sizeof(h->title) - 1;
        memcpy(h->title, base, tlen);
        h->title[tlen] = '\0';
        snprintf(h->author, sizeof(h->author), "%s", sep + 3);
    } else {
        snprintf(h->title, sizeof(h->title), "%s", base);
    }
    {
        /* A handful of results, so reading the real title is cheap.
         * Send-to files are stored under hash names. */
        bookinfo *info = GetBookInfo(path);
        if (info && info->title && info->title[0])
            snprintf(h->title, sizeof(h->title), "%s", info->title);
        if (info && info->author && info->author[0])
            snprintf(h->author, sizeof(h->author), "%s", info->author);
    }
    g_app.suggest_count++;
}

static void collect_books(const char *dir, int depth, int max_depth,
                          const char *skip_dir, const char *reason, int limit)
{
    DIR *d;
    struct dirent *ent;
    if (!dir || !dir[0] || depth > max_depth || g_app.suggest_count >= limit)
        return;
    d = opendir(dir);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL && g_app.suggest_count < limit) {
        char path[768];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        if (skip_dir && strcmp(ent->d_name, skip_dir) == 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            collect_books(path, depth + 1, max_depth, NULL, reason, limit);
        else if (S_ISREG(st.st_mode) && is_book_name(ent->d_name))
            add_suggestion(path, reason);
    }
    closedir(d);
}

static const char *current_book_path(void)
{
    if (g_app.open_path[0])
        return g_app.open_path;
    if (g_app.cfg.last_book[0])
        return g_app.cfg.last_book;
    if (g_app.last_book[0])
        return g_app.last_book;
    return NULL;
}

/* Library files live at .../_Categories/<shelf>/<author>/<book>/file. */
static int split_library_path(const char *path,
                              char *categories, size_t categories_n,
                              char *shelf, size_t shelf_n,
                              char *author, size_t author_n)
{
    const char *mark;
    const char *p;
    const char *shelf_s;
    const char *author_s;
    const char *book_s;
    char tmp[512];
    if (!path)
        return 0;
    mark = strstr(path, "/_Categories/");
    if (!mark)
        return 0;
    p = mark + strlen("/_Categories/");
    shelf_s = p;
    author_s = strchr(p, '/');
    if (!author_s || author_s == shelf_s)
        return 0;
    book_s = strchr(author_s + 1, '/');
    if (!book_s)
        return 0;
    snprintf(categories, categories_n, "%.*s", (int)(mark - path + strlen("/_Categories")), path);
    snprintf(tmp, sizeof(tmp), "%.*s", (int)(author_s - shelf_s), shelf_s);
    snprintf(shelf, shelf_n, "%s/%s", categories, tmp);
    snprintf(tmp, sizeof(tmp), "%.*s", (int)(book_s - (author_s + 1)), author_s + 1);
    snprintf(author, author_n, "%s/%s", shelf, tmp);
    return 1;
}

typedef struct series_cand {
    char path[512];
    char title[160];
    char author[80];
    int index_x100;
} series_cand;

static series_cand g_series[96];
static int g_series_n;
static int g_opf_seen;
static int g_title_lookup;
static int g_title_found;
static int g_title_index;
static char g_title_want[160];
static char g_title_series[160];

static void fold_space(const char *src, char *dst, size_t n)
{
    size_t j = 0;
    int sp = 1;
    if (!dst || n == 0)
        return;
    if (!src)
        src = "";
    for (; *src && j + 1 < n; src++) {
        char c = *src;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c == ' ' || c == '\t') {
            if (sp)
                continue;
            sp = 1;
            dst[j++] = ' ';
            continue;
        }
        sp = 0;
        dst[j++] = c;
    }
    while (j > 0 && dst[j - 1] == ' ')
        j--;
    dst[j] = '\0';
}

static int series_eq(const char *a, const char *b)
{
    char fa[160], fb[160];
    size_t na, nb;
    fold_space(a, fa, sizeof(fa));
    fold_space(b, fb, sizeof(fb));
    if (!fa[0] || !fb[0])
        return 0;
    if (strcmp(fa, fb) == 0)
        return 1;
    /* "James Bond" and "James Bond Original Series" are the same shelf. */
    na = strlen(fa);
    nb = strlen(fb);
    if (na >= 8 && nb > na && strstr(fb, fa))
        return 1;
    if (nb >= 8 && na > nb && strstr(fa, fb))
        return 1;
    return 0;
}

static int title_eq(const char *a, const char *b)
{
    char fa[160], fb[160];
    fold_space(a, fa, sizeof(fa));
    fold_space(b, fb, sizeof(fb));
    return fa[0] && strcmp(fa, fb) == 0;
}

static void xml_unescape(char *s)
{
    char *r;
    char *w;
    if (!s)
        return;
    r = w = s;
    while (*r) {
        if (*r == '&') {
            if (strncmp(r, "&amp;", 5) == 0) {
                *w++ = '&';
                r += 5;
                continue;
            }
            if (strncmp(r, "&lt;", 4) == 0) {
                *w++ = '<';
                r += 4;
                continue;
            }
            if (strncmp(r, "&gt;", 4) == 0) {
                *w++ = '>';
                r += 4;
                continue;
            }
            if (strncmp(r, "&quot;", 6) == 0) {
                *w++ = '"';
                r += 6;
                continue;
            }
            if (strncmp(r, "&apos;", 6) == 0) {
                *w++ = '\'';
                r += 6;
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

static int attr_in(const char *start, const char *end, const char *key,
                   char *out, size_t outlen)
{
    char pat[64];
    const char *p;
    const char *q;
    size_t klen;
    size_t n;
    if (!out || outlen == 0)
        return 0;
    out[0] = '\0';
    snprintf(pat, sizeof(pat), "%s=", key);
    klen = strlen(pat);
    for (p = start; p + klen < end; p++) {
        char quote;
        if (memcmp(p, pat, klen) != 0)
            continue;
        p += klen;
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end || (*p != '"' && *p != '\''))
            continue;
        quote = *p++;
        q = memchr(p, quote, (size_t)(end - p));
        if (!q)
            return 0;
        n = (size_t)(q - p);
        if (n >= outlen)
            n = outlen - 1;
        memcpy(out, p, n);
        out[n] = '\0';
        xml_unescape(out);
        return 1;
    }
    return 0;
}

static int parse_index_x100(const char *text)
{
    int whole = 0;
    int frac = 0;
    int digits = 0;
    if (!text || !text[0])
        return 0;
    if (sscanf(text, "%d.%d", &whole, &frac) >= 1) {
        /* Keep two digits of the fraction: 3.5 -> 350, 3 -> 300. */
        if (frac > 0) {
            int v = frac;
            digits = 0;
            while (v > 0 && digits < 4) {
                v /= 10;
                digits++;
            }
            while (digits < 2) {
                frac *= 10;
                digits++;
            }
            while (digits > 2) {
                frac /= 10;
                digits--;
            }
        }
        return whole * 100 + frac;
    }
    return 0;
}

static void xml_text_of(const char *buf, const char *tag, char *out, size_t outlen)
{
    char open[32];
    const char *p;
    const char *gt;
    const char *end;
    size_t n;
    if (!out || outlen == 0)
        return;
    out[0] = '\0';
    snprintf(open, sizeof(open), "<%s", tag);
    p = strstr(buf, open);
    if (!p)
        return;
    gt = strchr(p, '>');
    if (!gt)
        return;
    gt++;
    end = strstr(gt, "</");
    if (!end || end < gt)
        return;
    n = (size_t)(end - gt);
    if (n >= outlen)
        n = outlen - 1;
    memcpy(out, gt, n);
    out[n] = '\0';
    xml_unescape(out);
}

/* Descriptions in Calibre metadata.opf often push the series tags well
 * past the first few kilobytes. Keep one buffer and scan the whole file. */
static char g_opf_buf[256 * 1024];

static void parse_opf_mem(const char *buf, char *series, size_t series_n,
                          int *index_x100, char *title, size_t title_n,
                          char *author, size_t author_n)
{
    const char *p;
    int series_from_calibre = 0;
    if (!buf)
        return;
    if (title && title_n && !title[0])
        xml_text_of(buf, "dc:title", title, title_n);
    if (author && author_n && !author[0])
        xml_text_of(buf, "dc:creator", author, author_n);
    p = buf;
    while ((p = strstr(p, "<meta"))) {
        const char *end = strchr(p, '>');
        const char *text_end;
        char name[80];
        char content[160];
        int self_close;
        if (!end)
            break;
        name[0] = content[0] = '\0';
        if (!attr_in(p, end, "name", name, sizeof(name)))
            attr_in(p, end, "property", name, sizeof(name));
        attr_in(p, end, "content", content, sizeof(content));
        self_close = (end > p && end[-1] == '/');
        if (!content[0] && !self_close) {
            text_end = strstr(end + 1, "</meta>");
            if (text_end && (size_t)(text_end - (end + 1)) < sizeof(content)) {
                size_t n = (size_t)(text_end - (end + 1));
                memcpy(content, end + 1, n);
                content[n] = '\0';
                xml_unescape(content);
            }
        }
        if (series && series_n && !series_from_calibre &&
            strcmp(name, "calibre:series") == 0 && content[0]) {
            snprintf(series, series_n, "%s", content);
            series_from_calibre = 1;
        } else if (series && series_n && !series[0] &&
                   strcmp(name, "belongs-to-collection") == 0 && content[0]) {
            snprintf(series, series_n, "%s", content);
        }
        if (index_x100 && strcmp(name, "calibre:series_index") == 0) {
            int v = parse_index_x100(content);
            if (v)
                *index_x100 = v;
        } else if (index_x100 && *index_x100 == 0 &&
                   strcmp(name, "group-position") == 0) {
            int v = parse_index_x100(content);
            if (v)
                *index_x100 = v;
        }
        p = end + 1;
    }
}

static int read_opf_path(const char *opf_path, char *series, size_t series_n,
                         int *index_x100, char *title, size_t title_n,
                         char *author, size_t author_n)
{
    FILE *fp;
    size_t nread;
    size_t keep = 0;
    if (series && series_n)
        series[0] = '\0';
    if (title && title_n)
        title[0] = '\0';
    if (author && author_n)
        author[0] = '\0';
    if (index_x100)
        *index_x100 = 0;
    fp = fopen(opf_path, "rb");
    if (!fp)
        return 0;
    for (;;) {
        size_t room = sizeof(g_opf_buf) - 1 - keep;
        nread = fread(g_opf_buf + keep, 1, room, fp);
        g_opf_buf[keep + nread] = '\0';
        parse_opf_mem(g_opf_buf, series, series_n, index_x100,
                      title, title_n, author, author_n);
        if (series && series[0] && (!index_x100 || *index_x100))
            break;
        if (nread < room)
            break;
        /* Whole buffer used and the series tag is still ahead. */
        memmove(g_opf_buf, g_opf_buf + keep + nread - 400, 400);
        keep = 400;
    }
    fclose(fp);
    return series && series[0];
}

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int name_ends(const char *name, size_t nlen, const char *suf)
{
    size_t sl = strlen(suf);
    size_t i;
    if (nlen < sl)
        return 0;
    for (i = 0; i < sl; i++) {
        char a = name[nlen - sl + i];
        char b = suf[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

/* Inflate one ZIP entry into g_opf_buf. Sizes come from the central directory. */
static int zip_read_entry(FILE *fp, unsigned local, unsigned method,
                          unsigned comp, unsigned uncomp)
{
    unsigned char lh[30];
    unsigned name_len;
    unsigned extra_len;
    unsigned char *compbuf;
    size_t got;
    (void)uncomp;
    if (comp == 0 || comp > 1024 * 1024)
        return 0;
    if (fseek(fp, (long)local, SEEK_SET) != 0)
        return 0;
    if (fread(lh, 1, 30, fp) != 30 || rd32(lh) != 0x04034b50u)
        return 0;
    name_len = rd16(lh + 26);
    extra_len = rd16(lh + 28);
    if (fseek(fp, (long)(name_len + extra_len), SEEK_CUR) != 0)
        return 0;
    if (method == 0) {
        size_t n = comp;
        if (n >= sizeof(g_opf_buf))
            n = sizeof(g_opf_buf) - 1;
        if (fread(g_opf_buf, 1, n, fp) != n)
            return 0;
        g_opf_buf[n] = '\0';
        return 1;
    }
    if (method != 8)
        return 0;
    compbuf = (unsigned char *)malloc(comp);
    if (!compbuf)
        return 0;
    if (fread(compbuf, 1, comp, fp) != comp) {
        free(compbuf);
        return 0;
    }
    {
        z_stream strm;
        int rc;
        memset(&strm, 0, sizeof(strm));
        if (inflateInit2(&strm, -15) != Z_OK) {
            free(compbuf);
            return 0;
        }
        strm.next_in = compbuf;
        strm.avail_in = comp;
        strm.next_out = (unsigned char *)g_opf_buf;
        strm.avail_out = (unsigned)(sizeof(g_opf_buf) - 1);
        rc = inflate(&strm, Z_FINISH);
        got = strm.total_out;
        inflateEnd(&strm);
        free(compbuf);
        if (got == 0 || (rc != Z_STREAM_END && rc != Z_OK && rc != Z_BUF_ERROR))
            return 0;
        g_opf_buf[got] = '\0';
        return 1;
    }
}

/* Books copied into SendToPb have no sidecar. The series is inside the epub. */
static int read_epub_series(const char *book_path, char *series, size_t series_n,
                            int *index_x100)
{
    FILE *fp;
    unsigned char *tail = NULL;
    unsigned char *cd = NULL;
    long sz;
    size_t ntail;
    long start;
    int i;
    unsigned cd_off = 0;
    unsigned cd_size = 0;
    int found_eocd = 0;
    char opf_name[512];
    unsigned opf_local = 0, opf_method = 0, opf_comp = 0, opf_uncomp = 0;
    int have_opf = 0;
    int ok = 0;
    if (!book_path || !ends_with_ci(book_path, ".epub"))
        return 0;
    fp = fopen(book_path, "rb");
    if (!fp)
        return 0;
    if (fseek(fp, 0, SEEK_END) != 0)
        goto done;
    sz = ftell(fp);
    if (sz < 22)
        goto done;
    ntail = (size_t)(sz > 65536 ? 65536 : sz);
    tail = (unsigned char *)malloc(ntail);
    if (!tail)
        goto done;
    start = sz - (long)ntail;
    if (fseek(fp, start, SEEK_SET) != 0 || fread(tail, 1, ntail, fp) != ntail)
        goto done;
    for (i = (int)ntail - 22; i >= 0; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b &&
            tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            cd_size = rd32(tail + i + 12);
            cd_off = rd32(tail + i + 16);
            found_eocd = 1;
            break;
        }
    }
    free(tail);
    tail = NULL;
    if (!found_eocd || cd_size == 0 || cd_size > 2 * 1024 * 1024)
        goto done;
    cd = (unsigned char *)malloc(cd_size);
    if (!cd)
        goto done;
    if (fseek(fp, (long)cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, fp) != cd_size)
        goto done;
    opf_name[0] = '\0';
    {
        size_t pos = 0;
        unsigned c_local = 0, c_method = 0, c_comp = 0, c_uncomp = 0;
        int have_container = 0;
        while (pos + 46 <= cd_size) {
            unsigned method, comp, uncomp, namelen, extralen, commentlen, local;
            char name[512];
            if (rd32(cd + pos) != 0x02014b50u)
                break;
            method = rd16(cd + pos + 10);
            comp = rd32(cd + pos + 20);
            uncomp = rd32(cd + pos + 24);
            namelen = rd16(cd + pos + 28);
            extralen = rd16(cd + pos + 30);
            commentlen = rd16(cd + pos + 32);
            local = rd32(cd + pos + 42);
            if (namelen < sizeof(name)) {
                memcpy(name, cd + pos + 46, namelen);
                name[namelen] = '\0';
            } else {
                name[0] = '\0';
            }
            if (!have_container && name_ends(name, namelen < sizeof(name) ? namelen : 0,
                                            "container.xml")) {
                c_local = local;
                c_method = method;
                c_comp = comp;
                c_uncomp = uncomp;
                have_container = 1;
            }
            if (!have_opf && name[0] && name_ends(name, strlen(name), ".opf")) {
                snprintf(opf_name, sizeof(opf_name), "%s", name);
                opf_local = local;
                opf_method = method;
                opf_comp = comp;
                opf_uncomp = uncomp;
                have_opf = 1;
            }
            pos += 46u + namelen + extralen + commentlen;
        }
        if (have_container &&
            zip_read_entry(fp, c_local, c_method, c_comp, c_uncomp)) {
            const char *fp_attr = strstr(g_opf_buf, "full-path=\"");
            char quote = '"';
            if (!fp_attr) {
                fp_attr = strstr(g_opf_buf, "full-path='");
                quote = '\'';
            }
            if (fp_attr) {
                const char *s = strchr(fp_attr, quote);
                const char *e;
                if (s) {
                    s++;
                    e = strchr(s, quote);
                    if (e && (size_t)(e - s) < sizeof(opf_name)) {
                        char want[512];
                        size_t pos2 = 0;
                        unsigned saved_local = opf_local;
                        unsigned saved_method = opf_method;
                        unsigned saved_comp = opf_comp;
                        unsigned saved_uncomp = opf_uncomp;
                        int saved_have = have_opf;
                        memcpy(want, s, (size_t)(e - s));
                        want[e - s] = '\0';
                        have_opf = 0;
                        while (pos2 + 46 <= cd_size) {
                            unsigned namelen, extralen, commentlen;
                            char name[512];
                            if (rd32(cd + pos2) != 0x02014b50u)
                                break;
                            namelen = rd16(cd + pos2 + 28);
                            extralen = rd16(cd + pos2 + 30);
                            commentlen = rd16(cd + pos2 + 32);
                            if (namelen < sizeof(name)) {
                                memcpy(name, cd + pos2 + 46, namelen);
                                name[namelen] = '\0';
                                if (strcmp(name, want) == 0) {
                                    opf_local = rd32(cd + pos2 + 42);
                                    opf_method = rd16(cd + pos2 + 10);
                                    opf_comp = rd32(cd + pos2 + 20);
                                    opf_uncomp = rd32(cd + pos2 + 24);
                                    have_opf = 1;
                                    break;
                                }
                            }
                            pos2 += 46u + namelen + extralen + commentlen;
                        }
                        if (!have_opf && saved_have) {
                            have_opf = 1;
                            opf_local = saved_local;
                            opf_method = saved_method;
                            opf_comp = saved_comp;
                            opf_uncomp = saved_uncomp;
                        }
                    }
                }
            }
        }
    }
    if (!have_opf)
        goto done;
    if (!zip_read_entry(fp, opf_local, opf_method, opf_comp, opf_uncomp))
        goto done;
    if (series && series_n)
        series[0] = '\0';
    if (index_x100)
        *index_x100 = 0;
    parse_opf_mem(g_opf_buf, series, series_n, index_x100, NULL, 0, NULL, 0);
    ok = series && series[0];
done:
    free(tail);
    free(cd);
    fclose(fp);
    return ok;
}

static int read_book_series(const char *book_path, char *series, size_t series_n,
                            int *index_x100)
{
    char dir[512];
    char opf[576];
    bookinfo *info;
    if (index_x100)
        *index_x100 = 0;
    if (series && series_n)
        series[0] = '\0';
    parent_dir(book_path, dir, sizeof(dir));
    if (dir[0]) {
        snprintf(opf, sizeof(opf), "%s/metadata.opf", dir);
        if (read_opf_path(opf, series, series_n, index_x100, NULL, 0, NULL, 0) &&
            series && series[0])
            return 1;
    }
    if (read_epub_series(book_path, series, series_n, index_x100) &&
        series && series[0])
        return 1;
    info = GetBookInfo(book_path);
    if (info && info->series && info->series[0]) {
        snprintf(series, series_n, "%s", info->series);
        if (index_x100 && info->numinseries > 0)
            *index_x100 = info->numinseries * 100;
        return 1;
    }
    return 0;
}

static void add_series_cand(const char *epub, const char *title, const char *author,
                            int index_x100)
{
    series_cand *c;
    const char *cur = current_book_path();
    if (!epub || !epub[0])
        return;
    if (cur && strcmp(cur, epub) == 0)
        return;
    if (g_series_n >= (int)(sizeof(g_series) / sizeof(g_series[0]))) {
        int low = 0;
        int i;
        for (i = 1; i < g_series_n; i++) {
            if (g_series[i].index_x100 < g_series[low].index_x100)
                low = i;
        }
        /* Keep later volumes when the shelf has more copies than the list. */
        if (index_x100 <= g_series[low].index_x100)
            return;
        c = &g_series[low];
    } else {
        c = &g_series[g_series_n++];
    }
    memset(c, 0, sizeof(*c));
    snprintf(c->path, sizeof(c->path), "%s", epub);
    if (title)
        snprintf(c->title, sizeof(c->title), "%s", title);
    if (author)
        snprintf(c->author, sizeof(c->author), "%s", author);
    c->index_x100 = index_x100;
}

static void consider_opf(const char *opf_path, const char *want)
{
    char series[160];
    char title[160];
    char author[80];
    char dir[512];
    int index_x100 = 0;
    DIR *d;
    struct dirent *ent;
    if (!read_opf_path(opf_path, series, sizeof(series), &index_x100,
                       title, sizeof(title), author, sizeof(author)))
        return;
    g_opf_seen++;
    if (g_title_lookup) {
        if (!g_title_found && title_eq(title, g_title_want)) {
            snprintf(g_title_series, sizeof(g_title_series), "%s", series);
            g_title_index = index_x100;
            g_title_found = 1;
        }
        return;
    }
    if (!series_eq(series, want))
        return;
    parent_dir(opf_path, dir, sizeof(dir));
    d = opendir(dir);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL) {
        char epub[768];
        if (ent->d_name[0] == '.')
            continue;
        if (!is_book_name(ent->d_name))
            continue;
        snprintf(epub, sizeof(epub), "%s/%s", dir, ent->d_name);
        add_series_cand(epub, title[0] ? title : ent->d_name, author, index_x100);
        break;
    }
    closedir(d);
}

static void walk_series(const char *dir, int depth, const char *want)
{
    DIR *d;
    struct dirent *ent;
    if (!dir || !dir[0] || depth > 8)
        return;
    if (g_title_lookup && g_title_found)
        return;
    d = opendir(dir);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL) {
        char path[768];
        if (g_title_lookup && g_title_found)
            break;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (ends_with_ci(ent->d_name, ".opf")) {
            consider_opf(path, want);
            continue;
        }
        {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
                walk_series(path, depth + 1, want);
        }
    }
    closedir(d);
}

static int series_cand_cmp(const void *a, const void *b)
{
    const series_cand *ea = (const series_cand *)a;
    const series_cand *eb = (const series_cand *)b;
    if (ea->index_x100 != eb->index_x100)
        return ea->index_x100 < eb->index_x100 ? -1 : 1;
    return strcmp(ea->title, eb->title);
}

static void add_ready(const char *path, const char *title, const char *author,
                      const char *reason)
{
    suggest_hit *h;
    if (!path || !path[0] || g_app.suggest_count >= 5)
        return;
    if (already_suggested(path))
        return;
    h = &g_app.suggestions[g_app.suggest_count];
    memset(h, 0, sizeof(*h));
    snprintf(h->path, sizeof(h->path), "%s", path);
    if (title && title[0])
        snprintf(h->title, sizeof(h->title), "%s", title);
    if (author && author[0])
        snprintf(h->author, sizeof(h->author), "%s", author);
    if (reason)
        snprintf(h->reason, sizeof(h->reason), "%s", reason);
    g_app.suggest_count++;
}

static void emit_next_in_series(const char *series, int current_index)
{
    int i;
    int later = 0;
    if (g_series_n > 1)
        qsort(g_series, (size_t)g_series_n, sizeof(g_series[0]), series_cand_cmp);
    for (i = 0; i < g_series_n && g_app.suggest_count < 5; i++) {
        char reason[128];
        int n = g_series[i].index_x100 / 100;
        if (current_index > 0 && g_series[i].index_x100 <= current_index)
            continue;
        if (n > 0)
            snprintf(reason, sizeof(reason), "Next in %s · %d", series, n);
        else
            snprintf(reason, sizeof(reason), "Next in %s", series);
        add_ready(g_series[i].path, g_series[i].title, g_series[i].author, reason);
        later = 1;
    }
    if (!later) {
        for (i = 0; i < g_series_n && g_app.suggest_count < 5; i++) {
            char reason[128];
            int n = g_series[i].index_x100 / 100;
            if (n > 0)
                snprintf(reason, sizeof(reason), "%s · %d", series, n);
            else
                snprintf(reason, sizeof(reason), "%s", series);
            add_ready(g_series[i].path, g_series[i].title, g_series[i].author, reason);
        }
    }
    if (g_app.suggest_count == 0) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "No other books in %s", series);
    } else if (later) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Next in %s", series);
    } else {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Other books in %s", series);
    }
}

static void suggest_other_shelf(const char *categories, const char *current_shelf)
{
    DIR *d;
    struct dirent *ent;
    const char *cur_name = strrchr(current_shelf, '/');
    int passed_current = 0;
    char picked[512];
    cur_name = cur_name ? cur_name + 1 : "";
    picked[0] = '\0';
    d = opendir(categories);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL) {
        char path[768];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", categories, ent->d_name);
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        if (!picked[0])
            snprintf(picked, sizeof(picked), "%s", path);
        if (passed_current) {
            snprintf(picked, sizeof(picked), "%s", path);
            break;
        }
        if (strcmp(ent->d_name, cur_name) == 0)
            passed_current = 1;
    }
    closedir(d);
    if (picked[0] && (!current_shelf || strcmp(picked, current_shelf) != 0))
        collect_books(picked, 0, 3, NULL, "Another shelf on this reader", 5);
}

void suggest_run(int mode)
{
    const char *path = current_book_path();
    char categories[512];
    char shelf[512];
    char author[512];
    int on_shelf = 0;

    g_app.suggest_count = 0;
    categories[0] = shelf[0] = author[0] = '\0';
    if (path)
        on_shelf = split_library_path(path, categories, sizeof(categories),
                                      shelf, sizeof(shelf),
                                      author, sizeof(author));

    ClearScreen();
    if (g_app.font_msg)
        SetFont(g_app.font_msg, g_app.cfg.night ? WHITE : BLACK);
    DrawTextRect(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40,
                 "Looking through your books", ALIGN_CENTER | VALIGN_MIDDLE);
    PartialUpdate(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40);

    if (mode == 0) {
        char series[160];
        int series_index = 0;
        /* Same-folder matching listed unrelated Send-to books. Follow the
         * series name (Calibre metadata) and offer the later volumes. */
        g_opf_seen = 0;
        g_title_lookup = 0;
        g_title_found = 0;
        if (path && read_book_series(path, series, sizeof(series), &series_index)) {
            g_series_n = 0;
            walk_series(CFG_LIBRARY_DIR, 0, series);
            reader_log("series [%s] index %d matches %d (opf %d)",
                       series, series_index, g_series_n, g_opf_seen);
            emit_next_in_series(series, series_index);
        } else if (path) {
            bookinfo *info = GetBookInfo(path);
            if (info && info->title && info->title[0]) {
                g_title_lookup = 1;
                snprintf(g_title_want, sizeof(g_title_want), "%s", info->title);
                walk_series(CFG_LIBRARY_DIR, 0, "");
                g_title_lookup = 0;
            }
            if (g_title_found && g_title_series[0]) {
                snprintf(series, sizeof(series), "%s", g_title_series);
                series_index = g_title_index;
                g_series_n = 0;
                g_opf_seen = 0;
                walk_series(CFG_LIBRARY_DIR, 0, series);
                reader_log("series by title [%s] index %d matches %d",
                           series, series_index, g_series_n);
                emit_next_in_series(series, series_index);
            } else if (on_shelf) {
                collect_books(author, 0, 2, NULL, "Same author on this reader", 5);
                snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%s",
                         g_app.suggest_count ? "Same author, on this reader"
                                             : "No other books by this author");
            } else {
                reader_log("series: none for %s", path);
                snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                         "No series information for this book");
            }
        } else if (on_shelf) {
            collect_books(author, 0, 2, NULL, "Same author on this reader", 5);
            snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%s",
                     g_app.suggest_count ? "Same author, on this reader"
                                         : "No other books by this author");
        } else {
            snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                     "No series information for this book");
        }
    } else if (mode == 1) {
        if (on_shelf) {
            const char *author_name = strrchr(author, '/');
            author_name = author_name ? author_name + 1 : "";
            collect_books(shelf, 0, 3, author_name, "Same shelf on this reader", 5);
        }
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%s",
                 g_app.suggest_count ? "Same shelf, on this reader"
                                     : "No other books on this shelf");
    } else {
        if (on_shelf)
            suggest_other_shelf(categories, shelf);
        else
            collect_books(CFG_LIBRARY_DIR "/_Categories", 0, 4, NULL,
                          "On this reader", 5);
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%s",
                 g_app.suggest_count ? "Another shelf, on this reader"
                                     : "No other shelves found");
    }

    if (!path && g_app.suggest_count == 0) {
        collect_books(CFG_LIBRARY_DIR, 0, 4, NULL, "On this reader", 5);
        if (g_app.suggest_count == 0)
            collect_books(CFG_SENDTO_DIR2, 0, 1, NULL, "Send to PocketBook", 5);
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%s",
                 g_app.suggest_count ? "Books on this reader"
                                     : "No books found on this reader");
    }

    g_app.screen = SCREEN_SUGGEST_RESULTS;
    ui_draw();
}

void suggest_ask_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int row_h = ui_row_h();
    int m = ui_margin();
    int top = ui_title_h() + ui_row_h();
    int y;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 0, sw, ui_title_h(), "What do you want next?",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(m, ui_title_h(), sw / 2, ui_row_h(), "Home",
                 ALIGN_LEFT | VALIGN_MIDDLE);

    y = top;
    DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Continue a series",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    y += row_h;
    DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Same theme",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    y += row_h;
    DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Something different",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    (void)sh;
}

int suggest_ask_hit(int x, int y)
{
    int row_h = ui_row_h();
    int top = ui_title_h() + ui_row_h();
    int idx;
    (void)x;
    if (y >= ui_title_h() && y < top) {
        ui_show_home();
        return 1;
    }
    if (y < top)
        return 0;
    idx = (y - top) / row_h;
    if (idx >= 0 && idx <= 2) {
        suggest_run(idx);
        return 1;
    }
    return 0;
}

void suggest_results_draw(void)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int row_h = ui_row_h();
    int item_h = row_h * 2;
    int m = ui_margin();
    int top = ui_title_h() + row_h + row_h;
    int i;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_ui)
        SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 0, sw, ui_title_h(), "Suggestions", ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(m, ui_title_h(), sw / 2 - m, row_h, "Home",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(sw / 2, ui_title_h(), sw / 2 - m, row_h, "Ask again",
                 ALIGN_RIGHT | VALIGN_MIDDLE);
    DrawTextRect(m, ui_title_h() + row_h, sw - 2 * m, row_h,
                 g_app.suggest_status, ALIGN_LEFT | VALIGN_MIDDLE);

    for (i = 0; i < g_app.suggest_count; i++) {
        char line[384];
        int y = top + i * item_h;
        if (y + item_h > sh)
            break;
        if (g_app.suggestions[i].author[0])
            snprintf(line, sizeof(line), "%s — %s",
                     g_app.suggestions[i].title, g_app.suggestions[i].author);
        else
            snprintf(line, sizeof(line), "%s", g_app.suggestions[i].title);
        DrawTextRect(m, y, sw - 2 * m, row_h - 8, line,
                     ALIGN_LEFT | VALIGN_MIDDLE);
        if (g_app.suggestions[i].reason[0])
            DrawTextRect(m, y + row_h, sw - 2 * m, row_h - 8,
                         g_app.suggestions[i].reason, ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(m, y + item_h - 1, sw - m, y + item_h - 1, fg);
    }
}

int suggest_results_hit(int x, int y)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int item_h = row_h * 2;
    int top = ui_title_h() + row_h + row_h;
    int i;
    if (y >= ui_title_h() && y < ui_title_h() + row_h) {
        if (x < sw / 2) {
            ui_show_home();
            return 1;
        }
        g_app.screen = SCREEN_SUGGEST_ASK;
        ui_draw();
        return 1;
    }
    for (i = 0; i < g_app.suggest_count; i++) {
        int yy = top + i * item_h;
        if (yy + item_h > sh)
            break;
        if (y >= yy && y < yy + item_h) {
            if (g_app.suggestions[i].path[0])
                ui_open_book(g_app.suggestions[i].path);
            return 1;
        }
    }
    return 0;
}
