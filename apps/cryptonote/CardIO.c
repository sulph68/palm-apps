/*
 * CardIO.c - VFSMgr wrapper implementation. See CardIO.h.
 */
#include "CardIO.h"
#include <VFSMgr.h>

Boolean CardHasVolume(UInt16 *volRefP)
{
	UInt32 ver;
	if (FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &ver) != errNone) return false;
	return (CardFindVolume(volRefP) == errNone);
}

Err CardFindVolume(UInt16 *volRefP)
{
	UInt32 iter = vfsIteratorStart;
	UInt16 volRef;
	Err err;

	err = VFSVolumeEnumerate(&volRef, &iter);   /* first volume */
	if (err == errNone)
		*volRefP = volRef;
	return err;
}

Err CardEnsureDir(UInt16 volRef, const char *path)
{
	Err err = VFSDirCreate(volRef, path);
	if (err == vfsErrFileAlreadyExists)
		return errNone;
	return err;
}

Err CardListDir(UInt16 volRef, const char *path, CardEntry *entries,
                UInt16 maxEntries, UInt16 *countP)
{
	FileRef dir;
	Err err;
	UInt32 iter = vfsIteratorStart;
	FileInfoType info;
	char nameBuf[cardMaxName];
	UInt16 n = 0;

	*countP = 0;
	err = VFSFileOpen(volRef, path, vfsModeRead, &dir);   /* a dir is opened like a file */
	if (err != errNone)
		return err;

	while (iter != vfsIteratorStop && n < maxEntries)
	{
		info.nameP = nameBuf;
		info.nameBufLen = sizeof(nameBuf);
		nameBuf[0] = 0;
		err = VFSDirEntryEnumerate(dir, &iter, &info);
		if (err != errNone)
			break;                 /* end of directory or error */

		entries[n].isDir = (info.attributes & vfsFileAttrDirectory) != 0;
		entries[n].size = 0;
		StrNCopy(entries[n].name, nameBuf, cardMaxName - 1);
		entries[n].name[cardMaxName - 1] = 0;

		if (!entries[n].isDir)     /* fetch file size for display */
		{
			FileRef f;
			char full[cardMaxPath];
			CardJoinPath(full, path, nameBuf);
			if (VFSFileOpen(volRef, full, vfsModeRead, &f) == errNone)
			{
				VFSFileSize(f, &entries[n].size);
				VFSFileClose(f);
			}
		}
		n++;
	}

	VFSFileClose(dir);
	*countP = n;
	return errNone;
}

Err CardReadFile(UInt16 volRef, const char *path, MemHandle *outH, UInt32 *sizeP)
{
	FileRef f;
	Err err = errNone;
	UInt32 size = 0, totalRead = 0;
	MemHandle h;
	UInt8 *p;

	*outH = NULL; *sizeP = 0;
	err = VFSFileOpen(volRef, path, vfsModeRead, &f);
	if (err != errNone)
		return err;
	VFSFileSize(f, &size);

	h = MemHandleNew(size ? size : 1);
	if (!h) { VFSFileClose(f); return memErrNotEnoughSpace; }
	p = (UInt8 *) MemHandleLock(h);

	/* Some VFS slot drivers cap how much a single VFSFileRead call actually
	 * returns (silently reading less than requested, even on a successful
	 * errNone), instead of an outright error -- loop until the whole file
	 * is read or a call makes no progress. A single non-looping read was
	 * the suspected cause of a large (~98KB) file opening the busy dialog
	 * ("Opening...") and then silently going nowhere -- no error, just an
	 * incomplete/zero-length result nothing downstream reported. */
	while (totalRead < size)
		{
		UInt32 nread = 0;
		err = VFSFileRead(f, size - totalRead, p + totalRead, &nread);
		totalRead += nread;
		if (nread == 0) break;			/* no progress -- stop, avoid spinning */
		if (err != errNone && err != vfsErrFileEOF) break;
		}
	MemHandleUnlock(h);
	VFSFileClose(f);

	/* vfsErrFileEOF with a short read is not fatal for a full read */
	if (totalRead < size && err != errNone && err != vfsErrFileEOF)
		{
		MemHandleFree(h);
		return err;
		}
	*outH = h; *sizeP = totalRead;
	return errNone;
}

/* Reads only the first `maxLen` bytes of a file into `buf` (or the whole
 * file if it's smaller), but always reports the file's TOTAL size via
 * `sizeOut` regardless of how much was actually read -- for callers that
 * only need to peek a header (e.g. CardCryptIsEncrypted's magic bytes)
 * without reading a possibly-large whole file into one buffer the way
 * CardReadFile does. `buf` is NOT NUL-terminated or otherwise touched
 * past whatever was actually read. */
Err CardReadFileHeader(UInt16 volRef, const char *path, void *buf, UInt32 maxLen, UInt32 *sizeOut)
{
	FileRef f;
	Err err;
	UInt32 size = 0, want, got = 0;
	UInt8 *p = (UInt8 *) buf;

	*sizeOut = 0;
	err = VFSFileOpen(volRef, path, vfsModeRead, &f);
	if (err != errNone)
		return err;
	VFSFileSize(f, &size);
	*sizeOut = size;

	want = (size < maxLen) ? size : maxLen;
	while (got < want)
		{
		UInt32 nread = 0;
		err = VFSFileRead(f, want - got, p + got, &nread);
		got += nread;
		if (nread == 0) break;
		if (err != errNone && err != vfsErrFileEOF) break;
		}
	VFSFileClose(f);

	if (got < want && err != errNone && err != vfsErrFileEOF)
		return err;
	return errNone;
}

Err CardWriteFile(UInt16 volRef, const char *path, const void *data, UInt32 len)
{
	FileRef f;
	Err err;
	UInt32 nwrite = 0;

	err = VFSFileOpen(volRef, path,
	                  vfsModeReadWrite | vfsModeCreate | vfsModeTruncate, &f);
	if (err != errNone)
		return err;
	if (len)
		err = VFSFileWrite(f, len, data, &nwrite);
	VFSFileClose(f);
	return err;
}

Err CardDeleteFile(UInt16 volRef, const char *path)
{
	return VFSFileDelete(volRef, path);
}

Err CardRename(UInt16 volRef, const char *oldPath, const char *newPath)
{
	/* VFSFileRename takes the NEW leaf name, not a full path; but many drivers
	 * accept a path. Callers in CryptoNote rename within the same dir, passing
	 * the new leaf name as newPath. */
	return VFSFileRename(volRef, oldPath, newPath);
}

Boolean CardExists(UInt16 volRef, const char *path)
{
	FileRef f;
	if (VFSFileOpen(volRef, path, vfsModeRead, &f) == errNone)
	{
		VFSFileClose(f);
		return true;
	}
	return false;
}

void CardJoinPath(char *dest, const char *dir, const char *name)
{
	UInt16 len;
	StrCopy(dest, dir);
	len = StrLen(dest);
	if (len == 0 || dest[len - 1] != '/')
	{
		dest[len++] = '/';
		dest[len] = 0;
	}
	StrCat(dest, name);
}

void CardParentPath(char *path)
{
	Int16 i = StrLen(path) - 1;
	if (i > 0 && path[i] == '/') i--;       /* ignore trailing slash */
	while (i > 0 && path[i] != '/') i--;
	if (i <= 0) { path[0] = '/'; path[1] = 0; }  /* root */
	else path[i] = 0;
}
