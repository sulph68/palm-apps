/*
 * LzTPLog.h - lightweight run-scoped FTP command/connection logger. See
 * LzTPLog.c's own header comment for the storage design.
 */
#ifndef LZTPLOG_H
#define LZTPLOG_H

#include "LazyFTP.h"

/* Wipes any log left over from a previous run and starts a fresh, empty
 * one - call once from StartApplication. */
void LzTPLogInit(void) SEC002;

/* Appends one line (no trailing newline needed - LzTPLogReadAll adds the
 * separators). Silently drops the oldest entries first if needed to keep
 * the total under budget - see LzTPLog.c. Safe to call even if the log DB
 * couldn't be created (no-ops). */
void LzTPLogWrite(const char *msg) SEC002;

/* Reads the whole log into a freshly allocated, newline-joined, NUL-
 * terminated MemHandle (caller owns it - MemHandleFree when done), for
 * LogForm.c's viewer. Returns false (outH untouched) if there's nothing to
 * show. */
Boolean LzTPLogReadAll(MemHandle *outH, UInt32 *outLen) SEC002;

#endif
