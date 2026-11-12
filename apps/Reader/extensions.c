#include "extensions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct {
    const char *ext;
    const char *type_tag;
    const char *icon;
    const char *stock; /* preferred stock handler for this type */
} managed_ext;

/* Match fb2.zip before fb2 when scanning (longer first). */
static const managed_ext k_managed[] = {
    { "epub",    "@EPUB_file",  "ICON_EPUB", "eink-reader_with_epub2.app" },
    { "fb2.zip", "@FB2_file",   "ICON_FB2",  "eink-reader.app" },
    { "fb2",     "@FB2_file",   "ICON_FB2",  "eink-reader.app" },
    { "txt",     "@Text_file",  "ICON_TXT",  "eink-reader.app" },
    { "html",    "@HTML_file",  "ICON_HTML", "eink-reader.app" },
    { "htm",     "@HTML_file",  "ICON_HTM",  "eink-reader.app" },
    { "rtf",     "@RTF_file",   "ICON_RTF",  "eink-reader.app" },
    { "pdf",     "@PDF_file",   "ICON_PDF",  "eink-reader_with_rmsdk.app" },
};
#define MANAGED_N ((int)(sizeof(k_managed) / sizeof(k_managed[0])))

static int eq_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    return strcasecmp(a, b) == 0;
}

static int starts_with_ci(const char *s, const char *prefix)
{
    size_t n;
    if (!s || !prefix)
        return 0;
    n = strlen(prefix);
    return strncasecmp(s, prefix, n) == 0;
}

static int is_naskilla_handler(const char *h)
{
    if (!h || !h[0])
        return 0;
    /* Case-sensitive: stock firmware used lowercase reader.app historically. */
    if (strcmp(h, "Reader.app") == 0)
        return 1;
    if (eq_ci(h, "NaSkilla Reader.app"))
        return 1;
    if (eq_ci(h, "NaSkilla.app"))
        return 1;
    /* Full path variants from SetFileHandler / handlers.cfg */
    if (strstr(h, "NaSkilla Reader.app"))
        return 1;
    if (strstr(h, "/applications/Reader.app"))
        return 1;
    return 0;
}

static int is_stock_handler(const char *h)
{
    if (!h || !h[0])
        return 0;
    if (strcmp(h, "reader.app") == 0)
        return 1;
    if (starts_with_ci(h, "eink-reader"))
        return 1;
    return 0;
}

static void display_name(const char *handler, char *buf, size_t n)
{
    if (!handler || !handler[0]) {
        snprintf(buf, n, "unavailable");
        return;
    }
    if (is_naskilla_handler(handler)) {
        snprintf(buf, n, "NaSkilla Reader");
        return;
    }
    if (is_stock_handler(handler)) {
        snprintf(buf, n, "PocketBook Reader");
        return;
    }
    if (eq_ci(handler, "koreader.app")) {
        snprintf(buf, n, "KOReader");
        return;
    }
    {
        size_t len = strlen(handler);
        if (len > 4 && strcasecmp(handler + len - 4, ".app") == 0) {
            if (len - 4 >= n)
                len = n + 3;
            snprintf(buf, n, "%.*s", (int)(len - 4), handler);
            return;
        }
    }
    snprintf(buf, n, "%s", handler);
}

/* Choice slots for cycling: naskilla, stock, then other unique apps. */
typedef struct {
    char handler[EXTCFG_HANDLER_MAX]; /* representative basename to promote */
    char label[EXTCFG_LABEL_MAX];
} choice_t;

static int choice_same(const choice_t *a, const char *handler)
{
    if (is_naskilla_handler(a->handler) && is_naskilla_handler(handler))
        return 1;
    if (is_stock_handler(a->handler) && is_stock_handler(handler))
        return 1;
    return eq_ci(a->handler, handler);
}

