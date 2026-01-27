#include <inkview.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

// --- CONFIGURATION ---
#define TARGET_FOLDER_VISIBLE "/mnt/ext1/Private"
#define TARGET_FOLDER_HIDDEN  "/mnt/ext1/.Private"
#define PIN_FILE_PATH "/mnt/ext1/.vault_pin"  // Hidden PIN storage file
#define MIN_PIN_LENGTH 4
#define MAX_PIN_LENGTH 8
#define ENCRYPTION_ENABLED 1  // Set to 0 to disable encryption
#define ENCRYPTED_EXT ".vlt"  // Extension for encrypted files
#define CHUNK_SIZE 4096       // Process files in chunks for memory efficiency

// --- SCREEN STATES ---
typedef enum {
    SCREEN_MAIN,           // Normal PIN entry for lock/unlock
    SCREEN_SETUP_NEW,      // First time: enter new PIN
    SCREEN_SETUP_CONFIRM,  // First time: confirm new PIN
    SCREEN_CHANGE_OLD,     // Change: enter current PIN
    SCREEN_CHANGE_NEW,     // Change: enter new PIN
    SCREEN_CHANGE_CONFIRM  // Change: confirm new PIN
} ScreenState;

// --- STATE ---
static char input_buffer[MAX_PIN_LENGTH + 1] = {0};
static char stored_pin[MAX_PIN_LENGTH + 1] = {0};     // PIN loaded from file
static char temp_new_pin[MAX_PIN_LENGTH + 1] = {0};   // Temporary for confirmation
static char status_msg[64] = "Enter PIN to Unlock";
static ifont *font_large = NULL;
static ifont *font_small = NULL;
static int is_initialized = 0;
static int files_processed = 0;
static ScreenState current_screen = SCREEN_MAIN;
static int settings_btn_x = 0;
static int settings_btn_y = 0;
static int settings_btn_size = 0;
static int close_btn_x = 0;
static int close_btn_y = 0;
static int close_btn_size = 0;
static int show_reset_confirm = 0;  // Flag for reset confirmation dialog

// Encryption key derived from PIN (expanded to 256 bytes)
static unsigned char encryption_key[256];

// Button Layout
typedef struct {
    int x, y, w, h;
    char label[4];  // Static buffer for label (max "OK" + null)
    int value; // -1 for DEL, -2 for OK, 0-9 for numbers
} Button;

#define BTN_COUNT 12
static Button buttons[BTN_COUNT];
static const char* num_labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK"};
static const int num_values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, -1, 0, -2};

// --- PIN STORAGE FUNCTIONS ---

// Secure hash for PIN storage (djb2 + salt mixing)
static void HashPin(const char *pin, const char *salt, char *hash_out) {
    unsigned int hash1 = 5381;
    unsigned int hash2 = 0x12345678;
    int c;
    const char *p = pin;
    
    // First pass with djb2
    while ((c = *p++)) {
        hash1 = ((hash1 << 5) + hash1) + c;
    }
    
    // Mix with salt
    p = salt;
    while ((c = *p++)) {
        hash2 = ((hash2 << 5) + hash2) ^ c;
        hash1 ^= (hash2 >> 3);
    }
    
    // Second pass for more diffusion
    p = pin;
    while ((c = *p++)) {
        hash2 = ((hash2 << 7) + hash2) ^ c;
    }
    
    // Convert to hex string (32 chars)
    snprintf(hash_out, 33, "%08X%08X%08X%08X", 
             hash1, hash2, hash1 ^ hash2, hash1 + hash2);
}

