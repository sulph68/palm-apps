/*
 * LazyFTP.h - shared resource IDs, app-wide types, and cross-file
 * declarations for lazyFTP. No separate resource header (this suite's
 * convention for apps this size) - the .rcp resource IDs are #defined here.
 */
#ifndef LAZYFTP_H
#define LAZYFTP_H

#include <PalmOS.h>
#include "lz_seg.h"

/* ---- resource ids -------------------------------------------------- */
#define frmMain			1000
#define sclMain			1002
#define btnUpload		1004
#define bmpFolder		1005
#define bmpCardUp		1006
#define fldMainFilter	1007
#define btnMainCancel	1008
#define btnMainPrevPage	1009
#define btnMainNextPage	1010
#define bmpCut			1011

#define frmConnect		1200
#define popConnSlot		1201
#define lstConnSlot		1202
#define fldConnServer	1203
#define fldConnPort		1204
#define fldConnUser		1205
#define fldConnPass		1206
#define btnConnConnect	1207
#define btnConnCancel	1208
#define ctlConnAnon		1216
#define ctlConnShow		1217
#define popConnRetry	1219
#define lstConnRetry	1220

#define frmPrefs			1300
#define popPrefsTtl			1301
#define lstPrefsTtl			1302
#define chkPrefsDisconnect	1304
#define btnPrefsOk			1305
#define btnPrefsCancel		1306

#define frmPrompt		1400
#define fldPromptValue	1401
#define btnPromptOk		1402
#define btnPromptCancel	1403
#define lblPromptText	1410

#define frmPreview			1500
#define fldPreviewText		1501
#define sclPreview			1502
#define btnPreviewImport	1503
#define btnPreviewClose		1504

#define frmLogs				1520
#define fldLogsText			1521
#define sclLogs				1522
#define btnLogsClose		1523

#define mniConnect			1601
#define mniDisconnectServer	1602
#define mniNewFolder		1603
#define mniAbout			1604
#define mniRefresh			1605
#define mniPreferences		1606
#define mniDelete			1607
#define mniDisconnectNetwork	1608
#define mniLogs				1609
#define mniCut				1610
#define mniPaste			1611
#define mniRenameFolder		1612

#define altErr			1700
#define altErr2			1701
#define altAbout		1702
#define altConfirmDelete	1705
#define altImportDone		1706
#define altConfirmOverwrite	1707
#define altInstalled		1708
#define altNeedsReset		1709
#define altConfirmDelFolder	1720

#define frmFileAction		1710
#define fldFileActionMsg	1711
#define btnFileAction0		1712
#define btnFileAction1		1713
#define btnFileAction2		1714
#define btnFileAction3		1715

#define frmUpload			1800
#define lstUpload			1801
#define btnUploadCancel		1802
#define fldUploadFilter		1803

/* Transfer progress - centered modal, title set at runtime to "Uploading"/
 * "Downloading", bar drawn by C code same as Sec35's own frmProgress. */
#define frmXferProgress		1900
#define ctlXferCancel		1901

#define appCreator		'LzTP'

/* ---- app-wide entry points ------------------------------------------- */
Boolean MainFormHandleEvent(EventPtr eventP) SEC001;
Boolean ConnectFormHandleEvent(EventPtr eventP) SEC001;

/* Re-runs LIST for the current directory and redraws - exported so
 * FileActions.c's Delete/Rename/New Folder flows (which change the
 * directory's contents) can trigger a refresh the same way MainForm.c's
 * own CWD navigation does. */
void MainRefreshList(void) SEC001;

/* ---- busy / progress dialogs (BusyDialog.c) --------------------------- */
void LzTPBusyBegin(const char *title) SEC001;
void LzTPBusyEnd(void) SEC001;

/* FtpStatusProc-shaped (see FtpProtocol.h's FtpSetStatusHook). Registered
 * once at StartApplication for FtpAutoReconnect's own "Reconnecting..."
 * signal. `status` is intentionally unused now - see BusyDialog.c's own
 * comment for the real crash (colliding with PalmOS's own network redial
 * UI) that this was changed to fix: it just tears the busy overlay down
 * instead of relabeling it, getting out of the way before a possible OS
 * redial rather than trying to coexist with it. For the DIFFERENT case of
 * relabeling a busy dialog's text mid-wait (no NetLib call in flight, so
 * none of that risk applies), see LzTPBusyRetitle + FtpSetConnRetryHook
 * instead. */
void LzTPBusyStatusHook(const char *status) SEC001;

/* Swaps the currently-shown busy dialog's title for `title` (end+re-begin -
 * LzTPBusyBegin/End have no separate "just change the text" call). Used for
 * FtpConnRetryProc's "Retry N in Ns..." countdown (FtpProtocol.h) - safe to
 * call repeatedly since it only ever runs between attempts, never while a
 * NetLib call is blocked (see FtpConnRetryProc's own comment for why that
 * distinction matters, contrasted with LzTPBusyStatusHook just above). */
void LzTPBusyRetitle(const char *title) SEC001;

/* Transfer progress - a real modal form (frmXferProgress), same design as
 * Sec35's own frmProgress. `body` (may be NULL/empty) is drawn once,
 * centered, typically the remote filename - matches Sec35's own optional
 * body line shown above its bar. LzTPProgressBar is FtpProgressProc-shaped
 * (see FtpProtocol.h) so it can be registered directly via
 * FtpSetProgressHook with no adapter needed. */
void LzTPProgressBegin(const char *title, const char *body) SEC001;
void LzTPProgressBar(UInt32 done, UInt32 total) SEC001;
void LzTPProgressEnd(void) SEC001;

/* Switches the progress dialog's title mid-flight (e.g. "Uploading" ->
 * "CRC Checking") and resets the bar/auto-off throttle so the next
 * LzTPProgressBar call reads as a fresh phase, not a stall. */
void LzTPProgressSetPhase(const char *title) SEC001;

/* True once the user has tapped Cancel on the progress form. Polled by
 * FtpProtocol.c's transfer callbacks (via FtpSetCancelQuery) to abort a
 * transfer in progress; reset by each LzTPProgressBegin. */
Boolean LzTPProgressCancelled(void) SEC001;

#endif
