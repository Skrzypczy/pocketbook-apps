#include "ui.h"

#include <ctype.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inkview.h"

#define GEMINI_URL_BASE \
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key="
#define MAX_CATALOG 12000
#define MAX_PAYLOAD (MAX_CATALOG + 4096)
#define MAX_RESP 8192
#define MAX_KEY 128

typedef struct {
    char *data;
    size_t size;
} curl_buf;

static size_t curl_write(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_buf *b = (curl_buf *)userdata;
    size_t n = size * nmemb;
    char *p = (char *)realloc(b->data, b->size + n + 1);
    if (!p)
        return 0;
    b->data = p;
    memcpy(b->data + b->size, ptr, n);
    b->size += n;
    b->data[b->size] = '\0';
    return n;
}

static void json_escape(const char *src, char *dst, size_t dstlen)
{
    size_t i = 0, j = 0;
    if (!src || !dst || dstlen == 0)
        return;
    for (; src[i] && j + 2 < dstlen; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 3 >= dstlen)
                break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            if (j + 3 >= dstlen)
                break;
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if ((unsigned char)c < 0x20) {
            continue;
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static int load_api_key(char *key, size_t keylen)
{
    FILE *fp;
    size_t len;
    fp = fopen(CFG_API_KEY_FILE, "r");
    if (!fp)
        return -1;
    if (!fgets(key, (int)keylen, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    len = strlen(key);
    while (len > 0 && (key[len - 1] == '\n' || key[len - 1] == '\r' || key[len - 1] == ' '))
        key[--len] = '\0';
    if (len < 20 || strncmp(key, "YOUR_", 5) == 0)
        return -1;
    return 0;
}

static void build_catalog(char *out, size_t outlen)
{
    size_t used = 0;
    int i;
    const char *sendto;
    out[0] = '\0';

    /* Prefer a fresh scan of both roots into a temporary list via sequential scans. */
    library_scan(BROWSE_LIBRARY);
    for (i = 0; i < g_app.book_count && used + 200 < outlen; i++) {
        int n = snprintf(out + used, outlen - used, "%s | %s\n",
                         g_app.books[i].title,
                         g_app.books[i].author[0] ? g_app.books[i].author : "?");
        if (n > 0)
            used += (size_t)n;
    }
    sendto = config_find_sendto_dir();
    if (sendto) {
        library_scan(BROWSE_SENDTO);
        for (i = 0; i < g_app.book_count && used + 200 < outlen; i++) {
            int n = snprintf(out + used, outlen - used, "%s | %s\n",
                             g_app.books[i].title,
                             g_app.books[i].author[0] ? g_app.books[i].author : "?");
            if (n > 0)
                used += (size_t)n;
        }
    }
}

static int ci_contains(const char *hay, const char *needle)
{
    size_t i, j;
    if (!hay || !needle || !needle[0])
        return 0;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j]; j++) {
            char a = hay[i + j];
            char b = needle[j];
            if (!a)
                return 0;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (!needle[j])
            return 1;
    }
    return 0;
}

static void match_path_for_title(suggest_hit *hit)
{
    int pass;
    hit->path[0] = '\0';
    /* Search both catalogs. */
    for (pass = 0; pass < 2; pass++) {
        int i;
        library_scan(pass == 0 ? BROWSE_LIBRARY : BROWSE_SENDTO);
        for (i = 0; i < g_app.book_count; i++) {
            if (ci_contains(g_app.books[i].title, hit->title) ||
                ci_contains(hit->title, g_app.books[i].title)) {
                snprintf(hit->path, sizeof(hit->path), "%s", g_app.books[i].path);
                if (!hit->author[0] && g_app.books[i].author[0])
                    snprintf(hit->author, sizeof(hit->author), "%s",
                             g_app.books[i].author);
                return;
            }
        }
    }
}

static void parse_suggestions(const char *text)
{
    /* Expect lines like: 1. Title by Author - reason */
    const char *p = text;
    g_app.suggest_count = 0;
    while (*p && g_app.suggest_count < 5) {
        char line[512];
        size_t n = 0;
        const char *by;
        const char *dash;
        suggest_hit *h;
        while (*p && *p != '\n' && n + 1 < sizeof(line))
            line[n++] = *p++;
        line[n] = '\0';
        if (*p == '\n')
            p++;
        /* skip empty / headers */
        if (n < 3)
            continue;
        /* strip leading number. */
        {
            char *s = line;
            while (*s && (isdigit((unsigned char)*s) || *s == '.' || *s == ')' || *s == ' '))
                s++;
            if (s != line)
                memmove(line, s, strlen(s) + 1);
        }
        h = &g_app.suggestions[g_app.suggest_count];
        memset(h, 0, sizeof(*h));
        by = strstr(line, " by ");
        dash = strstr(line, " - ");
        if (!dash)
            dash = strstr(line, " — ");
        if (by && (!dash || by < dash)) {
            size_t tlen = (size_t)(by - line);
            if (tlen >= sizeof(h->title))
                tlen = sizeof(h->title) - 1;
            memcpy(h->title, line, tlen);
            h->title[tlen] = '\0';
            {
                const char *a = by + 4;
                const char *end = dash ? dash : (line + strlen(line));
                size_t alen = (size_t)(end - a);
                if (alen >= sizeof(h->author))
                    alen = sizeof(h->author) - 1;
                memcpy(h->author, a, alen);
                h->author[alen] = '\0';
            }
            if (dash)
                snprintf(h->reason, sizeof(h->reason), "%s", dash + 3);
        } else if (dash) {
            size_t tlen = (size_t)(dash - line);
            if (tlen >= sizeof(h->title))
                tlen = sizeof(h->title) - 1;
            memcpy(h->title, line, tlen);
            h->title[tlen] = '\0';
            snprintf(h->reason, sizeof(h->reason), "%s", dash + 3);
        } else {
            snprintf(h->title, sizeof(h->title), "%s", line);
        }
        if (h->title[0]) {
            match_path_for_title(h);
            g_app.suggest_count++;
        }
    }
}

static void extract_gemini_text(const char *json)
{
    const char *t = strstr(json, "\"text\"");
    char buf[MAX_RESP];
    size_t i = 0, j = 0;
    if (!t) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "No suggestions");
        g_app.suggest_count = 0;
        return;
    }
    t = strchr(t + 6, '"');
    if (!t) {
        g_app.suggest_count = 0;
        return;
    }
    t++;
    while (*t && i + 1 < sizeof(buf)) {
        if (*t == '\\' && t[1]) {
            if (t[1] == 'n') {
                buf[i++] = '\n';
                t += 2;
                continue;
            }
            if (t[1] == '"' || t[1] == '\\') {
                buf[i++] = t[1];
                t += 2;
                continue;
            }
        }
        if (*t == '"')
            break;
        buf[i++] = *t++;
    }
    buf[i] = '\0';
    /* trim trailing junk after last suggestion */
    (void)j;
    parse_suggestions(buf);
    if (g_app.suggest_count == 0)
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Could not parse suggestions");
    else
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "%d suggestions", g_app.suggest_count);
}

