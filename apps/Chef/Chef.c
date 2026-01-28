#include <inkview.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

// --- CONFIGURATION ---
#define GEMINI_API_KEY_FILE "/mnt/ext1/.ai_api_key"
#define GEMINI_URL_BASE "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key="
#define PROFILE_FILE "/mnt/ext1/.chef_profile.json"
#define COOKBOOK_FILE "/mnt/ext1/.chef_cookbook.json"
#define MAX_INGREDIENTS_LEN 256
#define MAX_RESPONSE_LEN 8192
#define MAX_API_KEY_LEN 64
#define MAX_RECIPES 50
#define MAX_STEPS 20
#define REQUEST_TIMEOUT 30000

// --- ENUMS ---
typedef enum { LANG_EN = 0, LANG_PL = 1, LANG_ES = 2 } LanguageCode;
typedef enum { COMP_SIMPLE = 0, COMP_MEDIUM = 1, COMP_PRO = 2 } Complexity;
typedef enum { TAB_GENERATE = 0, TAB_READING = 1, TAB_CONFIG = 2, TAB_SAVED = 3 } TabView;

// --- USER PROFILE ---
typedef struct {
    int diet_flags[10];
    int allergen_flags[12];
    int dislike_flags[12];
    int target_calories;
    int max_time_minutes;
    Complexity complexity;
    LanguageCode lang;
} UserProfile;

// --- RECIPE STORAGE ---
typedef struct {
    char id[32];
    char title[128];
    char ingredients_used[256];
    char steps[MAX_STEPS][512];
    int step_count;
} SavedRecipe;

// --- STATE ---
static UserProfile profile;
static SavedRecipe recipes[MAX_RECIPES];
static int recipe_count = 0;
static SavedRecipe current_recipe;
static int current_step = 0;
static TabView current_tab = TAB_GENERATE;
static char ingredients_buffer[MAX_INGREDIENTS_LEN] = {0};
static char api_key[MAX_API_KEY_LEN] = {0};
static char status_msg[128] = "Enter ingredients...";
static int is_loading = 0;
static int api_key_loaded = 0;
static int scroll_offset = 0;

// Fonts
static ifont *font_title = NULL;
static ifont *font_large = NULL;
static ifont *font_medium = NULL;
static ifont *font_small = NULL;
static ifont *font_recipe = NULL;  // Large font for recipe text

// UI Layout
static int tab_height = 80;
static int tab_width = 0;
static int content_y = 0;
static int content_h = 0;

// Diet labels
static const char* diet_labels[] = {"Keto", "Paleo", "Anti-Inf", "Vegan", "Halal", "Low-FODMAP", "Pescatarian", "Vegetarian", "Carnivore", "Whole30"};
static const char* allergen_labels[] = {"Gluten", "Dairy", "Peanuts", "Shellfish", "Eggs", "Soy", "TreeNuts", "Fish", "Sesame", "Corn", "Sulfites", "Mustard"};
static const char* dislike_labels[] = {"Thyme", "Cilantro", "Spicy", "Mushrooms", "Olives", "BellPep", "Onions", "Garlic", "Ginger", "Coconut", "Avocado", "Tomato"};

// --- CURL RESPONSE BUFFER ---
typedef struct {
    char *data;
    size_t size;
} CurlResponse;

static size_t CurlWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    CurlResponse *resp = (CurlResponse *)userp;
    
    char *ptr = realloc(resp->data, resp->size + total_size + 1);
    if (!ptr) return 0;  // Out of memory
    
    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, total_size);
    resp->size += total_size;
    resp->data[resp->size] = '\0';
    
    return total_size;
}

// Forward declarations
static void DrawUI();
static void SaveProfile();
static void LoadProfile();
static void SaveCookbook();
static void LoadCookbook();
static int LoadApiKey();
static void GenerateRecipe();
static int ParseRecipeJSON(const char *json);
static void DrawTabBar();

// --- PROFILE PERSISTENCE ---
static void SaveProfile() {
    FILE *fp = fopen(PROFILE_FILE, "w");
    if (!fp) return;
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"diet_flags\": [");
    for (int i = 0; i < 10; i++) {
        fprintf(fp, "%d%s", profile.diet_flags[i], (i < 9) ? "," : "");
    }
    fprintf(fp, "],\n");
    
    fprintf(fp, "  \"allergen_flags\": [");
    for (int i = 0; i < 12; i++) {
        fprintf(fp, "%d%s", profile.allergen_flags[i], (i < 11) ? "," : "");
    }
    fprintf(fp, "],\n");
    
    fprintf(fp, "  \"dislike_flags\": [");
    for (int i = 0; i < 12; i++) {
        fprintf(fp, "%d%s", profile.dislike_flags[i], (i < 11) ? "," : "");
    }
    fprintf(fp, "],\n");
    
    fprintf(fp, "  \"target_calories\": %d,\n", profile.target_calories);
    fprintf(fp, "  \"max_time_minutes\": %d,\n", profile.max_time_minutes);
    fprintf(fp, "  \"complexity\": %d,\n", profile.complexity);
    fprintf(fp, "  \"lang\": %d\n", profile.lang);
    fprintf(fp, "}\n");
    
    fclose(fp);
}