// Generate random salt with improved entropy
static void GenerateSalt(char *salt_out, size_t len) {
    // Use multiple sources for entropy
    unsigned int seed = (unsigned int)time(NULL);
    seed ^= (unsigned int)getpid();
    seed ^= (unsigned int)(size_t)salt_out;  // Stack address as entropy
    seed ^= (unsigned int)clock();           // CPU ticks
    
    // Additional mixing
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        unsigned int extra;
        if (fread(&extra, sizeof(extra), 1, urandom) == 1) {
            seed ^= extra;
        }
        fclose(urandom);
    }
    
    srand(seed);
    
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i < len - 1; i++) {
        salt_out[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    salt_out[len - 1] = '\0';
}

// Stored salt and hash for verification (must be before SavePin)
static char stored_salt[17] = {0};
static char stored_hash[33] = {0};

// Save PIN hash to file (PIN is NOT stored, only hash + salt)
static int SavePin(const char *pin) {
    FILE *fp = fopen(PIN_FILE_PATH, "w");
    if (!fp) return -1;
    
    char salt[17] = {0};
    char hash[33] = {0};
    
    GenerateSalt(salt, sizeof(salt));
    HashPin(pin, salt, hash);
    
    // Store only salt and hash - PIN is NEVER stored
    fprintf(fp, "%s\n%s", salt, hash);
    fclose(fp);
    
    // Copy salt and hash to memory for immediate verification
    strncpy(stored_salt, salt, sizeof(stored_salt) - 1);
    stored_salt[sizeof(stored_salt) - 1] = '\0';
    strncpy(stored_hash, hash, sizeof(stored_hash) - 1);
    stored_hash[sizeof(stored_hash) - 1] = '\0';
    
    // Copy PIN to memory for this session (cleared on exit)
    strncpy(stored_pin, pin, MAX_PIN_LENGTH);
    stored_pin[MAX_PIN_LENGTH] = '\0';
    
    return 0;
}

// Load PIN hash from file (returns 0 if file exists and is valid)
static int LoadPinHash() {
    FILE *fp = fopen(PIN_FILE_PATH, "r");
    if (!fp) return -1;
    
    // Use exact width specifiers to prevent buffer overflow
    char line1[18] = {0};
    char line2[34] = {0};
    
    if (!fgets(line1, sizeof(line1), fp) || !fgets(line2, sizeof(line2), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    // Remove newline if present
    size_t len1 = strlen(line1);
    if (len1 > 0 && line1[len1-1] == '\n') line1[len1-1] = '\0';
    size_t len2 = strlen(line2);
    if (len2 > 0 && line2[len2-1] == '\n') line2[len2-1] = '\0';
    
    // Validate lengths
    if (strlen(line1) != 16 || strlen(line2) != 32) {
        return -1;
    }
    
    strncpy(stored_salt, line1, sizeof(stored_salt) - 1);
    stored_salt[sizeof(stored_salt) - 1] = '\0';
    strncpy(stored_hash, line2, sizeof(stored_hash) - 1);
    stored_hash[sizeof(stored_hash) - 1] = '\0';
    
    return 0;
}

// Verify entered PIN against stored hash
static int VerifyPin(const char *pin) {
    char hash[33] = {0};
    HashPin(pin, stored_salt, hash);
    return strcmp(hash, stored_hash) == 0;
}

// Check if PIN file exists
static int PinExists() {
    struct stat st;
    return stat(PIN_FILE_PATH, &st) == 0;
}

// --- ENCRYPTION FUNCTIONS ---

// Simple key derivation: expand PIN into 256-byte key using hash-like mixing
static void DeriveKey(const char *pin) {
    int pin_len = strlen(pin);
    
    // Initialize with PIN bytes repeated
    for (int i = 0; i < 256; i++) {
        encryption_key[i] = (unsigned char)(pin[i % pin_len] + i);
    }
    
    // Mix the key (simple pseudo-random permutation)
    unsigned char temp;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + encryption_key[i] + pin[i % pin_len]) % 256;
        temp = encryption_key[i];
        encryption_key[i] = encryption_key[j];
        encryption_key[j] = temp;
    }
    
    // Additional mixing rounds for better diffusion
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 256; i++) {
            encryption_key[i] ^= encryption_key[(i + 128) % 256];
            encryption_key[i] += (unsigned char)(i * round);
        }
    }
}

// XOR encrypt/decrypt a buffer (symmetric - same operation for both)
static void XorCrypt(unsigned char *data, size_t len, size_t offset) {
    for (size_t i = 0; i < len; i++) {
        // Use position-dependent key byte for better security
        size_t key_idx = (offset + i) % 256;
        data[i] ^= encryption_key[key_idx];
        // Add position mixing to prevent pattern detection
        data[i] ^= (unsigned char)((offset + i) & 0xFF);
    }
}

// Check if file is encrypted (has .vlt extension)
static int IsEncrypted(const char *filename) {
    size_t len = strlen(filename);
    size_t ext_len = strlen(ENCRYPTED_EXT);
    if (len < ext_len) return 0;
    return strcmp(filename + len - ext_len, ENCRYPTED_EXT) == 0;
}

