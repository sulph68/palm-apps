/* See fakevfs.h. The embedded backup: resource 'bkix' 1 = { UInt16 numFiles; { UInt16 firstPart,
 * numParts; UInt32 size; } [numFiles] }, and each file's bytes in consecutive 'bkpf' resources
 * (a resource cannot exceed 64K). Built by test/build_fake.sh. One file is open at a time. */
#include <PalmOS.h>
#include <VFSMgr.h>

#define REF_DIR		0x7FFF0001UL

static UInt16 gFirst, gParts;		/* of the open file */
static UInt32 gSize, gPos;
static Boolean gOpen;

static UInt16 fakeNumFiles(void)
{
	MemHandle h = DmGetResource('bkix', 1); UInt16 n = 0;
	if (h) { n = *(UInt16 *) MemHandleLock(h); MemHandleUnlock(h); DmReleaseResource(h); }
	return n;
}

static void fakeEntry(UInt16 idx, UInt16 *firstP, UInt16 *partsP, UInt32 *sizeP)
{
	MemHandle h = DmGetResource('bkix', 1); UInt8 *p = MemHandleLock(h);
	*firstP = *(UInt16 *) (p + 2 + idx * 8); *partsP = *(UInt16 *) (p + 4 + idx * 8); *sizeP = *(UInt32 *) (p + 6 + idx * 8);
	MemHandleUnlock(h); DmReleaseResource(h);
}

static UInt32 fakeReadAt(UInt32 pos, UInt8 *dst, UInt32 n)
{
	UInt32 done = 0, partStart = 0; UInt16 part;
	for (part = 0; part < gParts && done < n; part++) {
		MemHandle h = DmGetResource('bkpf', gFirst + part); UInt32 len = MemHandleSize(h);
		if (pos + done < partStart + len) {
			UInt32 off = pos + done - partStart, now = len - off;
			if (now > n - done) now = n - done;
			MemMove(dst + done, (UInt8 *) MemHandleLock(h) + off, now); MemHandleUnlock(h); done += now;
		}
		DmReleaseResource(h); partStart += len;
	}
	return done;
}

Err FakeVolumeEnumerate(UInt16 *volRefNumP, UInt32 *volIteratorP)
{
	if (*volIteratorP != vfsIteratorStart) return expErrEnumerationEmpty;
	*volRefNumP = 1; *volIteratorP = vfsIteratorStop; return errNone;
}
Err FakeVolumeGetLabel(UInt16 volRefNum, Char *labelP, UInt16 bufLen) { StrCopy(labelP, "Embedded"); return errNone; }
Err FakeDirCreate(UInt16 volRefNum, const Char *dirNameP) { return errNone; }
Err FakeFileCreate(UInt16 volRefNum, const Char *pathNameP) { return expErrUnsupportedOperation; }
Err FakeFileWrite(FileRef fileRef, UInt32 numBytes, const void *dataP, UInt32 *numBytesWrittenP) { return expErrUnsupportedOperation; }
Err FakeFileDelete(UInt16 volRefNum, const Char *pathNameP) { return expErrUnsupportedOperation; }
Err FakeFileRename(UInt16 volRefNum, const Char *pathNameP, const Char *newNameP) { return expErrUnsupportedOperation; }

Err FakeFileOpen(UInt16 volRefNum, const Char *pathNameP, UInt16 openMode, FileRef *fileRefP)
{
	UInt16 len = StrLen(pathNameP), idx = 0, i; UInt8 attr1 = 0;
	const Char *p;

	/* a backup file is ".../XXXXXXXX.PRC" or ".PDB"; anything else is the directory of backups */
	if (len < 13 || pathNameP[len - 4] != '.' || pathNameP[len - 13] != '/') { *fileRefP = REF_DIR; return errNone; }
	p = pathNameP + len - 12;
	for (i = 0; i < 8; i++) {
		Char c = p[i]; UInt16 v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 0xff;
		if (v == 0xff) return vfsErrFileNotFound;
		idx = (idx << 4) | v;
	}
	if (idx >= fakeNumFiles()) return vfsErrFileNotFound;
	fakeEntry(idx, &gFirst, &gParts, &gSize);
	(void) fakeReadAt(33, &attr1, 1);					/* low byte of the header's attributes */
	if (((attr1 & 1) != 0) != (p[10] == 'R')) return vfsErrFileNotFound;	/* .PRC = resource database */
	gPos = 0; gOpen = true; *fileRefP = (FileRef) (idx + 1);
	return errNone;
}
Err FakeFileClose(FileRef fileRef) { if (fileRef != REF_DIR) gOpen = false; return errNone; }
Err FakeFileSize(FileRef fileRef, UInt32 *fileSizeP) { *fileSizeP = gSize; return errNone; }
Err FakeFileSeek(FileRef fileRef, FileOrigin origin, Int32 offset)
{
	if (origin != vfsOriginBeginning || (UInt32) offset > gSize) return sysErrParamErr;
	gPos = offset; return errNone;
}
Err FakeFileRead(FileRef fileRef, UInt32 numBytes, void *bufP, UInt32 *numBytesReadP)
{
	UInt32 got = fakeReadAt(gPos, bufP, numBytes);
	gPos += got; if (numBytesReadP) *numBytesReadP = got;
	return got < numBytes ? vfsErrFileEOF : errNone;
}
Err FakeDirEntryEnumerate(FileRef dirRef, UInt32 *dirEntryIteratorP, FileInfoType *infoP)
{
	if (*dirEntryIteratorP != vfsIteratorStart) return expErrEnumerationEmpty;
	StrCopy(infoP->nameP, "Embedded backup"); infoP->attributes = vfsFileAttrDirectory;
	*dirEntryIteratorP = vfsIteratorStop; return errNone;
}