static void LoadProfile() {
    // Set defaults
    memset(&profile, 0, sizeof(UserProfile));
    profile.target_calories = 650;
    profile.max_time_minutes = 45;
    profile.complexity = COMP_SIMPLE;
    profile.lang = LANG_EN;
    
    FILE *fp = fopen(PROFILE_FILE, "r");
    if (!fp) return;
    
    char line[512];
    int section = -1;
    int idx = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"diet_flags\"")) section = 0;
        else if (strstr(line, "\"allergen_flags\"")) section = 1;
        else if (strstr(line, "\"dislike_flags\"")) section = 2;
        else if (strstr(line, "\"target_calories\"")) {
            sscanf(line, " \"target_calories\": %d", &profile.target_calories);
        }
        else if (strstr(line, "\"max_time_minutes\"")) {
            sscanf(line, " \"max_time_minutes\": %d", &profile.max_time_minutes);
        }
        else if (strstr(line, "\"complexity\"")) {
            int c;
            sscanf(line, " \"complexity\": %d", &c);
            profile.complexity = (Complexity)c;
        }
        else if (strstr(line, "\"lang\"")) {
            int l;
            sscanf(line, " \"lang\": %d", &l);
            profile.lang = (LanguageCode)l;
        }
        else if (section >= 0 && strchr(line, '[')) {
            idx = 0;
            char *p = strchr(line, '[');
            if (p) {
                p++;
                while (*p && idx < (section == 0 ? 10 : 12)) {
                    int val;
                    if (sscanf(p, "%d", &val) == 1) {
                        if (section == 0) profile.diet_flags[idx] = val;
                        else if (section == 1) profile.allergen_flags[idx] = val;
                        else if (section == 2) profile.dislike_flags[idx] = val;
                        idx++;
                    }
                    p = strchr(p, ',');
                    if (!p) break;
                    p++;
                }
            }
            section = -1;
        }
    }
    
    fclose(fp);
}

// --- COOKBOOK PERSISTENCE ---
static void SaveCookbook() {
    FILE *fp = fopen(COOKBOOK_FILE, "w");
    if (!fp) return;
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"recipes\": [\n");
    
    for (int i = 0; i < recipe_count; i++) {
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"id\": \"%s\",\n", recipes[i].id);
        fprintf(fp, "      \"title\": \"");
        
        // Escape quotes in title
        for (const char *p = recipes[i].title; *p; p++) {
            if (*p == '"' || *p == '\\') fputc('\\', fp);
            fputc(*p, fp);
        }
        fprintf(fp, "\",\n");
        
        fprintf(fp, "      \"ingredients_used\": \"");
        for (const char *p = recipes[i].ingredients_used; *p; p++) {
            if (*p == '"' || *p == '\\') fputc('\\', fp);
            fputc(*p, fp);
        }
        fprintf(fp, "\",\n");
        
        fprintf(fp, "      \"steps\": [\n");
        for (int j = 0; j < recipes[i].step_count; j++) {
            fprintf(fp, "        \"");
            for (const char *p = recipes[i].steps[j]; *p; p++) {
                if (*p == '"' || *p == '\\') fputc('\\', fp);
                fputc(*p, fp);
            }
            fprintf(fp, "\"%s\n", (j < recipes[i].step_count - 1) ? "," : "");
        }
        fprintf(fp, "      ]\n");
        fprintf(fp, "    }%s\n", (i < recipe_count - 1) ? "," : "");
    }
    
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
}

static void LoadCookbook() {
    recipe_count = 0;
    memset(recipes, 0, sizeof(recipes));
    
    FILE *fp = fopen(COOKBOOK_FILE, "r");
    if (!fp) return;
    
    char buffer[MAX_RESPONSE_LEN];
    size_t read = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[read] = '\0';
    fclose(fp);
    
    // Simple JSON parsing for recipes array
    char *p = strstr(buffer, "\"recipes\"");
    if (!p) return;
    
    p = strchr(p, '[');
    if (!p) return;
    p++;
    
    while (*p && recipe_count < MAX_RECIPES) {
        // Find next recipe object
        p = strchr(p, '{');
        if (!p) break;
        p++;
        
        SavedRecipe *r = &recipes[recipe_count];
        
        // Parse id
        char *id_start = strstr(p, "\"id\"");
        if (id_start) {
            id_start = strchr(id_start, ':');
            if (id_start) {
                id_start = strchr(id_start, '"');
                if (id_start) {
                    id_start++;
                    char *id_end = strchr(id_start, '"');
                    if (id_end) {
                        size_t len = id_end - id_start;
                        if (len >= sizeof(r->id)) len = sizeof(r->id) - 1;
                        strncpy(r->id, id_start, len);
                        r->id[len] = '\0';
                    }
                }
            }
        }
        
        // Parse title
        char *title_start = strstr(p, "\"title\"");
        if (title_start) {
            title_start = strchr(title_start, ':');
            if (title_start) {
                title_start = strchr(title_start, '"');
                if (title_start) {
                    title_start++;
                    char *title_end = title_start;
                    while (*title_end && !(*title_end == '"' && title_end > title_start && *(title_end - 1) != '\\')) {
                        title_end++;
                    }
                    size_t len = title_end - title_start;
                    if (len >= sizeof(r->title)) len = sizeof(r->title) - 1;
                    strncpy(r->title, title_start, len);
                    r->title[len] = '\0';
                }
            }
        }
        
        // Parse ingredients_used
        char *ing_start = strstr(p, "\"ingredients_used\"");
        if (ing_start) {
            ing_start = strchr(ing_start, ':');
            if (ing_start) {
                ing_start = strchr(ing_start, '"');
                if (ing_start) {
                    ing_start++;
                    char *ing_end = ing_start;
                    while (*ing_end && !(*ing_end == '"' && ing_end > ing_start && *(ing_end - 1) != '\\')) {
                        ing_end++;
                    }
                    size_t len = ing_end - ing_start;
                    if (len >= sizeof(r->ingredients_used)) len = sizeof(r->ingredients_used) - 1;
                    strncpy(r->ingredients_used, ing_start, len);
                    r->ingredients_used[len] = '\0';
                }
            }
        }
        
        // Parse steps array
        char *steps_start = strstr(p, "\"steps\"");
        if (steps_start) {
            steps_start = strchr(steps_start, '[');
            if (steps_start) {
                steps_start++;
                r->step_count = 0;
                while (*steps_start && r->step_count < MAX_STEPS) {
                    steps_start = strchr(steps_start, '"');
                    if (!steps_start || *steps_start != '"') break;
                    steps_start++;
                    
                    char *step_end = steps_start;
                    while (*step_end && !(*step_end == '"' && step_end > steps_start && *(step_end - 1) != '\\')) {
                        step_end++;
                    }
                    
                    size_t len = step_end - steps_start;
                    if (len >= sizeof(r->steps[r->step_count])) len = sizeof(r->steps[r->step_count]) - 1;
                    strncpy(r->steps[r->step_count], steps_start, len);
                    r->steps[r->step_count][len] = '\0';
                    r->step_count++;
                    
                    steps_start = strchr(step_end, ',');
                    if (!steps_start) {
                        steps_start = strchr(step_end, ']');
                        break;
                    }
                    steps_start++;
                }
            }
        }
        
        if (r->title[0] != '\0' && r->step_count > 0) {
            recipe_count++;
        }
        
        // Move to next recipe
        p = strchr(p, '}');
        if (!p) break;
        p++;
    }
}