// Encrypt a single file (with verification to prevent data loss)
static int EncryptFile(const char *filepath) {
    // Get original file size for verification
    struct stat orig_st;
    if (stat(filepath, &orig_st) != 0) return -1;
    
    char new_path[512];
    snprintf(new_path, sizeof(new_path), "%s%s", filepath, ENCRYPTED_EXT);
    
    FILE *in = fopen(filepath, "rb");
    if (!in) return -1;
    
    FILE *out = fopen(new_path, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    
    unsigned char buffer[CHUNK_SIZE];
    size_t bytes_read;
    size_t total_offset = 0;
    size_t total_written = 0;
    int write_error = 0;
    
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, in)) > 0) {
        XorCrypt(buffer, bytes_read, total_offset);
        size_t written = fwrite(buffer, 1, bytes_read, out);
        if (written != bytes_read) {
            write_error = 1;
            break;
        }
        total_written += written;
        total_offset += bytes_read;
    }
    
    fclose(in);
    
    // Ensure all data is flushed to disk
    if (fflush(out) != 0) write_error = 1;
    fclose(out);
    
    // Verify: only delete original if encrypted file size matches
    if (write_error || total_written != (size_t)orig_st.st_size) {
        unlink(new_path);  // Remove failed encrypted file
        return -1;
    }
    
    // Remove original file only after successful encryption
    unlink(filepath);
    
    return 0;
}

// Decrypt a single file (with verification to prevent data loss)
static int DecryptFile(const char *filepath) {
    if (!IsEncrypted(filepath)) return -1;
    
    // Get encrypted file size for verification
    struct stat enc_st;
    if (stat(filepath, &enc_st) != 0) return -1;
    
    // Remove .vlt extension for output
    char new_path[512];
    strncpy(new_path, filepath, sizeof(new_path) - 1);
    new_path[sizeof(new_path) - 1] = '\0';
    size_t path_len = strlen(new_path);
    if (path_len > strlen(ENCRYPTED_EXT)) {
        new_path[path_len - strlen(ENCRYPTED_EXT)] = '\0';
    }
    
    FILE *in = fopen(filepath, "rb");
    if (!in) return -1;
    
    FILE *out = fopen(new_path, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    
    unsigned char buffer[CHUNK_SIZE];
    size_t bytes_read;
    size_t total_offset = 0;
    size_t total_written = 0;
    int write_error = 0;
    
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, in)) > 0) {
        XorCrypt(buffer, bytes_read, total_offset);  // XOR is symmetric
        size_t written = fwrite(buffer, 1, bytes_read, out);
        if (written != bytes_read) {
            write_error = 1;
            break;
        }
        total_written += written;
        total_offset += bytes_read;
    }
    
    fclose(in);
    
    // Ensure all data is flushed to disk
    if (fflush(out) != 0) write_error = 1;
    fclose(out);
    
    // Verify: only delete encrypted file if decrypted file size matches
    if (write_error || total_written != (size_t)enc_st.st_size) {
        unlink(new_path);  // Remove failed decrypted file
        return -1;
    }
    
    // Remove encrypted file only after successful decryption
    unlink(filepath);
    
    return 0;
}

// Process all files in a directory (encrypt or decrypt)
static int ProcessDirectory(const char *dirpath, int encrypt) {
    DIR *dir = opendir(dirpath);
    if (!dir) return -1;
    
    struct dirent *entry;
    char filepath[512];
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            // Recursively process subdirectories
            count += ProcessDirectory(filepath, encrypt);
        } else if (S_ISREG(st.st_mode)) {
            if (encrypt && !IsEncrypted(entry->d_name)) {
                if (EncryptFile(filepath) == 0) count++;
            } else if (!encrypt && IsEncrypted(entry->d_name)) {
                if (DecryptFile(filepath) == 0) count++;
            }
        }
    }
    
    closedir(dir);
    return count;
}

