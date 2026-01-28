# PocketBook Apps

Three custom applications for PocketBook e-readers (tested on PocketBook Era).

---

## Chef

AI-powered recipe generator with dietary restrictions and meal planning.

### Features

| Feature | Description |
|---------|-------------|
| **AI Recipe Generation** | Uses Google Gemini 2.0 Flash for custom recipes |
| **4-Tab Interface** | Generate, Food, Config, Saved sections |
| **Dietary Constraints** | Support for Keto, Paleo, Vegan, etc. |
| **Allergen Exclusion** | Strict filtering for gluten, dairy, nuts, etc. |
| **Preference Control** | Exclude dislikes (cilantro, spicy, mushrooms, etc.) |
| **Multi-Language** | Recipe output in English, Polish, or Spanish |
| **Step-by-Step Mode** | Large text display for cooking |
| **Recipe Cookbook** | Save and load up to 50 recipes |
| **Calorie/Time Control** | Set target calories and prep time |

### Setup (Required)

**1. Create API Key File:**
```
/mnt/ext1/.ai_api_key
```
Paste your Gemini API key into this file (just the key, nothing else).

**Note:** This is the same API key file used by AISearch. You only need one key for both apps.

**2. Get a Gemini API Key:**
- Go to: https://aistudio.google.com/apikey
- Create a new API key for Gemini 2.0

### Configuration

The app stores two JSON files on your device:
```
/mnt/ext1/.chef_profile.json   # Your dietary preferences
/mnt/ext1/.chef_cookbook.json  # Saved recipes
```

### Usage

**Tab 1: GENERATE**
1. Tap the input field to enter available ingredients
2. Tap the large **GENERATE** button
3. AI creates a custom recipe based on your profile
4. Navigate through steps with PREV/NEXT buttons
5. Tap **SAVE** to add recipe to cookbook
6. Tap **DONE** to return to input screen

**Tab 2: FOOD (Profile)**
- **Lifestyle**: Toggle dietary modes (Keto, Paleo, Vegan, etc.)
- **Safety**: Mark strict allergen exclusions (Gluten, Dairy, Nuts, etc.)
- **Taste**: Exclude ingredients you don't like (Cilantro, Spicy, etc.)
- Changes save automatically

**Tab 3: CONFIG (Settings)**
- **Calories/Meal**: Adjust target calories (±50 increments)
- **Max Prep Time**: Set maximum cooking time (±15 minute increments)
- **Complexity**: Choose Simple, Medium, or Pro/Lab style
- **Output Language**: English, Polish (Polski), or Spanish (Español)
- Changes save automatically

**Tab 4: SAVED (Cookbook)**
- View all saved recipes (up to 50)
- Tap any recipe to view it in step-by-step mode
- Tap **CLEAR ALL** to delete all saved recipes

### Example Queries

With ingredients entered:
- "chicken breast, broccoli, olive oil"
- "eggs, spinach, feta cheese"
- "ground beef, tomatoes, onions"

The AI generates recipes matching:
- Your dietary restrictions (Keto, Vegan, etc.)
- Allergen exclusions (No Gluten, No Dairy, etc.)
- Personal dislikes (No Cilantro, Not Spicy, etc.)
- Target calories and time limits
- Preferred complexity level
- Selected output language

### Requirements

- WiFi connection for AI recipe generation
- Valid Google Gemini API key (in `/mnt/ext1/.ai_api_key`)

---

## Vault

A secure folder locker with PIN protection and file encryption.

### Features

| Feature | Description |
|---------|-------------|
| **PIN Setup** | Create your own 4-8 digit PIN on first launch |
| **PIN Change** | Change PIN anytime via settings button (*) |
| **File Encryption** | XOR cipher with 256-byte key derived from PIN |
| **Hidden Folder** | Locked folder renamed to `.Private` (hidden on device) |
| **Recursive Encryption** | All files in subdirectories are encrypted |
| **E-ink Optimized** | Large buttons, clean UI for touchscreen |
| **Memory Efficient** | 4KB chunk processing, no memory leaks |
| **Secure Cleanup** | Sensitive data cleared from memory on exit |
| **Data Loss Prevention** | File writes verified before deleting originals |

### Security Features

| Feature | Implementation |
|---------|----------------|
| **PIN Storage** | Salted hash only - PIN never stored |
| **Salt Generation** | Uses /dev/urandom + multiple entropy sources |
| **Hash Algorithm** | djb2 with salt mixing (32-char output) |
| **Key Derivation** | RC4-like KSA producing 256-byte key |
| **Encryption** | Position-dependent XOR with diffusion |
| **Memory** | All sensitive buffers zeroed on exit |

### PIN Management

**First Launch:**
1. App prompts "Create PIN (4-8 digits)"
2. Enter your desired PIN and press OK
3. Confirm by entering the same PIN again
4. PIN hash saved to hidden file on device

**Changing PIN:**
1. Tap the settings button (*) in top-right corner
2. Enter your current PIN
3. Enter new PIN (4-8 digits)
4. Confirm new PIN
5. PIN updated!

