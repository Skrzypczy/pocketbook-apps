#include "engine.h"

#include <ctype.h>
#include <string.h>

/* Adapters register themselves. */
extern const reader_engine engine_crengine;
extern const reader_engine engine_mupdf;

static const reader_engine *g_active;

static const char *ext_of(const char *path)
{
    const char *dot;
    const char *slash;
    if (!path)
        return NULL;
    slash = strrchr(path, '/');
    dot = strrchr(path, '.');
    if (!dot || (slash && dot < slash))
        return NULL;
    return dot + 1;
}

static int eq_ext(const char *ext, const char *want)
{
    size_t i;
    if (!ext || !want)
        return 0;
    for (i = 0; want[i]; i++) {
        char a = (char)tolower((unsigned char)ext[i]);
        char b = (char)tolower((unsigned char)want[i]);
        if (a != b)
            return 0;
    }
    return ext[i] == '\0';
}

static int is_fb2_zip(const char *path)
{
    const char *p = path;
    size_t n;
    if (!path)
        return 0;
    n = strlen(path);
    if (n < 8)
        return 0;
    p = path + n - 8;
    return (tolower((unsigned char)p[0]) == 'f' &&
            tolower((unsigned char)p[1]) == 'b' &&
            tolower((unsigned char)p[2]) == '2' &&
            p[3] == '.' &&
            tolower((unsigned char)p[4]) == 'z' &&
            tolower((unsigned char)p[5]) == 'i' &&
            tolower((unsigned char)p[6]) == 'p');
}

const reader_engine *engine_for_path(const char *path)
{
    const char *ext = ext_of(path);
    if (is_fb2_zip(path))
        return &engine_crengine;
    if (!ext)
        return NULL;
    if (eq_ext(ext, "pdf"))
        return &engine_mupdf;
    if (eq_ext(ext, "epub") || eq_ext(ext, "fb2") || eq_ext(ext, "txt") ||
        eq_ext(ext, "html") || eq_ext(ext, "htm") || eq_ext(ext, "rtf"))
        return &engine_crengine;
    return NULL;
}

int engine_supports_extension(const char *path)
{
    return engine_for_path(path) != NULL;
}

int engine_open_path(const char *path, int width, int height)
{
    const reader_engine *eng = engine_for_path(path);
    int rc;
    if (!eng)
        return -1;
    if (g_active)
        g_active->close();
    g_active = NULL;
    rc = eng->open(path, width, height);
    if (rc != 0)
        return rc;
    g_active = eng;
    return 0;
}

void engine_close(void)
{
    if (g_active) {
        g_active->close();
        g_active = NULL;
    }
}

const reader_engine *engine_active(void)
{
    return g_active;
}