// Initialize Button Coordinates based on screen size
void InitLayout() {
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    
    // PocketBook Era has 1264x1680 resolution - adjust button size accordingly
    // Buttons are 30% smaller than original (sw/5 * 0.7 ≈ sw/7)
    int btnW = sw / 7;
    int btnH = btnW;  // Square buttons
    int gap = sw / 50;  // Proportional gap (smaller for smaller buttons)
    int totalW = (btnW * 3) + (gap * 2);
    int totalH = (btnH * 4) + (gap * 3);  // 4 rows of buttons
    int startX = (sw - totalW) / 2;
    
    // Reserve space for title (top 1/5) and center buttons in remaining area
    int topMargin = sh / 5;  // Space for title and status
    int availableH = sh - topMargin - gap;  // Available height below title
    int startY = topMargin + (availableH - totalH) / 2;  // Center buttons vertically

    for (int i = 0; i < BTN_COUNT; i++) {
        int row = i / 3;
        int col = i % 3;
        
        buttons[i].x = startX + col * (btnW + gap);
        buttons[i].y = startY + row * (btnH + gap);
        buttons[i].w = btnW;
        buttons[i].h = btnH;
        strncpy(buttons[i].label, num_labels[i], sizeof(buttons[i].label) - 1);
        buttons[i].label[sizeof(buttons[i].label) - 1] = '\0';
        buttons[i].value = num_values[i];
    }
    
    // Settings button (gear icon in top right)
    settings_btn_size = sw / 15;
    settings_btn_x = sw - settings_btn_size - 20;
    settings_btn_y = 20;
    
    // Close button (X in top left)
    close_btn_size = sw / 15;
    close_btn_x = 20;
    close_btn_y = 20;
    
    is_initialized = 1;
}

// Check if folder exists
static int DirExists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

// Create folder if it doesn't exist
static int EnsureFolderExists(const char *path) {
    if (DirExists(path)) return 1;
    return mkdir(path, 0755) == 0;
}

// Get current folder state for status display
static int IsFolderHidden() {
    return DirExists(TARGET_FOLDER_HIDDEN);
}

// Forward declaration for Draw
static void Draw();

// Core Logic: Toggle Visibility with Encryption
static void ToggleVisibility() {
    // Derive encryption key from stored PIN
    DeriveKey(stored_pin);
    
    if (DirExists(TARGET_FOLDER_VISIBLE)) {
        // LOCK: Encrypt files then hide folder
        snprintf(status_msg, sizeof(status_msg), "Encrypting files...");
        Draw();
        PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
        
        #if ENCRYPTION_ENABLED
        files_processed = ProcessDirectory(TARGET_FOLDER_VISIBLE, 1);  // Encrypt
        #endif
        
        // Rename folder to hidden
        if (rename(TARGET_FOLDER_VISIBLE, TARGET_FOLDER_HIDDEN) == 0) {
            #if ENCRYPTION_ENABLED
            snprintf(status_msg, sizeof(status_msg), "LOCKED (%d files)", files_processed);
            #else
            snprintf(status_msg, sizeof(status_msg), "Folder LOCKED");
            #endif
        } else {
            snprintf(status_msg, sizeof(status_msg), "Error: Lock failed (%d)", errno);
        }
    } else if (DirExists(TARGET_FOLDER_HIDDEN)) {
        // UNLOCK: Show folder then decrypt files
        
        // Check if visible folder already exists (shouldn't happen normally)
        if (DirExists(TARGET_FOLDER_VISIBLE)) {
            // Try to remove it (only works if empty)
            if (rmdir(TARGET_FOLDER_VISIBLE) != 0) {
                // Folder not empty - user needs to handle this
                snprintf(status_msg, sizeof(status_msg), "Error: 'Private' folder exists");
                return;
            }
        }
        
        if (rename(TARGET_FOLDER_HIDDEN, TARGET_FOLDER_VISIBLE) == 0) {
            snprintf(status_msg, sizeof(status_msg), "Decrypting files...");
            Draw();
            PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
            
            #if ENCRYPTION_ENABLED
            files_processed = ProcessDirectory(TARGET_FOLDER_VISIBLE, 0);  // Decrypt
            snprintf(status_msg, sizeof(status_msg), "UNLOCKED (%d files decrypted)", files_processed);
            #else
            snprintf(status_msg, sizeof(status_msg), "Folder UNLOCKED");
            #endif
        } else {
            snprintf(status_msg, sizeof(status_msg), "Error: Unlock failed (%d)", errno);
        }
    } else {
        // Neither exists - create the hidden folder
        if (EnsureFolderExists(TARGET_FOLDER_HIDDEN)) {
            snprintf(status_msg, sizeof(status_msg), "Created locked folder");
        } else {
            snprintf(status_msg, sizeof(status_msg), "Error: Cannot create folder");
        }
    }
}

