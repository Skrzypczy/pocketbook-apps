#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/* Defined later; used from config_load / config_remember_book. */
void book_meta_load(void);
void book_meta_save(void);
void book_meta_prune_recent(const reader_config *cfg);

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void config_set_defaults(reader_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->font_face, sizeof(cfg->font_face), "%s", CFG_DEFAULT_FACE);
    cfg->font_size = CFG_DEFAULT_SIZE;
    cfg->margin = CFG_DEFAULT_MARGIN;
    cfg->line_spacing = CFG_DEFAULT_SPACING;
    cfg->night = 0;
    cfg->publisher_styles = 1;
    cfg->full_refresh = CFG_DEFAULT_REFRESH;
    cfg->brightness = CFG_DEFAULT_BRIGHT;
    cfg->light_on = -1;
    cfg->warmth = -1;
    cfg->warmth_valid = 0;
    cfg->last_book[0] = '\0';
}

static void apply_kv(reader_config *cfg, const char *key, const char *val)
{
    int n;
    if (strcmp(key, "font_face") == 0) {
        snprintf(cfg->font_face, sizeof(cfg->font_face), "%s", val);
        return;
    }
    if (strcmp(key, "last_book") == 0) {
        snprintf(cfg->last_book, sizeof(cfg->last_book), "%s", val);
        return;
    }
    if (strncmp(key, "recent.", 7) == 0 && val[0]) {
        int i = 0;
        if (sscanf(key + 7, "%d", &i) == 1 && i >= 0 && i < CFG_RECENT_MAX) {
            snprintf(cfg->recent[i], sizeof(cfg->recent[i]), "%s", val);
            if (i + 1 > cfg->recent_count)
                cfg->recent_count = i + 1;
        }
        return;
    }
    if (sscanf(val, "%d", &n) != 1)
        return;
    if (strcmp(key, "font_size") == 0)
        cfg->font_size = clamp_int(n, CFG_FONT_MIN, CFG_FONT_MAX);
    else if (strcmp(key, "margin") == 0)
        cfg->margin = clamp_int(n, CFG_MARGIN_MIN, CFG_MARGIN_MAX);
    else if (strcmp(key, "line_spacing") == 0)
        cfg->line_spacing = clamp_int(n, CFG_SPACING_MIN, CFG_SPACING_MAX);
    else if (strcmp(key, "night") == 0)
        cfg->night = n ? 1 : 0;
    else if (strcmp(key, "publisher_styles") == 0)
        cfg->publisher_styles = n ? 1 : 0;
    else if (strcmp(key, "full_refresh") == 0)
        cfg->full_refresh = clamp_int(n, CFG_REFRESH_MIN, CFG_REFRESH_MAX);
    else if (strcmp(key, "brightness") == 0)
        cfg->brightness = clamp_int(n, CFG_BRIGHT_MIN, CFG_BRIGHT_MAX);
    else if (strcmp(key, "light_on") == 0)
        cfg->light_on = n ? 1 : 0;
    else if (strcmp(key, "warmth") == 0) {
        cfg->warmth = clamp_int(n, CFG_WARMTH_MIN, CFG_WARMTH_MAX);
        cfg->warmth_valid = 1;
    }
}

int config_load(reader_config *cfg)
{
    FILE *fp;
    char line[ENGINE_POS_MAX + 512];

    config_set_defaults(cfg);
    fp = fopen(CFG_PATH, "r");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        char *nl;
        char *key;
        char *val;

        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;
        if (strncmp(line, "pos.", 4) == 0)
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = line;
        val = eq + 1;
        apply_kv(cfg, key, val);
    }
    fclose(fp);
    if (cfg->recent_count == 0 && cfg->last_book[0]) {
        snprintf(cfg->recent[0], sizeof(cfg->recent[0]), "%s", cfg->last_book);
        cfg->recent_count = 1;
    }
    book_meta_load();
    book_meta_prune_recent(cfg);
    return 1;
}

void config_remember_book(reader_config *cfg, const char *path)
{
    char tmp[CFG_RECENT_MAX][512];
    int n = 0;
    int i;
    if (!cfg || !path || !path[0])
        return;
    snprintf(tmp[n], sizeof(tmp[n]), "%s", path);
    n++;
    for (i = 0; i < cfg->recent_count && n < CFG_RECENT_MAX; i++) {
        if (!cfg->recent[i][0] || strcmp(cfg->recent[i], path) == 0)
            continue;
        snprintf(tmp[n], sizeof(tmp[n]), "%s", cfg->recent[i]);
        n++;
    }
    memset(cfg->recent, 0, sizeof(cfg->recent));
    memcpy(cfg->recent, tmp, sizeof(tmp[0]) * (size_t)n);
    cfg->recent_count = n;
    /* Drop cached metadata for books that fell off the recent list. */
    book_meta_prune_recent(cfg);
}