> ⚠️ **Important**: If you change your PIN while files are encrypted, you must first unlock (decrypt) them with the old PIN, then lock (encrypt) again with the new PIN.

**Back Button:**
- During PIN setup/change: Returns to main screen (if PIN exists)
- On main screen: Exits app

### Configuration

Edit these values in `Vault.c` before building:

```c
#define TARGET_FOLDER_VISIBLE "/mnt/ext1/Private"   // Unlocked folder path
#define TARGET_FOLDER_HIDDEN  "/mnt/ext1/.Private"  // Locked folder path
#define PIN_FILE_PATH "/mnt/ext1/.vault_pin"        // PIN storage location
#define MIN_PIN_LENGTH 4                             // Minimum PIN digits
#define MAX_PIN_LENGTH 8                             // Maximum PIN digits
#define ENCRYPTION_ENABLED 1                         // Set to 0 to disable
```

### Usage

1. **First Launch**: Create your PIN (4-8 digits), then confirm it
2. Enter your PIN using the on-screen keypad
3. Press **OK** to toggle lock/unlock:
   - If folder is **visible** → encrypts files and hides folder
   - If folder is **hidden** → shows folder and decrypts files
4. Status shows `[LOCKED]` or `[UNLOCKED]` and file count
5. Tap **\*** (settings) in top-right to change PIN

### Screenshots (UI Layout)

```
┌─────────────────────────┐
│         Vault        [*]│  ← Settings button
│                         │
│        * * * *          │  ← PIN entry (masked)
│    Enter PIN to Unlock  │
│       [LOCKED]          │
│                         │
│   ┌───┐ ┌───┐ ┌───┐    │
│   │ 1 │ │ 2 │ │ 3 │    │
│   └───┘ └───┘ └───┘    │
│   ┌───┐ ┌───┐ ┌───┐    │
│   │ 4 │ │ 5 │ │ 6 │    │
│   └───┘ └───┘ └───┘    │
│   ┌───┐ ┌───┐ ┌───┐    │
│   │ 7 │ │ 8 │ │ 9 │    │
│   └───┘ └───┘ └───┘    │
│   ┌───┐ ┌───┐ ┌───┐    │
│   │ < │ │ 0 │ │OK │    │
│   └───┘ └───┘ └───┘    │
└─────────────────────────┘
```

---

## AI Search

AI-powered book recommendation app using Google Gemini API.

### Features

| Feature | Description |
|---------|-------------|
| **AI Recommendations** | Uses Gemini 2.5 Flash for book suggestions |
| **Device Library** | Reads books directly from PocketBook's database |
| **Natural Language** | Ask in plain English for book suggestions |
| **Native Networking** | Uses PocketBook APIs (no external libs) |
| **Battery Efficient** | Network only on-demand, proper power management |
| **Secure API Key** | Key stored in separate file, not hardcoded |

### How It Works

1. Reads from PocketBook's internal SQLite database
   - Location: `/mnt/ext1/system/explorer-3/explorer-3.db`
   - No setup needed - uses books already on your device

### Setup (Required)

**1. Create API Key File:**
```
/mnt/ext1/.ai_api_key
```
Paste your Gemini API key into this file (just the key, nothing else).

**2. Get a Gemini API Key:**
- Go to: https://console.cloud.google.com
- Or: https://aistudio.google.com/apikey
- Create a new API key for Gemini

**3. Books:**
- Books on device are detected automatically from PocketBook database

### Configuration

Edit these values in `AISearch.c` before building:

```c
#define GEMINI_API_KEY_FILE "/mnt/ext1/.ai_api_key"   // API key file location
#define BOOKS_DB_PATH "/mnt/ext1/system/explorer-3/explorer-3.db"  // PocketBook database
```

### Usage

1. Launch AI Search app
2. If prompted, create the API key file as described above
3. Tap the search box to open keyboard
4. Enter a query like:
   - "Find me a thriller"
   - "Something by Philip K. Dick"
   - "A relaxing weekend read"
   - "Science fiction with time travel"
5. Tap **Search** button
6. AI returns 2-3 book recommendations from your library

### Requirements

- WiFi connection for API calls
- Valid Google Gemini API key (in `/mnt/ext1/.ai_api_key`)
- Books on device (detected automatically)

---

## Installation

### Method 1: Use Install Script (Recommended)

1. Build the apps first: `scripts\build.bat`
2. Connect PocketBook via USB (select "USB Drive" mode)
3. Run: `scripts\install.bat`
4. Safely eject PocketBook
5. Apps appear in Applications menu

### Method 2: Manual Copy