// Render UI
static void Draw() {
    if (!is_initialized) return;
    
    int sw = ScreenWidth();
    int sh = ScreenHeight();

    // 1. Clear screen with white background
    ClearScreen();

    // 2. Draw title based on screen state
    SetFont(font_large, BLACK);
    const char *title = "Vault";
    switch (current_screen) {
        case SCREEN_SETUP_NEW:
            title = "Setup PIN";
            break;
        case SCREEN_SETUP_CONFIRM:
            title = "Confirm PIN";
            break;
        case SCREEN_CHANGE_OLD:
            title = "Current PIN";
            break;
        case SCREEN_CHANGE_NEW:
            title = "New PIN";
            break;
        case SCREEN_CHANGE_CONFIRM:
            title = "Confirm New PIN";
            break;
        default:
            title = "Vault";
            break;
    }
    DrawTextRect(0, sh / 8, sw, 50, title, ALIGN_CENTER);

    // 3. Draw close button (X in top left) - always visible
    FillArea(close_btn_x, close_btn_y, close_btn_size, close_btn_size, LGRAY);
    DrawRect(close_btn_x, close_btn_y, close_btn_size, close_btn_size, BLACK);
    SetFont(font_small, BLACK);
    DrawTextRect(close_btn_x, close_btn_y + (close_btn_size / 4), 
                 close_btn_size, close_btn_size / 2, "X", ALIGN_CENTER);
    
    // 4. Draw settings button (gear icon) - only on main screen when PIN exists
    if (current_screen == SCREEN_MAIN && PinExists()) {
        FillArea(settings_btn_x, settings_btn_y, settings_btn_size, settings_btn_size, LGRAY);
        DrawRect(settings_btn_x, settings_btn_y, settings_btn_size, settings_btn_size, BLACK);
        SetFont(font_small, BLACK);
        DrawTextRect(settings_btn_x, settings_btn_y + (settings_btn_size / 4), 
                     settings_btn_size, settings_btn_size / 2, "*", ALIGN_CENTER);
    }

    // 5. Status / Input Area - fixed position to prevent layout shift
    int textY = sh / 4;
    
    // Always draw PIN area with consistent height using large font
    int pinLen = strlen(input_buffer);
    char display[32];  // Larger buffer for safety
    memset(display, 0, sizeof(display));
    
    if (pinLen > 0) {
        // Show asterisks for entered digits
        SetFont(font_large, BLACK);
        for (int i = 0; i < pinLen && i < MAX_PIN_LENGTH; i++) {
            display[i] = '*';
        }
    } else {
        // Show underscores as placeholder (same font size to prevent shift)
        SetFont(font_large, DGRAY);
        strcpy(display, "- - - -");
    }
    DrawTextRect(0, textY, sw, 60, display, ALIGN_CENTER);

    // 6. Draw Status Message
    SetFont(font_small, DGRAY);
    DrawTextRect(0, textY + 80, sw, 40, status_msg, ALIGN_CENTER);
    
    // Show current state indicator (only on main screen)
    if (current_screen == SCREEN_MAIN && PinExists()) {
        const char *state = IsFolderHidden() ? "[LOCKED]" : "[UNLOCKED]";
        DrawTextRect(0, textY + 120, sw, 30, state, ALIGN_CENTER);
    }
    
    // Show reset confirmation if active
    if (show_reset_confirm) {
        // Draw overlay
        int boxW = sw * 3 / 4;
        int boxH = sh / 4;
        int boxX = (sw - boxW) / 2;
        int boxY = (sh - boxH) / 2;
        FillArea(boxX, boxY, boxW, boxH, WHITE);
        DrawRect(boxX, boxY, boxW, boxH, BLACK);
        DrawRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, BLACK);
        
        SetFont(font_large, BLACK);
        DrawTextRect(boxX, boxY + 20, boxW, 50, "RESET PIN?", ALIGN_CENTER);
        SetFont(font_small, BLACK);
        DrawTextRect(boxX + 20, boxY + 80, boxW - 40, 60, "WARNING: All encrypted files will be LOST!", ALIGN_CENTER);
        
        // Yes/No buttons
        int btnW = boxW / 3;
        int btnH = 60;
        int btnY = boxY + boxH - btnH - 20;
        
        // NO button
        FillArea(boxX + 30, btnY, btnW, btnH, LGRAY);
        DrawRect(boxX + 30, btnY, btnW, btnH, BLACK);
        DrawTextRect(boxX + 30, btnY + 15, btnW, 30, "NO", ALIGN_CENTER);
        
        // YES button
        FillArea(boxX + boxW - btnW - 30, btnY, btnW, btnH, LGRAY);
        DrawRect(boxX + boxW - btnW - 30, btnY, btnW, btnH, BLACK);
        DrawTextRect(boxX + boxW - btnW - 30, btnY + 15, btnW, 30, "YES", ALIGN_CENTER);
    }

    // 7. Draw Keypad with filled buttons for better E-ink visibility (skip if dialog shown)
    if (!show_reset_confirm) {
        SetFont(font_large, BLACK);
        for (int i = 0; i < BTN_COUNT; i++) {
            // Draw button background (light gray fill)
            FillArea(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, LGRAY);
            // Draw button border
            DrawRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, BLACK);
            
            // Center text vertically and horizontally in button
            int textOffsetY = (buttons[i].h - 40) / 2;  // Approximate font height
            DrawTextRect(buttons[i].x, buttons[i].y + textOffsetY, 
                         buttons[i].w, buttons[i].h, buttons[i].label, ALIGN_CENTER);
        }
        
        // 8. Draw RESET button on change PIN screen (when entering old PIN)
        // Position it at top, similar to close and settings buttons
        if (current_screen == SCREEN_CHANGE_OLD) {
            int resetW = sw / 4;
            int resetH = close_btn_size;  // Same height as close/settings buttons
            int resetX = (sw - resetW) / 2;  // Centered horizontally
            int resetY = 20;  // Same Y as close/settings buttons
            FillArea(resetX, resetY, resetW, resetH, LGRAY);
            DrawRect(resetX, resetY, resetW, resetH, BLACK);
            SetFont(font_small, BLACK);
            DrawTextRect(resetX, resetY + (resetH / 4), resetW, resetH / 2, "FORGOT PIN?", ALIGN_CENTER);
        }
    }
}