void suggest_run(int mode)
{
    char key[MAX_KEY];
    char *catalog = NULL;
    char *esc_cat = NULL;
    char *payload = NULL;
    char url[384];
    const char *intent;
    CURL *curl;
    curl_buf resp = {0};
    struct curl_slist *headers = NULL;
    iv_netinfo *net;

    g_app.suggest_count = 0;
    snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Thinking...");

    if (load_api_key(key, sizeof(key)) != 0) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Missing API key in /.ai_api_key");
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }

    if (mode == 0)
        intent = "Continue a series the user may already be reading. Prefer next books in a series from the catalog.";
    else if (mode == 1)
        intent = "Suggest books with a similar theme or genre to what the user likely enjoys, using only the catalog.";
    else
        intent = "Suggest something different — a change of pace — still only from the catalog.";

    catalog = (char *)malloc(MAX_CATALOG);
    esc_cat = (char *)malloc(MAX_CATALOG * 2);
    payload = (char *)malloc(MAX_PAYLOAD);
    if (!catalog || !esc_cat || !payload) {
        free(catalog);
        free(esc_cat);
        free(payload);
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Out of memory");
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }

    ClearScreen();
    SetFont(g_app.font_msg, g_app.cfg.night ? WHITE : BLACK);
    DrawTextRect(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40, "Asking Gemini...",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    PartialUpdate(0, ScreenHeight() / 2 - 20, ScreenWidth(), 40);

    build_catalog(catalog, MAX_CATALOG);
    if (!catalog[0]) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "No books on device");
        free(catalog);
        free(esc_cat);
        free(payload);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }
    json_escape(catalog, esc_cat, MAX_CATALOG * 2);

    snprintf(payload, MAX_PAYLOAD,
             "{"
             "\"contents\":[{"
             "\"parts\":[{"
             "\"text\":\"You recommend books ONLY from AVAILABLE TITLES below. "
             "Return exactly 3 lines. Format each line as: "
             "Title by Author - short reason. No other text.\\n\\n"
             "GOAL: %s\\n\\n"
             "AVAILABLE TITLES:\\n%s\""
             "}]"
             "}]"
             "}",
             intent, esc_cat);

    snprintf(url, sizeof(url), "%s%s", GEMINI_URL_BASE, key);
    PostponeTimedPoweroff();

    net = NetInfo();
    if (!net || net->connected == 0)
        NetConnect2(NULL, 1);

    curl = curl_easy_init();
    if (!curl) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Network init failed");
        free(catalog);
        free(esc_cat);
        free(payload);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
    }
    resp.data = (char *)malloc(1);
    resp.data[0] = '\0';
    resp.size = 0;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (curl_easy_perform(curl) != CURLE_OK) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Network error");
    } else if (resp.size > 0) {
        extract_gemini_text(resp.data);
    } else {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status), "Empty response");
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    free(catalog);
    free(esc_cat);
    free(payload);

    g_app.screen = SCREEN_SUGGEST_RESULTS;
    ui_draw();
}

