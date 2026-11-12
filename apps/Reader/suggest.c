#include "ui.h"
#include "layout.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <curl/curl.h>
#include "inkview.h"

#define GEMINI_URL_BASE "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-lite-latest:generateContent?key="
#define SUGGEST_SAMPLE_MAX 40

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

static int already_suggested(const char *path)
{
    int i;
    for (i = 0; i < g_app.suggest_count; i++) {
        if (strcmp(g_app.suggestions[i].path, path) == 0)
            return 1;
    }
    return 0;
}

static void format_ordinal(int n, char *out, size_t out_n)
{
    const char *suf = "th";
    int mod100;
    if (!out || out_n == 0)
        return;
    if (n <= 0) {
        out[0] = '\0';
        return;
    }
    mod100 = n % 100;
    if (mod100 < 11 || mod100 > 13) {
        int mod10 = n % 10;
        if (mod10 == 1)
            suf = "st";
        else if (mod10 == 2)
            suf = "nd";
        else if (mod10 == 3)
            suf = "rd";
    }
    snprintf(out, out_n, "%d%s", n, suf);
}

/* True when the model already wrote a position like "3rd" or "#3". */
static int reason_has_position(const char *reason)
{
    const char *p;
    if (!reason || !reason[0])
        return 0;
    for (p = reason; *p; p++) {
        if (*p == '#' && p[1] >= '0' && p[1] <= '9')
            return 1;
        if (*p >= '0' && *p <= '9') {
            const char *q = p;
            while (*q >= '0' && *q <= '9')
                q++;
            if ((q[0] == 's' && q[1] == 't') ||
                (q[0] == 'n' && q[1] == 'd') ||
                (q[0] == 'r' && q[1] == 'd') ||
                (q[0] == 't' && q[1] == 'h'))
                return 1;
        }
    }
    return 0;
}

static void add_suggestion(const char *path, const char *title,
                           const char *author, const char *reason,
                           const char *series, int index)
{
    suggest_hit *h;
    if (!path || !path[0] || g_app.suggest_count >= 5)
        return;
    if (g_app.open_path[0] && strcmp(path, g_app.open_path) == 0)
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
    if (series && series[0])
        snprintf(h->series, sizeof(h->series), "%s", series);
    if (index > 0)
        h->index = index;
    if (!h->title[0] || !h->author[0] || !h->series[0] || h->index <= 0) {
        char ser[120];
        int idx = 0;
        ser[0] = '\0';
        book_info_from_path(path, h->title, sizeof(h->title),
                            h->author, sizeof(h->author),
                            ser, sizeof(ser), &idx, NULL);
        if (!h->series[0] && ser[0])
            snprintf(h->series, sizeof(h->series), "%s", ser);
        if (h->index <= 0 && idx > 0)
            h->index = idx;
    }
    if (!h->title[0]) {
        const char *slash = strrchr(path, '/');
        snprintf(h->title, sizeof(h->title), "%s", slash ? slash + 1 : path);
    }
    if (reason && reason[0]) {
        if (h->index > 0 && h->series[0] && !reason_has_position(reason)) {
            char ord[16];
            format_ordinal(h->index, ord, sizeof(ord));
            snprintf(h->reason, sizeof(h->reason), "%s in %s. %s",
                     ord, h->series, reason);
        } else {
            snprintf(h->reason, sizeof(h->reason), "%s", reason);
        }
    } else if (h->index > 0 && h->series[0]) {
        char ord[16];
        format_ordinal(h->index, ord, sizeof(ord));
        snprintf(h->reason, sizeof(h->reason), "%s in %s", ord, h->series);
    }
    g_app.suggest_count++;
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

typedef struct cat_entry {
    char path[512];
    char title[160];
    char author[80];
    char author_alt[80]; /* Calibre folder author; may differ from file name */
    char series[120];
    int index;
    int year;
    int tier;  /* 2 exact, 1 near, 0 none/hashed-pending */
    int hashed;
} cat_entry;

static void paint_wait(const char *msg)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    if (g_app.font_msg)
        SetFont(g_app.font_msg, fg);
    DrawTextRect(0, sh / 2 - 30, sw, 60, msg, ALIGN_CENTER | VALIGN_MIDDLE);
    PartialUpdate(0, sh / 2 - 40, sw, 80);
}

static int load_api_key(char *key, size_t key_n)
{
    FILE *fp;
    size_t n;
    if (!key || key_n == 0)
        return 0;
    key[0] = '\0';
    fp = fopen(CFG_API_KEY_FILE, "r");
    if (!fp)
        return 0;
    if (!fgets(key, (int)key_n, fp)) {
        fclose(fp);
        key[0] = '\0';
        return 0;
    }
    fclose(fp);
    n = strlen(key);
    while (n > 0 && (key[n - 1] == '\n' || key[n - 1] == '\r' || key[n - 1] == ' '))
        key[--n] = '\0';
    if (n < 20 || strncmp(key, "YOUR_", 5) == 0) {
        key[0] = '\0';
        return 0;
    }
    return 1;
}

