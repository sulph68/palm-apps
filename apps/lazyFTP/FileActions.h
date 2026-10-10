/*
 * FileActions.h - per-file/folder actions reached from MainForm.c: the
 * action popup shown on a FILE tap (Preview/Download, Delete, Rename),
 * and the Edit menu (Cut/Paste/New Folder/Rename Folder/Delete).
 */
#ifndef FILEACTIONS_H
#define FILEACTIONS_H

#include "LazyFTP.h"

/* Shows the contextual action alert for a tapped FILE (never called for a
 * directory - that's still plain CWD navigation in MainForm.c) and carries
 * out whichever action is chosen. May refresh the current directory
 * listing itself (Delete/Rename actually change it) - caller should still
 * redraw the hand-drawn listing area afterward, same as any other flow
 * that returns from a sub-dialog. */
void FileActionRun(const char *name) SEC001;

/* Edit > New Folder - prompts for a name, creates it via FtpMakeDir,
 * refreshes the listing on success. */
void NewFolderRun(void) SEC001;

/* Edit > Delete - acts on the current two-tap selection (Listing.h's
 * gListSelActive/gListSelName/gListSelIsDir): a folder gets the same
 * "ALL of its contents... cannot be undone" confirm and recursive delete
 * Folder > Del Folder used to (now reachable from wherever it's browsed,
 * not just from inside it); a file gets the same confirm/delete the
 * file-action sheet's own Delete button uses (see FileActionRun) - one
 * dialog, one code path, reachable both ways. Alerts if nothing is
 * selected. Replaces the old Folder > Del Folder entirely. */
void DoDelete(void) SEC001;

/* Edit > Cut - captures the current two-tap selection (Listing.h's
 * gListSelActive/gListSelName/gListSelIsDir) into a persistent cut buffer
 * (absolute source path snapshotted now, since Paste happens after
 * navigating away) via FtpJoinPath. Alerts if nothing is selected. */
void DoCut(void) SEC001;

/* Edit > Paste - moves the cut buffer's file/directory to the CURRENT
 * directory via FtpRename (RNFR/RNTO) - a real move, not a copy, and works
 * identically for a file or a directory (and its entire contents) since the
 * server implements rename as a single generic filesystem rename(). Alerts
 * if the buffer is empty or the destination is the same directory the item
 * is already in; keeps the buffer intact on a transport failure so the user
 * can retry. Refreshes the listing (showing the moved item) on success. */
void DoPaste(void) SEC001;

/* True iff Edit > Paste currently has something to act on - the title-bar
 * cut indicator (Listing.h's ListingDrawCutIndicator) queries this rather
 * than duplicating the buffer's own state. */
Boolean FileActionsHasCut(void) SEC001;

/* Clears the cut buffer unconditionally (and redraws the title-bar
 * indicator to match) - called by DoPaste's own success path, and by
 * MainForm.c's DoConnect the moment a NEW connection is started, since a
 * buffer left over from a previous session/server shouldn't silently carry
 * forward into a different one. */
void FileActionsClearCut(void) SEC001;

/* Edit > Rename Folder - same PromptFormRun flow as the file-action sheet's
 * own Rename, but acts on the current two-tap selection (must be a
 * directory) instead of a tapped filename, since folders have no action
 * sheet of their own. Same-directory rename only (unlike Cut/Paste, which
 * move across directories) - alerts if nothing/a file is selected. */
void DoRenameFolder(void) SEC001;

#endif
