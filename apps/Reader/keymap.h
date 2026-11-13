/*
 * PocketBook system key mapping (Settings > Key mapping).
 * Loads GetKeyMappingEx and turns key codes into page/navigation actions.
 */
#ifndef SIMPLE_READER_KEYMAP_H
#define SIMPLE_READER_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KA_NONE = 0,
    KA_NEXT,
    KA_PREV,
    KA_NEXT10,
    KA_PREV10,
    KA_FIRST,
    KA_LAST,
    KA_CONTENTS
} keymap_action;

/* Reload from system config. Uses KEYMAPPING_PDF for mupdf, else TXT. */
void keymap_reload(void);

/* Free copied strings. Safe to call when nothing is loaded. */
void keymap_unload(void);

/* Look up action for key. is_long: 0 = short press, 1 = long/repeat. */
keymap_action keymap_lookup(int key, int is_long);

/* 1 if the key has a recognized long-press action (defer short until release). */
int keymap_has_long(int key);

/* 1 if action should repeat while the key is held (par2 > 1). */
int keymap_action_repeatable(keymap_action act);

#ifdef __cplusplus
}
#endif

#endif /* SIMPLE_READER_KEYMAP_H */
