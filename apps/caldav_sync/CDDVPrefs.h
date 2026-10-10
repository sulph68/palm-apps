/*
 * CDDVPrefs.h - CalDAV Sync's own prefs database (CalDAVRelayDB). One
 * record holds the whole CDDVPrefsType blob: schema version, global
 * sync-window defaults, and all 8 slots.
 */
#ifndef CDDVPREFS_H
#define CDDVPREFS_H

#include "CalDAVSync.h"

/* Loads into *p, or resets to defaults if the DB doesn't exist or its
 * stored version doesn't match. */
void CDDVLoadPrefs(CDDVPrefsType *p) SEC002;
void CDDVSavePrefs(const CDDVPrefsType *p) SEC002;

/* Baud rate to WiRSa - separate from CDDVPrefsType, see CDDVPrefs.c.
 * Index order mirrors WiRSa's own bauds[] (main.cpp). */
UInt8  CDDVGetBaudIndex(void) SEC002;
void   CDDVSetBaudIndex(UInt8 idx) SEC002;
UInt32 CDDVGetBaud(void) SEC002;

#endif
