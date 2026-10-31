/*
 * DavView.h - read-only view of the contacts CalDAV Sync keeps in
 * "Address35DAV_<n>" (CardDAV / iCloud Contacts slots). The record layout is
 * ContactDb.h in apps/caldav_sync - keep the two in step. All SEC003.
 */
#ifndef DAVVIEW_H
#define DAVVIEW_H

#include <PalmOS.h>
#include "AddressDB35.h"
#include "vcard.h"

#define davMaxSlots		8
#define davNameMax		64
#define davSideMax		34

enum { davSlotNone = 0, davSlotOk = 1, davSlotLocked = 2 };

/* Is there a (non-stale) database for slot (0..7)? out gets "iCloud - Alias"
 * or "CardDAV - Alias"; with Sec35 locked the alias can't be read, out is the
 * type name alone and the result davSlotLocked. */
Int16   DavSlotLabel(UInt8 slot, char *out) SEC003;
/* Indexes of the records in category cat (dmAllCategories: all) whose names
 * contain term (caseless; "" = all), in database order. */
UInt16  DavBuild(UInt8 slot, UInt16 cat, const char *term, UInt16 *out, UInt16 cap) SEC003;
UInt16  DavCount(UInt8 slot) SEC003;
/* Name parts and the right-hand list value of record idx. *locked: the list
 * value couldn't be decrypted (side is ""). */
Boolean DavRow(UInt8 slot, UInt16 idx, char *last, char *first, char *company,
	char *side, Boolean *locked) SEC003;
/* Parse record idx into *r (values in *arena). 0 ok, 1 Sec35 locked, 2 error. */
Int16   DavLoad(UInt8 slot, UInt16 idx, A35Record *r, VCardArena *arena) SEC003;
/* Category names of slot's database (for the popup): same as CategoryGetName
 * on the opened database. Returns the open database (caller closes) or NULL. */
DmOpenRef DavOpen(UInt8 slot) SEC003;

/* Phone Lookup: the phone numbers, emails and social / IM handles of record idx, read from its vCard text
 * (label "Mob"/"Hom"/"Wrk"/"Fax"/"Main"/"Pgr"/"Oth"/"Email", value). *n gets the count (<= cap).
 * 0 ok, 1 Sec35 locked, 2 error. */
#define davFlagSocial	0x04		/* header flags: no list value, but IM / social handles */
/* kind 0: phone or email (label "Mob"... / "Email"); 1: social / IM handle (label = the network,
 * "" for a network Address35 doesn't know, whose value then reads "Network: handle") */
typedef struct { char label[12]; char value[64]; UInt8 kind; } DavItem;
Int16   DavItems(UInt8 slot, UInt16 idx, DavItem *out, UInt8 cap, UInt8 *n) SEC003;

/* "Me" per CardDAV book, independent of Local's: the contact is remembered by a hash of its vCard
 * UID (the record IDs change on every sync, the UID doesn't). DavMeFind: record index of slot's Me
 * contact, 0xFFFF if none / gone. DavSetViewed: the contact the record view shows now;
 * DavMeSetViewed makes it its book's Me. */
UInt16  DavMeFind(UInt8 slot) SEC003;
void    DavSetViewed(UInt8 slot, UInt16 idx) SEC003;
void    DavMeSetViewed(void) SEC003;

/* GoTo from Datebook35's birthday rows: gp->matchCustom = davGoToMagic | slot
 * (0..7), gp->recordNum = record index in Address35DAV_<slot+1>. Opens the
 * contact in the read-only Record form; if the book can't be read (Sec35
 * locked, record gone) says so and shows the main list. */
#define davGoToMagic	0x44415600UL	/* 'DAV' << 8 */
void    DavGoTo(const GoToParamsType *gp) SEC003;

#endif
