/* SecDb.h -- CryptoNote's databases ("sources"), record metadata and the
 * keying used for memos in user-created databases.
 *
 * Sources (the value stored in currentDB / topDB / the table's kRecordColumn):
 *   0 memoPadDB    the ROM Memo Pad database (shown when a migration was declined)
 *   1 cryptoPadDB  CryptoNoteDB, the main database ("Main")
 *   2..            user databases "CryptoNoteDB_<suffix>" (type 'CNsc'), RAM or ROM
 *
 * dbCryptoPadRef always points at the source of the memo being worked on
 * (SrcSelect); dbMainRef always points at CryptoNoteDB.
 *
 * Record marker byte (offset 0, outside any encryption), see the plan
 * (apps/cryptonote_secondary_db.md):
 *   0x80 marked (new format)       0x01 Sec35-wrapped (main only)
 *   0x02 read-only (user flag)     0x04 an 8-byte date trailer ends the record
 *   0x08 user-database keying      0x10 keyed with the database key (no device ID)
 * The trailer (created, modified: UInt32 Palm seconds, 0 = unknown) is plain
 * and is NOT covered by the Sec35 wrap.
 */
#ifndef __SECDB_H__
#define __SECDB_H__

#include <PalmOS.h>
#include "sects.h"

#define kSrcMax				14		/* memo + main + 12 user databases */
#define secDbType			'CNsc'
#define secDbPrefix			"CryptoNoteDB_"
#define secDbPrefixLen		13
#define secDbSuffixMax		18		/* 31-character Palm name limit */
#define secDbKeyLen			16

#define kRecReadOnlyBit		0x02
#define kRecTrailerBit		0x04
#define kRecSecKeyBit		0x08
#define kRecDbKeyBit		0x10
#define kRecTrailerLen		8

extern DmOpenRef	dbMainRef;
extern UInt8		gSrcCount;		/* number of sources, including memo and main */

/* --- sources --- */
void		SrcOpenAll (UInt16 mode) DB3_SECTION;
void		SrcCloseAll (void) DB3_SECTION;
void		SrcReopenAll (UInt16 mode) DB3_SECTION;
DmOpenRef	SrcRef (UInt8 s) DB3_SECTION;
void		SrcSelect (UInt8 s) DB3_SECTION;
Boolean		SrcIsRom (UInt8 s) DB3_SECTION;
Boolean		SrcRecReadOnly (UInt8 s, UInt16 idx) DB3_SECTION;
UInt32		SrcCrDate (UInt8 s) DB3_SECTION;
LocalID		SrcDbID (UInt8 s) DB3_SECTION;
UInt8		SrcOfDbID (LocalID id) DB3_SECTION;
UInt8		SrcOfCrDate (UInt32 crDate) DB3_SECTION;
UInt8		SrcOfRef (DmOpenRef ref) DB3_SECTION;
void		SrcLabel (UInt8 s, Char* out) DB3_SECTION;		/* "Main" / suffix / "+ (ROM)" */
UInt16		SrcRecCategory (UInt8 s, UInt16 idx) DB3_SECTION;	/* mapped to Memo Pad's names */
void		SrcSetRecCategory (UInt8 s, UInt16 idx, UInt16 cat) DB3_SECTION;
UInt16		SrcCountRecords (UInt8 s) DB3_SECTION;
UInt32		SrcSizeBytes (UInt8 s) DB3_SECTION;

/* Create / rename / delete user databases. Return 0 or an alert ID to show. */
UInt16		SrcCreate (const Char* suffix, UInt8* outSrc) DB3_SECTION;
UInt16		SrcRename (UInt8 s, const Char* suffix) DB3_SECTION;
UInt16		SrcDelete (UInt8 s) DB3_SECTION;

/* --- device ID and keying --- */
Boolean		DevHasDeviceID (void) DB3_SECTION;
void		SrcKeyForRecord (UInt8 s, UInt16 idx) DB3_SECTION;	/* before decrypting */
void		SrcKeyForWrite (UInt8 s) DB3_SECTION;				/* before encrypting */

/* --- record metadata --- */
void		MetaCapture (DmOpenRef db, UInt16 idx) DB3_SECTION;	/* before a save path rewrites a record */
void		MetaRebind (DmOpenRef db, UInt16 idx) DB3_SECTION;	/* captured data goes to another record */
void		MetaTouch (void) DB3_SECTION;				/* the pending save changed the text */
void		MetaNoDates (Boolean on) DB3_SECTION;		/* legacy migration: no dates */
UInt8		MetaSealBits (DmOpenRef db, UInt16 idx, Boolean wrapped, UInt32* created, UInt32* modified, Boolean* trailer) DB3_SECTION;
Boolean		MetaGet (DmOpenRef db, UInt16 idx, UInt32* created, UInt32* modified, UInt8* marker) DB3_SECTION;
void		MetaSetReadOnly (DmOpenRef db, UInt16 idx, Boolean on) DB3_SECTION;
UInt32		MetaTrailerLen (const UInt8* raw, UInt32 size) DB3_SECTION;

/* The memo's text in a new handle, decrypted when it has a password (NULL:
 * wrong/missing password or no memory). */
MemHandle	SrcRecordText (UInt8 s, UInt16 idx, Char* password, Boolean* encrypted) DB3_SECTION;

/* --- moving a memo between databases (the memo is open, password known) --- */
Boolean		SrcMoveRecord (UInt8 fromS, UInt16 fromIdx, UInt8 toS, Char* password, UInt16* outIdx) DB3_SECTION;

/* Protection line for Memo Details. */
void		SrcProtectionText (UInt8 s, UInt16 idx, Boolean pwProtected, Char* out) DB3_SECTION;

#endif
