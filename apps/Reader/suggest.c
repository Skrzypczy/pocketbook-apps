#include "ui.h"
#include "layout.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include "inkview.h"

#define GEMINI_URL_BASE "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key="

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

static void add_suggestion(const char *path, const char *reason)
{
    suggest_hit *h;
    char base[256];
    const char *slash;
    const char *dot;
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
    book_names_from_path(path, h->title, sizeof(h->title),
                         h->author, sizeof(h->author), NULL, 0, NULL);
    if (!h->title[0]) {
        slash = strrchr(path, '/');
        slash = slash ? slash + 1 : path;
        dot = strrchr(slash, '.');
        n = dot && dot > slash ? (size_t)(dot - slash) : strlen(slash);
        if (n >= sizeof(base))
            n = sizeof(base) - 1;
        memcpy(base, slash, n);
        base[n] = '\0';
        snprintf(h->title, sizeof(h->title), "%s", base);
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
                         e->author, sizeof(e->author), NULL, 0, NULL);
    if (!e->title[0]) {
        const char *slash = strrchr(path, '/');
        snprintf(e->title, sizeof(e->title), "%s", slash ? slash + 1 : path);
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
    /* Close the text, the part, the parts list, and the content item.
     * The outer object stays open so generationConfig is inside it. */
    const char *tail =
        "\"}]}],\"generationConfig\":{\"thinkingConfig\":{\"thinkingBudget\":0}}}";
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
            add_suggestion(cat[idx - 1].path, reason);
        }
        if (!eol)
            break;
        p = eol + 1;
    }
}

static const char *ask_for_mode(int mode)
{
    if (mode == 0)
        return "Continue the series. Pick the next books in that series.";
    if (mode == 1)
        return "Suggest books with a similar theme or the same kind of story.";
    return "Suggest something different from the current book.";
}

void suggest_run(int mode)
{
    const char *path = current_book_path();
    cat_entry *cat = NULL;
    int count = 0;
    char cur_title[160];
    char cur_author[80];
    char *list = NULL;
    char *prompt = NULL;
    char *reply = NULL;
    char err[128];
    size_t list_n = 0;
    size_t list_cap = 0;
    int i;

    g_app.suggest_count = 0;
    err[0] = '\0';
    cur_title[0] = cur_author[0] = '\0';
    paint_wait("Reading file names");
    if (path)
        book_names_from_path(path, cur_title, sizeof(cur_title),
                             cur_author, sizeof(cur_author), NULL, 0, NULL);
    cat_build(&cat, &count, path);
    reader_log("suggest catalog %d books", count);
    if (count == 0) {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "No books found on this reader");
        free(cat);
        g_app.screen = SCREEN_SUGGEST_RESULTS;
        ui_draw();
        return;
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
        char row[360];
        int wrote;
        if (cat[i].author[0])
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

    {
        const char *head =
            "You recommend books that are already on this e-reader. "
            "Choose only from the numbered list. Do not invent titles.\n\n"
            "Request: ";
        const char *mid = "\nCurrent book: ";
        const char *tail =
            "\n\nBooks on this reader:\n";
        const char *end =
            "\nReply with at most 5 lines. Each line starts with the list number, "
            "then a vertical bar, then a short reason.\n"
            "Example: 4 | Next in the series\n"
            "If none fit, reply NONE.\n";
        size_t need = strlen(head) + strlen(ask_for_mode(mode)) + strlen(mid) +
                      strlen(cur_title) + strlen(cur_author) + 8 + strlen(tail) +
                      list_n + strlen(end) + 8;
        prompt = (char *)malloc(need);
        if (prompt) {
            snprintf(prompt, need,
                     "%s%s%s%s%s%s%s%s%s",
                     head, ask_for_mode(mode), mid,
                     cur_title[0] ? cur_title : "(none open)",
                     cur_author[0] ? " — " : "",
                     cur_author,
                     tail, list, end);
        }
    }
    free(list);
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
    } else {
        snprintf(g_app.suggest_status, sizeof(g_app.suggest_status),
                 "Different books on this reader");
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