static void add_choice(choice_t *out, int *n, int max, const char *handler)
{
    int i;
    if (!handler || !handler[0] || *n >= max)
        return;
    /* Prefer short app names over full paths for the stored choice. */
    if (is_naskilla_handler(handler))
        handler = "Reader.app";
    else if (is_stock_handler(handler) && !starts_with_ci(handler, "eink-reader") &&
             !eq_ci(handler, "reader.app"))
        handler = "eink-reader.app";
    for (i = 0; i < *n; i++) {
        if (choice_same(&out[i], handler))
            return;
    }
    snprintf(out[*n].handler, sizeof(out[*n].handler), "%s", handler);
    display_name(handler, out[*n].label, sizeof(out[*n].label));
    (*n)++;
}

static int split_handlers(const char *list, char out[][EXTCFG_HANDLER_MAX], int max)
{
    int n = 0;
    const char *p = list;
    while (p && *p && n < max) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        while (len > 0 && (p[0] == ' ' || p[0] == '\t')) {
            p++;
            len--;
        }
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
            len--;
        if (len > 0 && len < EXTCFG_HANDLER_MAX) {
            memcpy(out[n], p, len);
            out[n][len] = '\0';
            n++;
        }
        if (!comma)
            break;
        p = comma + 1;
    }
    return n;
}

static const managed_ext *find_managed(const char *line)
{
    int i;
    for (i = 0; i < MANAGED_N; i++) {
        size_t n = strlen(k_managed[i].ext);
        if (strncmp(line, k_managed[i].ext, n) == 0 && line[n] == ':')
            return &k_managed[i];
    }
    return NULL;
}

static void stock_for_ext(const managed_ext *m, char *out, size_t n)
{
    snprintf(out, n, "%s", m && m->stock ? m->stock : "eink-reader.app");
}

static void promote_handlers(const managed_ext *m, const char *preferred,
                             char handlers[][EXTCFG_HANDLER_MAX], int *hn, int max)
{
    char stock[EXTCFG_HANDLER_MAX];
    char promoted[EXTCFG_HANDLER_MAX];
    char tmp[EXTCFG_HANDLERS_MAX][EXTCFG_HANDLER_MAX];
    int tn = 0;
    int i;
    int have_promoted = 0;

    stock_for_ext(m, stock, sizeof(stock));
    if (is_naskilla_handler(preferred))
        snprintf(promoted, sizeof(promoted), "Reader.app");
    else if (is_stock_handler(preferred))
        snprintf(promoted, sizeof(promoted), "%s", stock);
    else
        snprintf(promoted, sizeof(promoted), "%s", preferred);

    /* Start with promoted. */
    snprintf(tmp[tn], EXTCFG_HANDLER_MAX, "%s", promoted);
    tn++;
    have_promoted = 1;

    for (i = 0; i < *hn && tn < max; i++) {
        if (is_naskilla_handler(handlers[i])) {
            /* Collapse all NaSkilla names to Reader.app; skip duplicates. */
            int j, seen = 0;
            for (j = 0; j < tn; j++) {
                if (is_naskilla_handler(tmp[j])) {
                    seen = 1;
                    break;
                }
            }
            if (!seen) {
                snprintf(tmp[tn], EXTCFG_HANDLER_MAX, "Reader.app");
                tn++;
            }
            continue;
        }
        if (is_stock_handler(handlers[i]) && is_stock_handler(promoted)) {
            /* Already have stock first; keep other stock variants later. */
            if (eq_ci(handlers[i], promoted))
                continue;
        }
        if (have_promoted && eq_ci(handlers[i], promoted))
            continue;
        snprintf(tmp[tn], EXTCFG_HANDLER_MAX, "%s", handlers[i]);
        tn++;
    }

    /* Ensure Reader.app remains somewhere if NaSkilla is not preferred. */
    if (!is_naskilla_handler(promoted)) {
        int j, seen = 0;
        for (j = 0; j < tn; j++) {
            if (is_naskilla_handler(tmp[j])) {
                seen = 1;
                break;
            }
        }
        if (!seen && tn < max) {
            snprintf(tmp[tn], EXTCFG_HANDLER_MAX, "Reader.app");
            tn++;
        }
    }

    for (i = 0; i < tn; i++)
        snprintf(handlers[i], EXTCFG_HANDLER_MAX, "%s", tmp[i]);
    *hn = tn;
}

