# PocketBook SDK Reference (from pmartin/pocketbook-demo)

A developer reference compiled from https://github.com/pmartin/pocketbook-demo - a collection of demos and tools for native PocketBook development.

---

## Table of Contents
1. [Application Structure](#application-structure)
2. [Event Handling](#event-handling)
3. [Screen & Drawing](#screen--drawing)
4. [Fonts](#fonts)
5. [HTTP Requests](#http-requests)
6. [WiFi Control](#wifi-control)
7. [Touch Input](#touch-input)
8. [UI Dialogs & Widgets](#ui-dialogs--widgets)
9. [SQLite Database](#sqlite-database)
10. [JSON Parsing](#json-parsing)
11. [Threading](#threading)
12. [Book/File Operations](#bookfile-operations)
13. [Debugging](#debugging)
14. [Development Tools](#development-tools)
15. [Available Libraries](#available-libraries)

---

## Application Structure

### Basic App Template
```c
#include "inkview.h"

static int main_handler(int event_type, int param_one, int param_two)
{
    if (EVT_INIT == event_type) {
        // Initialize app
    }
    else if (EVT_KEYPRESS == event_type) {
        CloseApp();
    }
    return 0;
}

int main(int argc, char* argv[])
{
    InkViewMain(main_handler);
    return 0;
}
```

### Handler Return Values
- `0` = Event not handled by the application; ereader will deal with it
- `non-0` = Event was handled by application; ereader won't process it further

---

## Event Handling

### Common Events
| Event | Description | param_one | param_two |
|-------|-------------|-----------|-----------|
| `EVT_INIT` | App initialization | - | - |
| `EVT_SHOW` | Screen needs redraw | - | - |
| `EVT_HIDE` | App hidden | - | - |
| `EVT_KEYPRESS` | Physical button press | key code | - |
| `EVT_POINTERDOWN` | Touch start | x | y |
| `EVT_POINTERMOVE` | Touch drag | x | y |
| `EVT_POINTERUP` | Touch release | x | y |
| `EVT_EXIT` | App closing | - | - |

### Key Codes
```c
KEY_PREV    // Previous/Back button
KEY_NEXT    // Next/Forward button
```

### Complete Event Handler Pattern
```c
static int main_handler(int event_type, int param_one, int param_two)
{
    int result = 0;
    
    switch (event_type) {
    case EVT_INIT:
        SetPanelType(0);  // CRITICAL: Disable system panel for full screen
        // Open fonts, initialize state
        break;
    case EVT_SHOW:
        // Draw interface
        break;
    case EVT_KEYPRESS:
        if (param_one == KEY_PREV) {
            CloseApp();
            return 1;  // Event handled
        }
        break;
    case EVT_EXIT:
        // Cleanup: close fonts, free memory
        break;
    default:
        break;
    }
    return result;
}
```

---

## Screen & Drawing

### Screen Functions
```c
int sw = ScreenWidth();   // Get screen width
int sh = ScreenHeight();  // Get screen height
ClearScreen();            // Clear to white
```

### Drawing Primitives
```c
// Lines
DrawLine(x1, y1, x2, y2, color);

// Rectangles
DrawRect(x, y, w, h, color);        // Outline
FillArea(x, y, w, h, color);        // Filled

// Circles
DrawCircle(x, y, radius, color);

// Text (requires SetFont first)
DrawTextRect(x, y, w, h, "text", ALIGN_LEFT);
DrawTextRect(x, y, w, h, "text", ALIGN_CENTER);
DrawTextRect(x, y, w, h, "text", ALIGN_LEFT | VALIGN_TOP);

// Bitmaps
DrawBitmap(x, y, bitmap);
```

### Screen Updates (E-ink Critical!)
```c
FullUpdate();  // Complete refresh - slow but clean, no ghosting
PartialUpdate(x, y, w, h);  // Fast partial update - may have ghosting
```

### Colors
```c
BLACK   // 0x000000
WHITE   // 0xFFFFFF
LGRAY   // Light gray
DGRAY   // Dark gray
// Or use hex: 0x00RRGGBB
```

---

## Fonts

### Font Management
```c
// Open font (name, size, bold)
ifont *font = OpenFont("LiberationSans", 32, 0);      // Regular
ifont *font_bold = OpenFont("LiberationSans", 32, 1); // Bold

// Set active font
SetFont(font, BLACK);  // font, color

// ALWAYS close fonts when done
CloseFont(font);
```

### Available Fonts
- `LiberationSans`
- `LiberationSans-Bold`
- `LiberationMono`

---

## HTTP Requests

### Critical: WiFi Auto-Disconnect Behavior

**PocketBook aggressively disconnects WiFi to save battery.** After a period of inactivity (or after the device sleeps), WiFi will be turned off. This affects how you handle network requests:

| Method | Auto-Reconnects WiFi? | Custom Headers? | Use Case |
|--------|----------------------|-----------------|----------|
| `QuickDownloadExt` | ✅ Yes (shows dialog) | ❌ No | Simple GET/POST requests |
| `QuickDownloadExt3` | ✅ Yes (shows dialog) | ❌ No | Simple requests with error codes |
| Session-based | ✅ Yes | ❌ Limited | File downloads |
| **libcurl** | ❌ **NO** | ✅ Yes | APIs requiring Content-Type, auth headers |

### Method 1: QuickDownloadExt (Simple, Recommended for Basic Requests)
```c
// Auto-shows WiFi dialog if not connected!
// Best for simple GET/POST without custom headers
PostponeTimedPoweroff();  // Prevent device sleep during network op

const char *url = "http://example.com/api";
int retsize;
char *cookie = NULL;
char *post = NULL;  // or "key=value&key2=value2" for POST

void *result = QuickDownloadExt(url, &retsize, 15, cookie, post);
// timeout is in seconds

if (result) {
    // Use result as (char *)result
    free(result);  // Don't forget to free!
}
```

### Method 2: Session-based (Async, Download to File)
```c
int session = NewSession();
iv_sessioninfo *sinf = GetSessionInfo(session);
sinf->response = 0;  // Initialize for polling

const char *url = "https://example.com/file";
const char *filename = USERDATA TEMPDIR "/download.dat";

SetUserAgent(session, "MyApp/1.0");
int result = DownloadTo(session, url, NULL, filename, 15);

// Poll for completion
int status = GetSessionStatus(session);
while (status >= 0 && sinf->response == 0) {
    GoSleep(250, 1);  // Wait
    status = GetSessionStatus(session);
    sinf = GetSessionInfo(session);
}

// Check sinf->response for HTTP status code
CloseSession(session);
```

### Method 3: libcurl (Full Control - REQUIRES MANUAL WIFI!)

**⚠️ CRITICAL**: libcurl does NOT auto-connect WiFi! You MUST manually ensure WiFi is connected before using curl. If WiFi disconnected while device was idle, curl requests will fail silently or return connection errors.

```c
#include "curl/curl.h"

// Response buffer for curl
typedef struct {
    char *data;
    size_t size;
} CurlResponse;

static size_t CurlWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    CurlResponse *resp = (CurlResponse *)userp;
    char *ptr = realloc(resp->data, resp->size + total + 1);
    if (!ptr) return 0;
    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, total);
    resp->size += total;
    resp->data[resp->size] = '\0';
    return total;
}

int make_api_request(const char *url, const char *json_body, char **response) {
    PostponeTimedPoweroff();  // Prevent sleep during network
    
    // STEP 1: Ensure WiFi is connected (curl won't auto-connect!)
    iv_netinfo *net = NetInfo();
    if (!net || net->connected == 0) {
        int conn_result = NetConnect2(NULL, 1);  // NULL = last network, 1 = show hourglass
        if (conn_result != 0) {
            return -1;  // WiFi connection failed
        }
        // Verify connection
        net = NetInfo();
        if (!net || net->connected == 0) {
            return -2;  // Still not connected
        }
    }
    
    // STEP 2: Now safe to use curl
    CURL *curl = curl_easy_init();
    if (!curl) return -3;
    
    CurlResponse resp = {0};
    struct curl_slist *headers = NULL;
    
    // Set custom headers (why we use curl!)
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        free(resp.data);
        return -4;  // curl error
    }
    
    *response = resp.data;
    return 0;  // Success
}
```

### When to Use Each Method

**Use QuickDownloadExt/QuickDownloadExt3 when:**
- Making simple GET or POST requests
- No custom headers needed (like `Content-Type: application/json`)
- You want automatic WiFi reconnection

**Use libcurl when:**
- API requires `Content-Type: application/json` header (most modern APIs!)
- Need custom authentication headers
- Need fine-grained control over request/response
- **Remember: Always call NetConnect2() first!**

### Common Pitfall: "Works After Reboot, Fails Later"

If your app works right after device reboot but fails after the device has been idle:
1. WiFi was connected after reboot (from sync or user activity)
2. Device went idle, PocketBook disconnected WiFi to save power
3. Your curl request fails because WiFi is off and curl doesn't reconnect

**Solution**: Always check `NetInfo()->connected` and call `NetConnect2()` before curl requests.

---

## WiFi Control

### Check Connection Status
```c
iv_netinfo *netinfo = NetInfo();
if (netinfo->connected) {
    // WiFi is connected
}
```

### Connect to WiFi
```c
// network_name = NULL uses last known network
int result = NetConnect2(NULL, 1);  // 1 = show hourglass
if (result == 0) {
    // Connected successfully
}
```

### Disconnect
```c
NetDisconnect();
```

### Complete WiFi Activation Pattern
```c
int wifi_activate() {
    iv_netinfo *netinfo = NetInfo();
    if (netinfo->connected) {
        return 0;  // Already connected
    }
    
    int result = NetConnect2(NULL, 1);
    if (result != 0) {
        return 1;  // Connection failed
    }
    
    netinfo = NetInfo();
    if (netinfo->connected) {
        return 0;  // Success
    }
    return 2;  // Unknown failure
}
```

---

## Touch Input

### Touch Events
```c
// EVT_POINTERDOWN, EVT_POINTERMOVE, EVT_POINTERUP
// param_one = x coordinate, param_two = y coordinate

case EVT_POINTERDOWN:
case EVT_POINTERMOVE:
case EVT_POINTERUP:
    iv_mtinfo *touch = GetTouchInfo();
    int x = touch->x;
    int y = touch->y;
    break;
```

### Touch State Pattern
```c
static bool is_touched;
static int x, y, old_x, old_y;

case EVT_POINTERDOWN:
case EVT_POINTERMOVE:
    old_x = x; old_y = y;
    iv_mtinfo *touch = GetTouchInfo();
    x = touch->x;
    y = touch->y;
    is_touched = (event_type == EVT_POINTERDOWN || event_type == EVT_POINTERMOVE);
    break;
case EVT_POINTERUP:
    is_touched = false;
    break;
```

---

## UI Dialogs & Widgets

### Message (Auto-dismiss)
```c
// Shows for specified milliseconds
Message(ICON_INFORMATION, "Title", "Message content", 3000);  // 3 seconds
```

### Dialog (Non-blocking with callback)
```c
static void *dialog_handler(int button) {
    // button = 1 for first button, 2 for second
    return NULL;
}

Dialog(ICON_QUESTION, "Title", "Question?", "Yes", "No", 
       (iv_dialoghandler)dialog_handler);
// Code continues immediately! Callback fires when button pressed.
```

### DialogSynchro (Blocking)
```c
int result = DialogSynchro(ICON_QUESTION, "Title", "Question?", "Yes", "No", NULL);
// result = 1 for first button, 2 for second
// Code blocks until button is pressed
```

### Menu
```c
static void menu_handler(int index) {
    // index = selected item index
}

// Create menu structure
imenu menu[] = {
    { ITEM_ACTIVE, 0, "Option 1", NULL },
    { ITEM_ACTIVE, 1, "Option 2", NULL },
    { ITEM_SEPARATOR, 0, NULL, NULL },
    { ITEM_ACTIVE, 2, "Option 3", NULL },
    { 0, 0, NULL, NULL }  // Terminator
};

OpenMenu(menu, 0, x, y, menu_handler);
```

### Progress Bar
```c
// With timer updates
int percent = 0;

static void progress_timer() {
    percent += 10;
    UpdateProgressbar("Loading...", percent);
    if (percent < 100) {
        SetHardTimer("PROGRESS", progress_timer, 500);  // Update every 500ms
    }
}

static void progress_handler(int button) {
    // Called if user cancels
}

OpenProgressbar(ICON_INFORMATION, "Title", "Loading...", 0, progress_handler);
SetHardTimer("PROGRESS", progress_timer, 500);
```

### Icons
- `ICON_INFORMATION`
- `ICON_QUESTION`
- `ICON_WARNING`
- `ICON_ERROR`

---

## SQLite Database

### Basic Database Operations
```c
#include "sqlite3.h"

sqlite3 *db;
char *err_msg;

// Open database
int rc = sqlite3_open("/mnt/ext1/mydb.sqlite3", &db);
if (rc != SQLITE_OK) {
    // Error: sqlite3_errmsg(db)
}

// Execute with callback
static int callback(void *data, int argc, char **argv, char **col_name) {
    for (int i = 0; i < argc; i++) {
        printf("%s = %s\n", col_name[i], argv[i] ? argv[i] : "NULL");
    }
    return 0;
}

rc = sqlite3_exec(db, "SELECT * FROM books", callback, NULL, &err_msg);
if (rc != SQLITE_OK) {
    // Error: err_msg
    sqlite3_free(err_msg);
}

// Close
sqlite3_close(db);
```

### System Database Path
```c
#define BOOKS_DB_PATH "/mnt/ext1/system/explorer-3/explorer-3.db"
```

---

## JSON Parsing

### Using json-c Library
```c
#include "json-c/json.h"

// Parse JSON string
const char *json_str = "{\"name\": \"value\", \"num\": 42}";
struct json_object *parsed = json_tokener_parse(json_str);

// Get values
struct json_object *name_obj;
if (json_object_object_get_ex(parsed, "name", &name_obj)) {
    const char *name = json_object_get_string(name_obj);
}

struct json_object *num_obj;
if (json_object_object_get_ex(parsed, "num", &num_obj)) {
    int num = json_object_get_int(num_obj);
}

// Create JSON
struct json_object *obj = json_object_new_object();
json_object_object_add(obj, "key", json_object_new_string("value"));
json_object_object_add(obj, "number", json_object_new_int(123));
const char *result = json_object_to_json_string(obj);

// Cleanup
json_object_put(parsed);
json_object_put(obj);
```

---

## Threading

### Using pthreads
```c
#include <pthread.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t thread1, thread2;

static void *thread_function(void *arg) {
    pthread_mutex_lock(&mutex);
    // Thread-safe code
    pthread_mutex_unlock(&mutex);
    
    pthread_exit(NULL);
}

// Create thread
pthread_create(&thread1, NULL, thread_function, NULL);

// Wait for thread
pthread_join(thread1, NULL);
```

---

## Book/File Operations

### Open Book in Reader
```c
// Opens system reader app
const char *filepath = "/mnt/ext1/Books/mybook.epub";
const char *parameters = "r";  // read mode
int flags = 0;
OpenBook(filepath, parameters, flags);
```

### Get Book Information
```c
bookinfo *info = GetBookInfo("/mnt/ext1/Books/mybook.epub");
// info->title
// info->author
// info->size
// info->lang
// info->identifiers
```

### Get Book Cover
```c
ibitmap *cover = GetBookCover("/mnt/ext1/Books/mybook.epub", 120, 120);
if (cover) {
    DrawBitmap(x, y, cover);
}
```

### Get File Type Info
```c
iv_filetype *type = FileType("/path/to/file.epub");
// type->type
// type->extension
// type->description
// type->program (handler app)

const char *handler = GetFileHandler("/path/to/file.epub");
```

### File Operations
```c
FILE *fp = iv_fopen("/path/file", "rb");
int count = iv_fread(buffer, 1, size, fp);
iv_fwrite(buffer, 1, size, fp);
iv_fclose(fp);

iv_unlink("/path/file");  // Delete file
```

---

## Debugging

### Remote Debugging with gdbserver

1. **On PocketBook** - Create a launcher script:
```bash
#!/bin/sh
/mnt/ext1/system/usr/bin/gdbserver 0.0.0.0:10002 /mnt/ext1/applications/myapp.app
```

2. **On Computer** - Connect with gdb:
```bash
$PBSDK/bin/arm-obreey-linux-gnueabi-gdb myapp.app
(gdb) target remote 192.168.x.x:10002
(gdb) break main_handler
(gdb) continue
```

### Useful gdb Commands
```
(gdb) break file.cpp:17        # Set breakpoint
(gdb) continue                 # Resume execution
(gdb) step / s                 # Step into
(gdb) next / n                 # Step over
(gdb) print variable           # Print value
(gdb) info args                # Show function arguments
(gdb) info locals              # Show local variables
(gdb) bt                       # Backtrace
```

### Compile with Debug Symbols
```
arm-obreey-linux-gnueabi-gcc app.c -o app.app -linkview -g -gdwarf-3
```

---

## Development Tools

### App Sender/Receiver (WiFi Deploy)

**On PocketBook** - Run receiver app that listens on port 10003

**On Computer**:
```bash
./app-sender.sh ~/path/to/myapp.app "myapp.app" "192.168.0.12"
```

### DevUtils Commands
```bash
# Activate WiFi
devutils.app 'wifi:activate'

# Deactivate WiFi  
devutils.app 'wifi:deactivate'
```

---

## Available Libraries

Link with these when compiling:

| Library | Link Flag | Purpose |
|---------|-----------|---------|
| inkview | `-linkview` | Core PocketBook SDK (required) |
| curl | `-lcurl` | HTTP requests with full control |
| sqlite3 | `-lsqlite3` | Database operations |
| json-c | `-ljson-c` | JSON parsing |
| pthread | (built-in) | Threading |

### Compilation Example
```bash
arm-obreey-linux-gnueabi-gcc app.c -o app.app \
    -linkview -lcurl -lsqlite3 -ljson-c \
    -Wall -Wextra -g -gdwarf-3
```

---

## Important Constants & Paths

```c
// User data directory
USERDATA          // Usually /mnt/ext1

// Temp directory  
TEMPDIR           // /system/tmp

// Applications folder
"/mnt/ext1/applications/"

// Books database
"/mnt/ext1/system/explorer-3/explorer-3.db"

// Combined temp path
USERDATA TEMPDIR  // "/mnt/ext1/system/tmp"
```

---

## Best Practices Summary

1. **Always call `SetPanelType(0)` first in EVT_INIT** - Prevents layout shift bugs
2. **Close all fonts in EVT_EXIT** - Prevents memory leaks
3. **Use `PartialUpdate()` for small changes** - Faster than `FullUpdate()`
4. **Use `FullUpdate()` periodically** - Clears ghosting on e-ink
5. **Call `PostponeTimedPoweroff()` before network ops** - Prevents sleep during download
6. **Free memory from `QuickDownloadExt()`** - It allocates memory
7. **Check return values** - All functions can fail
8. **Use `snprintf()` not `sprintf()`** - Buffer overflow protection
9. **Return 1 from handler for handled events** - Prevents system from processing them
10. **WiFi auto-connects with inkview HTTP functions** - But not with raw curl

---

## References

- Source: https://github.com/pmartin/pocketbook-demo
- SDK: https://github.com/pocketbook-free/SDK_481
- Our SDK docs: [docs/sdk/inkview.h](sdk/inkview.h)
