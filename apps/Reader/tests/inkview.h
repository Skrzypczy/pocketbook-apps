/* Minimal stub for host tests */
#ifndef INKVIEW_STUB_H
#define INKVIEW_STUB_H
#define ALIGN_CENTER 1
#define ALIGN_LEFT 2
#define VALIGN_MIDDLE 4
#define BLACK 0
#define WHITE 0xFFFFFF
#define LGRAY 0xAAAAAA
#define DGRAY 0x555555
int ScreenWidth(void);
int ScreenHeight(void);
void FillArea(int x, int y, int w, int h, int c);
void DrawLine(int x1, int y1, int x2, int y2, int c);
void DrawTextRect(int x, int y, int w, int h, const char *s, int f);
#endif
