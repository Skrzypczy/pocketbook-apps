#include <inkview.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

// --- CONFIGURATION ---
#define GEMINI_API_KEY_FILE "/mnt/ext1/.ai_api_key"  // Store API key in separate file
#define GEMINI_URL_BASE "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key="
#define BOOKS_DB_PATH "/mnt/ext1/system/explorer-3/explorer-3.db"
#define MAX_QUERY_LEN 256
#define MAX_RESPONSE_LEN 4096
#define MAX_BOOKS_CONTEXT 8000
#define MAX_API_KEY_LEN 64
#define REQUEST_TIMEOUT 30000  // 30 seconds

// --- STATE ---
static char query_buffer[MAX_QUERY_LEN] = {0};
static char response_buffer[MAX_RESPONSE_LEN] = {0};
static char books_context[MAX_BOOKS_CONTEXT] = {0};
static char api_key[MAX_API_KEY_LEN] = {0};
static char status_msg[128] = "Enter search query";
static ifont *font_large = NULL;
static ifont *font_medium = NULL;
static ifont *font_small = NULL;
static int is_initialized = 0;
static int is_loading = 0;
static int books_loaded = 0;
static int api_key_loaded = 0;

// UI Elements
static int search_box_y = 0;
static int search_btn_x = 0;
static int search_btn_y = 0;
static int search_btn_w = 0;
static int search_btn_h = 0;
static int response_area_y = 0;
static int response_area_h = 0;