static void ProcessInput(int val) {
    int len = strlen(input_buffer);
    
    if (val >= 0 && val <= 9) {
        // Number input - append if not at max length
        if (len < MAX_PIN_LENGTH) {
            input_buffer[len] = '0' + val;
            input_buffer[len + 1] = '\0';
        }
    } else if (val == -1) {
        // Delete last character
        if (len > 0) {
            input_buffer[len - 1] = '\0';
        }
    } else if (val == -2) {
        // OK button pressed - action depends on screen state
        switch (current_screen) {
            case SCREEN_SETUP_NEW:
                // Setting up new PIN for first time
                if (len < MIN_PIN_LENGTH) {
                    snprintf(status_msg, sizeof(status_msg), "PIN must be %d-%d digits", MIN_PIN_LENGTH, MAX_PIN_LENGTH);
                } else {
                    // Save to temp and ask for confirmation
                    strncpy(temp_new_pin, input_buffer, MAX_PIN_LENGTH);
                    temp_new_pin[MAX_PIN_LENGTH] = '\0';
                    memset(input_buffer, 0, sizeof(input_buffer));
                    current_screen = SCREEN_SETUP_CONFIRM;
                    snprintf(status_msg, sizeof(status_msg), "Enter PIN again to confirm");
                }
                break;
                
            case SCREEN_SETUP_CONFIRM:
                // Confirming new PIN
                if (strcmp(input_buffer, temp_new_pin) == 0) {
                    // PINs match - save it
                    if (SavePin(temp_new_pin) == 0) {
                        snprintf(status_msg, sizeof(status_msg), "PIN set successfully!");
                        current_screen = SCREEN_MAIN;
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "Error saving PIN");
                        current_screen = SCREEN_SETUP_NEW;
                    }
                } else {
                    snprintf(status_msg, sizeof(status_msg), "PINs don't match. Try again.");
                    current_screen = SCREEN_SETUP_NEW;
                }
                memset(input_buffer, 0, sizeof(input_buffer));
                memset(temp_new_pin, 0, sizeof(temp_new_pin));
                break;
                
            case SCREEN_CHANGE_OLD:
                // Verify current PIN before allowing change
                if (len == 0) {
                    snprintf(status_msg, sizeof(status_msg), "Enter current PIN");
                } else if (VerifyPin(input_buffer)) {
                    // Store verified PIN for encryption operations
                    strncpy(stored_pin, input_buffer, MAX_PIN_LENGTH);
                    stored_pin[MAX_PIN_LENGTH] = '\0';
                    memset(input_buffer, 0, sizeof(input_buffer));
                    current_screen = SCREEN_CHANGE_NEW;
                    snprintf(status_msg, sizeof(status_msg), "Enter new PIN (%d-%d digits)", MIN_PIN_LENGTH, MAX_PIN_LENGTH);
                } else {
                    snprintf(status_msg, sizeof(status_msg), "Wrong PIN!");
                    memset(input_buffer, 0, sizeof(input_buffer));
                }
                break;
                
            case SCREEN_CHANGE_NEW:
                // Enter new PIN
                if (len < MIN_PIN_LENGTH) {
                    snprintf(status_msg, sizeof(status_msg), "PIN must be %d-%d digits", MIN_PIN_LENGTH, MAX_PIN_LENGTH);
                } else {
                    strncpy(temp_new_pin, input_buffer, MAX_PIN_LENGTH);
                    temp_new_pin[MAX_PIN_LENGTH] = '\0';
                    memset(input_buffer, 0, sizeof(input_buffer));
                    current_screen = SCREEN_CHANGE_CONFIRM;
                    snprintf(status_msg, sizeof(status_msg), "Confirm new PIN");
                }
                break;
                
            case SCREEN_CHANGE_CONFIRM:
                // Confirm new PIN
                if (strcmp(input_buffer, temp_new_pin) == 0) {
                    if (SavePin(temp_new_pin) == 0) {
                        snprintf(status_msg, sizeof(status_msg), "PIN changed successfully!");
                        current_screen = SCREEN_MAIN;
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "Error saving PIN");
                        current_screen = SCREEN_MAIN;
                    }
                } else {
                    snprintf(status_msg, sizeof(status_msg), "PINs don't match. Try again.");
                    current_screen = SCREEN_CHANGE_NEW;
                }
                memset(input_buffer, 0, sizeof(input_buffer));
                memset(temp_new_pin, 0, sizeof(temp_new_pin));
                break;
                
            case SCREEN_MAIN:
            default:
                // Normal operation - verify PIN and toggle vault
                if (len == 0) {
                    snprintf(status_msg, sizeof(status_msg), "Enter PIN first");
                } else if (VerifyPin(input_buffer)) {
                    // Store verified PIN for encryption key derivation
                    strncpy(stored_pin, input_buffer, MAX_PIN_LENGTH);
                    stored_pin[MAX_PIN_LENGTH] = '\0';
                    ToggleVisibility();
                    memset(input_buffer, 0, sizeof(input_buffer));
                } else {
                    snprintf(status_msg, sizeof(status_msg), "Wrong PIN!");
                    memset(input_buffer, 0, sizeof(input_buffer));
                }
                break;
        }
    }
}

