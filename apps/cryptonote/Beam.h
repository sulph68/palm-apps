#include <ExgMgr.h>
#include "sects.h"

#define memoFilenameExtension		".TXT"
#define mimeVersionString			"MIME-Version: 1.0\015\012"
#define simpleBoundary				"simple boundary"
#define delimiter						"--" simpleBoundary
#define crlf							"\015\012"

#define stringZLen -1 				// pass to WriteFunc to calculate strlen

// Stream interface to exgsockets to optimize performance
#define maxStreamBuf 512  // MUST BE LARGER than importBufferMaxLength

typedef struct StreamType {
	ExgSocketPtr socket;
	UInt16 pos;
	UInt16 len;
	UInt16 bufSize;
	Char   buf[maxStreamBuf];
} StreamType;

typedef UInt32 ReadFunctionF (const void * stream, Char * bufferP, UInt32 length);
typedef UInt32 WriteFunctionF (void * stream, const Char * const bufferP, Int32 length);

void MemoSendRecord (DmOpenRef, Int16) BEAM_SECTION;
void MemoSendCategory (DmOpenRef, UInt16) BEAM_SECTION;
void MemoExportMime(MemPtr recordP, void * outputStr,
	WriteFunctionF outputFunc, Boolean outputMimeInfo) BEAM_SECTION;
