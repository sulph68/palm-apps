/*
 * a35lookup.h - Phone Lookup against Address35 (shared by Datebook35, ToDo35).
 *
 * The ROM's PhoneNumberLookup is hard-wired to the built-in Address Book
 * (DmGetNextDatabaseByTypeCreator 'appl'/'addr' + SysAppLaunch with flags 0 -
 * verified by disassembly of the Palm OS 4.1 ROM). Address35 keeps its own
 * database, so the suite apps call A35LookupPhone instead: it sublaunches
 * Address35 with the standard sysAppLaunchCmdLookup / AddrLookupParamsType
 * protocol (plus sysAppLaunchFlagNewGlobals - Address35 is multi-segment),
 * and pastes the "Name - number" it returns over the looked-up word.
 *
 * Falls back to the ROM PhoneNumberLookup when Address35 isn't installed.
 * When Address35's contacts are encrypted and Sec35 is locked, it hands off
 * to Sec35's unlock screen, which switches back to the calling app.
 */
#ifndef A35LOOKUP_H
#define A35LOOKUP_H

#include <PalmOS.h>

#ifndef A35LK_SECTION
#define A35LK_SECTION
#endif

#define a35LookupCreator	'Ad35'

/* Address35 sets params->uniqueID to this (with no resultStringH) when its
 * contacts need the Sec35 key and Sec35 is locked. Signalled through the
 * caller's own parameter block, not SysAppLaunch's result: in POSE64 that
 * 32-bit result came back with its high word zeroed. */
#define a35LookupLockedID	0xFFFFFFFFUL

/* Look up a phone number for the word at (or selection in) fld and paste
 * "Name - number" over it. */
void A35LookupPhone(FieldType *fld) A35LK_SECTION;

#endif
