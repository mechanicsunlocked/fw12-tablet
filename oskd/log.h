/* Minimal logging. Goes to stderr, which systemd routes to the journal. */
#ifndef FW12_LOG_H
#define FW12_LOG_H

#include <stdio.h>
#include <string.h>
#include <errno.h>

extern int fw12_debug; /* set from FW12_OSKD_DEBUG=1 */

#define log_info(...)  do { fprintf(stderr, "fw12-oskd: " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define log_warn(...)  do { fprintf(stderr, "fw12-oskd: warning: " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define log_err(...)   do { fprintf(stderr, "fw12-oskd: error: " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define log_dbg(...)   do { if (fw12_debug) { fprintf(stderr, "fw12-oskd: debug: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

#define log_errno(msg) log_err("%s: %s", msg, strerror(errno))

#endif