static int Handler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            // Disable system panel to use full screen
            SetPanelType(0);
            
            // Open fonts
            font_large = OpenFont("LiberationSans-Bold", 48, 1);
            if (!font_large) font_large = OpenFont("LiberationSans", 48, 1);
            font_small = OpenFont("LiberationSans", 28, 0);
            if (!font_small) font_small = OpenFont("LiberationSans", 24, 0);
            
            // Initialize layout
            InitLayout();
            
            // Check if PIN exists and set initial screen state
            if (!PinExists()) {
                current_screen = SCREEN_SETUP_NEW;
                snprintf(status_msg, sizeof(status_msg), "Create PIN (%d-%d digits)", MIN_PIN_LENGTH, MAX_PIN_LENGTH);
            } else {
                if (LoadPinHash() == 0) {
                    current_screen = SCREEN_MAIN;
                    if (IsFolderHidden()) {
                        snprintf(status_msg, sizeof(status_msg), "Folder is locked");
                    } else if (DirExists(TARGET_FOLDER_VISIBLE)) {
                        snprintf(status_msg, sizeof(status_msg), "Folder is unlocked");
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "Enter PIN to create vault");
                    }
                } else {
                    current_screen = SCREEN_SETUP_NEW;
                    snprintf(status_msg, sizeof(status_msg), "Create new PIN");
                }
            }
            break;
            
        case EVT_SHOW:
            Draw();
            PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
            break;
            
        case EVT_POINTERUP: {
            int sw = ScreenWidth();
            int sh = ScreenHeight();
            
            // Handle reset confirmation dialog first
            if (show_reset_confirm) {
                int boxW = sw * 3 / 4;
                int boxH = sh / 4;
                int boxX = (sw - boxW) / 2;
                int boxY = (sh - boxH) / 2;
                int btnW = boxW / 3;
                int btnH = 60;
                int btnY = boxY + boxH - btnH - 20;
                
                // Check NO button
                if (par1 >= boxX + 30 && par1 < boxX + 30 + btnW &&
                    par2 >= btnY && par2 < btnY + btnH) {
                    show_reset_confirm = 0;
                    Draw();
                    FullUpdate();  // Full refresh to clear dialog
                    break;
                }
                // Check YES button - perform reset
                if (par1 >= boxX + boxW - btnW - 30 && par1 < boxX + boxW - 30 &&
                    par2 >= btnY && par2 < btnY + btnH) {
                    show_reset_confirm = 0;
                    // Delete PIN file
                    remove(PIN_FILE_PATH);
                    // Clear memory
                    memset(stored_pin, 0, sizeof(stored_pin));
                    memset(stored_salt, 0, sizeof(stored_salt));
                    memset(stored_hash, 0, sizeof(stored_hash));
                    memset(input_buffer, 0, sizeof(input_buffer));
                    // Go to setup screen
                    current_screen = SCREEN_SETUP_NEW;
                    snprintf(status_msg, sizeof(status_msg), "Create new PIN (%d-%d digits)", MIN_PIN_LENGTH, MAX_PIN_LENGTH);
                    Draw();
                    FullUpdate();
                    break;
                }
                // Ignore other touches when dialog is shown
                break;
            }
            
            // Check close button (always visible)
            if (par1 >= close_btn_x && par1 < close_btn_x + close_btn_size &&
                par2 >= close_btn_y && par2 < close_btn_y + close_btn_size) {
                CloseApp();
                break;
            }
            
            // Check settings button (only on main screen)
            if (current_screen == SCREEN_MAIN && PinExists()) {
                if (par1 >= settings_btn_x && par1 < settings_btn_x + settings_btn_size &&
                    par2 >= settings_btn_y && par2 < settings_btn_y + settings_btn_size) {
                    // Settings tapped - go to change PIN screen
                    current_screen = SCREEN_CHANGE_OLD;
                    memset(input_buffer, 0, sizeof(input_buffer));
                    snprintf(status_msg, sizeof(status_msg), "Enter current PIN");
                    Draw();
                    PartialUpdate(0, 0, sw, sh);
                    break;
                }
            }
            
            // Check FORGOT PIN button (only on change PIN screen)
            // Position matches drawing: top area, centered
            if (current_screen == SCREEN_CHANGE_OLD) {
                int resetW = sw / 4;
                int resetH = close_btn_size;
                int resetX = (sw - resetW) / 2;
                int resetY = 20;
                if (par1 >= resetX && par1 < resetX + resetW &&
                    par2 >= resetY && par2 < resetY + resetH) {
                    // Show reset confirmation dialog
                    show_reset_confirm = 1;
                    Draw();
                    PartialUpdate(0, 0, sw, sh);
                    break;
                }
            }
            
            // Check keypad button hits
            int touched = 0;
            for (int i = 0; i < BTN_COUNT; i++) {
                if (par1 >= buttons[i].x && par1 < buttons[i].x + buttons[i].w &&
                    par2 >= buttons[i].y && par2 < buttons[i].y + buttons[i].h) {
                    ProcessInput(buttons[i].value);
                    touched = 1;
                    break;  // Only process one button
                }
            }
            if (touched) {
                Draw();
                PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
            }
            break;
        }
        
        case EVT_KEYPRESS:
            // Handle hardware back button (IV_KEY_BACK = 28)
            if (par1 == 28) {
                // On setup/change screens, go back to main (if PIN exists)
                if (current_screen != SCREEN_MAIN && PinExists()) {
                    current_screen = SCREEN_MAIN;
                    memset(input_buffer, 0, sizeof(input_buffer));
                    memset(temp_new_pin, 0, sizeof(temp_new_pin));
                    if (IsFolderHidden()) {
                        snprintf(status_msg, sizeof(status_msg), "Folder is locked");
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "Folder is unlocked");
                    }
                    Draw();
                    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
                } else {
                    CloseApp();
                }
            }
            break;
            
        case EVT_EXIT:
            // Cleanup fonts
            if (font_large) {
                CloseFont(font_large);
                font_large = NULL;
            }
            if (font_small) {
                CloseFont(font_small);
                font_small = NULL;
            }
            // Clear sensitive data
            memset(stored_pin, 0, sizeof(stored_pin));
            memset(temp_new_pin, 0, sizeof(temp_new_pin));
            memset(input_buffer, 0, sizeof(input_buffer));
            memset(encryption_key, 0, sizeof(encryption_key));
            is_initialized = 0;
            break;
    }
    return 0;
}

int main(int argc, char **argv) {
    InkViewMain(Handler);
    return 0;
}