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
- Don't crash on errors - show clear error message to user
- **NO FALLBACKS**: Main features must work correctly; don't add fallback code paths that hide failures

### PocketBook-Specific Patterns

```c
// CRITICAL: Disable system panel for full-screen apps
// Must be called FIRST in EVT_INIT to prevent layout shift issues
SetPanelType(0);  // 0 = disabled, gives full screen access

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

// Event handler pattern - CORRECT ORDER IS CRITICAL
static int Handler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            SetPanelType(0);  // FIRST! Disable system panel
            // Open fonts...
            // InitLayout()...
            // Set initial state...
            break;
        case EVT_SHOW:
            Draw();
            PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
            break;
        case EVT_HIDE:    // Screen hidden
        case EVT_POINTERUP: // Touch released (par1=x, par2=y)
        case EVT_KEYPRESS:  // Hardware button (par1=key code)
        case EVT_EXIT:    // App closing - cleanup here
    }
    return 0;
}

// Network requests - IMPORTANT: QuickDownloadExt auto-shows WiFi dialog!
PostponeTimedPoweroff();  // Prevent sleep during network
// No manual NetConnect2 needed - QuickDownloadExt handles it automatically
char *response = QuickDownloadExt3(url, &size, timeout_ms, NULL, post_data, &error);

// HOWEVER: If using libcurl (for Content-Type headers), you MUST connect WiFi manually:
// PocketBook disconnects WiFi to save power - curl won't auto-reconnect!
iv_netinfo *net = NetInfo();
if (!net || net->connected == 0) {
    NetConnect2(NULL, 1);  // NULL = last network, 1 = show hourglass
}
// Then use curl...
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
6. **Missing `SetPanelType(0)` in EVT_INIT** → UI draws at wrong position, shifts when touched (system panel steals screen space)

## Advanced SDK Patterns

### HTTP Requests - Three Methods

```c
// Method 1: QuickDownloadExt (simple, auto-shows WiFi dialog)
void *result = QuickDownloadExt(url, &retsize, timeout_sec, cookie, post);
// IMPORTANT: result must be free()'d

// Method 2: Session-based (async, download to file)
int session = NewSession();
iv_sessioninfo *sinf = GetSessionInfo(session);
SetUserAgent(session, "MyApp/1.0");
DownloadTo(session, url, postdata, filename, timeout_sec);
// Poll GetSessionStatus() and sinf->response
CloseSession(session);

// Method 3: libcurl (full control, requires -lcurl)
CURL *curl = curl_easy_init();
curl_easy_setopt(curl, CURLOPT_URL, url);
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
curl_easy_perform(curl);
curl_easy_cleanup(curl);
```

### WiFi Control Pattern
```c
int wifi_activate() {
    iv_netinfo *netinfo = NetInfo();
    if (netinfo->connected) return 0;  // Already connected
    
    int result = NetConnect2(NULL, 1);  // NULL = last network, 1 = show hourglass
    if (result != 0) return 1;  // Failed
    
    netinfo = NetInfo();
    return netinfo->connected ? 0 : 2;  // Verify connection
}
```

### Handler Return Values
```c
// 0 = Event not handled - system will process it
// 1 = Event handled - system won't process it further
// ALWAYS return 1 after CloseApp() to prevent system handling
```

### UI Dialogs
```c
// Blocking dialog (waits for response)
int result = DialogSynchro(ICON_QUESTION, "Title", "Message", "Yes", "No", NULL);

// Non-blocking dialog (callback-based)
Dialog(ICON_INFO, "Title", "Message", "OK", NULL, callback_handler);

// Auto-dismiss message
Message(ICON_INFORMATION, "Title", "Message", 3000);  // 3 seconds
```

### Touch Event Handling
```c
case EVT_POINTERDOWN:
case EVT_POINTERMOVE:
case EVT_POINTERUP:
    iv_mtinfo *touch = GetTouchInfo();
    int x = touch->x;
    int y = touch->y;
    // EVT_POINTERUP = touch released (best for button clicks)
    break;
```

### Book Information APIs
```c
bookinfo *info = GetBookInfo("/path/to/book.epub");
// info->title, info->author, info->size, info->lang

ibitmap *cover = GetBookCover("/path/to/book.epub", 120, 120);
DrawBitmap(x, y, cover);

const char *handler = GetFileHandler("/path/to/file");
```

## SDK Reference
See [docs/POCKETBOOK_SDK_REFERENCE.md](../docs/POCKETBOOK_SDK_REFERENCE.md) for complete API documentation.
