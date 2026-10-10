/*
 * UploadForm.h - Upload button on frmMain: full-screen picker over every
 * installed database, then a remote-filename prompt (reuses PromptForm),
 * then FtpStoreDatabase (FtpProtocol.h) to actually send it.
 */
#ifndef UPLOADFORM_H
#define UPLOADFORM_H

#include "LazyFTP.h"

/* Shows the picker; on a real upload, refreshes the current directory
 * listing itself (same as FileActions.c's Delete/Rename/New Folder) since
 * the caller (MainForm.c) can't otherwise tell whether anything changed. */
void UploadFormRun(void) SEC001;

#endif
