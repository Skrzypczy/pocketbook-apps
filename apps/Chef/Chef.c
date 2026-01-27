#include <inkview.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
typedef enum { TAB_GENERATE = 0, TAB_FOOD = 1, TAB_CONFIG = 2, TAB_SAVED = 3 } TabView;
typedef enum { VIEW_INPUT = 0, VIEW_RECIPE = 1 } GenerateView;

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
static GenerateView generate_view = VIEW_INPUT;
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

// UI Layout
static int tab_height = 80;
static int tab_width = 0;
static int content_y = 0;
static int content_h = 0;

// Diet labels
static const char* diet_labels[] = {"Keto", "Paleo", "Anti-Inf", "Vegan", "Halal", "Low-FODMAP", "Pescatarian", "Vegetarian", "Carnivore", "Whole30"};
static const char* allergen_labels[] = {"Gluten", "Dairy", "Peanuts", "Shellfish", "Eggs", "Soy", "TreeNuts", "Fish", "Sesame", "Corn", "Sulfites", "Mustard"};
static const char* dislike_labels[] = {"Thyme", "Cilantro", "Spicy", "Mushrooms", "Olives", "BellPep", "Onions", "Garlic", "Ginger", "Coconut", "Avocado", "Tomato"};

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
    
    PostponeTimedPoweroff();
    iv_netinfo *net = NetInfo();
    if (!net || net->connected == 0) {
        NetConnect2("Chef", 1);
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
    
    // Make request
    int error = 0;
    int size = 0;
    char *response = QuickDownloadExt3(url, &size, REQUEST_TIMEOUT, NULL, request, &error);
    
    is_loading = 0;
    
    if (!response || error != 0) {
        strncpy(status_msg, "Network error. Check connection.", sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
        if (response) free(response);
        DrawUI();
        return;
    }
    
    // Extract JSON from response
    char *text_start = strstr(response, "\"text\"");
    if (text_start) {
        text_start = strchr(text_start, ':');
        if (text_start) {
            text_start = strchr(text_start, '"');
            if (text_start) {
                text_start++;
                
                // Find the actual JSON content (look for { after "text":)
                char *json_start = strchr(text_start, '{');
                if (json_start) {
                    char *json_end = strrchr(json_start, '}');
                    if (json_end) {
                        size_t json_len = json_end - json_start + 1;
                        char json_buffer[MAX_RESPONSE_LEN];
                        if (json_len < sizeof(json_buffer)) {
                            strncpy(json_buffer, json_start, json_len);
                            json_buffer[json_len] = '\0';
                            
                            if (ParseRecipeJSON(json_buffer) == 0) {
                                generate_view = VIEW_RECIPE;
                                current_step = 0;
                                strncpy(status_msg, "Recipe generated!", sizeof(status_msg) - 1);
                                status_msg[sizeof(status_msg) - 1] = '\0';
                            } else {
                                strncpy(status_msg, "Failed to parse recipe.", sizeof(status_msg) - 1);
                                status_msg[sizeof(status_msg) - 1] = '\0';
                            }
                        }
                    }
                }
            }
        }
    }
    
    if (generate_view != VIEW_RECIPE) {
        strncpy(status_msg, "Invalid response format.", sizeof(status_msg) - 1);
        status_msg[sizeof(status_msg) - 1] = '\0';
    }
    
    free(response);
    DrawUI();
}

// --- UI DRAWING ---
static void DrawTabBar() {
    int sw = ScreenWidth();
    tab_width = sw / 4;
    
    const char *tab_labels[] = {"GENERATE", "FOOD", "CONFIG", "SAVED"};
    
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
    int sh = ScreenHeight();
    
    if (generate_view == VIEW_INPUT) {
        // Input field
        SetFont(font_medium, BLACK);
        DrawRect(50, content_y + 50, sw - 100, 80, BLACK);
        
        char display_text[MAX_INGREDIENTS_LEN + 20];
        if (strlen(ingredients_buffer) == 0) {
            snprintf(display_text, sizeof(display_text), "Tap to enter ingredients...");
        } else {
            snprintf(display_text, sizeof(display_text), "%s", ingredients_buffer);
        }
        DrawString(60, content_y + 80, display_text);
        
        // Generate button
        int btn_w = sw - 200;
        int btn_h = 120;
        int btn_x = 100;
        int btn_y = content_y + 200;
        
        FillArea(btn_x, btn_y, btn_w, btn_h, BLACK);
        SetFont(font_large, WHITE);
        int text_w = StringWidth("GENERATE");
        DrawString(btn_x + (btn_w - text_w) / 2, btn_y + btn_h / 2 - 20, "GENERATE");
        
        // Status message
        SetFont(font_small, BLACK);
        DrawString(100, content_y + 400, status_msg);
        
    } else {
        // Recipe viewer
        SetFont(font_title, BLACK);
        DrawString(100, content_y + 30, current_recipe.title);
        
        // Current step
        SetFont(font_large, BLACK);
        char step_label[32];
        snprintf(step_label, sizeof(step_label), "Step %d/%d:", current_step + 1, current_recipe.step_count);
        DrawString(100, content_y + 100, step_label);
        
        // Step text (wrapped)
        SetFont(font_medium, BLACK);
        int y = content_y + 160;
        int max_w = sw - 200;
        char *text = current_recipe.steps[current_step];
        char line[256];
        int line_pos = 0;
        
        for (char *p = text; *p; p++) {
            if (line_pos >= (int)(sizeof(line) - 1) || TextRectHeight(max_w, line, 0) > 40) {
                line[line_pos] = '\0';
                DrawTextRect(100, y, max_w, 60, line, ALIGN_LEFT);
                y += 60;
                line_pos = 0;
            }
            line[line_pos++] = *p;
        }
        if (line_pos > 0) {
            line[line_pos] = '\0';
            DrawTextRect(100, y, max_w, 60, line, ALIGN_LEFT);
        }
        
        // Navigation buttons
        int btn_y = sh - 150;
        int prev_w = sw / 5;
        int next_w = sw * 3 / 5;
        int save_w = sw / 5;
        
        // Prev button
        if (current_step > 0) {
            FillArea(50, btn_y, prev_w, 80, LGRAY);
            SetFont(font_medium, BLACK);
            int text_w = StringWidth("< PREV");
            DrawString(50 + (prev_w - text_w) / 2, btn_y + 30, "< PREV");
        }
        
        // Next/Done button
        FillArea(50 + prev_w + 20, btn_y, next_w, 80, BLACK);
        SetFont(font_medium, WHITE);
        const char *next_label = (current_step < current_recipe.step_count - 1) ? "NEXT STEP >" : "DONE";
        int next_w_text = StringWidth(next_label);
        DrawString(50 + prev_w + 20 + (next_w - next_w_text) / 2, btn_y + 30, next_label);
        
        // Save button
        FillArea(50 + prev_w + next_w + 40, btn_y, save_w, 80, LGRAY);
        SetFont(font_medium, BLACK);
        int save_w_text = StringWidth("SAVE");
        DrawString(50 + prev_w + next_w + 40 + (save_w - save_w_text) / 2, btn_y + 30, "SAVE");
    }
}

static void DrawFoodTab() {
    int sw = ScreenWidth();
    int chip_w = (sw - 100) / 3;
    int chip_h = 70;
    int spacing = 20;
    int x_start = 50;
    int y = content_y + 20 + scroll_offset;
    
    SetFont(font_medium, BLACK);
    
    // Lifestyle section
    DrawString(x_start, y, "1. LIFESTYLE (Dietary Base)");
    y += 50;
    for (int i = 0; i < 10; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = x_start + col * (chip_w + spacing);
        int chip_y = y + row * (chip_h + spacing);
        DrawChip(x, chip_y, chip_w, chip_h, diet_labels[i], profile.diet_flags[i]);
    }
    y += 5 * (chip_h + spacing);
    
    // Safety section
    DrawString(x_start, y, "2. SAFETY (Strict Exclusions)");
    y += 50;
    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = x_start + col * (chip_w + spacing);
        int chip_y = y + row * (chip_h + spacing);
        DrawChip(x, chip_y, chip_w, chip_h, allergen_labels[i], profile.allergen_flags[i]);
    }
    y += 5 * (chip_h + spacing);
    
    // Taste section
    DrawString(x_start, y, "3. TASTE (The \"No Thanks\" List)");
    y += 50;
    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = x_start + col * (chip_w + spacing);
        int chip_y = y + row * (chip_h + spacing);
        DrawChip(x, chip_y, chip_w, chip_h, dislike_labels[i], profile.dislike_flags[i]);
    }
}

