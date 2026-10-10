/* minimal host shim of VFSMgr.h - vcard.c's streaming reader is compiled but
 * never exercised by this host harness (no real card file), so these just
 * need to exist for the link to resolve. */
#ifndef FAKE_VFSMGR_H
#define FAKE_VFSMGR_H

typedef UInt32 FileRef;
#define vfsModeRead 1
#define vfsErrFileEOF 2

Err  VFSFileOpen(UInt16 volRefNum, const char *pathNameP, UInt16 openMode, FileRef *fileRefP);
Err  VFSFileSize(FileRef fileRef, UInt32 *fileSizeP);
Err  VFSFileClose(FileRef fileRef);
Err  VFSFileRead(FileRef fileRef, UInt32 numBytes, void *bufP, UInt32 *numBytesReadP);

#endif
