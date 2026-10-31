/*
 * DupSlot.h - Record > Duplicate Slot...: copy one slot's settings (and
 * login) to another slot. Design: apps/carddav_sync.md §3.3.
 */
#ifndef DUPSLOT_H
#define DUPSLOT_H

#include "CalDAVSync.h"

/* Asks for the source (preselected: current) and target slot, confirms
 * before replacing a slot in use, then copies. True if prefs->slots changed
 * (the caller refreshes and saves); *target = the slot copied to. */
Boolean DupSlotRun(CDDVPrefsType *prefs, UInt8 current, UInt8 *target) SEC001;

#endif