static void DrawConfigTab() {
    int sw = ScreenWidth();
    int y = content_y + 100;
    int center_x = sw / 2;
    
    SetFont(font_medium, BLACK);
    
    // Calories
    DrawString(100, y, "CALORIES / MEAL:");
    y += 60;
    
    int btn_w = 100;
    int btn_h = 60;
    DrawRect(center_x - 250, y, btn_w, btn_h, BLACK);
    DrawString(center_x - 220, y + 25, "-");
    
    char cal_str[32];
    snprintf(cal_str, sizeof(cal_str), "%d kcal", profile.target_calories);
    int text_w = StringWidth(cal_str);
    DrawString(center_x - text_w / 2, y + 25, cal_str);
    
    DrawRect(center_x + 150, y, btn_w, btn_h, BLACK);
    DrawString(center_x + 180, y + 25, "+");
    
    y += 120;
    
    // Time
    DrawString(100, y, "MAX PREP TIME:");
    y += 60;
    
    DrawRect(center_x - 250, y, btn_w, btn_h, BLACK);
    DrawString(center_x - 220, y + 25, "-");
    
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d mins", profile.max_time_minutes);
    text_w = StringWidth(time_str);
    DrawString(center_x - text_w / 2, y + 25, time_str);
    
    DrawRect(center_x + 150, y, btn_w, btn_h, BLACK);
    DrawString(center_x + 180, y + 25, "+");
    
    y += 120;
    
    // Complexity
    DrawString(100, y, "COMPLEXITY:");
    y += 60;
    
    const char *comp_labels[] = {"Simple", "Medium", "Pro/Lab"};
    int comp_w = 250;
    for (int i = 0; i < 3; i++) {
        int x = 100 + i * (comp_w + 20);
        if (i == profile.complexity) {
            FillArea(x, y, comp_w, btn_h, BLACK);
            SetFont(font_medium, WHITE);
        } else {
            DrawRect(x, y, comp_w, btn_h, BLACK);
            SetFont(font_medium, BLACK);
        }
        text_w = StringWidth(comp_labels[i]);
        DrawString(x + (comp_w - text_w) / 2, y + 25, comp_labels[i]);
        SetFont(font_medium, BLACK);
    }
    
    y += 120;
    
    // Language
    DrawString(100, y, "OUTPUT LANGUAGE:");
    y += 60;
    
    const char *lang_labels[] = {"English", "Polski", "Español"};
    for (int i = 0; i < 3; i++) {
        int x = 100 + i * (comp_w + 20);
        if (i == profile.lang) {
            FillArea(x, y, comp_w, btn_h, BLACK);
            SetFont(font_medium, WHITE);
        } else {
            DrawRect(x, y, comp_w, btn_h, BLACK);
            SetFont(font_medium, BLACK);
        }
        text_w = StringWidth(lang_labels[i]);
        DrawString(x + (comp_w - text_w) / 2, y + 25, lang_labels[i]);
        SetFont(font_medium, BLACK);
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
            
            // Item background
            DrawRect(50, item_y, sw - 100, 80, BLACK);
            
            // Title
            char truncated[100];
            strncpy(truncated, recipes[i].title, sizeof(truncated) - 1);
            truncated[sizeof(truncated) - 1] = '\0';
            if (strlen(recipes[i].title) > 40) {
                truncated[40] = '.';
                truncated[41] = '.';
                truncated[42] = '.';
                truncated[43] = '\0';
            }
            DrawString(70, item_y + 20, truncated);
            
            // ID/Date
            SetFont(font_small, BLACK);
            DrawString(70, item_y + 50, recipes[i].id);
            SetFont(font_medium, BLACK);
            
            // Arrow
            DrawString(sw - 120, item_y + 30, "[>]");
        }
        
        y += recipe_count * 100 + 50;
        
        // Clear all button
        if (recipe_count > 0) {
            int btn_w = 300;
            int btn_h = 70;
            int btn_x = sw / 2 - btn_w / 2;
            FillArea(btn_x, y, btn_w, btn_h, LGRAY);
            SetFont(font_medium, BLACK);
            int text_w = StringWidth("CLEAR ALL");
            DrawString(btn_x + (btn_w - text_w) / 2, y + 25, "CLEAR ALL");
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
        case TAB_FOOD:
            DrawFoodTab();
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
            font_title = OpenFont("LiberationSans-Bold", 40, 1);
            font_large = OpenFont("LiberationSans-Bold", 36, 1);
            font_medium = OpenFont("LiberationSans", 28, 0);
            font_small = OpenFont("LiberationSans", 22, 0);
            
            if (!font_title) font_title = OpenFont("LiberationSans", 40, 1);
            if (!font_large) font_large = OpenFont("LiberationSans", 36, 1);
            if (!font_medium) font_medium = OpenFont("LiberationSans", 28, 0);
            if (!font_small) font_small = OpenFont("LiberationSans", 22, 0);
            
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
                if (generate_view == VIEW_INPUT) {
                    // Input field click
                    if (y > content_y + 50 && y < content_y + 130 && x > 50 && x < sw - 50) {
                        OpenKeyboard("Ingredients", ingredients_buffer, sizeof(ingredients_buffer) - 1, KBD_NORMAL, KeyboardHandler);
                    }
                    // Generate button
                    else if (y > content_y + 200 && y < content_y + 320 && x > 100 && x < sw - 100) {
                        GenerateRecipe();
                    }
                } else {
                    // Recipe view - navigation buttons
                    int btn_y = sh - 150;
                    int prev_w = sw / 5;
                    int next_w = sw * 3 / 5;
                    int save_w = sw / 5;
                    
                    // Prev
                    if (y > btn_y && y < btn_y + 80 && x > 50 && x < 50 + prev_w && current_step > 0) {
                        current_step--;
                        DrawUI();
                    }
                    // Next/Done
                    else if (y > btn_y && y < btn_y + 80 && x > 50 + prev_w + 20 && x < 50 + prev_w + 20 + next_w) {
                        if (current_step < current_recipe.step_count - 1) {
                            current_step++;
                            DrawUI();
                        } else {
                            generate_view = VIEW_INPUT;
                            current_step = 0;
                            DrawUI();
                        }
                    }
                    // Save
                    else if (y > btn_y && y < btn_y + 80 && x > 50 + prev_w + next_w + 40 && x < sw - 50) {
                        if (recipe_count < MAX_RECIPES) {
                            memcpy(&recipes[recipe_count], &current_recipe, sizeof(SavedRecipe));
                            recipe_count++;
                            SaveCookbook();
                            strncpy(status_msg, "Recipe saved!", sizeof(status_msg) - 1);
                            status_msg[sizeof(status_msg) - 1] = '\0';
                        }
                        DrawUI();
                    }
                }
            }
            
            else if (current_tab == TAB_FOOD) {
                int chip_w = (sw - 100) / 3;
                int chip_h = 70;
                int spacing = 20;
                int x_start = 50;
                int base_y = content_y + 70 + scroll_offset;
                
                // Check diet chips
                for (int i = 0; i < 10; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int chip_x = x_start + col * (chip_w + spacing);
                    int chip_y = base_y + row * (chip_h + spacing);
                    
                    if (x >= chip_x && x <= chip_x + chip_w && y >= chip_y && y <= chip_y + chip_h) {
                        profile.diet_flags[i] = !profile.diet_flags[i];
                        SaveProfile();
                        DrawUI();
                        break;
                    }
                }
                
                base_y += 5 * (chip_h + spacing) + 50;
                
                // Check allergen chips
                for (int i = 0; i < 12; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int chip_x = x_start + col * (chip_w + spacing);
                    int chip_y = base_y + row * (chip_h + spacing);
                    
                    if (x >= chip_x && x <= chip_x + chip_w && y >= chip_y && y <= chip_y + chip_h) {
                        profile.allergen_flags[i] = !profile.allergen_flags[i];
                        SaveProfile();
                        DrawUI();
                        break;
                    }
                }
                
                base_y += 5 * (chip_h + spacing) + 50;
                
                // Check dislike chips
                for (int i = 0; i < 12; i++) {
                    int col = i % 3;
                    int row = i / 3;
                    int chip_x = x_start + col * (chip_w + spacing);
                    int chip_y = base_y + row * (chip_h + spacing);
                    
                    if (x >= chip_x && x <= chip_x + chip_w && y >= chip_y && y <= chip_y + chip_h) {
                        profile.dislike_flags[i] = !profile.dislike_flags[i];
                        SaveProfile();
                        DrawUI();
                        break;
                    }
                }
            }
            
            else if (current_tab == TAB_CONFIG) {
                int center_x = sw / 2;
                int y_offset = content_y + 160;
                int btn_w = 100;
                int btn_h = 60;
                
                // Calories +/-
                if (y >= y_offset && y <= y_offset + btn_h) {
                    if (x >= center_x - 250 && x <= center_x - 150) {
                        if (profile.target_calories > 50) profile.target_calories -= 50;
                        SaveProfile();
                        DrawUI();
                    } else if (x >= center_x + 150 && x <= center_x + 250) {
                        if (profile.target_calories < 2000) profile.target_calories += 50;
                        SaveProfile();
                        DrawUI();
                    }
                }
                
                y_offset += 180;
                
                // Time +/-
                if (y >= y_offset && y <= y_offset + btn_h) {
                    if (x >= center_x - 250 && x <= center_x - 150) {
                        if (profile.max_time_minutes > 15) profile.max_time_minutes -= 15;
                        SaveProfile();
                        DrawUI();
                    } else if (x >= center_x + 150 && x <= center_x + 250) {
                        if (profile.max_time_minutes < 180) profile.max_time_minutes += 15;
                        SaveProfile();
                        DrawUI();
                    }
                }
                
                y_offset += 180;
                
                // Complexity buttons
                if (y >= y_offset && y <= y_offset + btn_h) {
                    int comp_w = 250;
                    for (int i = 0; i < 3; i++) {
                        int btn_x = 100 + i * (comp_w + 20);
                        if (x >= btn_x && x <= btn_x + comp_w) {
                            profile.complexity = (Complexity)i;
                            SaveProfile();
                            DrawUI();
                            break;
                        }
                    }
                }
                
                y_offset += 180;
                
                // Language buttons
                if (y >= y_offset && y <= y_offset + btn_h) {
                    int comp_w = 250;
                    for (int i = 0; i < 3; i++) {
                        int btn_x = 100 + i * (comp_w + 20);
                        if (x >= btn_x && x <= btn_x + comp_w) {
                            profile.lang = (LanguageCode)i;
                            SaveProfile();
                            DrawUI();
                            break;
                        }
                    }
                }
            }
            
            else if (current_tab == TAB_SAVED) {
                int item_y_start = content_y + 130 + scroll_offset;
                
                // Recipe items
                for (int i = 0; i < recipe_count; i++) {
                    int item_y = item_y_start + i * 100;
                    if (y >= item_y && y <= item_y + 80 && x >= 50 && x < sw - 100) {
                        // Load and view recipe
                        memcpy(&current_recipe, &recipes[i], sizeof(SavedRecipe));
                        current_step = 0;
                        current_tab = TAB_GENERATE;
                        generate_view = VIEW_RECIPE;
                        DrawUI();
                        break;
                    }
                }
                
                // Clear all button
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
                if (generate_view == VIEW_RECIPE) {
                    generate_view = VIEW_INPUT;
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
            memset(api_key, 0, sizeof(api_key));
            break;
    }
    
    return 0;
}

int main() {
    InkViewMain(Handler);
    return 0;
}
