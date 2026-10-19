#ifndef GUI_ACTIVITY_LOG_H
#define GUI_ACTIVITY_LOG_H

#include <exec/types.h>
#include <intuition/classusr.h>

/* Activity log: every line is shown in the GUI log panel (an NList of
 * strings) and appended to PROGDIR:WHDSelector.log with a timestamp. */

void alog_init(Object *nlist);
void alog_close(void);

void alog(const char *fmt, ...);

/* Output capture for whdfetch's code: while active, its printf output
 * (stdout) and everything written to the process Output() - including
 * c:lha / c:unzip run through SystemTagList() - goes to temporary files.
 * alog_capture_end() copies those lines into the log, without ANSI/CSI
 * control codes and progress-bar redraws. */
void alog_capture_begin(void);
void alog_capture_end(void);

#endif