static void join_handlers(char handlers[][EXTCFG_HANDLER_MAX], int hn,
                          char *out, size_t out_n)
{
    size_t used = 0;
    int i;
    out[0] = '\0';
    for (i = 0; i < hn; i++) {
        size_t need = strlen(handlers[i]) + (i ? 1 : 0);
        if (used + need + 1 >= out_n)
            break;
        if (i)
            out[used++] = ',';
        memcpy(out + used, handlers[i], strlen(handlers[i]));
        used += strlen(handlers[i]);
        out[used] = '\0';
    }
}

static void default_line(const managed_ext *m, const char *preferred,
                         char *out, size_t out_n)
{
    char handlers[EXTCFG_HANDLERS_MAX][EXTCFG_HANDLER_MAX];
    char joined[512];
    int hn = 0;
    char stock[EXTCFG_HANDLER_MAX];

    stock_for_ext(m, stock, sizeof(stock));
    snprintf(handlers[hn++], EXTCFG_HANDLER_MAX, "Reader.app");
    snprintf(handlers[hn++], EXTCFG_HANDLER_MAX, "%s", stock);
    if (access("/mnt/ext1/applications/koreader.app", F_OK) == 0 ||
        access("/mnt/ext1/system/bin/koreader.app", F_OK) == 0)
        snprintf(handlers[hn++], EXTCFG_HANDLER_MAX, "koreader.app");
    promote_handlers(m, preferred, handlers, &hn, EXTCFG_HANDLERS_MAX);
    join_handlers(handlers, hn, joined, sizeof(joined));
    snprintf(out, out_n, "%s:%s:1:%s:%s", m->ext, m->type_tag, joined, m->icon);
}

char *extcfg_rewrite_text(const char *in_text, const char *preferred)
{
    char *out = NULL;
    size_t cap = 0;
    size_t len = 0;
    int seen[MANAGED_N];
    const char *p;
    int i;

    if (!preferred || !preferred[0])
        preferred = "Reader.app";
    memset(seen, 0, sizeof(seen));

    p = in_text ? in_text : "";
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
        char line[1024];
        const managed_ext *m;
        size_t copy = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;

        memcpy(line, p, copy);
        line[copy] = '\0';
        if (copy > 0 && line[copy - 1] == '\r')
            line[--copy] = '\0';

        m = find_managed(line);
        if (m) {
            /* ext:@TYPE:flags:handlers:ICON — keep fields, replace handlers. */
            char *parts[5];
            int pc = 0;
            char buf[1024];
            char *tok;
            char handlers[EXTCFG_HANDLERS_MAX][EXTCFG_HANDLER_MAX];
            char joined[512];
            int hn;
            char rebuilt[1024];
            int mi;

            snprintf(buf, sizeof(buf), "%s", line);
            tok = buf;
            while (pc < 5) {
                parts[pc++] = tok;
                tok = strchr(tok, ':');
                if (!tok)
                    break;
                *tok++ = '\0';
            }
            for (mi = 0; mi < MANAGED_N; mi++) {
                if (k_managed[mi].ext == m->ext) {
                    seen[mi] = 1;
                    break;
                }
            }
            if (pc >= 4) {
                hn = split_handlers(parts[3], handlers, EXTCFG_HANDLERS_MAX);
                promote_handlers(m, preferred, handlers, &hn, EXTCFG_HANDLERS_MAX);
                join_handlers(handlers, hn, joined, sizeof(joined));
                if (pc >= 5)
                    snprintf(rebuilt, sizeof(rebuilt), "%s:%s:%s:%s:%s",
                             parts[0], parts[1], parts[2], joined, parts[4]);
                else
                    snprintf(rebuilt, sizeof(rebuilt), "%s:%s:%s:%s",
                             parts[0], parts[1], parts[2], joined);
            } else {
                default_line(m, preferred, rebuilt, sizeof(rebuilt));
            }
            {
                size_t add = strlen(rebuilt) + 1;
                if (len + add + 1 > cap) {
                    size_t ncap = cap ? cap * 2 : 2048;
                    char *nbuf;
                    while (ncap < len + add + 1)
                        ncap *= 2;
                    nbuf = (char *)realloc(out, ncap);
                    if (!nbuf) {
                        free(out);
                        return NULL;
                    }
                    out = nbuf;
                    cap = ncap;
                }
                memcpy(out + len, rebuilt, strlen(rebuilt));
                len += strlen(rebuilt);
                out[len++] = '\n';
                out[len] = '\0';
            }
        } else if (copy > 0 || eol) {
            size_t add = copy + 1;
            if (len + add + 1 > cap) {
                size_t ncap = cap ? cap * 2 : 2048;
                char *nbuf;
                while (ncap < len + add + 1)
                    ncap *= 2;
                nbuf = (char *)realloc(out, ncap);
                if (!nbuf) {
                    free(out);
                    return NULL;
                }
                out = nbuf;
                cap = ncap;
            }
            memcpy(out + len, line, copy);
            len += copy;
            out[len++] = '\n';
            out[len] = '\0';
        }

        if (!eol)
            break;
        p = eol + 1;
    }

    for (i = 0; i < MANAGED_N; i++) {
        char rebuilt[1024];
        size_t add;
        if (seen[i])
            continue;
        default_line(&k_managed[i], preferred, rebuilt, sizeof(rebuilt));
        add = strlen(rebuilt) + 1;
        if (len + add + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 2048;
            char *nbuf;
            while (ncap < len + add + 1)
                ncap *= 2;
            nbuf = (char *)realloc(out, ncap);
            if (!nbuf) {
                free(out);
                return NULL;
            }
            out = nbuf;
            cap = ncap;
        }
        memcpy(out + len, rebuilt, strlen(rebuilt));
        len += strlen(rebuilt);
        out[len++] = '\n';
        out[len] = '\0';
    }

    if (!out) {
        out = (char *)malloc(1);
        if (out)
            out[0] = '\0';
    }
    return out;
}

