#ifndef TM_LIVE_LOG_H
#define TM_LIVE_LOG_H
#include <stdio.h>
typedef struct tm_live_log tm_live_log;
tm_live_log *tm_live_log_open(FILE *sink);
/* One producer, bounded queue; never wait for the output device. */
void tm_live_log_printf(tm_live_log *log,const char *format,...)
    __attribute__((format(printf,2,3)));
/* Drain after playback has closed. Return the number of lost records. */
unsigned tm_live_log_close(tm_live_log *log);
#endif