// --- API KEY LOADING ---
static int LoadApiKey() {
    FILE *fp = fopen(GEMINI_API_KEY_FILE, "r");
    if (!fp) {
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
    
    size_t len = strlen(api_key);
    while (len > 0 && (api_key[len-1] == '\n' || api_key[len-1] == '\r' || api_key[len-1] == ' ')) {
        api_key[--len] = '\0';
    }
    
    if (len < 20 || strncmp(api_key, "YOUR_", 5) == 0) {
        return -1;
    }
    
    return 0;
}

// --- RECIPE GENERATION ---
static int ParseRecipeJSON(const char *json) {
    memset(&current_recipe, 0, sizeof(SavedRecipe));
    
    // Find "title" field
    const char *title_start = strstr(json, "\"title\"");
    if (title_start) {
        title_start = strchr(title_start, ':');
        if (title_start) {
            title_start = strchr(title_start, '"');
            if (title_start) {
                title_start++;
                const char *title_end = strchr(title_start, '"');
                if (title_end) {
                    size_t len = title_end - title_start;
                    if (len >= sizeof(current_recipe.title)) len = sizeof(current_recipe.title) - 1;
                    strncpy(current_recipe.title, title_start, len);
                    current_recipe.title[len] = '\0';
                }
            }
        }
    }
    
    // Find "steps" array
    const char *steps_start = strstr(json, "\"steps\"");
    if (steps_start) {
        steps_start = strchr(steps_start, '[');
        if (steps_start) {
            steps_start++;
            current_recipe.step_count = 0;
            
            while (*steps_start && current_recipe.step_count < MAX_STEPS) {
                steps_start = strchr(steps_start, '"');
                if (!steps_start) break;
                steps_start++;
                
                const char *step_end = steps_start;
                while (*step_end && *step_end != '"') {
                    if (*step_end == '\\' && *(step_end + 1)) step_end++;
                    step_end++;
                }
                
                size_t len = step_end - steps_start;
                if (len >= sizeof(current_recipe.steps[current_recipe.step_count])) {
                    len = sizeof(current_recipe.steps[current_recipe.step_count]) - 1;
                }
                
                strncpy(current_recipe.steps[current_recipe.step_count], steps_start, len);
                current_recipe.steps[current_recipe.step_count][len] = '\0';
                current_recipe.step_count++;
                
                steps_start = strchr(step_end, ',');
                if (!steps_start) break;
                steps_start++;
            }
        }
    }
    
    // Generate ID based on timestamp
    snprintf(current_recipe.id, sizeof(current_recipe.id), "%ld", (long)time(NULL));
    
    // Copy ingredients used
    strncpy(current_recipe.ingredients_used, ingredients_buffer, sizeof(current_recipe.ingredients_used) - 1);
    current_recipe.ingredients_used[sizeof(current_recipe.ingredients_used) - 1] = '\0';
    
    return (current_recipe.title[0] != '\0' && current_recipe.step_count > 0) ? 0 : -1;
}

static void GenerateRecipe() {
    if (!api_key_loaded) {
        strncpy(status_msg, "API key not loaded. Check file.", sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        DrawUI();
        return;
    }
    
    if (strlen(ingredients_buffer) == 0) {
        strncpy(status_msg, "Please enter ingredients first.", sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        DrawUI();
        return;
    }
    
    is_loading = 1;
    strncpy(status_msg, "Generating recipe...", sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = '\0';
    DrawUI();
    
    // Prevent device sleep during network operation
    PostponeTimedPoweroff();
    
    // Ensure WiFi is connected (curl doesn't auto-connect like inkview functions)
    iv_netinfo *net = NetInfo();
    if (!net || net->connected == 0) {
        strncpy(status_msg, "Connecting to WiFi...", sizeof(status_msg) - 1);
        DrawUI();
        
        // NetConnect2 shows system WiFi dialog if needed
        int conn_result = NetConnect2(NULL, 1);  // NULL = use last network, 1 = show hourglass
        if (conn_result != 0) {
            is_loading = 0;
            strncpy(status_msg, "WiFi connection failed. Please connect manually.", sizeof(status_msg) - 1);
            status_msg[sizeof(status_msg) - 1] = '\0';
            DrawUI();
            return;
        }
        
        // Verify connection
        net = NetInfo();
        if (!net || net->connected == 0) {
            is_loading = 0;
            strncpy(status_msg, "WiFi not available. Connect and try again.", sizeof(status_msg) - 1);
            status_msg[sizeof(status_msg) - 1] = '\0';
            DrawUI();
            return;
        }
        
        strncpy(status_msg, "Generating recipe...", sizeof(status_msg) - 1);
        DrawUI();
    }
    
    // Build prompt based on profile
    char prompt[2048];
    int pos = 0;
    
    pos += snprintf(prompt + pos, sizeof(prompt) - pos, "You are a professional chef. ");
    
    // Language
    const char *lang_names[] = {"English", "Polish", "Spanish"};
    pos += snprintf(prompt + pos, sizeof(prompt) - pos, 
                    "Output recipe in %s. JSON keys in English, values in %s. ", 
                    lang_names[profile.lang], lang_names[profile.lang]);
    
    // Dietary constraints
    if (profile.diet_flags[0] || profile.diet_flags[1] || profile.diet_flags[2] || 
        profile.diet_flags[3] || profile.diet_flags[4] || profile.diet_flags[5]) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, "DIETARY: ");
        for (int i = 0; i < 10; i++) {
            if (profile.diet_flags[i]) {
                pos += snprintf(prompt + pos, sizeof(prompt) - pos, "%s ", diet_labels[i]);
            }
        }
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, ". ");
    }
    
    // Allergens
    int has_allergens = 0;
    for (int i = 0; i < 12; i++) {
        if (profile.allergen_flags[i]) {
            has_allergens = 1;
            break;
        }
    }
    if (has_allergens) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, "STRICTLY EXCLUDE (Allergens): ");
        for (int i = 0; i < 12; i++) {
            if (profile.allergen_flags[i]) {
                pos += snprintf(prompt + pos, sizeof(prompt) - pos, "%s ", allergen_labels[i]);
            }
        }
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, ". ");
    }
    
    // Dislikes
    int has_dislikes = 0;
    for (int i = 0; i < 12; i++) {
        if (profile.dislike_flags[i]) {
            has_dislikes = 1;
            break;
        }
    }
    if (has_dislikes) {
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, "PREFERENCE EXCLUDE: ");
        for (int i = 0; i < 12; i++) {
            if (profile.dislike_flags[i]) {
                pos += snprintf(prompt + pos, sizeof(prompt) - pos, "%s ", dislike_labels[i]);
            }
        }
        pos += snprintf(prompt + pos, sizeof(prompt) - pos, ". ");
    }
    
    // Parameters
    const char *complexity_names[] = {"Simple", "Medium", "Professional/Lab"};
    pos += snprintf(prompt + pos, sizeof(prompt) - pos, 
                    "Target: ~%d kcal, Max: %d min, Complexity: %s. ",
                    profile.target_calories, profile.max_time_minutes, 
                    complexity_names[profile.complexity]);
    
    // User ingredients
    pos += snprintf(prompt + pos, sizeof(prompt) - pos, 
                    "Ingredients available: %s. ", ingredients_buffer);
    
    // Output format
    pos += snprintf(prompt + pos, sizeof(prompt) - pos,
                    "OUTPUT: Raw JSON only: {\"title\": \"string\", \"steps\": [\"step1\", \"step2\"]}");
    
    // Escape prompt for JSON
    char escaped_prompt[4096];
    int esc_pos = 0;
    for (int i = 0; i < pos && esc_pos < sizeof(escaped_prompt) - 2; i++) {
        if (prompt[i] == '"' || prompt[i] == '\\') {
            escaped_prompt[esc_pos++] = '\\';
        }
        escaped_prompt[esc_pos++] = prompt[i];
    }
    escaped_prompt[esc_pos] = '\0';
    
    // Build request JSON
    char request[6144];
    snprintf(request, sizeof(request),
             "{\"contents\":[{\"parts\":[{\"text\":\"%s\"}]}]}", 
             escaped_prompt);
    
    // Build URL (Note: Gemini API requires key as query parameter)
    char url[512];
    snprintf(url, sizeof(url), "%s%s", GEMINI_URL_BASE, api_key);
    
    // Make request using curl (needed for Content-Type: application/json header)
    CURL *curl = curl_easy_init();
    if (!curl) {
        is_loading = 0;
        strncpy(status_msg, "Failed to init network", sizeof(status_msg) - 1);
        DrawUI();
        return;
    }
    
    CurlResponse resp = {0};
    resp.data = malloc(1);
    resp.data[0] = '\0';
    resp.size = 0;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, REQUEST_TIMEOUT / 1000);  // Convert ms to seconds
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Skip SSL verification on embedded device
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    is_loading = 0;
    
    if (res != CURLE_OK) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Network error: %s", curl_easy_strerror(res));
        strncpy(status_msg, err_msg, sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        free(resp.data);
        DrawUI();
        return;
    }
    
    // Check for empty response
    if (resp.size == 0) {
        strncpy(status_msg, "Empty response from server", sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        free(resp.data);
        DrawUI();
        return;
    }
    
    char *response = resp.data;
    
    // Debug: show first 100 chars of response
    // char debug[128];
    // snprintf(debug, sizeof(debug), "Resp: %.80s", response);
    // strncpy(status_msg, debug, sizeof(status_msg) - 1);
    
    // Extract JSON from response - Gemini returns: {"candidates":[{"content":{"parts":[{"text":"..."}]}}]}
    char *text_start = strstr(response, "\"text\"");
    if (text_start) {
        text_start = strchr(text_start, ':');
        if (text_start) {
            // Skip whitespace and opening quote
            text_start++;
            while (*text_start == ' ' || *text_start == '\n' || *text_start == '\r') text_start++;
            if (*text_start == '"') text_start++;
            
            // The text content may have escaped JSON or markdown code blocks
            // Look for { which starts our recipe JSON
            char *json_start = NULL;
            
            // Skip markdown code block markers if present (```json or ```)
            char *code_block = strstr(text_start, "```");
            if (code_block && code_block < text_start + 20) {
                // Skip past ```json or ```
                text_start = code_block + 3;
                if (strncmp(text_start, "json", 4) == 0) text_start += 4;
                while (*text_start == '\n' || *text_start == '\\' || *text_start == 'n') text_start++;
            }
            
            // Find opening brace - may be escaped as \n before it
            json_start = strchr(text_start, '{');
            
            if (json_start) {
                // Find matching closing brace (last } in the text section)
                char *json_end = NULL;
                char *search = json_start;
                int brace_count = 0;
                
                // Find the matching closing brace
                while (*search && *search != '\0') {
                    if (*search == '{') brace_count++;
                    else if (*search == '}') {
                        brace_count--;
                        if (brace_count == 0) {
                            json_end = search;
                            break;
                        }
                    }
                    // Stop at end of text field
                    if (*search == '"' && *(search-1) != '\\') break;
                    search++;
                }
                
                if (json_end) {
                    size_t json_len = json_end - json_start + 1;
                    char json_buffer[MAX_RESPONSE_LEN];
                    if (json_len < sizeof(json_buffer)) {
                        strncpy(json_buffer, json_start, json_len);
                        json_buffer[json_len] = '\0';
                        
                        // Unescape \\n to actual content (Gemini escapes newlines)
                        char *p = json_buffer;
                        char *w = json_buffer;
                        while (*p) {
                            if (*p == '\\' && *(p+1) == 'n') {
                                *w++ = ' ';  // Replace \n with space
                                p += 2;
                            } else if (*p == '\\' && *(p+1) == '"') {
                                *w++ = '"';  // Unescape quotes
                                p += 2;
                            } else if (*p == '\\' && *(p+1) == '\\') {
                                *w++ = '\\';  // Unescape backslash
                                p += 2;
                            } else {
                                *w++ = *p++;
                            }
                        }
                        *w = '\0';
                        
                        if (ParseRecipeJSON(json_buffer) == 0) {
                            current_tab = TAB_READING;  // Switch to Reading tab
                            current_step = 0;
                            strncpy(status_msg, "Recipe generated!", sizeof(status_msg) - 1);
                            status_msg[sizeof(status_msg) - 1] = '\0';
                        } else {
                            strncpy(status_msg, "Failed to parse recipe JSON.", sizeof(status_msg) - 1);
                            status_msg[sizeof(status_msg) - 1] = '\0';
                        }
                    }
                }
            }
        }
    }
    
    if (current_tab != TAB_READING) {
        // Show more helpful error with partial response
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Parse error. Response: %.60s...", response);
        strncpy(status_msg, err_msg, sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
    }
    
    free(response);
    DrawUI();
}

// --- UI DRAWING ---
static void DrawTabBar() {
    int sw = ScreenWidth();
    tab_width = sw / 4;
    
    const char *tab_labels[] = {"GENERATE", "READING", "CONFIG", "SAVED"};
    
    for (int i = 0; i < 4; i++) {
        int x = i * tab_width;
        int y = 0;
        
        SetFont(font_medium, BLACK);
        
        if (i == current_tab) {
            FillArea(x, y, tab_width, tab_height, BLACK);
            SetFont(font_medium, WHITE);
        } else {
            DrawRect(x, y, tab_width, tab_height, BLACK);
        }
        
        int text_w = StringWidth(tab_labels[i]);
        DrawString(x + (tab_width - text_w) / 2, y + tab_height / 2 - 15, tab_labels[i]);
    }
    
    // Close button
    SetFont(font_large, BLACK);
    DrawString(sw - 60, 15, "[X]");
}

static void DrawChip(int x, int y, int w, int h, const char *label, int active) {
    if (active) {
        FillArea(x, y, w, h, BLACK);
        SetFont(font_small, WHITE);
    } else {
        DrawRect(x, y, w, h, BLACK);
        SetFont(font_small, BLACK);
    }
    
    int text_w = StringWidth(label);
    DrawString(x + (w - text_w) / 2, y + h / 2 - 10, label);
}

static void DrawGenerateTab() {
    int sw = ScreenWidth();
    int center_x = sw / 2;
    int btn_w = 100;
    int btn_h = 50;
    int comp_w = 250;
    
    // Input field
    SetFont(font_medium, BLACK);
    DrawRect(50, content_y + 30, sw - 100, 70, BLACK);
    
    char display_text[MAX_INGREDIENTS_LEN + 20];
    if (strlen(ingredients_buffer) == 0) {
        snprintf(display_text, sizeof(display_text), "Tap to enter ingredients...");
    } else {
        snprintf(display_text, sizeof(display_text), "%s", ingredients_buffer);
    }
    DrawString(60, content_y + 55, display_text);
    
    // Generate button
    int gen_btn_w = sw - 200;
    int gen_btn_h = 100;
    int gen_btn_x = 100;
    int gen_btn_y = content_y + 120;
    
    FillArea(gen_btn_x, gen_btn_y, gen_btn_w, gen_btn_h, BLACK);
    SetFont(font_large, WHITE);
    int text_w = StringWidth("GENERATE");
    DrawString(gen_btn_x + (gen_btn_w - text_w) / 2, gen_btn_y + gen_btn_h / 2 - 18, "GENERATE");
    
    // Status message
    SetFont(font_small, BLACK);
    DrawString(100, content_y + 240, status_msg);
    
    // === RECIPE SETTINGS ===
    int y = content_y + 300;
    SetFont(font_medium, BLACK);
    
    // Calories row
    DrawString(50, y, "Calories:");
    DrawRect(center_x - 100, y - 5, btn_w, btn_h, BLACK);
    SetFont(font_medium, BLACK);
    DrawString(center_x - 70, y + 10, "-");
    
    char cal_str[32];
    snprintf(cal_str, sizeof(cal_str), "%d", profile.target_calories);
    text_w = StringWidth(cal_str);
    DrawString(center_x + 50 - text_w / 2, y + 10, cal_str);
    
    DrawRect(center_x + 150, y - 5, btn_w, btn_h, BLACK);
    DrawString(center_x + 180, y + 10, "+");
    
    y += 70;
    
    // Time row
    SetFont(font_medium, BLACK);
    DrawString(50, y, "Time (min):");
    DrawRect(center_x - 100, y - 5, btn_w, btn_h, BLACK);
    DrawString(center_x - 70, y + 10, "-");
    
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d", profile.max_time_minutes);
    text_w = StringWidth(time_str);
    DrawString(center_x + 50 - text_w / 2, y + 10, time_str);
    
    DrawRect(center_x + 150, y - 5, btn_w, btn_h, BLACK);
    DrawString(center_x + 180, y + 10, "+");
    
    y += 70;
    
    // Complexity row
    SetFont(font_medium, BLACK);
    DrawString(50, y, "Complexity:");
    const char *comp_labels[] = {"Simple", "Medium", "Pro"};
    int comp_btn_w = 180;
    for (int i = 0; i < 3; i++) {
        int x = center_x - 150 + i * (comp_btn_w + 10);
        if (i == profile.complexity) {
            FillArea(x, y - 5, comp_btn_w, btn_h, BLACK);
            SetFont(font_small, WHITE);
        } else {
            DrawRect(x, y - 5, comp_btn_w, btn_h, BLACK);
            SetFont(font_small, BLACK);
        }
        text_w = StringWidth(comp_labels[i]);
        DrawString(x + (comp_btn_w - text_w) / 2, y + 10, comp_labels[i]);
    }
    
    y += 70;
    
    // Language row
    SetFont(font_medium, BLACK);
    DrawString(50, y, "Language:");
    const char *lang_labels[] = {"EN", "PL", "ES"};
    int lang_btn_w = 120;
    for (int i = 0; i < 3; i++) {
        int x = center_x - 50 + i * (lang_btn_w + 10);
        if (i == profile.lang) {
            FillArea(x, y - 5, lang_btn_w, btn_h, BLACK);
            SetFont(font_small, WHITE);
        } else {
            DrawRect(x, y - 5, lang_btn_w, btn_h, BLACK);
            SetFont(font_small, BLACK);
        }
        text_w = StringWidth(lang_labels[i]);
        DrawString(x + (lang_btn_w - text_w) / 2, y + 10, lang_labels[i]);
    }
}

static void DrawReadingTab() {
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    
    if (current_recipe.step_count == 0) {
        // No recipe loaded
        SetFont(font_large, BLACK);
        DrawString(100, content_y + 100, "No recipe loaded.");
        SetFont(font_medium, BLACK);
        DrawString(100, content_y + 180, "Generate a recipe or load from Saved.");
        return;
    }
    
    // Recipe title
    SetFont(font_title, BLACK);
    DrawString(100, content_y + 30, current_recipe.title);
    
    // Current step indicator
    SetFont(font_large, BLACK);
    char step_label[32];
    snprintf(step_label, sizeof(step_label), "Step %d/%d:", current_step + 1, current_recipe.step_count);
    DrawString(100, content_y + 100, step_label);
    
    // Step text - use larger font (2x size)
    SetFont(font_recipe, BLACK);
    int max_w = sw - 150;
    DrawTextRect(75, content_y + 180, max_w, sh - content_y - 400, current_recipe.steps[current_step], ALIGN_LEFT);
    
    // Navigation buttons - balanced widths
    int btn_y = sh - 150;
    int margin = 40;
    int gap = 20;
    int total_w = sw - margin * 2 - gap * 2;
    int prev_w = total_w / 4;
    int next_w = total_w / 2;
    int save_w = total_w / 4;
    
    // Prev button
    if (current_step > 0) {
        FillArea(margin, btn_y, prev_w, 80, LGRAY);
        SetFont(font_medium, BLACK);
        int text_w = StringWidth("< PREV");
        DrawString(margin + (prev_w - text_w) / 2, btn_y + 30, "< PREV");
    }
    
    // Next/Done button
    int next_x = margin + prev_w + gap;
    FillArea(next_x, btn_y, next_w, 80, BLACK);
    SetFont(font_medium, WHITE);
    const char *next_label = (current_step < current_recipe.step_count - 1) ? "NEXT >" : "DONE";
    int next_w_text = StringWidth(next_label);
    DrawString(next_x + (next_w - next_w_text) / 2, btn_y + 30, next_label);
    
    // Save button
    int save_x = next_x + next_w + gap;
    FillArea(save_x, btn_y, save_w, 80, LGRAY);
    SetFont(font_medium, BLACK);
    int save_w_text = StringWidth("SAVE");
    DrawString(save_x + (save_w - save_w_text) / 2, btn_y + 30, "SAVE");
}

// Food preferences are now drawn within Config tab via scroll_offset

static void DrawConfigTab() {
    int sw = ScreenWidth();
    int y = content_y + 20 + scroll_offset;
    int chip_w = (sw - 100) / 3;
    int chip_h = 60;
    int spacing = 15;
    int x_start = 50;
    
    // === FOOD PREFERENCES ===
    SetFont(font_large, BLACK);
    DrawString(x_start, y, "FOOD PREFERENCES");
    y += 60;
    SetFont(font_medium, BLACK);
    
    // Lifestyle section
    DrawString(x_start, y, "Dietary:");
    y += 40;
    for (int i = 0; i < 10; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = x_start + col * (chip_w + spacing);
        int chip_y = y + row * (chip_h + spacing);
        DrawChip(x, chip_y, chip_w, chip_h, diet_labels[i], profile.diet_flags[i]);
    }
    y += 4 * (chip_h + spacing) + 30;
    
    // Safety section
    DrawString(x_start, y, "Allergens (Strict Exclude):");
    y += 40;
    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = x_start + col * (chip_w + spacing);
        int chip_y = y + row * (chip_h + spacing);
        DrawChip(x, chip_y, chip_w, chip_h, allergen_labels[i], profile.allergen_flags[i]);
    }
    y += 4 * (chip_h + spacing) + 30;
    
    // Taste section
    DrawString(x_start, y, "Dislikes:");
    y += 40;
    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = x_start + col * (chip_w + spacing);
        int chip_y = y + row * (chip_h + spacing);
        DrawChip(x, chip_y, chip_w, chip_h, dislike_labels[i], profile.dislike_flags[i]);
    }
}