static char *read_file(const char *path)
{
    FILE *fp;
    long sz;
    char *buf;
    size_t n;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    sz = ftell(fp);
    if (sz < 0 || sz > 512 * 1024) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

static int write_file_atomic(const char *path, const char *text)
{
    char tmp[512];
    FILE *fp;
    size_t n;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "wb");
    if (!fp)
        return -1;
    n = text ? strlen(text) : 0;
    if (n && fwrite(text, 1, n, fp) != n) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fclose(fp) != 0) {
        remove(tmp);
        return -1;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

static void backup_once(void)
{
    char *cur;
    if (access(EXTCFG_BAK, F_OK) == 0)
        return;
    if (access(EXTCFG_PATH, F_OK) != 0)
        return;
    cur = read_file(EXTCFG_PATH);
    if (!cur)
        return;
    write_file_atomic(EXTCFG_BAK, cur);
    free(cur);
}

static int first_epub_handler(char *out, size_t n)
{
    char *text = read_file(EXTCFG_PATH);
    const char *p;
    out[0] = '\0';
    if (!text)
        return -1;
    p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
        char line[1024];
        size_t copy = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        const managed_ext *m;
        memcpy(line, p, copy);
        line[copy] = '\0';
        m = find_managed(line);
        if (m && strcmp(m->ext, "epub") == 0) {
            char *parts[5];
            int pc = 0;
            char buf[1024];
            char *tok;
            char handlers[EXTCFG_HANDLERS_MAX][EXTCFG_HANDLER_MAX];
            int hn;
            snprintf(buf, sizeof(buf), "%s", line);
            tok = buf;
            while (pc < 5) {
                parts[pc++] = tok;
                tok = strchr(tok, ':');
                if (!tok)
                    break;
                *tok++ = '\0';
            }
            if (pc >= 4) {
                hn = split_handlers(parts[3], handlers, EXTCFG_HANDLERS_MAX);
                if (hn > 0) {
                    snprintf(out, n, "%s", handlers[0]);
                    free(text);
                    return 0;
                }
            }
        }
        if (!eol)
            break;
        p = eol + 1;
    }
    free(text);
    return -1;
}