static int ensure_wifi(void)
{
    iv_netinfo *net = NetInfo();
    if (net && net->connected)
        return 1;
    paint_wait("Connecting to Wi-Fi");
    if (NetConnect2(NULL, 1) != 0)
        return 0;
    net = NetInfo();
    return net && net->connected;
}

typedef struct curl_buf {
    char *data;
    size_t size;
} curl_buf;

static size_t curl_write(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_buf *buf = (curl_buf *)userdata;
    size_t bytes = size * nmemb;
    char *next;
    if (!buf || bytes == 0)
        return 0;
    next = (char *)realloc(buf->data, buf->size + bytes + 1);
    if (!next)
        return 0;
    buf->data = next;
    memcpy(buf->data + buf->size, ptr, bytes);
    buf->size += bytes;
    buf->data[buf->size] = '\0';
    return bytes;
}

static int looks_like_hash_name(const char *name)
{
    size_t n;
    size_t i;
    int digit = 0;
    if (!name)
        return 0;
    n = strlen(name);
    if (n < 8 || n > 48)
        return 0;
    for (i = 0; i < n; i++) {
        char c = name[i];
        if (c >= '0' && c <= '9')
            digit = 1;
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   c == '_' || c == '-'))
            return 0;
    }
    return digit;
}

static char *json_escape(const char *src)
{
    size_t extra = 0;
    size_t i;
    size_t j = 0;
    char *dst;
    const unsigned char *s = (const unsigned char *)(src ? src : "");
    for (i = 0; s[i]; i++) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r')
            extra++;
    }
    dst = (char *)malloc(i + extra + 1);
    if (!dst)
        return NULL;
    for (i = 0; s[i]; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)s[i];
        } else if (s[i] == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (s[i] == '\r') {
            continue;
        } else if (s[i] < 32) {
            continue;
        } else {
            dst[j++] = (char)s[i];
        }
    }
    dst[j] = '\0';
    return dst;
}

static int cat_add(cat_entry **cat, int *n, int *cap, const char *path,
                   const char *skip)
{
    cat_entry *e;
    int i;
    if (!path || !path[0] || !cat || !n || !cap)
        return 0;
    if (skip && strcmp(path, skip) == 0)
        return 0;
    for (i = 0; i < *n; i++) {
        if (strcmp((*cat)[i].path, path) == 0)
            return 0;
    }
    if (*n >= 8000)
        return 0;
    if (*n >= *cap) {
        int next = *cap ? *cap * 2 : 256;
        cat_entry *grown;
        if (next > 8000)
            next = 8000;
        grown = (cat_entry *)realloc(*cat, (size_t)next * sizeof(cat_entry));
        if (!grown)
            return 0;
        *cat = grown;
        *cap = next;
    }
    e = &(*cat)[*n];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    book_names_from_path(path, e->title, sizeof(e->title),
                         e->author, sizeof(e->author),
                         e->series, sizeof(e->series), &e->index);
    folder_author_from_path(path, e->author_alt, sizeof(e->author_alt));
    /* Hash detection must use the on-disk basename without extension.
     * book_names_from_path leaves title empty for SendToPb hashes, and the
     * fallback title includes ".epub", which would fail the hash check. */
    {
        char base[256];
        const char *slash = strrchr(path, '/');
        const char *name = slash ? slash + 1 : path;
        const char *dot = strrchr(name, '.');
        size_t bn = (dot && dot > name) ? (size_t)(dot - name) : strlen(name);
        if (bn >= sizeof(base))
            bn = sizeof(base) - 1;
        memcpy(base, name, bn);
        base[bn] = '\0';
        e->hashed = looks_like_hash_name(base);
        if (!e->title[0])
            snprintf(e->title, sizeof(e->title), "%s", base);
    }
    (*n)++;
    return 1;
}

static void cat_scan_dir(const char *dir, int depth, cat_entry **cat, int *n, int *cap,
                         const char *skip)
{
    DIR *d;
    struct dirent *ent;
    if (!dir || !dir[0] || depth > 8 || *n >= 8000)
        return;
    d = opendir(dir);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL && *n < 8000) {
        char path[768];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            cat_scan_dir(path, depth + 1, cat, n, cap, skip);
        else if (S_ISREG(st.st_mode) && is_book_name(ent->d_name))
            cat_add(cat, n, cap, path, skip);
    }
    closedir(d);
}