static int ensure_parent_dirs(void)
{
    mkdir("/mnt/ext1/system", 0755);
    mkdir("/mnt/ext1/system/state", 0755);
    mkdir(CFG_STATE_DIR, 0755);
    mkdir(CFG_CACHE_DIR, 0755);
    return 0;
}

int config_save(const reader_config *cfg)
{
    FILE *in;
    FILE *out;
    char tmp[sizeof(CFG_PATH) + 8];
    char line[ENGINE_POS_MAX + 512];
    int ok;
    int n;

    ensure_parent_dirs();
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_PATH);

    out = fopen(tmp, "w");
    if (!out)
        return -1;

    fprintf(out, "font_face=%s\n", cfg->font_face);
    if (cfg->last_book[0])
        fprintf(out, "last_book=%s\n", cfg->last_book);
    for (n = 0; n < cfg->recent_count && n < CFG_RECENT_MAX; n++) {
        if (cfg->recent[n][0])
            fprintf(out, "recent.%d=%s\n", n, cfg->recent[n]);
    }
    fprintf(out, "font_size=%d\n", cfg->font_size);
    fprintf(out, "margin=%d\n", cfg->margin);
    fprintf(out, "line_spacing=%d\n", cfg->line_spacing);
    fprintf(out, "night=%d\n", cfg->night);
    fprintf(out, "publisher_styles=%d\n", cfg->publisher_styles);
    fprintf(out, "full_refresh=%d\n", cfg->full_refresh);
    fprintf(out, "brightness=%d\n", cfg->brightness);
    if (cfg->light_on >= 0)
        fprintf(out, "light_on=%d\n", cfg->light_on);
    if (cfg->warmth_valid)
        fprintf(out, "warmth=%d\n", cfg->warmth);

    /* Flush the book-meta cache alongside the main config. */
    book_meta_save();

    in = fopen(CFG_PATH, "r");
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, "pos.", 4) == 0 ||
                strncmp(line, "prog.", 5) == 0 ||
                strncmp(line, "fsize.", 6) == 0 ||
                strncmp(line, "bmk.", 4) == 0)
                fputs(line, out);
        }
        fclose(in);
    }

    fflush(out);
    fsync(fileno(out));
    ok = (fclose(out) == 0);
    if (!ok) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, CFG_PATH) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int config_get_position(const char *path, char *buf, size_t buflen)
{
    FILE *fp;
    char line[ENGINE_POS_MAX + 512];
    char key[600];
    size_t keylen;

    if (!path || !buf || buflen == 0)
        return -1;
    buf[0] = '\0';
    snprintf(key, sizeof(key), "pos.%s=", path);
    keylen = strlen(key);

    fp = fopen(CFG_PATH, "r");
    if (!fp)
        return -1;
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, key, keylen) == 0) {
            snprintf(buf, buflen, "%s", line + keylen);
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return -1;
}