static void DrawSavedTab() {
    int sw = ScreenWidth();
    int y = content_y + 50 + scroll_offset;
    
    SetFont(font_large, BLACK);
    char header[64];
    snprintf(header, sizeof(header), "SAVED RECIPES (%d)", recipe_count);
    DrawString(100, y, header);
    y += 80;
    
    SetFont(font_medium, BLACK);
    
    if (recipe_count == 0) {
        DrawString(100, y, "No saved recipes yet.");
        DrawString(100, y + 50, "Generate a recipe and tap SAVE.");
    } else {
        for (int i = 0; i < recipe_count; i++) {
            int item_y = y + i * 100;
            
            // Item background (leave space for X button)
            DrawRect(50, item_y, sw - 180, 80, BLACK);
            
            // Title
            char truncated[100];
            strncpy(truncated, recipes[i].title, sizeof(truncated) - 1);
            truncated[sizeof(truncated) - 1] = '\0';
            if (strlen(recipes[i].title) > 35) {
                truncated[35] = '.';
                truncated[36] = '.';
                truncated[37] = '.';
                truncated[38] = '\0';
            }
            DrawString(70, item_y + 20, truncated);
            
            // ID/Date
            SetFont(font_small, BLACK);
            DrawString(70, item_y + 50, recipes[i].id);
            SetFont(font_medium, BLACK);
            
            // X delete button
            FillArea(sw - 120, item_y, 70, 80, LGRAY);
            SetFont(font_large, BLACK);
            int x_w = StringWidth("X");
            DrawString(sw - 120 + (70 - x_w) / 2, item_y + 25, "X");
            SetFont(font_medium, BLACK);
        }
        
        y += recipe_count * 100 + 50;
        
        // Delete all button
        if (recipe_count > 0) {
            int btn_w = 300;
            int btn_h = 70;
            int btn_x = sw / 2 - btn_w / 2;
            FillArea(btn_x, y, btn_w, btn_h, LGRAY);
            SetFont(font_medium, BLACK);
            int text_w = StringWidth("DELETE ALL");
            DrawString(btn_x + (btn_w - text_w) / 2, y + 25, "DELETE ALL");
        }
    }
}

