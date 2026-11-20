/*
 * encrekey.h - the record loop behind every app's kSecLaunchRekey handler.
 *
 * Sec35 sublaunches each suite app with an EncRekeyParam (see enccore.h);
 * the app calls EncRekeyDb once per database it owns, giving two small
 * functions that know its own record format:
 *   isEnc  - is this record encrypted (by the app's own flag)?
 *   crypt  - convert ONE record in place: decrypt with fromKey (NULL = it is
 *            plaintext), encrypt with toKey (NULL = leave it plaintext), and
 *            set/clear the app's encrypted flag. On failure it must leave the
 *            record untouched and return memErrNotEnoughSpace (out of memory)
 *            or another error.
 * EncRekeyDb picks the keys for the operation and the database's key scheme,
 * walks the records, keeps a checkpoint so a reset part-way can be resumed
 * without converting any record twice, marks the database as scheme 2 at the
 * end, and fills the progress bar Sec35 asked for.
 */
#ifndef ENCREKEY_H
#define ENCREKEY_H

#include <PalmOS.h>
#include "enccore.h"

#ifndef ENCREKEY_SECTION
#define ENCREKEY_SECTION ENCSTORE_SECTION
#endif

typedef Boolean (*EncRecIsEncFn) (const UInt8* rec, UInt32 size);
typedef Err     (*EncRecCryptFn) (DmOpenRef db, UInt16 index,
                                  const UInt8* fromKey, const UInt8* toKey);

/* One database. dbOrdinal numbers the app's databases for this operation
 * (0, 1, 2...) so the checkpoint knows which one it was in. ckptCreator is the
 * app's creator (the checkpoint is an unsaved preference of the app).
 * Returns kRekeyResultDone, kRekeyResultLowMem or kRekeyResultFailed. */
UInt16 EncRekeyDb (DmOpenRef db, UInt32 storeId, const EncRekeyParam* p,
                   UInt16 dbOrdinal, UInt32 ckptCreator,
                   EncRecIsEncFn isEnc, EncRecCryptFn crypt) ENCREKEY_SECTION;

/* The keys EncRekeyDb would use for a database in this scheme (marked =
 * EncDbHasStoreKeys), for apps whose stores are not record databases
 * (preferences blocks, one-record databases). from/to are set to NULL when
 * the operation has no such side. */
void EncRekeyKeys (const EncRekeyParam* p, UInt32 storeId, Boolean marked,
                   UInt8 fromKey[ENC_KEY_BYTES], Boolean* haveFrom,
                   UInt8 toKey[ENC_KEY_BYTES], Boolean* haveTo) ENCREKEY_SECTION;

/* Draw the progress bar in p->progress (done of total). */
void EncRekeyProgress (const EncRekeyParam* p, UInt32 done, UInt32 total) ENCREKEY_SECTION;

/* For a store that is one block (a preferences struct, a feature) rather
 * than records: call Begin with the block as it is now; false = this block was
 * already converted for this operation (resumed after a reset) -- skip it.
 * true = convert it, write it in one go, then call End. dbOrdinal numbers it
 * among the app's stores, as for EncRekeyDb. */
Boolean EncRekeyBlobBegin (const EncRekeyParam* p, UInt16 dbOrdinal, UInt32 ckptCreator,
                           const void* blob, UInt32 size) ENCREKEY_SECTION;
void    EncRekeyBlobEnd (const EncRekeyParam* p, UInt16 dbOrdinal, UInt32 ckptCreator) ENCREKEY_SECTION;

/* Forget the checkpoint (an app calls it once the whole operation is done). */
void EncRekeyClearCheckpoint (UInt32 ckptCreator) ENCREKEY_SECTION;

#endif /* ENCREKEY_H */
