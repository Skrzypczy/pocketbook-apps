/*
 * Shared screen-relative layout for NaSkilla menus/pages.
 * Tuned to match Chef (≈80px rows on Era 1264×1680) and Vault (sw-based).
 */
#ifndef NASKILLA_LAYOUT_H
#define NASKILLA_LAYOUT_H

#include "inkview.h"

static inline int ui_sw(void) { return ScreenWidth(); }
static inline int ui_sh(void) { return ScreenHeight(); }

/* Side margin ~32–48px */
static inline int ui_margin(void)
{
    int m = ui_sw() / 40;
    if (m < 24) return 24;
    if (m > 48) return 48;
    return m;
}

/* Menu / list row height — Chef uses 80 on this class of devices */
static inline int ui_row_h(void)
{
    int h = ui_sh() / 21;
    if (h < 72) return 72;
    if (h > 96) return 96;
    return h;
}

static inline int ui_title_h(void) { return ui_row_h(); }

/* Top reading bar */
static inline int ui_bar_h(void)
{
    int h = ui_sh() / 26;
    if (h < 56) return 56;
    if (h > 72) return 72;
    return h;
}

/* Bottom action strip (Back, etc.) */
static inline int ui_action_h(void) { return ui_row_h(); }

static inline int ui_content_top(void) { return ui_title_h() + ui_row_h() / 2; }

#endif /* NASKILLA_LAYOUT_H */
