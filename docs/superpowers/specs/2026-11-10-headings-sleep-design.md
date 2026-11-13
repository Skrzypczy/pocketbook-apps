# Headings + sleep / bar (2026-11-10)

## Bar (current)

Order: **time | pages/progress + Options (bunched) | Night | Light | Sleep**

- Options + pages both open settings (goto/find/contents).
- Library: hardware Back from reading (no Books bar slot).
- Sleep → `SendGlobalRequest(REQ_KEYLOCK)` after `iv_sleepmode(1)`. `ForcingSleep()` only shortens idle; `GoSleep()` only pauses the process.

## Headings

- Cause: no EPUB `h1`…`h6` scale in UA sheet.
- Fix: `setStyleSheet` **before** `LoadDocument` only, with crengine base UA rules **plus** heading overrides (`!important`).
- Never call `setStyleSheet` from `set_layout` (full restyle hangs the device for minutes).
- `setStyleSheet` replaces the whole sheet — always include base rules, do not ship headings-only CSS.

## Text fallback

- While bitmap Draw is off, show only the current crengine page. Do not append the next page (that repeats text on the next turn).
- Empty space at the bottom is the Inkview font being denser than crengine pagination. Publisher layout does not control it; it only toggles the book's embedded CSS.