static int cat_build(cat_entry **cat, int *n, const char *skip)
{
    int cap = 0;
    const char *sendto;
    *cat = NULL;
    *n = 0;
    cat_scan_dir(CFG_LIBRARY_DIR, 0, cat, n, &cap, skip);
    cat_scan_dir(CFG_BOOKS_DIR, 0, cat, n, &cap, skip);
    sendto = config_find_sendto_dir();
    if (sendto)
        cat_scan_dir(sendto, 0, cat, n, &cap, skip);
    return *n;
}

static void cat_swap(cat_entry *a, cat_entry *b)
{
    cat_entry tmp;
    if (!a || !b || a == b)
        return;
    tmp = *a;
    *a = *b;
    *b = tmp;
}

static void cat_shuffle(cat_entry *cat, int n)
{
    int i;
    if (!cat || n < 2)
        return;
    srand((unsigned)(time(NULL) ^ (n * 2654435761u)));
    for (i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        cat_swap(&cat[i], &cat[j]);
    }
}

/* Letter or digit, including non-ASCII bytes so Polish names still match. */
static int author_word_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c >= 0x80;
}

static int author_next_word(const char **p, char *word, size_t word_n)
{
    const char *s;
    size_t n = 0;
    if (!p || !*p || !word || word_n == 0)
        return 0;
    s = *p;
    while (*s && !author_word_char((unsigned char)*s))
        s++;
    if (!*s) {
        *p = s;
        word[0] = '\0';
        return 0;
    }
    while (*s && author_word_char((unsigned char)*s)) {
        unsigned char c = (unsigned char)*s++;
        if (n + 1 < word_n) {
            if (c >= 'A' && c <= 'Z')
                c = (unsigned char)(c - 'A' + 'a');
            word[n++] = (char)c;
        }
    }
    word[n] = '\0';
    *p = s;
    return n > 0;
}

/* True when a and b differ by at most one insert, delete, or substitution. */
static int edit_distance_at_most_1(const char *a, const char *b)
{
    size_t na;
    size_t nb;
    size_t i;
    size_t j;
    if (!a || !b)
        return 0;
    na = strlen(a);
    nb = strlen(b);
    if (na > nb) {
        const char *t = a;
        size_t tn = na;
        a = b;
        na = nb;
        b = t;
        nb = tn;
    }
    if (nb - na > 1)
        return 0;
    if (na == nb) {
        int diff = 0;
        for (i = 0; i < na; i++) {
            if (a[i] != b[i] && ++diff > 1)
                return 0;
        }
        return 1;
    }
    /* nb == na + 1: one insertion in b / deletion in a */
    i = j = 0;
    while (i < na && j < nb) {
        if (a[i] == b[j]) {
            i++;
            j++;
        } else {
            j++;
            if (j - i > 1)
                return 0;
        }
    }
    return 1;
}

static int series_stop_word(const char *w)
{
    return strcmp(w, "series") == 0 || strcmp(w, "seria") == 0 ||
           strcmp(w, "cykl") == 0 || strcmp(w, "cycle") == 0 ||
           strcmp(w, "the") == 0 || strcmp(w, "a") == 0 ||
           strcmp(w, "an") == 0;
}

static int word_all_digits(const char *w)
{
    size_t k;
    if (!w || !w[0])
        return 0;
    for (k = 0; w[k]; k++) {
        if (w[k] < '0' || w[k] > '9')
            return 0;
    }
    return 1;
}

/* Collect significant tokens: skip 1-letter initials; for series also skip
 * stop words and pure digits. Returns token count. */
static int collect_tokens(const char *s, char out[][40], int max,
                          int for_series)
{
    const char *p = s ? s : "";
    char word[40];
    int n = 0;
    while (n < max && author_next_word(&p, word, sizeof(word))) {
        size_t len = strlen(word);
        if (len <= 1)
            continue;
        if (for_series && (series_stop_word(word) || word_all_digits(word)))
            continue;
        memcpy(out[n], word, sizeof(word));
        n++;
    }
    return n;
}