int config_set_position(const char *path, const char *pos)
{
    FILE *in;
    FILE *out;
    char tmp[sizeof(CFG_PATH) + 8];
    char line[ENGINE_POS_MAX + 512];
    char key[600];
    char clipped[ENGINE_POS_MAX];
    int wrote = 0;

    if (!path || !pos)
        return -1;
    if (strlen(pos) >= ENGINE_POS_MAX) {
        memcpy(clipped, pos, ENGINE_POS_MAX - 1);
        clipped[ENGINE_POS_MAX - 1] = '\0';
        pos = clipped;
    }

    ensure_parent_dirs();
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_PATH);
    snprintf(key, sizeof(key), "pos.%s=", path);

    out = fopen(tmp, "w");
    if (!out)
        return -1;

    in = fopen(CFG_PATH, "r");
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, key, strlen(key)) == 0) {
                fprintf(out, "%s%s\n", key, pos);
                wrote = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    if (!wrote)
        fprintf(out, "%s%s\n", key, pos);

    fflush(out);
    fsync(fileno(out));
    if (fclose(out) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, CFG_PATH) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

const char *config_find_sendto_dir(void)
{
    struct stat st;
    if (stat(CFG_SENDTO_DIR1, &st) == 0 && S_ISDIR(st.st_mode))
        return CFG_SENDTO_DIR1;
    if (stat(CFG_SENDTO_DIR2, &st) == 0 && S_ISDIR(st.st_mode))
        return CFG_SENDTO_DIR2;
    if (stat(CFG_SENDTO_DIR3, &st) == 0 && S_ISDIR(st.st_mode))
        return CFG_SENDTO_DIR3;
    return NULL;
}

static int rewrite_kv_line(const char *full_key, const char *val)
{
    FILE *in;
    FILE *out;
    char tmp[sizeof(CFG_PATH) + 8];
    char line[ENGINE_POS_MAX + 512];
    char prefix[700];
    int wrote = 0;
    size_t plen;

    ensure_parent_dirs();
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_PATH);
    snprintf(prefix, sizeof(prefix), "%s=", full_key);
    plen = strlen(prefix);

    out = fopen(tmp, "w");
    if (!out)
        return -1;
    in = fopen(CFG_PATH, "r");
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, prefix, plen) == 0) {
                if (val && val[0])
                    fprintf(out, "%s%s\n", prefix, val);
                wrote = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    if (!wrote && val && val[0])
        fprintf(out, "%s%s\n", prefix, val);
    fflush(out);
    fsync(fileno(out));
    if (fclose(out) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, CFG_PATH) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int config_get_book_kv(const char *prefix, const char *path, char *buf, size_t buflen)
{
    FILE *fp;
    char line[ENGINE_POS_MAX + 512];
    char key[700];
    size_t keylen;

    if (!prefix || !path || !buf || buflen == 0)
        return -1;
    buf[0] = '\0';
    snprintf(key, sizeof(key), "%s.%s=", prefix, path);
    keylen = strlen(key);
    fp = fopen(CFG_PATH, "r");
    if (!fp)
        return -1;
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, key, keylen) == 0) {
            snprintf(buf, buflen, "%s", line + keylen);
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return -1;
}

int config_set_book_kv(const char *prefix, const char *path, const char *val)
{
    char full[700];
    if (!prefix || !path)
        return -1;
    snprintf(full, sizeof(full), "%s.%s", prefix, path);
    return rewrite_kv_line(full, val);
}

int config_get_progress(const char *path, int *page, int *pages)
{
    char buf[64];
    int p = 0, t = 0;
    if (config_get_book_kv("prog", path, buf, sizeof(buf)) != 0)
        return -1;
    if (sscanf(buf, "%d/%d", &p, &t) != 2)
        return -1;
    if (page) *page = p;
    if (pages) *pages = t;
    return 0;
}

int config_set_progress(const char *path, int page, int pages)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d/%d", page, pages);
    return config_set_book_kv("prog", path, buf);
}

int config_get_book_font_size(const char *path, int *size_out)
{
    char buf[32];
    int sz;
    if (!size_out || config_get_book_kv("fsize", path, buf, sizeof(buf)) != 0)
        return -1;
    sz = atoi(buf);
    if (sz < CFG_FONT_MIN || sz > CFG_FONT_MAX)
        return -1;
    *size_out = sz;
    return 0;
}

int config_set_book_font_size(const char *path, int size)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", size);
    return config_set_book_kv("fsize", path, buf);
}

int config_bookmarks_load(const char *path, book_bookmark *out, int max)
{
    FILE *fp;
    char line[ENGINE_POS_MAX + 512];
    char keypref[600];
    int n = 0;
    size_t pref_len;

    if (!path || !out || max <= 0)
        return 0;
    snprintf(keypref, sizeof(keypref), "bmk.%s.", path);
    pref_len = strlen(keypref);
    fp = fopen(CFG_PATH, "r");
    if (!fp)
        return 0;
    while (fgets(line, sizeof(line), fp) && n < max) {
        char *nl = strchr(line, '\n');
        char *eq;
        int page = 0;
        char *p2;
        char *p3;
        if (nl) *nl = '\0';
        if (strncmp(line, keypref, pref_len) != 0)
            continue;
        eq = strchr(line + pref_len, '=');
        if (!eq)
            continue;
        /* value: page|pos|title */
        p2 = strchr(eq + 1, '|');
        if (!p2)
            continue;
        *p2 = '\0';
        page = atoi(eq + 1);
        p3 = strchr(p2 + 1, '|');
        out[n].page = page;
        out[n].pos[0] = '\0';
        out[n].title[0] = '\0';
        if (p3) {
            *p3 = '\0';
            snprintf(out[n].pos, sizeof(out[n].pos), "%s", p2 + 1);
            snprintf(out[n].title, sizeof(out[n].title), "%s", p3 + 1);
        } else {
            snprintf(out[n].pos, sizeof(out[n].pos), "%s", p2 + 1);
        }
        n++;
    }
    fclose(fp);
    return n;
}