static void collect_choices(choice_t *choices, int *n)
{
    char *text;
    char first[EXTCFG_HANDLER_MAX];
    *n = 0;
    add_choice(choices, n, EXTCFG_HANDLERS_MAX, "Reader.app");
    add_choice(choices, n, EXTCFG_HANDLERS_MAX, "eink-reader_with_epub2.app");

    if (access("/mnt/ext1/applications/koreader.app", F_OK) == 0 ||
        access("/mnt/ext1/system/bin/koreader.app", F_OK) == 0)
        add_choice(choices, n, EXTCFG_HANDLERS_MAX, "koreader.app");

    text = read_file(EXTCFG_PATH);
    if (text) {
        const char *p = text;
        while (*p) {
            const char *eol = strchr(p, '\n');
            size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
            char line[1024];
            size_t copy = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
            const managed_ext *m;
            memcpy(line, p, copy);
            line[copy] = '\0';
            m = find_managed(line);
            if (m && strcmp(m->ext, "epub") == 0) {
                char *parts[5];
                int pc = 0;
                char buf[1024];
                char *tok;
                char handlers[EXTCFG_HANDLERS_MAX][EXTCFG_HANDLER_MAX];
                int hn, i;
                snprintf(buf, sizeof(buf), "%s", line);
                tok = buf;
                while (pc < 5) {
                    parts[pc++] = tok;
                    tok = strchr(tok, ':');
                    if (!tok)
                        break;
                    *tok++ = '\0';
                }
                if (pc >= 4) {
                    hn = split_handlers(parts[3], handlers, EXTCFG_HANDLERS_MAX);
                    for (i = 0; i < hn; i++)
                        add_choice(choices, n, EXTCFG_HANDLERS_MAX, handlers[i]);
                }
            }
            if (!eol)
                break;
            p = eol + 1;
        }
        free(text);
    }

    /* Keep current first at index 0 so cycle advances to choices[1]. */
    if (first_epub_handler(first, sizeof(first)) == 0 && first[0]) {
        int i, cur = -1;
        choice_t tmp;
        for (i = 0; i < *n; i++) {
            if (choice_same(&choices[i], first)) {
                cur = i;
                break;
            }
        }
        if (cur > 0) {
            tmp = choices[0];
            choices[0] = choices[cur];
            choices[cur] = tmp;
        }
    } else if (access(EXTCFG_PATH, F_OK) != 0) {
        /* Firmware default is stock — put it first so the next tap is NaSkilla. */
        int i, cur = -1;
        choice_t tmp;
        for (i = 0; i < *n; i++) {
            if (is_stock_handler(choices[i].handler)) {
                cur = i;
                break;
            }
        }
        if (cur > 0) {
            tmp = choices[0];
            choices[0] = choices[cur];
            choices[cur] = tmp;
        }
    }
}

void extcfg_current_label(char *buf, size_t n)
{
    char handler[EXTCFG_HANDLER_MAX];
    if (!buf || n == 0)
        return;
    if (first_epub_handler(handler, sizeof(handler)) != 0) {
        /* No extensions.cfg: firmware uses the stock reader until we create one. */
        if (access(EXTCFG_PATH, F_OK) != 0) {
            snprintf(buf, n, "PocketBook Reader");
            return;
        }
        snprintf(buf, n, "unavailable");
        return;
    }
    display_name(handler, buf, n);
}

int extcfg_naskilla_is_default(void)
{
    char handler[EXTCFG_HANDLER_MAX];
    if (first_epub_handler(handler, sizeof(handler)) != 0) {
        /* Missing extensions.cfg: firmware default is stock; do not pin. */
        return 0;
    }
    return is_naskilla_handler(handler);
}

int extcfg_cycle_default(void)
{
    choice_t choices[EXTCFG_HANDLERS_MAX];
    int n = 0;
    int next = 0;
    char *cur_text;
    char *new_text;
    int rc;

    collect_choices(choices, &n);
    if (n <= 0)
        return -1;
    next = (n > 1) ? 1 : 0;

    backup_once();
    cur_text = read_file(EXTCFG_PATH);
    new_text = extcfg_rewrite_text(cur_text ? cur_text : "", choices[next].handler);
    free(cur_text);
    if (!new_text)
        return -1;
    rc = write_file_atomic(EXTCFG_PATH, new_text);
    free(new_text);
    return rc;
}
