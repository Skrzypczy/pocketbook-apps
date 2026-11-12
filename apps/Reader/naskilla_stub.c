/*
 * Minimal NaSkilla launcher probe — no engines, no curl, no png.
 * Proves the PocketBook can start our binary and write logs.
 */
#include "inkview.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void boot_log(const char *msg)
{
    const char *paths[] = {
        "/mnt/ext1/naskilla_boot.log",
        "/mnt/ext1/system/state/naskilla_boot.log",
        "/tmp/naskilla_boot.log",
        NULL
    };
    int i;
    mkdir("/mnt/ext1/system", 0755);
    mkdir("/mnt/ext1/system/state", 0755);
    for (i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "a");
        if (fp) {
            fputs(msg, fp);
            fputc('\n', fp);
            fclose(fp);
        }
    }
}

static void draw_home(void)
{
    int sw = ScreenWidth();
    int sh = ScreenHeight();
    ifont *font = OpenFont("LiberationSans", 36, 1);
    if (!font)
        font = OpenFont("LiberationSans", 28, 0);

    ClearScreen();
    FillArea(0, 0, sw, sh, WHITE);
    if (font)
        SetFont(font, BLACK);
    DrawTextRect(20, sh / 2 - 80, sw - 40, 60, "NaSkilla Reader",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(20, sh / 2, sw - 40, 40, "Stub OK — logging works",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    DrawTextRect(20, sh - 80, sw - 40, 40, "Back to exit",
                 ALIGN_CENTER | VALIGN_MIDDLE);
    FullUpdate();
}

static int handler(int type, int par1, int par2)
{
    (void)par2;
    switch (type) {
    case EVT_INIT:
        boot_log("stub: EVT_INIT");
        SetPanelType(0);
        break;
    case EVT_SHOW:
        boot_log("stub: EVT_SHOW");
        draw_home();
        break;
    case EVT_KEYPRESS:
        boot_log("stub: KEY");
        if (par1 == KEY_BACK)
            CloseApp();
        break;
    case EVT_POINTERUP:
        boot_log("stub: POINTER");
        break;
    case EVT_EXIT:
        boot_log("stub: EXIT");
        break;
    default:
        break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    boot_log("stub: main entered");
    InkViewMain(handler);
    boot_log("stub: InkViewMain returned");
    return 0;
}