int config_bookmark_add(const char *path, const book_bookmark *bm)
{
    book_bookmark existing[CFG_BOOKMARKS_MAX];
    int n;
    int i;
    char full[700];
    char val[ENGINE_POS_MAX + 160];

    if (!path || !bm)
        return -1;
    n = config_bookmarks_load(path, existing, CFG_BOOKMARKS_MAX);
    if (n >= CFG_BOOKMARKS_MAX) {
        /* Drop oldest (index 0): rewrite all from 1..n-1 then append. */
        for (i = 0; i < n; i++) {
            snprintf(full, sizeof(full), "bmk.%s.%d", path, i);
            rewrite_kv_line(full, NULL);
        }
        for (i = 1; i < n; i++) {
            snprintf(full, sizeof(full), "bmk.%s.%d", path, i - 1);
            snprintf(val, sizeof(val), "%d|%s|%s",
                     existing[i].page, existing[i].pos, existing[i].title);
            rewrite_kv_line(full, val);
        }
        n = CFG_BOOKMARKS_MAX - 1;
    }
    snprintf(full, sizeof(full), "bmk.%s.%d", path, n);
    snprintf(val, sizeof(val), "%d|%s|%s", bm->page, bm->pos, bm->title);
    return rewrite_kv_line(full, val);
}

/* ---- recent-sized book metadata cache (avoids repeat GetBookInfo) ---- */

static book_meta g_book_meta[CFG_RECENT_MAX];
static int g_book_meta_count;
static int g_book_meta_loaded;
static int g_book_meta_dirty;

static void book_meta_clear_all(void)
{
    memset(g_book_meta, 0, sizeof(g_book_meta));
    g_book_meta_count = 0;
}

void book_meta_load(void)
{
    FILE *fp;
    book_meta cur;
    char line[640];
    int have = 0;

    book_meta_clear_all();
    g_book_meta_loaded = 1;
    g_book_meta_dirty = 0;
    fp = fopen(CFG_BOOKMETA_PATH, "r");
    if (!fp)
        return;
    memset(&cur, 0, sizeof(cur));
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        char *eq;
        if (nl)
            *nl = '\0';
        if (line[0] == '\0') {
            if (have && cur.path[0] && g_book_meta_count < CFG_RECENT_MAX) {
                g_book_meta[g_book_meta_count++] = cur;
            }
            memset(&cur, 0, sizeof(cur));
            have = 0;
            continue;
        }
        if (line[0] == '#')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (strcmp(line, "path") == 0) {
            snprintf(cur.path, sizeof(cur.path), "%s", eq + 1);
            have = 1;
        } else if (strcmp(line, "title") == 0) {
            snprintf(cur.title, sizeof(cur.title), "%s", eq + 1);
            have = 1;
        } else if (strcmp(line, "author") == 0) {
            snprintf(cur.author, sizeof(cur.author), "%s", eq + 1);
            have = 1;
        } else if (strcmp(line, "series") == 0) {
            snprintf(cur.series, sizeof(cur.series), "%s", eq + 1);
            have = 1;
        } else if (strcmp(line, "index") == 0) {
            cur.index = atoi(eq + 1);
            have = 1;
        } else if (strcmp(line, "year") == 0) {
            cur.year = atoi(eq + 1);
            have = 1;
        }
    }
    if (have && cur.path[0] && g_book_meta_count < CFG_RECENT_MAX)
        g_book_meta[g_book_meta_count++] = cur;
    fclose(fp);
}

void book_meta_save(void)
{
    FILE *fp;
    char tmp[sizeof(CFG_BOOKMETA_PATH) + 8];
    int i;

    if (!g_book_meta_dirty)
        return;
    ensure_parent_dirs();
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_BOOKMETA_PATH);
    fp = fopen(tmp, "w");
    if (!fp)
        return;
    fprintf(fp, "# naskilla book metadata cache (max %d)\n", CFG_RECENT_MAX);
    for (i = 0; i < g_book_meta_count; i++) {
        const book_meta *m = &g_book_meta[i];
        if (!m->path[0])
            continue;
        fprintf(fp, "path=%s\n", m->path);
        fprintf(fp, "title=%s\n", m->title);
        fprintf(fp, "author=%s\n", m->author);
        fprintf(fp, "series=%s\n", m->series);
        fprintf(fp, "index=%d\n", m->index);
        fprintf(fp, "year=%d\n", m->year);
        fputc('\n', fp);
    }
    fflush(fp);
    fsync(fileno(fp));
    if (fclose(fp) != 0) {
        unlink(tmp);
        return;
    }
    if (rename(tmp, CFG_BOOKMETA_PATH) != 0) {
        unlink(tmp);
        return;
    }
    g_book_meta_dirty = 0;
}

