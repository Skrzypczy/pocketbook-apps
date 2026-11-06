#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

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
    return 1;
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

    ensure_parent_dirs();
    snprintf(tmp, sizeof(tmp), "%s.tmp", CFG_PATH);

    out = fopen(tmp, "w");
    if (!out)
        return -1;

    fprintf(out, "font_face=%s\n", cfg->font_face);
    if (cfg->last_book[0])
        fprintf(out, "last_book=%s\n", cfg->last_book);
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

    in = fopen(CFG_PATH, "r");
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, "pos.", 4) == 0)
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
    fclose(fp);
}
