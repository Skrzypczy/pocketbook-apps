#ifndef SIMPLE_READER_FRONTLIGHT_H
#define SIMPLE_READER_FRONTLIGHT_H

/* Declares Get/SetFrontlightEnabled when missing from inkview.h. */
int frontlight_available(void);
int frontlight_is_on(void);
int frontlight_get_brightness(void);   /* 0 if off/unavailable */
int frontlight_get_warmth(void);       /* INT_MIN if unavailable */
int frontlight_set_on(int on, int brightness_1_100);
int frontlight_set_brightness(int brightness_1_100);
int frontlight_set_warmth(int warmth_0_100);

#endif