static int book_meta_find_index(const char *path)
{
    int i;
    if (!path || !path[0])
        return -1;
    for (i = 0; i < g_book_meta_count; i++) {
        if (strcmp(g_book_meta[i].path, path) == 0)
            return i;
    }
    return -1;
}

static void book_meta_touch(int idx)
{
    book_meta tmp;
    int i;
    if (idx <= 0 || idx >= g_book_meta_count)
        return;
    tmp = g_book_meta[idx];
    for (i = idx; i > 0; i--)
        g_book_meta[i] = g_book_meta[i - 1];
    g_book_meta[0] = tmp;
    g_book_meta_dirty = 1;
}

int book_meta_get(const char *path, book_meta *out)
{
    int idx;
    if (!g_book_meta_loaded)
        book_meta_load();
    idx = book_meta_find_index(path);
    if (idx < 0 || !out)
        return 0;
    *out = g_book_meta[idx];
    book_meta_touch(idx);
    return 1;
}

void book_meta_put(const char *path, const char *title, const char *author,
                   const char *series, int index, int year)
{
    book_meta m;
    int idx;
    int i;

    if (!path || !path[0])
        return;
    if (!g_book_meta_loaded)
        book_meta_load();
    memset(&m, 0, sizeof(m));
    snprintf(m.path, sizeof(m.path), "%s", path);
    if (title)
        snprintf(m.title, sizeof(m.title), "%s", title);
    if (author)
        snprintf(m.author, sizeof(m.author), "%s", author);
    if (series)
        snprintf(m.series, sizeof(m.series), "%s", series);
    m.index = index > 0 ? index : 0;
    m.year = (year >= 1000 && year <= 2100) ? year : 0;

    idx = book_meta_find_index(path);
    if (idx >= 0) {
        g_book_meta[idx] = m;
        book_meta_touch(idx);
        return;
    }
    if (g_book_meta_count < CFG_RECENT_MAX) {
        for (i = g_book_meta_count; i > 0; i--)
            g_book_meta[i] = g_book_meta[i - 1];
        g_book_meta[0] = m;
        g_book_meta_count++;
    } else {
        for (i = CFG_RECENT_MAX - 1; i > 0; i--)
            g_book_meta[i] = g_book_meta[i - 1];
        g_book_meta[0] = m;
    }
    g_book_meta_dirty = 1;
}

void book_meta_prune_recent(const reader_config *cfg)
{
    book_meta kept[CFG_RECENT_MAX];
    int n = 0;
    int i;
    int j;

    if (!cfg)
        return;
    if (!g_book_meta_loaded)
        book_meta_load();
    for (i = 0; i < g_book_meta_count; i++) {
        int ok = 0;
        if (!g_book_meta[i].path[0])
            continue;
        for (j = 0; j < cfg->recent_count && j < CFG_RECENT_MAX; j++) {
            if (cfg->recent[j][0] &&
                strcmp(cfg->recent[j], g_book_meta[i].path) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok && cfg->last_book[0] &&
            strcmp(cfg->last_book, g_book_meta[i].path) == 0)
            ok = 1;
        if (ok && n < CFG_RECENT_MAX)
            kept[n++] = g_book_meta[i];
    }
    if (n != g_book_meta_count) {
        memset(g_book_meta, 0, sizeof(g_book_meta));
        memcpy(g_book_meta, kept, sizeof(kept[0]) * (size_t)n);
        g_book_meta_count = n;
        g_book_meta_dirty = 1;
        book_meta_save();
    }
}

void reader_log(const char *fmt, ...)
{
    FILE *fp;
    struct stat st;
    va_list ap;
    time_t now;
    struct tm tm_now;
    char ts[32];

    ensure_parent_dirs();
    if (stat(CFG_LOG_PATH, &st) == 0 && st.st_size > CFG_LOG_MAX)
        unlink(CFG_LOG_PATH);

    fp = fopen(CFG_LOG_PATH, "a");
    if (!fp)
        return;
    now = time(NULL);
    localtime_r(&now, &tm_now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
    fprintf(fp, "%s ", ts);
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
}
