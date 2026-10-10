/* minimal host shim of PalmOS.h - just enough to compile/run vcard.c logic */
#ifndef FAKE_PALMOS_H
#define FAKE_PALMOS_H
#include <stddef.h>

typedef unsigned char  UInt8;   typedef signed char  Int8;
typedef unsigned short UInt16;  typedef short        Int16;
typedef unsigned int   UInt32;  typedef int          Int32;
typedef unsigned char  Boolean; typedef char         Char;
typedef UInt16 Err;             typedef UInt32 LocalID;
typedef void  *MemHandle;       typedef void  *DmOpenRef;

#ifndef true
#define true 1
#define false 0
#endif
#define errNone 0
#define dmRecNumCategories 16
#define dmCategoryLength   16

/* string/mem helpers (libc-backed in stubs.c) */
Int16  StrLen(const Char *s);
Char  *StrCopy(Char *d, const Char *s);
Char  *StrCat(Char *d, const Char *s);
Char  *StrNCopy(Char *d, const Char *s, Int16 n);
Char  *StrNCat(Char *d, const Char *s, Int16 n);
Char  *StrChr(const Char *s, Int16 c);
Char  *StrStr(const Char *s, const Char *sub);
Int16  StrCaselessCompare(const Char *a, const Char *b);
Int16  StrCompare(const Char *a, const Char *b);
Int16  StrNCompare(const Char *a, const Char *b, Int16 n);
Err    MemMove(void *d, const void *s, Int32 n);
Err    MemSet(void *d, Int32 n, UInt8 v);

/* data-manager bits used by vcard.c */
Int16  DmNumRecords(DmOpenRef db);
Err    DmRecordInfo(DmOpenRef db, UInt16 idx, UInt16 *attr, UInt32 *uid, LocalID *ch);

#endif
