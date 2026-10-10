#ifndef  __UTILS_H__
#define  __UTILS_H__

#include "Main.h"

MemPtr GetActiveFormObjectPtr(UInt16 objectID);
MemPtr GetFocusObjectPtr(void);

/* Safe replacement for ROM StrLen() on anything that might exceed 32767
 * bytes (StrLen returns Int16 and silently wraps/garbage past that) -- see
 * Utils.c for the full story. Use this for card-file/memo BODY text, which
 * this app allows up to ~60000 bytes; ordinary short strings (titles,
 * filenames, paths) are fine with plain StrLen. */
UInt32 SafeStrLen(const Char *s);

#endif /* __UTILS_H__ */