// Load API key from file (more secure than hardcoding)
static int LoadApiKey() {
    FILE *fp = fopen(GEMINI_API_KEY_FILE, "r");
    if (!fp) {
        // Try to create template file
        fp = fopen(GEMINI_API_KEY_FILE, "w");
        if (fp) {
            fprintf(fp, "YOUR_API_KEY_HERE");
            fclose(fp);
        }
        return -1;
    }
    
    if (fgets(api_key, sizeof(api_key), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    // Trim whitespace/newline
    size_t len = strlen(api_key);
    while (len > 0 && (api_key[len-1] == '\n' || api_key[len-1] == '\r' || api_key[len-1] == ' ')) {
        api_key[--len] = '\0';
    }
    
    // Validate key format (basic check)
    if (len < 20 || strncmp(api_key, "YOUR_", 5) == 0) {
        return -1;
    }
    
    return 0;
}

// Load books from PocketBook's SQLite database
static int LoadBooksFromDatabase() {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int book_count = 0;
    int context_len = 0;
    
    // Try to open the explorer database
    if (sqlite3_open_v2(BOOKS_DB_PATH, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        return -1;  // Database not found or can't open
    }
    
    // Start building context
    context_len = snprintf(books_context, sizeof(books_context), 
        "Books on device:\n");
    
    // PocketBook firmware uses books_impl table with title, authors columns
    const char *query = "SELECT title, authors FROM books_impl WHERE title IS NOT NULL AND title != '' LIMIT 500";
    
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;  // Schema not as expected
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW && context_len < MAX_BOOKS_CONTEXT - 300) {
        const char *title = (const char *)sqlite3_column_text(stmt, 0);
        const char *author = (const char *)sqlite3_column_text(stmt, 1);
        
        if (title && strlen(title) > 0) {
            int added = snprintf(books_context + context_len,
                sizeof(books_context) - context_len,
                "- \"%s\" by %s\n",
                title,
                (author && strlen(author) > 0) ? author : "Unknown");
            
            if (added > 0) {
                context_len += added;
                book_count++;
            }
        }
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    
    if (book_count > 0) {
        snprintf(status_msg, sizeof(status_msg), "Loaded %d books from device", book_count);
    }
    
    return book_count;
}

// Load books from PocketBook database
static void LoadBooksContext() {
    // Clear context
    books_context[0] = '\0';
    
    int count = LoadBooksFromDatabase();
    
    if (count <= 0) {
        snprintf(books_context, sizeof(books_context), 
            "No books found. The AI will provide general recommendations.");
        snprintf(status_msg, sizeof(status_msg), "No book database found");
    }
}

// Escape string for JSON (with proper bounds checking)
static void JsonEscape(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size < 2) {
        if (dst && dst_size > 0) dst[0] = '\0';
        return;
    }
    
    size_t j = 0;
    size_t max_j = dst_size - 2;  // Reserve space for escape char + null
    
    for (size_t i = 0; src[i] && j < max_j; i++) {
        char c = src[i];
        
        if (c == '"' || c == '\\') {
            if (j + 1 >= max_j) break;  // Need 2 chars
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            if (j + 1 >= max_j) break;
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            // Skip carriage returns
            continue;
        } else if (c == '\t') {
            dst[j++] = ' ';
        } else if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
            // Only ASCII printable characters
            dst[j++] = c;
        } else if ((unsigned char)c >= 128) {
            // Pass through UTF-8 bytes
            dst[j++] = c;
        }
        // Skip control characters < 32
    }
    dst[j] = '\0';
}

// Parse Gemini API response to extract text
static void ParseGeminiResponse(const char *json) {
    // Simple extraction - look for "text": "..."
    const char *text_start = strstr(json, "\"text\"");
    if (!text_start) {
        // Check for specific error types
        if (strstr(json, "RESOURCE_EXHAUSTED") || strstr(json, "429") || strstr(json, "quota")) {
            snprintf(response_buffer, sizeof(response_buffer), 
                "Quota exceeded. Free tier allows ~20 requests/day. Try again tomorrow.");
        } else if (strstr(json, "API_KEY_INVALID") || strstr(json, "401")) {
            snprintf(response_buffer, sizeof(response_buffer), 
                "Invalid API key. Check your key at aistudio.google.com/apikey");
        } else if (strstr(json, "NOT_FOUND") || strstr(json, "404")) {
            snprintf(response_buffer, sizeof(response_buffer), 
                "Model not found. API may have changed.");
        } else if (strstr(json, "\"error\"")) {
            snprintf(response_buffer, sizeof(response_buffer), "API Error. Check key.");
        } else {
            snprintf(response_buffer, sizeof(response_buffer), "No response from AI.");
        }
        return;
    }
    
    text_start = strchr(text_start + 6, '"');
    if (!text_start) return;
    text_start++;
    
    // Find end of string (handle escaped quotes)
    char *dst = response_buffer;
    size_t max_len = sizeof(response_buffer) - 1;
    size_t i = 0;
    
    while (*text_start && i < max_len) {
        if (*text_start == '\\' && *(text_start + 1)) {
            text_start++;
            if (*text_start == 'n') {
                dst[i++] = '\n';
            } else if (*text_start == '"') {
                dst[i++] = '"';
            } else if (*text_start == '\\') {
                dst[i++] = '\\';
            } else {
                dst[i++] = *text_start;
            }
        } else if (*text_start == '"') {
            break;
        } else {
            dst[i++] = *text_start;
        }
        text_start++;
    }
    dst[i] = '\0';
}

// Call Gemini API using PocketBook native networking
static void CallGeminiAPI(const char *user_query) {
    // Build the prompt
    char escaped_books[MAX_BOOKS_CONTEXT];
    char escaped_query[MAX_QUERY_LEN * 2];
    JsonEscape(books_context, escaped_books, sizeof(escaped_books));
    JsonEscape(user_query, escaped_query, sizeof(escaped_query));
    
    // Build JSON payload
    char *payload = malloc(MAX_BOOKS_CONTEXT + 2048);
    if (!payload) {
        snprintf(response_buffer, sizeof(response_buffer), "Error: Out of memory");
        return;
    }
    
    snprintf(payload, MAX_BOOKS_CONTEXT + 2048,
        "{"
        "\"contents\":[{"
        "\"parts\":[{"
        "\"text\":\"You are a helpful book recommendation assistant. "
        "Based on the user's library below, suggest 2-3 books that match their query. "
        "Keep response brief (under 200 words). Format: Book title by Author - brief reason.\\n\\n"
        "USER'S LIBRARY:\\n%s\\n\\n"
        "USER QUERY: %s\""
        "}]"
        "}]"
        "}", escaped_books, escaped_query);
    
    // Build URL with API key
    char url[256];
    snprintf(url, sizeof(url), "%s%s", GEMINI_URL_BASE, api_key);
    
    // Use PocketBook's native QuickDownloadExt3 for POST request
    // This integrates with power management and WiFi properly
    int response_size = 0;
    int error_code = 0;
    
    // Postpone auto-poweroff during network operation
    PostponeTimedPoweroff();
    
    // Make POST request using PocketBook API
    // QuickDownloadExt3(url, retsize, timeout, cookie, post_data, error_code)
    char *response = (char *)QuickDownloadExt3(
        url,                  // URL with API key
        &response_size,       // Response size output
        REQUEST_TIMEOUT,      // Timeout in ms
        NULL,                 // No cookies
        payload,              // POST data
        &error_code           // Error code output
    );
    
    if (response && response_size > 0) {
        ParseGeminiResponse(response);
        free(response);
    } else {
        if (error_code != 0) {
            snprintf(response_buffer, sizeof(response_buffer), 
                "Network error (code: %d). Check WiFi.", error_code);
        } else {
            snprintf(response_buffer, sizeof(response_buffer), 
                "No response from server.");
        }
    }
    
    free(payload);
}

// Forward declaration
static void Draw();

// Search task callback (runs in background)
static void DoSearch() {
    if (strlen(query_buffer) == 0) {
        snprintf(status_msg, sizeof(status_msg), "Enter a search query first");
        is_loading = 0;
        Draw();
        PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
        return;
    }
    
    snprintf(response_buffer, sizeof(response_buffer), "Searching...");
    CallGeminiAPI(query_buffer);
    
    is_loading = 0;
    snprintf(status_msg, sizeof(status_msg), "Search complete");
    Draw();
    FullUpdate();
}

// Initialize layout based on screen size
static void InitLayout() {
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    
    // Search box at top area
    search_box_y = sh / 10;
    
    // Search button
    search_btn_w = sw / 5;
    search_btn_h = sw / 12;
    search_btn_x = sw - search_btn_w - 20;
    search_btn_y = search_box_y;
    
    // Response area
    response_area_y = search_box_y + search_btn_h + 60;
    response_area_h = sh - response_area_y - 40;
    
    is_initialized = 1;
}

// Draw the UI
static void Draw() {
    if (!is_initialized) return;
    
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    
    // Clear screen
    ClearScreen();
    
    // Title
    SetFont(font_large, BLACK);
    DrawTextRect(0, 20, sw, 50, "AI Book Search", ALIGN_CENTER);
    
    // Search box area
    int search_box_w = search_btn_x - 40;
    
    // Draw search box border
    DrawRect(20, search_box_y, search_box_w, search_btn_h, BLACK);
    
    // Draw query text or placeholder
    SetFont(font_medium, BLACK);
    if (strlen(query_buffer) > 0) {
        DrawTextRect(30, search_box_y + 10, search_box_w - 20, search_btn_h - 20, 
            query_buffer, ALIGN_LEFT);
    } else {
        SetFont(font_medium, DGRAY);
        DrawTextRect(30, search_box_y + 10, search_box_w - 20, search_btn_h - 20, 
            "Tap to enter query...", ALIGN_LEFT);
    }
    
    // Draw search button
    FillArea(search_btn_x, search_btn_y, search_btn_w, search_btn_h, LGRAY);
    DrawRect(search_btn_x, search_btn_y, search_btn_w, search_btn_h, BLACK);
    SetFont(font_medium, BLACK);
    DrawTextRect(search_btn_x, search_btn_y + 10, search_btn_w, search_btn_h - 20, 
        is_loading ? "..." : "Search", ALIGN_CENTER);
    
    // Status message
    SetFont(font_small, DGRAY);
    DrawTextRect(20, search_box_y + search_btn_h + 10, sw - 40, 30, status_msg, ALIGN_LEFT);
    
    // Response area border
    DrawRect(20, response_area_y, sw - 40, response_area_h, BLACK);
    
    // Response text with word wrap
    SetFont(font_small, BLACK);
    if (strlen(response_buffer) > 0) {
        // Use DrawTextRect for wrapped text
        DrawTextRect(30, response_area_y + 10, sw - 60, response_area_h - 20, 
            response_buffer, ALIGN_LEFT | VALIGN_TOP);
    } else {
        SetFont(font_small, DGRAY);
        DrawTextRect(30, response_area_y + 10, sw - 60, response_area_h - 20, 
            "AI recommendations will appear here.\n\nExamples:\n- \"Find me a thriller\"\n- \"Something by Philip K. Dick\"\n- \"A relaxing read\"", 
            ALIGN_LEFT | VALIGN_TOP);
    }
}

// Keyboard callback
static void KeyboardHandler(char *text) {
    if (text && strlen(text) > 0) {
        strncpy(query_buffer, text, MAX_QUERY_LEN - 1);
        query_buffer[MAX_QUERY_LEN - 1] = '\0';
    }
    Draw();
    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
}

// Open on-screen keyboard
static void OpenSearchKeyboard() {
    OpenKeyboard("Enter search query", query_buffer, MAX_QUERY_LEN - 1, 0, KeyboardHandler);
}

// Event handler
static int Handler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            // Disable system panel to use full screen
            SetPanelType(0);
            
            // Open fonts
            font_large = OpenFont("LiberationSans-Bold", 42, 1);
            if (!font_large) font_large = OpenFont("LiberationSans", 42, 1);
            font_medium = OpenFont("LiberationSans", 32, 0);
            font_small = OpenFont("LiberationSans", 24, 0);
            
            // Initialize layout
            InitLayout();
            
            // Load API key first
            if (LoadApiKey() != 0) {
                snprintf(status_msg, sizeof(status_msg), "API key not found!");
                snprintf(response_buffer, sizeof(response_buffer), 
                    "Create file:\\n%s\\n\\nPaste your Gemini API key inside.\\n\\nGet key from:\\nconsole.cloud.google.com", 
                    GEMINI_API_KEY_FILE);
            } else {
                api_key_loaded = 1;
                // Load books context
                snprintf(status_msg, sizeof(status_msg), "Loading books...");
                LoadBooksContext();
                books_loaded = 1;
            }
            break;
            
        case EVT_SHOW:
            Draw();
            FullUpdate();
            break;
            
        case EVT_POINTERUP: {
            if (is_loading) break;
            
            int sw = ScreenWidth();
            int search_box_w = search_btn_x - 40;
            
            // Check if search box was tapped
            if (par1 >= 20 && par1 <= 20 + search_box_w &&
                par2 >= search_box_y && par2 <= search_box_y + search_btn_h) {
                OpenSearchKeyboard();
            }
            // Check if search button was tapped
            else if (par1 >= search_btn_x && par1 <= search_btn_x + search_btn_w &&
                     par2 >= search_btn_y && par2 <= search_btn_y + search_btn_h) {
                
                // Check if API key is loaded
                if (!api_key_loaded) {
                    snprintf(status_msg, sizeof(status_msg), "API key required!");
                    snprintf(response_buffer, sizeof(response_buffer), 
                        "Create file:\\n%s\\n\\nPaste your Gemini API key inside.", 
                        GEMINI_API_KEY_FILE);
                    Draw();
                    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
                    break;
                }
                
                // Check network connectivity first
                iv_netinfo *net = NetInfo();
                if (!net || net->connected == 0) {
                    // Try to connect
                    snprintf(status_msg, sizeof(status_msg), "Connecting to WiFi...");
                    Draw();
                    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
                    
                    int conn_result = NetConnect2("AI Search", 1);  // Show hourglass
                    if (conn_result != 0) {
                        snprintf(status_msg, sizeof(status_msg), "WiFi not available");
                        snprintf(response_buffer, sizeof(response_buffer), 
                            "Please connect to WiFi first.");
                        Draw();
                        PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
                        break;
                    }
                }
                
                is_loading = 1;
                snprintf(status_msg, sizeof(status_msg), "Searching...");
                snprintf(response_buffer, sizeof(response_buffer), "Contacting AI...");
                Draw();
                PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
                
                // Run search (blocks but updates after)
                DoSearch();
            }
            break;
        }
        
        case EVT_KEYPRESS:
            // Handle hardware back button
            if (par1 == 28) {
                CloseApp();
            }
            break;
            
        case EVT_EXIT:
            // Cleanup fonts
            if (font_large) CloseFont(font_large);
            if (font_medium) CloseFont(font_medium);
            if (font_small) CloseFont(font_small);
            font_large = font_medium = font_small = NULL;
            
            // Clear sensitive data
            memset(api_key, 0, sizeof(api_key));
            memset(query_buffer, 0, sizeof(query_buffer));
            memset(response_buffer, 0, sizeof(response_buffer));
            
            is_initialized = 0;
            books_loaded = 0;
            api_key_loaded = 0;
            break;
    }
    return 0;
}

int main(int argc, char **argv) {
    InkViewMain(Handler);
    return 0;
}