static int words_match_exact(char wa[][40], int na, char wb[][40], int nb)
{
    int used[8];
    int i;
    int j;
    if (na == 0 || na != nb)
        return 0;
    memset(used, 0, sizeof(used));
    for (i = 0; i < na; i++) {
        int found = 0;
        for (j = 0; j < nb; j++) {
            if (used[j])
                continue;
            if (strcmp(wa[i], wb[j]) == 0) {
                used[j] = 1;
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }
    return 1;
}

/* Shorter name's every word matches a distinct word of the longer name.
 * Words of length >= 5 may differ by edit distance 1. */
static int words_match_near(char wa[][40], int na, char wb[][40], int nb)
{
    char (*shorter)[40];
    char (*longer)[40];
    int ns;
    int nl;
    int used[8];
    int i;
    int j;
    int matches;
    if (na == 0 || nb == 0)
        return 0;
    if (na <= nb) {
        shorter = wa;
        ns = na;
        longer = wb;
        nl = nb;
    } else {
        shorter = wb;
        ns = nb;
        longer = wa;
        nl = na;
    }
    /* Need at least 2 matched words unless both names are a single word. */
    if (ns < 2 && !(na == 1 && nb == 1))
        return 0;
    memset(used, 0, sizeof(used));
    matches = 0;
    for (i = 0; i < ns; i++) {
        int found = 0;
        size_t slen = strlen(shorter[i]);
        for (j = 0; j < nl; j++) {
            if (used[j])
                continue;
            if (strcmp(shorter[i], longer[j]) == 0) {
                used[j] = 1;
                found = 1;
                break;
            }
            if (slen >= 5 && strlen(longer[j]) >= 5 &&
                edit_distance_at_most_1(shorter[i], longer[j])) {
                used[j] = 1;
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
        matches++;
    }
    return matches > 0;
}

/* 2 = exact multiset, 1 = near (subset + edit-distance-1), 0 = none. */
static int person_match(const char *a, const char *b)
{
    char wa[8][40];
    char wb[8][40];
    int na;
    int nb;
    if (!a || !b || !a[0] || !b[0])
        return 0;
    na = collect_tokens(a, wa, 8, 0);
    nb = collect_tokens(b, wb, 8, 0);
    if (na == 0 || nb == 0)
        return 0;
    if (words_match_exact(wa, na, wb, nb))
        return 2;
    if (words_match_near(wa, na, wb, nb))
        return 1;
    return 0;
}

static int series_match(const char *a, const char *b)
{
    char wa[8][40];
    char wb[8][40];
    int na;
    int nb;
    if (!a || !b || !a[0] || !b[0])
        return 0;
    na = collect_tokens(a, wa, 8, 1);
    nb = collect_tokens(b, wb, 8, 1);
    if (na == 0 || nb == 0)
        return 0;
    if (words_match_exact(wa, na, wb, nb))
        return 2;
    if (words_match_near(wa, na, wb, nb))
        return 1;
    return 0;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

/* Score one book against the open book. Tier 2 exact, 1 near, 0 none.
 * Hashed files keep tier 0 but are retained until after enrich. */
static int cat_score_entry(cat_entry *e, const char *series, int cur_index,
                           const char *author)
{
    int best = 0;
    int s;
    int a1;
    int a2;
    if (!e)
        return 0;
    if (series && series[0] && e->series[0]) {
        s = series_match(e->series, series);
        if (s > 0) {
            if (e->index > 0 && cur_index > 0 && e->index <= cur_index)
                s = 0;
            else if (s > best)
                best = s;
        }
    }
    if (author && author[0]) {
        a1 = person_match(e->author, author);
        a2 = person_match(e->author_alt, author);
        best = max_int(best, max_int(a1, a2));
    }
    e->tier = best;
    return best;
}

/* Series volume first, then higher tier, then year, then title. */
static int cat_rank_cmp(const void *a, const void *b)
{
    const cat_entry *ea = (const cat_entry *)a;
    const cat_entry *eb = (const cat_entry *)b;
    int ia = ea->index > 0 ? ea->index : 100000;
    int ib = eb->index > 0 ? eb->index : 100000;
    int ya;
    int yb;
    if (ea->tier != eb->tier)
        return eb->tier - ea->tier;
    if (ia != ib)
        return ia - ib;
    ya = ea->year >= 1000 ? ea->year : 100000;
    yb = eb->year >= 1000 ? eb->year : 100000;
    if (ya != yb)
        return ya - yb;
    return strcasecmp(ea->title, eb->title);
}

/* Keep exact/near matches and hashed files (enriched later). */
static int cat_select_series(cat_entry *cat, int n, const char *series,
                             int cur_index, const char *author,
                             int keep_unmatched_hash)
{
    int kept = 0;
    int i;
    if (!cat || n <= 0)
        return 0;
    if ((!series || !series[0]) && (!author || !author[0]))
        return 0;
    for (i = 0; i < n; i++) {
        int score = cat_score_entry(&cat[i], series, cur_index, author);
        int keep = score > 0 || (keep_unmatched_hash && cat[i].hashed);
        if (!keep)
            continue;
        if (kept != i)
            cat[kept] = cat[i];
        kept++;
    }
    return kept;
}

/* After enrich: drop hashed files that still match nothing; rank; cap. */
static int cat_finalize_series(cat_entry *cat, int n, const char *series,
                               int cur_index, const char *author)
{
    int kept = 0;
    int i;
    if (!cat || n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        int score = cat_score_entry(&cat[i], series, cur_index, author);
        if (score <= 0)
            continue;
        if (kept != i)
            cat[kept] = cat[i];
        kept++;
    }
    if (kept > 1)
        qsort(cat, (size_t)kept, sizeof(cat_entry), cat_rank_cmp);
    if (kept > SUGGEST_SAMPLE_MAX)
        kept = SUGGEST_SAMPLE_MAX;
    return kept;
}

static void cat_enrich(cat_entry *cat, int n)
{
    int i;
    if (!cat)
        return;
    for (i = 0; i < n; i++) {
        book_info_from_path(cat[i].path, cat[i].title, sizeof(cat[i].title),
                            cat[i].author, sizeof(cat[i].author),
                            cat[i].series, sizeof(cat[i].series),
                            &cat[i].index, &cat[i].year);
    }
}

static int cat_sample_random(cat_entry *cat, int n, int want)
{
    if (!cat || n <= 0)
        return 0;
    if (want < 1)
        want = 1;
    cat_shuffle(cat, n);
    return n < want ? n : want;
}

static char *build_recent_block(void)
{
    char *out;
    size_t cap = 1024;
    size_t n = 0;
    int i;
    out = (char *)malloc(cap);
    if (!out)
        return NULL;
    out[0] = '\0';
    if (g_app.cfg.recent_count <= 0)
        return out;
    for (i = 0; i < g_app.cfg.recent_count && i < CFG_RECENT_MAX; i++) {
        char title[160];
        char author[80];
        char row[360];
        int wrote;
        title[0] = author[0] = '\0';
        if (!g_app.cfg.recent[i][0])
            continue;
        book_info_from_path(g_app.cfg.recent[i], title, sizeof(title),
                            author, sizeof(author), NULL, 0, NULL, NULL);
        if (!title[0]) {
            const char *slash = strrchr(g_app.cfg.recent[i], '/');
            snprintf(title, sizeof(title), "%s",
                     slash ? slash + 1 : g_app.cfg.recent[i]);
        }
        if (author[0])
            wrote = snprintf(row, sizeof(row), "%d. %s — %s\n", i + 1,
                             title, author);
        else
            wrote = snprintf(row, sizeof(row), "%d. %s\n", i + 1, title);
        if (wrote < 0)
            continue;
        if (n + (size_t)wrote + 1 > cap) {
            size_t next = cap * 2 + (size_t)wrote;
            char *grown = (char *)realloc(out, next);
            if (!grown)
                break;
            out = grown;
            cap = next;
        }
        memcpy(out + n, row, (size_t)wrote + 1);
        n += (size_t)wrote;
    }
    return out;
}

/* Decode the JSON string that starts at the opening quote. */
static char *json_unquote(const char *q)
{
    char *out;
    size_t i = 0;
    size_t cap = 2048;
    if (!q || *q != '"')
        return NULL;
    q++;
    out = (char *)malloc(cap);
    if (!out)
        return NULL;
    while (*q && *q != '"') {
        char c = *q++;
        if (c == '\\' && *q) {
            char e = *q++;
            if (e == 'n')
                c = '\n';
            else if (e == 't')
                c = ' ';
            else if (e == 'u') {
                c = '?';
                if (q[0] && q[1] && q[2] && q[3])
                    q += 4;
            } else {
                c = e;
            }
        }
        if (i + 2 >= cap) {
            char *grown = (char *)realloc(out, cap * 2);
            if (!grown)
                break;
            out = grown;
            cap *= 2;
        }
        out[i++] = c;
    }
    out[i] = '\0';
    return out;
}

static int part_is_thought(const char *json, const char *at)
{
    char win[96];
    size_t n;
    const char *from = at;
    if (at - json > 80)
        from = at - 80;
    else
        from = json;
    n = (size_t)(at - from);
    if (n >= sizeof(win))
        n = sizeof(win) - 1;
    memcpy(win, from, n);
    win[n] = '\0';
    return strstr(win, "\"thought\":true") || strstr(win, "\"thought\": true") ? 1 : 0;
}

/* Gemini 2.5 often returns an empty thought part before the answer.
 * Keep the last real text, not the first empty one. */
static char *gemini_text(const char *json)
{
    const char *p;
    char *best = NULL;
    if (!json)
        return NULL;
    p = json;
    while ((p = strstr(p, "\"text\"")) != NULL) {
        const char *q = p + 6;
        char *piece;
        int thought = part_is_thought(json, p);
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r' || *q == ':')
            q++;
        piece = json_unquote(q);
        p = q + 1;
        if (!piece)
            continue;
        if (!piece[0] || thought) {
            free(piece);
            continue;
        }
        free(best);
        best = piece;
    }
    return best;
}

static void json_message(const char *json, char *err, size_t err_n)
{
    const char *p;
    char *msg;
    if (!err || err_n == 0)
        return;
    err[0] = '\0';
    if (!json)
        return;
    p = strstr(json, "\"message\"");
    if (!p)
        return;
    p += 9;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
        p++;
    msg = json_unquote(p);
    if (!msg || !msg[0]) {
        free(msg);
        return;
    }
    snprintf(err, err_n, "%s", msg);
    free(msg);
}

static int ask_gemini(const char *prompt, char **reply, char *err, size_t err_n)
{
    char key[128];
    char url[320];
    char *escaped;
    char *payload;
    CURL *curl;
    struct curl_slist *headers = NULL;
    curl_buf buf;
    CURLcode rc;
    const char *head = "{\"contents\":[{\"parts\":[{\"text\":\"";
    /* Close text, part, parts, content item, contents, and the object. */
    const char *tail = "\"}]}]}";
    *reply = NULL;
    if (!load_api_key(key, sizeof(key))) {
        snprintf(err, err_n, "Add a Gemini key to .ai_api_key");
        return 0;
    }
    if (!ensure_wifi()) {
        snprintf(err, err_n, "Wi-Fi is not connected");
        key[0] = '\0';
        return 0;
    }
    escaped = json_escape(prompt);
    if (!escaped) {
        snprintf(err, err_n, "Out of memory");
        key[0] = '\0';
        return 0;
    }
    payload = (char *)malloc(strlen(head) + strlen(escaped) + strlen(tail) + 1);
    if (!payload) {
        free(escaped);
        snprintf(err, err_n, "Out of memory");
        key[0] = '\0';
        return 0;
    }
    memcpy(payload, head, strlen(head));
    memcpy(payload + strlen(head), escaped, strlen(escaped));
    memcpy(payload + strlen(head) + strlen(escaped), tail, strlen(tail) + 1);
    free(escaped);
    snprintf(url, sizeof(url), "%s%s", GEMINI_URL_BASE, key);
    key[0] = '\0';
    memset(&buf, 0, sizeof(buf));
    curl = curl_easy_init();
    if (!curl) {
        free(payload);
        snprintf(err, err_n, "Network failed to start");
        return 0;
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    PostponeTimedPoweroff();
    rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(payload);
    if (rc != CURLE_OK) {
        snprintf(err, err_n, "Network error");
        free(buf.data);
        reader_log("gemini curl %s", curl_easy_strerror(rc));
        return 0;
    }
    if (!buf.data) {
        snprintf(err, err_n, "Empty reply from the AI");
        return 0;
    }
    *reply = gemini_text(buf.data);
    if (!*reply || !(*reply)[0]) {
        json_message(buf.data, err, err_n);
        if (err[0] && (strstr(err, "credit") || strstr(err, "billing") ||
                       strstr(err, "prepay")))
            snprintf(err, err_n, "The Gemini account has no credits left");
        if (!err[0])
            snprintf(err, err_n, "The AI reply could not be read");
        reader_log("gemini body %.180s", buf.data);
        free(*reply);
        *reply = NULL;
        free(buf.data);
        return 0;
    }
    free(buf.data);
    return 1;
}

static int line_index(const char *s, const char **rest)
{
    int n = 0;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t' || *s == '*' || *s == '-' || *s == '#')
        s++;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        if (n > 100000)
            return 0;
        s++;
    }
    if (*s == '.' || *s == ')' || *s == ':' || *s == '|' || *s == '-')
        s++;
    while (*s == ' ' || *s == '\t')
        s++;
    if (rest)
        *rest = s;
    return n;
}

static void apply_reply(const char *reply, cat_entry *cat, int count)
{
    const char *p = reply;
    while (p && *p && g_app.suggest_count < 5) {
        const char *eol = strchr(p, '\n');
        char line[256];
        const char *rest = NULL;
        int idx;
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        idx = line_index(line, &rest);
        if (idx >= 1 && idx <= count) {
            const char *reason = (rest && rest[0]) ? rest : "Chosen from your books";
            /* Keep the model's reply order; do not re-sort hits. */
            add_suggestion(cat[idx - 1].path, cat[idx - 1].title,
                           cat[idx - 1].author, reason,
                           cat[idx - 1].series, cat[idx - 1].index);
        }
        if (!eol)
            break;
        p = eol + 1;
    }
}

static const char *ask_for_mode(int mode)
{
    if (mode == 0)
        return "Continue the series. List the next unread books in reading "
               "order from the first of the series to the last. Skip books "
               "already in the recent list. Write the series position in the "
               "reason, for example 3rd of James Bond. Some entries may have "
               "misspelled names. Pick books that belong to the same author "
               "or series as the current book, even if a name is slightly "
               "different.";
    if (mode == 1)
        return "Suggest books with a similar theme or the same kind of story.";
    if (mode == 2)
        return "Suggest something different from the current book.";
    return "Suggest what to read next. Use the reading history and choose "
           "from the numbered list of books on this reader.";
}

void suggest_run(int mode)
{
    const char *path = current_book_path();
    cat_entry *cat = NULL;
    int count = 0;
    int full_count = 0;
    char cur_title[160];
    char cur_author[80];
    char cur_series[120];
    int cur_index = 0;
    char *list = NULL;
    char *recent = NULL;
    char *prompt = NULL;
    char *reply = NULL;
    char err[128];
    size_t list_n = 0;
    size_t list_cap = 0;
    int i;

    g_app.suggest_count = 0;
    err[0] = '\0';
    cur_title[0] = cur_author[0] = cur_series[0] = '\0';
    paint_wait("Reading file names");
    if (path)
        book_info_from_path(path, cur_title, sizeof(cur_title),
                            cur_author, sizeof(cur_author),
                            cur_series, sizeof(cur_series), &cur_index, NULL);
    reader_log("suggest current title=%.80s author=%.60s series=%.60s index=%d",
               cur_title, cur_author, cur_series, cur_index);
    cat_build(&cat, &count, path);
    full_count = count;
    reader_log("suggest catalog %d books mode %d", count, mode);
    if (count == 0) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "No books found on this reader");
        free(cat);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }

    if (mode == 1 || mode == 2) {
        count = cat_sample_random(cat, count, SUGGEST_SAMPLE_MAX);
        reader_log("suggest sample %d of %d", count, full_count);
    } else if (mode == 0) {
        if ((!cur_series[0] && !cur_author[0])) {
            snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                     "No matching book on this reader");
            free(cat);
            g_app.screen = SCREEN_SUGGEST_RESULTS;
            ui_draw();
            return;
        }
        count = cat_select_series(cat, count, cur_series, cur_index,
                                  cur_author, 1);
        reader_log("suggest series select %d of %d", count, full_count);
        if (count == 0) {
            snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                     "No matching book on this reader");
            free(cat);
            g_app.screen = SCREEN_SUGGEST_RESULTS;
            ui_draw();
            return;
        }
    }

    paint_wait("Reading titles");
    cat_enrich(cat, count);

    if (mode == 0) {
        count = cat_finalize_series(cat, count, cur_series, cur_index,
                                    cur_author);
        reader_log("suggest series candidates %d", count);
        for (i = 0; i < count; i++) {
            reader_log("cand %.60s|%.40s|%.40s|%d|tier=%d",
                       cat[i].title, cat[i].author, cat[i].series,
                       cat[i].index, cat[i].tier);
        }
        if (count == 0) {
            snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                     "No matching book on this reader");
            free(cat);
            g_app.screen = SCREEN_SUGGEST_RESULTS;
            ui_draw();
            return;
        }
    }

    list_cap = 1024;
    list = (char *)malloc(list_cap);
    if (!list) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Out of memory");
        free(cat);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }
    list[0] = '\0';
    for (i = 0; i < count; i++) {
        char row[420];
        int wrote;
        if (mode == 0 && cat[i].series[0] && cat[i].index > 0 && cat[i].author[0])
            wrote = snprintf(row, sizeof(row), "%d. %s — %s — %s #%d\n",
                             i + 1, cat[i].title, cat[i].author,
                             cat[i].series, cat[i].index);
        else if (mode == 0 && cat[i].series[0] && cat[i].index > 0)
            wrote = snprintf(row, sizeof(row), "%d. %s — %s #%d\n",
                             i + 1, cat[i].title, cat[i].series, cat[i].index);
        else if (cat[i].author[0])
            wrote = snprintf(row, sizeof(row), "%d. %s — %s\n", i + 1,
                             cat[i].title, cat[i].author);
        else
            wrote = snprintf(row, sizeof(row), "%d. %s\n", i + 1, cat[i].title);
        if (wrote < 0)
            continue;
        if (list_n + (size_t)wrote + 1 > list_cap) {
            size_t next = list_cap * 2 + (size_t)wrote;
            char *grown = (char *)realloc(list, next);
            if (!grown)
                break;
            list = grown;
            list_cap = next;
        }
        memcpy(list + list_n, row, (size_t)wrote + 1);
        list_n += (size_t)wrote;
    }

    if (mode == 0 || mode == 3)
        recent = build_recent_block();

    {
        const char *head =
            "You recommend books that are already on this e-reader. "
            "Choose only from the numbered list. Do not invent titles.\n\n"
            "Request: ";
        const char *mid = "\nCurrent book: ";
        const char *hist_head = "\n\nRecently opened (newest first):\n";
        const char *tail =
            "\n\nBooks on this reader:\n";
        const char *end = mode == 0
            ? "\nReply with at most 5 lines in reading order, next unread "
              "volume first. Each line starts with the list number, then a "
              "vertical bar, then the position and a short reason.\n"
              "Example: 4 | 3rd of James Bond\n"
              "If none fit, reply NONE.\n"
            : "\nReply with at most 5 lines. Each line starts with the list number, "
              "then a vertical bar, then a short reason.\n"
              "Example: 4 | Next in the series\n"
              "If none fit, reply NONE.\n";
        const char *recent_text =
            ((mode == 0 || mode == 3) && recent && recent[0]) ? recent : "";
        const char *hist = recent_text[0] ? hist_head : "";
        size_t need = strlen(head) + strlen(ask_for_mode(mode)) + strlen(mid) +
                      strlen(cur_title) + strlen(cur_author) + 8 +
                      strlen(hist) + strlen(recent_text) +
                      strlen(tail) + list_n + strlen(end) + 8;
        prompt = (char *)malloc(need);
        if (prompt) {
            snprintf(prompt, need,
                     "%s%s%s%s%s%s%s%s%s%s%s",
                     head, ask_for_mode(mode), mid,
                     cur_title[0] ? cur_title : "(none open)",
                     cur_author[0] ? " — " : "",
                     cur_author,
                     hist, recent_text,
                     tail, list, end);
        }
    }
    free(list);
    free(recent);
    if (!prompt) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Out of memory");
        free(cat);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }

    paint_wait("Asking");
    if (!ask_gemini(prompt, &reply, err, sizeof(err))) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%s",
                 err[0] ? err : "The AI request failed");
        free(prompt);
        free(reply);
        free(cat);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }
    free(prompt);
    apply_reply(reply, cat, count);
    reader_log("suggest ai hits %d", g_app.suggest_count);
    free(reply);
    free(cat);
    if (g_app.suggest_count == 0) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "No matching book on this reader");
    } else if (mode == 0) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Next books from your library");
    } else if (mode == 1) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Similar books on this reader");
    } else if (mode == 2) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Different books on this reader");
    } else {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "What to read next");
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
    DrawTextRect(m, ui_title_h(), sw / 2, ui_row_h(), "Back",
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
    y += row_h;
    DrawTextRect(m, y, sw - 2 * m, row_h - 8, "Suggest next",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(m, y + row_h - 1, sw - m, y + row_h - 1, fg);
    DrawTextRect(m, sh - ui_action_h(), sw - 2 * m, ui_action_h(), "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

int suggest_ask_hit(int x, int y)
{
    int sh = ui_sh();
    int row_h = ui_row_h();
    int top = ui_title_h() + ui_row_h();
    int idx;
    (void)x;
    if (y >= sh - ui_action_h()) {
        ui_show_home();
        return 1;
    }
    if (y >= ui_title_h() && y < top) {
        ui_show_home();
        return 1;
    }
    if (y < top)
        return 0;
    idx = (y - top) / row_h;
    if (idx >= 0 && idx <= 3) {
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
    DrawTextRect(m, ui_title_h(), sw / 2 - m, row_h, "Back",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(sw / 2, ui_title_h(), sw / 2 - m, row_h, "Ask again",
                 ALIGN_RIGHT | VALIGN_MIDDLE);
    DrawTextRect(m, ui_title_h() + row_h, sw - 2 * m, row_h,
                 g_app.suggest_status, ALIGN_LEFT | VALIGN_MIDDLE);

    for (i = 0; i < g_app.suggest_count; i++) {
        char line[384];
        int y = top + i * item_h;
        if (y + item_h > sh - ui_action_h())
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
    DrawTextRect(m, sh - ui_action_h(), sw - 2 * m, ui_action_h(), "Back",
                 ALIGN_CENTER | VALIGN_MIDDLE);
}

int suggest_results_hit(int x, int y)
{
    int sw = ui_sw();
    int sh = ui_sh();
    int row_h = ui_row_h();
    int item_h = row_h * 2;
    int top = ui_title_h() + row_h + row_h;
    int i;
    if (y >= sh - ui_action_h()) {
        g_app.screen = SCREEN_SUGGEST_ASK;
        ui_draw();
        return 1;
    }
    if (y >= ui_title_h() && y < ui_title_h() + row_h) {
        if (x < sw / 2) {
            g_app.screen = SCREEN_SUGGEST_ASK;
            ui_draw();
            return 1;
        }
        g_app.screen = SCREEN_SUGGEST_ASK;
        ui_draw();
        return 1;
    }
    for (i = 0; i < g_app.suggest_count; i++) {
        int yy = top + i * item_h;
        if (yy + item_h > sh - ui_action_h())
            break;
        if (y >= yy && y < yy + item_h) {
            if (g_app.suggestions[i].path[0])
                ui_open_book(g_app.suggestions[i].path);
            return 1;
        }
    }
    return 0;
}