1. Connect PocketBook to computer via USB
2. Navigate to internal storage
3. Copy `.app` files from `build\` to `/applications/` folder:
   ```
   /mnt/ext1/applications/Vault.app
   /mnt/ext1/applications/AISearch.app
   /mnt/ext1/applications/Chef.app
   ```
4. Create API key file (for AI apps):
   ```
   /mnt/ext1/.ai_api_key      # For both AISearch and Chef
   ```
5. Disconnect and restart PocketBook

### Additional Files

For Chef:
```
/mnt/ext1/.ai_api_key          # Your Gemini API key (shared with AISearch)
/mnt/ext1/.chef_profile.json   # Auto-created on first run
/mnt/ext1/.chef_cookbook.json  # Auto-created on first save
```

For AI Search:
```
/mnt/ext1/.ai_api_key      # Your Gemini API key (shared with Chef)
```

For Vault:
```
/mnt/ext1/Private/         # Folder to protect (create this)
```

---

## Building from Source

### Requirements

- Docker Desktop
- PocketBook SDK Docker image: `larento/pocketbook-sdk:5.19-a13`

### Build Commands

```bash
# Build all apps (from repo root)
scripts\build.bat

# Output: build\Vault.app, build\AISearch.app, build\Chef.app
```

Manual build (single app):
```bash
docker run --rm -v "$(pwd):/src" -w /src --entrypoint sh \
  larento/pocketbook-sdk:5.19-a13 \
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && \
      /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc \
      -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 \
      apps/Vault/Vault.c -o build/Vault.app -linkview"
```

---

## Repository Structure

```
pocketbook-apps/
├── apps/                    # Source code
│   ├── Vault/Vault.c
│   ├── AISearch/AISearch.c
│   └── Chef/Chef.c
├── config/                  # Configuration files
│   └── .ai_api_key
├── docs/                    # Documentation
│   ├── README.md
│   ├── CONTRIBUTING.md
│   ├── POCKETBOOK_SDK_REFERENCE.md  # SDK API reference
│   └── sdk/
│       └── inkview.h       # PocketBook SDK header
├── scripts/                 # Build & install scripts
│   ├── build.bat
│   └── install.bat
├── build/                   # Output (git-ignored)
└── .github/                 # AI assistant config
    └── copilot-instructions.md
```

---

## SDK Documentation

For developers looking to understand or modify these apps:

- **[POCKETBOOK_SDK_REFERENCE.md](POCKETBOOK_SDK_REFERENCE.md)** - Comprehensive API guide with examples
  - Event handling patterns
  - Screen drawing & updates
  - HTTP requests (3 methods)
  - WiFi control
  - Touch input
  - UI dialogs & widgets
  - SQLite database
  - JSON parsing
  - Threading
  - Remote debugging with gdb
  
- **[sdk/inkview.h](sdk/inkview.h)** - Full SDK header file with all function signatures

---

## Technical Details

### Dependencies

| App | Libraries |
|-----|-----------|
| Vault | inkview.h (native), standard C libs |
| AISearch | inkview.h (native), sqlite3 |
| Chef | inkview.h (native), standard C libs |

No external dependencies - all apps use only PocketBook native APIs.

### Power Management

- **Vault**: No background processes, minimal battery impact
- **AISearch**: 
  - Uses `PostponeTimedPoweroff()` during network requests
  - Network only activated when Search is tapped
  - Proper cleanup on exit
- **Chef**:
  - Uses `PostponeTimedPoweroff()` during recipe generation
  - Network only activated when Generate is tapped
  - Proper cleanup on exit (API key cleared from memory)

### Tested On

- PocketBook Era (1264x1680 resolution)
- Firmware 6.x
- SDK 5.19-a13

---

## Changelog

### v1.2 (2026-01-27)
**Chef:**
- Added: New Chef app for AI-powered recipe generation
- Added: 4-tab interface (Generate, Food, Config, Saved)
- Added: Support for 10 dietary modes (Keto, Paleo, Vegan, etc.)
- Added: Allergen exclusion (12 common allergens)
- Added: Taste preference filtering (12 common dislikes)
- Added: Calorie and time constraints
- Added: Multi-language output (English, Polish, Spanish)
- Added: Recipe cookbook with persistent storage
- Added: Step-by-step cooking mode

### v1.1 (2026-01-26)
**Vault:**
- Fixed: PIN now stored as salted hash only (never plaintext)
- Fixed: Improved entropy for salt generation (uses /dev/urandom)
- Fixed: Buffer overflow vulnerabilities in PIN file loading
- Fixed: Data loss prevention - writes verified before deleting originals
- Fixed: Proper bounds checking in all string operations
- Added: time.h include for proper time() function support

**AISearch:**
- Fixed: API key no longer hardcoded - stored in separate file
- Fixed: Buffer overflow in JSON escape function
- Fixed: Proper null pointer checks for NetInfo()
- Fixed: Memory cleanup on exit (API key, buffers cleared)
- Added: User-friendly error messages when API key missing

### v1.0 (2026-01-25)
- Initial release
- PIN-protected folder locking with encryption
- AI-powered book recommendations

---

## Security Disclaimer

**Vault**: The encryption provides reasonable protection against casual access. It is NOT designed for:
- Military/government-grade security
- Protection against forensic analysis
- Determined attackers with file system access

The PIN hash uses a custom algorithm (not PBKDF2/bcrypt) due to device constraints. For highly sensitive data, use dedicated encryption tools.

**AISearch & Chef**: Your API keys are stored in hidden files on the device. Anyone with USB access can read them. Treat them like passwords. The apps clear API keys from memory on exit.

---

## License

These applications are provided as-is for personal use on PocketBook devices.
