# GitHub Copilot Instructions for PocketBook Apps

## Project Overview
PocketBook e-reader applications written in C using the native inkview SDK. Target device is PocketBook Era (1264x1680 E-ink display).

## Technical Stack
- **Language**: C (C99)
- **SDK**: PocketBook SDK 5.19 (inkview.h)
- **Build**: Docker with `larento/pocketbook-sdk:5.19-a13`
- **Compiler**: `arm-obreey-linux-gnueabi-gcc`
- **Target**: ARM Linux (PocketBook Era firmware 6.x)

## Code Style Requirements

### Memory Management
- Always check malloc/calloc return values
- Free all allocated memory before exit
- Use `memset()` to clear sensitive data (PINs, keys, buffers)
- Prefer stack allocation for small fixed-size buffers
- Use chunked processing for large files (4KB chunks)

### String Handling
- Always use `strncpy()` with explicit null termination
- Check buffer sizes before writing
- Use `snprintf()` instead of `sprintf()`
- Validate string lengths from file/user input

### File Operations
- Always check `fopen()` return values
- Use `fflush()` before closing files with important data
- Verify writes succeeded before deleting originals
- Close files in reverse order of opening

### Error Handling
- Return -1 for errors, 0 for success
- Set meaningful status messages for user feedback
- Don't crash on errors - gracefully degrade

### PocketBook-Specific Patterns

```c
// Font handling
ifont *font = OpenFont("LiberationSans", 32, 0);
if (!font) font = OpenFont("LiberationSans", 28, 0);  // Fallback
// ... use font ...
if (font) CloseFont(font);

// Screen updates (E-ink optimization)
Draw();
PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());  // Fast, some ghosting
// OR
FullUpdate();  // Slow, clean refresh

// Event handler pattern
static int Handler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:    // App starting
        case EVT_SHOW:    // Screen visible
        case EVT_HIDE:    // Screen hidden
        case EVT_POINTERUP: // Touch released (par1=x, par2=y)
        case EVT_KEYPRESS:  // Hardware button (par1=key code)
        case EVT_EXIT:    // App closing - cleanup here
    }
    return 0;
}

// Network requests
PostponeTimedPoweroff();  // Prevent sleep during network
iv_netinfo *net = NetInfo();
if (!net || net->connected == 0) {
    NetConnect2("App Name", 1);  // Connect with hourglass
}
char *response = QuickDownloadExt3(url, &size, timeout_ms, NULL, post_data, &error);
```

## Security Requirements

### PIN/Password Storage
- NEVER store plaintext passwords
- Use salted hashes (minimum 16-char salt)
- Get entropy from `/dev/urandom` when available
- Clear sensitive buffers with `memset()` on exit

### Encryption
- Document encryption strength limitations
- Verify writes before deleting originals (data loss prevention)
- Use position-dependent operations to prevent pattern detection

### API Keys
- Store in separate files, not in source code
- Validate key format before use
- Clear from memory on app exit

## File Naming
- Source: `AppName.c` (single file per app)
- Output: `AppName.app`
- Config: `.filename` (hidden on device)

## UI Guidelines (E-ink)
- Use high contrast (BLACK on WHITE)
- Large touch targets (minimum sw/5 width)
- Minimize screen refreshes
- Show loading states for slow operations
- Use `LGRAY` for button backgrounds

## Testing Checklist
- [ ] Compiles without warnings
- [ ] No memory leaks (check EVT_EXIT cleanup)
- [ ] Handles missing files gracefully
- [ ] Touch targets work on 1264x1680 screen
- [ ] Back button (key 28) works correctly
- [ ] Sensitive data cleared on exit

## Common Pitfalls
1. Forgetting to close fonts → memory leak
2. Using `sprintf` → buffer overflow
3. Not checking file operations → silent failures
4. Hardcoding screen dimensions → breaks on other devices
5. Missing `PostponeTimedPoweroff()` → device sleeps during network
