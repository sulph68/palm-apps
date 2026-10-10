/*
 * CardIO.h - thin VFSMgr wrapper for CryptoNote's SD-card ("Card") source.
 *
 * This is the ONLY place that talks to the VFS Manager. Everything above it
 * (the Card list source, the browser, open/save, encrypt/decrypt) is storage-
 * agnostic. Each call maps 1:1 to the VFSMgr traps validated by VfsProbe on a
 * real device, so the device build and any emulator build share this code.
 *
 * Paths are absolute, '/'-separated, e.g. "/Notes", "/Notes/Work/todo.txt".
 * All functions return a Palm Err (errNone == 0 on success); callers surface the
 * code on-screen (no device logs).
 */
#ifndef CARDIO_H
#define CARDIO_H

#include <PalmOS.h>
/* NB: VFSMgr.h is included ONLY by CardIO.c, not here -- pulling it into some
 * callers' include context triggers a VFSMgr.h trap-macro parse error. This
 * header exposes no VFS types, so callers never need it. */

#define cardMaxPath   256      /* max absolute path length we handle           */
#define cardMaxName   64       /* max single entry name (display/leaf)         */
#define cardNotesDir  "/Notes" /* top-level notes folder (app creates it)      */

typedef struct {
	Boolean	isDir;             /* true = subdirectory ([Folder])               */
	UInt32	size;              /* file size in bytes (0 for dirs / if unknown) */
	char	name[cardMaxName]; /* leaf name only (no path)                     */
} CardEntry;

/* First mounted VFS volume (the SD card). errNone + *volRefP, or a VFS error
 * (e.g. expErrEnumerationEmpty / no card). */
Err CardFindVolume(UInt16 *volRefP);
/* True if the VFS Manager exists AND a volume is mounted; *volRefP set to it.
 * Use this (not CardFindVolume) to decide whether to show the "Card" source. */
Boolean CardHasVolume(UInt16 *volRefP);
/* Create a directory. Treats "already exists" as success. */
Err CardEnsureDir(UInt16 volRef, const char *path);
/* Enumerate the entries of a directory into caller's array (up to maxEntries).
 * *countP receives how many were filled. Directories are flagged isDir; entries
 * are returned in filesystem order (caller may sort). */
Err CardListDir(UInt16 volRef, const char *path, CardEntry *entries,
                UInt16 maxEntries, UInt16 *countP);

/* Read a whole file into a freshly allocated handle (caller MemHandleFree's it).
 * *outH + *sizeP set on success. */
Err CardReadFile(UInt16 volRef, const char *path, MemHandle *outH, UInt32 *sizeP);
/* Peek just the first maxLen bytes (or the whole file if smaller) into buf,
 * without reading a possibly-large whole file into one buffer. *sizeOut
 * always gets the file's TOTAL size regardless of how much was read.
 * AES_SECTION (unlike the rest of this file) -- main is critically tight
 * and this is the newest addition; its only caller (MainFormOpenCardFile)
 * is already sec001 too. */
Err CardReadFileHeader(UInt16 volRef, const char *path, void *buf, UInt32 maxLen, UInt32 *sizeOut) AES_SECTION;
/* Create/truncate a file and write the buffer. */
Err CardWriteFile(UInt16 volRef, const char *path, const void *data, UInt32 len);
Err CardDeleteFile(UInt16 volRef, const char *path);
Err CardRename(UInt16 volRef, const char *oldPath, const char *newPath);
/* Does a file/dir exist? */
Boolean CardExists(UInt16 volRef, const char *path);
/* Join dir + leaf into dest (dest >= cardMaxPath). Handles the root and a
 * trailing slash on dir. e.g. ("/Notes","Work") -> "/Notes/Work". */
void CardJoinPath(char *dest, const char *dir, const char *name);
/* Pop the last path component in place (ascend). "/Notes/Work" -> "/Notes";
 * "/Notes" -> "/Notes" (floored at the notes root by the caller). */
void CardParentPath(char *path);
#endif /* CARDIO_H */
