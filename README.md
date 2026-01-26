# PocketBook Apps

Two custom applications for PocketBook e-readers (tested on PocketBook Era).

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
| **AI Recommendations** | Uses Gemini 2.0 Flash for book suggestions |
| **Library Aware** | Reads your Calibre `My books.xml` export |
| **Natural Language** | Ask in plain English for book suggestions |
| **Native Networking** | Uses PocketBook APIs (no external libs) |
| **Battery Efficient** | Network only on-demand, proper power management |
| **Secure API Key** | Key stored in separate file, not hardcoded |

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

**3. Export Your Library:**
- In Calibre: Right-click library → Export catalog → XML
- Save as `My books.xml`
- Copy to PocketBook root (`/mnt/ext1/`)

### Configuration

Edit these values in `AISearch.c` before building:

```c
#define GEMINI_API_KEY_FILE "/mnt/ext1/.ai_api_key"   // API key file location
#define BOOKS_XML_PATH "/mnt/ext1/My books.xml"       // Calibre export path
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
- Calibre library exported as XML

---

## Installation

### Method 1: Copy to Applications Folder

1. Connect PocketBook to computer via USB
2. Navigate to internal storage
3. Copy `.app` files to `/applications/` folder:
   ```
   /mnt/ext1/applications/Vault.app
   /mnt/ext1/applications/AISearch.app
   ```
4. Disconnect and restart PocketBook
5. Apps appear in Applications menu

### Method 2: Direct Execution

1. Copy `.app` files anywhere on device
2. Use file manager to navigate to the file
3. Tap to execute

### Additional Files

For AI Search:
```
/mnt/ext1/.ai_api_key      # Your Gemini API key
/mnt/ext1/My books.xml     # Calibre library export
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

Run `build.bat` (Windows) or manually:

```bash
# Vault
docker run --rm -v "$(pwd):/src" -w /src --entrypoint sh \
  larento/pocketbook-sdk:5.19-a13 \
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && \
      /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc \
      -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 \
      Vault.c -o Vault.app -linkview"

# AISearch
docker run --rm -v "$(pwd):/src" -w /src --entrypoint sh \
  larento/pocketbook-sdk:5.19-a13 \
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && \
      /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc \
      -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 \
      AISearch.c -o AISearch.app -linkview"
```

---

## Technical Details

### Dependencies

| App | Libraries |
|-----|-----------|
| Vault | inkview.h (native), standard C libs |
| AISearch | inkview.h (native), standard C libs |

No external dependencies - both apps use only PocketBook native APIs.

### Power Management

- **Vault**: No background processes, minimal battery impact
- **AISearch**: 
  - Uses `PostponeTimedPoweroff()` during network requests
  - Network only activated when Search is tapped
  - Proper cleanup on exit

### Tested On

- PocketBook Era (1264x1680 resolution)
- Firmware 6.x
- SDK 5.19-a13

---

## Changelog

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

**AISearch**: Your API key is stored in a hidden file on the device. Anyone with USB access can read it. Treat it like a password.

---

## License

These applications are provided as-is for personal use on PocketBook devices.
