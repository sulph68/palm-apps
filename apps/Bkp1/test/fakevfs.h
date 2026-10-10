/* Test build only (-DBKP1_FAKE_VFS): Bkp1's card calls are redirected to a backup embedded in the
 * PRC as resources, so the real screens and the real restore can run in POSE, which has no card.
 * Included at the end of compat/VfsMgr.h, i.e. after the real declarations. Read-only: there is
 * one volume holding one backup; creating, writing, deleting and renaming fail. */
#ifndef _FAKEVFS_H_
#define _FAKEVFS_H_

Err FakeVolumeEnumerate(UInt16 *volRefNumP, UInt32 *volIteratorP);
Err FakeVolumeGetLabel(UInt16 volRefNum, Char *labelP, UInt16 bufLen);
Err FakeDirCreate(UInt16 volRefNum, const Char *dirNameP);
Err FakeFileCreate(UInt16 volRefNum, const Char *pathNameP);
Err FakeFileOpen(UInt16 volRefNum, const Char *pathNameP, UInt16 openMode, FileRef *fileRefP);
Err FakeFileClose(FileRef fileRef);
Err FakeFileSize(FileRef fileRef, UInt32 *fileSizeP);
Err FakeFileRead(FileRef fileRef, UInt32 numBytes, void *bufP, UInt32 *numBytesReadP);
Err FakeFileWrite(FileRef fileRef, UInt32 numBytes, const void *dataP, UInt32 *numBytesWrittenP);
Err FakeFileSeek(FileRef fileRef, FileOrigin origin, Int32 offset);
Err FakeFileDelete(UInt16 volRefNum, const Char *pathNameP);
Err FakeFileRename(UInt16 volRefNum, const Char *pathNameP, const Char *newNameP);
Err FakeDirEntryEnumerate(FileRef dirRef, UInt32 *dirEntryIteratorP, FileInfoType *infoP);

#define VFSVolumeEnumerate		FakeVolumeEnumerate
#define VFSVolumeGetLabel		FakeVolumeGetLabel
#define VFSDirCreate			FakeDirCreate
#define VFSFileCreate			FakeFileCreate
#define VFSFileOpen				FakeFileOpen
#define VFSFileClose			FakeFileClose
#define VFSFileSize				FakeFileSize
#define VFSFileRead				FakeFileRead
#define VFSFileWrite			FakeFileWrite
#define VFSFileSeek				FakeFileSeek
#define VFSFileDelete			FakeFileDelete
#define VFSFileRename			FakeFileRename
#define VFSDirEntryEnumerate	FakeDirEntryEnumerate

#endif
