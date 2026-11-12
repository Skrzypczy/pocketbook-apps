#ifndef SIMPLE_READER_EXTENSIONS_H
#define SIMPLE_READER_EXTENSIONS_H

#include <stddef.h>

#define EXTCFG_PATH "/mnt/ext1/system/config/extensions.cfg"
#define EXTCFG_BAK  "/mnt/ext1/system/config/extensions.cfg.simple-reader-bak"
#define EXTCFG_HANDLER_MAX 64
#define EXTCFG_HANDLERS_MAX 16
#define EXTCFG_LABEL_MAX 48

/* Human label for the current first epub handler (or "unavailable"). */
void extcfg_current_label(char *buf, size_t n);

/* 1 if the library default for managed types is NaSkilla / Reader.app. */
int extcfg_naskilla_is_default(void);

/* Cycle to the next available reader and rewrite managed lines. 0=ok. */
int extcfg_cycle_default(void);

/*
 * Pure rewriter for host tests and the device writer.
 * preferred is a handler basename (e.g. "Reader.app", "koreader.app",
 * "eink-reader_with_epub2.app"). Stock eink-reader* names are normalized
 * per extension. Returns malloc'd text or NULL on OOM.
 */
char *extcfg_rewrite_text(const char *in_text, const char *preferred);

#endif