static void DrawUI() {
    ClearScreen();
    
    DrawTabBar();
    
    content_y = tab_height + 20;
    content_h = ScreenHeight() - content_y;
    
    switch (current_tab) {
        case TAB_GENERATE:
            DrawGenerateTab();
            break;
        case TAB_READING:
            DrawReadingTab();
            break;
        case TAB_CONFIG:
            DrawConfigTab();
            break;
        case TAB_SAVED:
            DrawSavedTab();
            break;
    }
    
    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
}

// --- EVENT HANDLER ---
static void KeyboardHandler(char *text) {
    if (text != NULL) {
        strncpy(ingredients_buffer, text, sizeof(ingredients_buffer) - 1);
        ingredients_buffer[sizeof(ingredients_buffer) - 1] = '\0';
    }
    DrawUI();
}

static int Handler(int type, int par1, int par2) {
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    
    switch (type) {
        case EVT_INIT:
            // Disable system panel to use full screen
            SetPanelType(0);
            
            font_title = OpenFont("LiberationSans-Bold", 40, 1);
            font_large = OpenFont("LiberationSans-Bold", 36, 1);
            font_medium = OpenFont("LiberationSans", 28, 0);
            font_small = OpenFont("LiberationSans", 22, 0);
            font_recipe = OpenFont("LiberationSans", 48, 0);  // 2x larger for recipe text
            
            if (!font_title) font_title = OpenFont("LiberationSans", 40, 1);
            if (!font_large) font_large = OpenFont("LiberationSans", 36, 1);
            if (!font_medium) font_medium = OpenFont("LiberationSans", 28, 0);
            if (!font_small) font_small = OpenFont("LiberationSans", 22, 0);
            if (!font_recipe) font_recipe = OpenFont("LiberationSans", 48, 0);
            
            LoadProfile();
            LoadCookbook();
            api_key_loaded = (LoadApiKey() == 0);
            
            if (!api_key_loaded) {
                strncpy(status_msg, "API key missing. Create file.", sizeof(status_msg) - 1);
                status_msg[sizeof(status_msg) - 1] = '\0';
            }
            break;
            
        case EVT_SHOW:
            DrawUI();
            break;
            
        case EVT_POINTERUP: {
            int x = par1;
            int y = par2;
            
            // Tab clicks
            if (y < tab_height) {
                int new_tab = x / tab_width;
                if (new_tab >= 0 && new_tab < 4) {
                    current_tab = (TabView)new_tab;
                    scroll_offset = 0;
                    DrawUI();
                }
                
                // Close button
                if (x > sw - 100 && y < 60) {
                    CloseApp();
                }
            }
            
            // Tab-specific clicks
            else if (current_tab == TAB_GENERATE) {
                int center_x = sw / 2;
                int btn_h = 50;
                
                // Input field click
                if (y > content_y + 30 && y < content_y + 100 && x > 50 && x < sw - 50) {
                    OpenKeyboard("Ingredients", ingredients_buffer, sizeof(ingredients_buffer) - 1, KBD_NORMAL, KeyboardHandler);
                }
                // Generate button
                else if (y > content_y + 120 && y < content_y + 220 && x > 100 && x < sw - 100) {
                    GenerateRecipe();
                }
                
                // Recipe settings - Calories row (y = content_y + 300)
                int y_offset = content_y + 300 - 5;
                if (y >= y_offset && y <= y_offset + btn_h) {
                    if (x >= center_x - 100 && x <= center_x) {
                        if (profile.target_calories > 50) profile.target_calories -= 50;
                        SaveProfile();
                        DrawUI();
                    } else if (x >= center_x + 150 && x <= center_x + 250) {
                        if (profile.target_calories < 2000) profile.target_calories += 50;
                        SaveProfile();
                        DrawUI();
                    }
                }
                
                // Time row (y = content_y + 370)
                y_offset = content_y + 370 - 5;
                if (y >= y_offset && y <= y_offset + btn_h) {
                    if (x >= center_x - 100 && x <= center_x) {
                        if (profile.max_time_minutes > 15) profile.max_time_minutes -= 15;
                        SaveProfile();
                        DrawUI();
                    } else if (x >= center_x + 150 && x <= center_x + 250) {
                        if (profile.max_time_minutes < 180) profile.max_time_minutes += 15;
                        SaveProfile();
                        DrawUI();
                    }
                }
                
                // Complexity row (y = content_y + 440)
                y_offset = content_y + 440 - 5;
                int comp_btn_w = 180;
                if (y >= y_offset && y <= y_offset + btn_h) {
                    for (int i = 0; i < 3; i++) {
                        int btn_x = center_x - 150 + i * (comp_btn_w + 10);
                        if (x >= btn_x && x <= btn_x + comp_btn_w) {
                            profile.complexity = (Complexity)i;
                            SaveProfile();
                            DrawUI();
                            break;
                        }
                    }
                }
                
                // Language row (y = content_y + 510)
                y_offset = content_y + 510 - 5;
                int lang_btn_w = 120;
                if (y >= y_offset && y <= y_offset + btn_h) {
                    for (int i = 0; i < 3; i++) {
                        int btn_x = center_x - 50 + i * (lang_btn_w + 10);
                        if (x >= btn_x && x <= btn_x + lang_btn_w) {
                            profile.lang = (LanguageCode)i;
                            SaveProfile();
                            DrawUI();
                            break;
                        }
                    }
                }
            }
            
            else if (current_tab == TAB_READING) {
                if (current_recipe.step_count > 0) {
                    // Navigation buttons - same layout as DrawReadingTab
                    int btn_y = sh - 150;
                    int margin = 40;
                    int gap = 20;
                    int total_w = sw - margin * 2 - gap * 2;
                    int prev_w = total_w / 4;
                    int next_w = total_w / 2;
                    int save_w = total_w / 4;
                    
                    // Prev
                    if (y > btn_y && y < btn_y + 80 && x > margin && x < margin + prev_w && current_step > 0) {
                        current_step--;
                        DrawUI();
                    }
                    // Next/Done
                    int next_x = margin + prev_w + gap;
                    if (y > btn_y && y < btn_y + 80 && x > next_x && x < next_x + next_w) {
                        if (current_step < current_recipe.step_count - 1) {
                            current_step++;
                            DrawUI();
                        } else {
                            // Done - go back to generate tab
                            current_tab = TAB_GENERATE;
                            current_step = 0;
                            DrawUI();
                        }
                    }
                    // Save
                    int save_x = next_x + next_w + gap;
                    if (y > btn_y && y < btn_y + 80 && x > save_x && x < save_x + save_w) {
                        if (recipe_count < MAX_RECIPES) {
                            memcpy(&recipes[recipe_count], &current_recipe, sizeof(SavedRecipe));
                            recipe_count++;
                            SaveCookbook();
                            // Show 2-second popup
                            Message(ICON_INFORMATION, "Saved", "Recipe saved to cookbook!", 2000);
                        }
                        DrawUI();
                    }
                }
            }
            
            else if (current_tab == TAB_CONFIG) {
                int chip_w = (sw - 100) / 3;
                int chip_h = 60;
                int spacing = 15;
                int x_start = 50;
                
                // Food preferences section - y positions matching DrawConfigTab
                int y_base = content_y + 20 + scroll_offset;
                int y_offset = y_base + 60 + 40;  // After "FOOD PREFERENCES" + "Dietary:" label
                
                // Diet chips
                for (int i = 0; i < 10; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int chip_x = x_start + col * (chip_w + spacing);
                    int chip_y = y_offset + row * (chip_h + spacing);
                    
                    if (x >= chip_x && x <= chip_x + chip_w && y >= chip_y && y <= chip_y + chip_h) {
                        profile.diet_flags[i] = !profile.diet_flags[i];
                        SaveProfile();
                        DrawUI();
                        break;
                    }
                }
                
                y_offset += 4 * (chip_h + spacing) + 30 + 40;  // chips + gap + "Allergens:" label
                
                // Allergen chips
                for (int i = 0; i < 12; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int chip_x = x_start + col * (chip_w + spacing);
                    int chip_y = y_offset + row * (chip_h + spacing);
                    
                    if (x >= chip_x && x <= chip_x + chip_w && y >= chip_y && y <= chip_y + chip_h) {
                        profile.allergen_flags[i] = !profile.allergen_flags[i];
                        SaveProfile();
                        DrawUI();
                        break;
                    }
                }
                
                y_offset += 4 * (chip_h + spacing) + 30 + 40;  // chips + gap + "Dislikes:" label
                
                // Dislike chips
                for (int i = 0; i < 12; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int chip_x = x_start + col * (chip_w + spacing);
                    int chip_y = y_offset + row * (chip_h + spacing);
                    
                    if (x >= chip_x && x <= chip_x + chip_w && y >= chip_y && y <= chip_y + chip_h) {
                        profile.dislike_flags[i] = !profile.dislike_flags[i];
                        SaveProfile();
                        DrawUI();
                        break;
                    }
                }
            }
            
            else if (current_tab == TAB_SAVED) {
                int item_y_start = content_y + 130 + scroll_offset;
                
                // Recipe items
                for (int i = 0; i < recipe_count; i++) {
                    int item_y = item_y_start + i * 100;
                    
                    // X delete button (right side)
                    if (y >= item_y && y <= item_y + 80 && x >= sw - 120 && x <= sw - 50) {
                        // Delete this recipe - shift remaining recipes up
                        for (int j = i; j < recipe_count - 1; j++) {
                            memcpy(&recipes[j], &recipes[j + 1], sizeof(SavedRecipe));
                        }
                        recipe_count--;
                        SaveCookbook();
                        DrawUI();
                        break;
                    }
                    // Recipe item (open in Reading tab)
                    else if (y >= item_y && y <= item_y + 80 && x >= 50 && x < sw - 180) {
                        memcpy(&current_recipe, &recipes[i], sizeof(SavedRecipe));
                        current_step = 0;
                        current_tab = TAB_READING;
                        DrawUI();
                        break;
                    }
                }
                
                // Delete all button
                int clear_y = item_y_start + recipe_count * 100 + 50;
                int btn_w = 300;
                int btn_h = 70;
                int btn_x = sw / 2 - btn_w / 2;
                if (y >= clear_y && y <= clear_y + btn_h && x >= btn_x && x <= btn_x + btn_w && recipe_count > 0) {
                    recipe_count = 0;
                    SaveCookbook();
                    DrawUI();
                }
            }
            break;
        }
            
        case EVT_KEYPRESS:
            if (par1 == 28) {  // Back button
                if (current_tab == TAB_READING && current_recipe.step_count > 0) {
                    // Go back to Generate tab from Reading
                    current_tab = TAB_GENERATE;
                    current_step = 0;
                    DrawUI();
                } else {
                    CloseApp();
                }
            }
            break;
            
        case EVT_EXIT:
            if (font_title) CloseFont(font_title);
            if (font_large) CloseFont(font_large);
            if (font_medium) CloseFont(font_medium);
            if (font_small) CloseFont(font_small);
            if (font_recipe) CloseFont(font_recipe);
            memset(api_key, 0, sizeof(api_key));
            break;
    }
    
    return 0;
}

int main() {
    InkViewMain(Handler);
    return 0;
}
