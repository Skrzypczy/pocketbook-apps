#ifndef SIMPLE_READER_REFRESH_H
#define SIMPLE_READER_REFRESH_H

void refresh_init(void);
int refresh_busy(void);
void refresh_partial(int x, int y, int w, int h);
void refresh_full(void);

#endif
