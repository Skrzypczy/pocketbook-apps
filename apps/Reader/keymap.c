#define _POSIX_C_SOURCE 200809L
#include "keymap.h"
#include "config.h"
#include "engine.h"

#include <stdlib.h>
#include <string.h>

#include "inkview.h"

#define KEYMAP_SIZE 32

static char *g_short[KEYMAP_SIZE];
static char *g_long[KEYMAP_SIZE];
static int g_loaded;

static void free_table(char *table[KEYMAP_SIZE])
{
    int i;
    for (i = 0; i < KEYMAP_SIZE; i++) {
        free(table[i]);
        table[i] = NULL;
    }
}

void keymap_unload(void)
{
    free_table(g_short);
    free_table(g_long);
    g_loaded = 0;
}

static keymap_action parse_action(const char *s)
{
    if (!s || !s[0])
        return KA_NONE;
    if (strcmp(s, "@KA_next") == 0)
        return KA_NEXT;
    if (strcmp(s, "@KA_prev") == 0)
        return KA_PREV;
    if (strcmp(s, "@KA_nx10") == 0)
        return KA_NEXT10;
    if (strcmp(s, "@KA_pr10") == 0)
        return KA_PREV10;
    if (strcmp(s, "@KA_frst") == 0)
        return KA_FIRST;
    if (strcmp(s, "@KA_last") == 0)
        return KA_LAST;
    if (strcmp(s, "@KA_cnts") == 0)
        return KA_CONTENTS;
    return KA_NONE;
}

static void copy_slot(char **dst, const char *src)
{
    free(*dst);
    *dst = NULL;
    if (src && src[0])
        *dst = strdup(src);
}

void keymap_reload(void)
{
    const char *act0[KEYMAP_SIZE];
    const char *act1[KEYMAP_SIZE];
    const reader_engine *eng;
    int what = KEYMAPPING_TXT;
    int i;

    eng = engine_active();
    if (eng && eng->name && strcmp(eng->name(), "mupdf") == 0)
        what = KEYMAPPING_PDF;

    memset(act0, 0, sizeof(act0));
    memset(act1, 0, sizeof(act1));
    GetKeyMappingEx(what, act0, act1, KEYMAP_SIZE);

    for (i = 0; i < KEYMAP_SIZE; i++) {
        copy_slot(&g_short[i], act0[i]);
        copy_slot(&g_long[i], act1[i]);
        if (g_short[i] || g_long[i])
            reader_log("keymap: key 0x%x short=%s long=%s", i,
                       g_short[i] ? g_short[i] : "(null)",
                       g_long[i] ? g_long[i] : "(null)");
    }
    g_loaded = 1;
    reader_log("keymap: loaded what=%d (%s)", what,
               what == KEYMAPPING_PDF ? "PDF" : "TXT");
}

keymap_action keymap_lookup(int key, int is_long)
{
    const char *s;

    if (key < 0 || key >= KEYMAP_SIZE) {
        if (!g_loaded && !is_long) {
            if (key == KEY_NEXT)
                return KA_NEXT;
            if (key == KEY_PREV)
                return KA_PREV;
        }
        return KA_NONE;
    }

    if (!g_loaded) {
        if (is_long)
            return KA_NONE;
        if (key == KEY_NEXT)
            return KA_NEXT;
        if (key == KEY_PREV)
            return KA_PREV;
        return KA_NONE;
    }

    s = is_long ? g_long[key] : g_short[key];
    if (s == NULL) {
        if (!is_long) {
            if (key == KEY_NEXT)
                return KA_NEXT;
            if (key == KEY_PREV)
                return KA_PREV;
        }
        return KA_NONE;
    }
    return parse_action(s);
}

int keymap_has_long(int key)
{
    if (key < 0 || key >= KEYMAP_SIZE || !g_loaded)
        return 0;
    if (!g_long[key])
        return 0;
    return parse_action(g_long[key]) != KA_NONE;
}

int keymap_action_repeatable(keymap_action act)
{
    return act == KA_NEXT || act == KA_PREV ||
           act == KA_NEXT10 || act == KA_PREV10;
}
