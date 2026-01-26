# Contributing to PocketBook Apps

## Infrastructure

### Build Environment
```
┌─────────────────────────────────────────────────────────┐
│  Developer Machine (Windows/Mac/Linux)                  │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Docker Desktop                                   │  │
│  │  ┌─────────────────────────────────────────────┐  │  │
│  │  │  larento/pocketbook-sdk:5.19-a13 (2.25GB)   │  │  │
│  │  │  - ARM cross-compiler                       │  │  │
│  │  │  - inkview.h SDK headers                    │  │  │
│  │  │  - FreeType2 includes                       │  │  │
│  │  └─────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────┘  │
│                          ↓                              │
│  build.bat → Vault.app, AISearch.app                    │
│                          ↓                              │
│  install.bat → USB → PocketBook Era                     │
└─────────────────────────────────────────────────────────┘
```

### Repository Structure
```
pocketbook-apps/
├── .github/
│   └── copilot-instructions.md   # AI assistant guidelines
├── apps/
│   ├── Vault/
│   │   └── Vault.c               # Secure folder locker
│   └── AISearch/
│       └── AISearch.c            # AI book recommendations
├── config/
│   └── .ai_api_key               # Gemini API key (edit this)
├── docs/
│   ├── README.md                 # User documentation
│   └── CONTRIBUTING.md           # This file
├── scripts/
│   ├── build.bat                 # Docker build script
│   └── install.bat               # Device installer
├── build/                        # Build outputs (git-ignored)
│   ├── Vault.app
│   └── AISearch.app
└── .gitignore
```

### Build Commands
```bash
# Build both apps (from repo root)
scripts\build.bat

# Output goes to build\ folder

# Manual build (single app)
docker run --rm -v "$(pwd):/src" -w /src --entrypoint sh \
  larento/pocketbook-sdk:5.19-a13 \
  -c "export LD_LIBRARY_PATH=/sdk/usr/lib && \
      /sdk/usr/bin/arm-obreey-linux-gnueabi-gcc \
      -I/sdk/usr/arm-obreey-linux-gnueabi/sysroot/usr/include/freetype2 \
      apps/AppName/AppName.c -o build/AppName.app -linkview"
```

---

## Code Standards

### File Header Template
```c
/**
 * AppName - Brief description
 * 
 * Target: PocketBook Era (1264x1680)
 * SDK: PocketBook SDK 5.19
 * 
 * Author: Your Name
 * Date: YYYY-MM-DD
 */

#include <inkview.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
// ... other includes
```

### Naming Conventions
| Type | Convention | Example |
|------|------------|---------|
| Functions | PascalCase | `ProcessInput()`, `LoadApiKey()` |
| Variables | snake_case | `input_buffer`, `is_loading` |
| Constants | UPPER_SNAKE | `MAX_PIN_LENGTH`, `CHUNK_SIZE` |
| Static globals | snake_case | `static char status_msg[64]` |
| Structs | PascalCase | `typedef struct { } Button;` |

### Function Documentation
```c
/**
 * Brief description of function.
 * 
 * @param param1  Description of first parameter
 * @param param2  Description of second parameter
 * @return 0 on success, -1 on error
 */
static int FunctionName(int param1, const char *param2) {
    // Implementation
}
```

### Memory Safety Rules
1. **Buffer sizes**: Always define with `#define`, never magic numbers
2. **String copies**: `strncpy(dst, src, sizeof(dst) - 1); dst[sizeof(dst) - 1] = '\0';`
3. **Format strings**: `snprintf(buf, sizeof(buf), "format", args);`
4. **File reads**: Validate length before copying to fixed buffers
5. **Cleanup**: All `malloc` must have corresponding `free` in EVT_EXIT

### Error Handling Pattern
```c
static int DoSomething(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;  // Early return on error
    
    char *buffer = malloc(1024);
    if (!buffer) {
        fclose(fp);      // Clean up previous allocations
        return -1;
    }
    
    // ... do work ...
    
    free(buffer);
    fclose(fp);
    return 0;            // Success
}
```

### UI Layout Pattern
```c
static void InitLayout() {
    int sw = ScreenWidth();   // Don't hardcode 1264
    int sh = ScreenHeight();  // Don't hardcode 1680
    
    // Calculate proportionally
    int btn_width = sw / 5;
    int btn_height = btn_width;
    int margin = sw / 40;
    
    // Center elements
    int start_x = (sw - total_width) / 2;
}
```

---

## Principal Developer Code Review Checklist

### Security Review
- [ ] **No hardcoded secrets** - API keys, PINs in separate files
- [ ] **Passwords hashed** - Using salt, never stored plain
- [ ] **Entropy sources** - Using /dev/urandom where available
- [ ] **Buffer overflows** - All string ops use bounded functions
- [ ] **Integer overflows** - Size calculations checked
- [ ] **Memory cleared** - Sensitive data zeroed on exit

### Memory Review
- [ ] **All mallocs freed** - Check EVT_EXIT handler
- [ ] **Fonts closed** - CloseFont() for each OpenFont()
- [ ] **Files closed** - fclose() for each fopen()
- [ ] **No leaks in loops** - Allocations inside loops freed
- [ ] **Error paths clean up** - Early returns don't leak

### Robustness Review
- [ ] **NULL checks** - All pointer returns validated
- [ ] **File existence** - Graceful handling of missing files
- [ ] **Network failures** - Timeout and error handling
- [ ] **Invalid input** - Bounds checking on user input
- [ ] **Partial writes** - Verify before deleting originals

### PocketBook-Specific Review
- [ ] **Screen size independent** - Uses ScreenWidth()/ScreenHeight()
- [ ] **E-ink optimized** - Minimal updates, proper refresh types
- [ ] **Power aware** - PostponeTimedPoweroff() for long ops
- [ ] **Touch targets** - Buttons large enough for finger
- [ ] **Back button** - Key 28 handled appropriately
- [ ] **Font fallbacks** - Alternative font if primary missing

### Code Quality Review
- [ ] **No warnings** - Compiles clean with -Wall
- [ ] **Consistent style** - Follows naming conventions
- [ ] **Comments** - Complex logic explained
- [ ] **No dead code** - Unused functions removed
- [ ] **Magic numbers** - All constants defined

### Testing Verification
- [ ] **Happy path** - Normal operation works
- [ ] **Error paths** - Missing files, bad input handled
- [ ] **Edge cases** - Empty input, max length input
- [ ] **Memory stress** - Large files don't crash
- [ ] **Device tested** - Actually runs on PocketBook

---

## Pull Request Template

```markdown
## Description
Brief description of changes.

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Security fix
- [ ] Performance improvement
- [ ] Documentation

## Testing
- [ ] Compiled without warnings
- [ ] Tested on PocketBook device
- [ ] Edge cases verified

## Security Checklist
- [ ] No hardcoded secrets
- [ ] Sensitive data cleared on exit
- [ ] Buffer operations bounded

## Screenshots (if UI changes)
[Attach photos of device screen]
```

---

## Version History Format

```markdown
### vX.Y (YYYY-MM-DD)
**AppName:**
- Added: New feature description
- Fixed: Bug that was fixed
- Changed: Behavior that changed
- Removed: Feature that was removed
- Security: Security-related change
```