void suggest_ask_draw(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int row_h = 100;
    int top = 100;
    int y;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 16, sw, 40, "What do you want next?",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(16, 56, 120, 36, "Home", ALIGN_LEFT | VALIGN_MIDDLE);

    y = top;
    DrawTextRect(32, y, sw - 64, row_h - 8, "Continue a series",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
    y += row_h;
    DrawTextRect(32, y, sw - 64, row_h - 8, "Same theme",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
    y += row_h;
    DrawTextRect(32, y, sw - 64, row_h - 8, "Something different",
                 ALIGN_LEFT | VALIGN_MIDDLE);
    DrawLine(32, y + row_h - 1, sw - 32, y + row_h - 1, fg);
    (void)sh;
}

int suggest_ask_hit(int x, int y)
{
    int row_h = 100;
    int top = 100;
    int idx;
    (void)x;
    if (y >= 56 && y < 96) {
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
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int fg = g_app.cfg.night ? WHITE : BLACK;
    int bg = g_app.cfg.night ? BLACK : WHITE;
    int row_h = 110;
    int top = 96;
    int i;

    ClearScreen();
    FillArea(0, 0, sw, sh, bg);
    SetFont(g_app.font_ui, fg);
    DrawTextRect(0, 8, sw, 36, "Suggestions", ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(16, 48, 120, 40, "Home", ALIGN_LEFT | VALIGN_MIDDLE);
    DrawTextRect(sw / 2, 48, sw / 2 - 16, 40, "Ask again",
                 ALIGN_RIGHT | VALIGN_MIDDLE);
    DrawTextRect(24, 88, sw - 48, 28, g_app.suggest_status,
                 ALIGN_LEFT | VALIGN_MIDDLE);

    for (i = 0; i < g_app.suggest_count; i++) {
        char line[384];
        int y = top + i * row_h;
        if (y + row_h > sh)
            break;
        if (g_app.suggestions[i].author[0])
            snprintf(line, sizeof(line), "%s — %s",
                     g_app.suggestions[i].title, g_app.suggestions[i].author);
        else
            snprintf(line, sizeof(line), "%s", g_app.suggestions[i].title);
        DrawTextRect(24, y, sw - 48, 40, line, ALIGN_LEFT | VALIGN_MIDDLE);
        if (g_app.suggestions[i].reason[0])
            DrawTextRect(24, y + 40, sw - 48, 36, g_app.suggestions[i].reason,
                         ALIGN_LEFT | VALIGN_MIDDLE);
        if (!g_app.suggestions[i].path[0])
            DrawTextRect(24, y + 72, sw - 48, 28, "(not matched on device)",
                         ALIGN_LEFT | VALIGN_MIDDLE);
        DrawLine(24, y + row_h - 1, sw - 24, y + row_h - 1, fg);
    }
}

int suggest_results_hit(int x, int y)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    int row_h = 110;
    int top = 96;
    int i;
    if (y >= 48 && y < 96) {
        if (x < sw / 2) {
            ui_show_home();
            return 1;
        }
        g_app.screen = SCREEN_SUGGEST_ASK;
        ui_draw();
        return 1;
    }
    for (i = 0; i < g_app.suggest_count; i++) {
        int yy = top + i * row_h;
        if (yy + row_h > sh)
            break;
        if (y >= yy && y < yy + row_h) {
            if (g_app.suggestions[i].path[0])
                ui_open_book(g_app.suggestions[i].path);
            return 1;
        }
    }
    return 0;
}
