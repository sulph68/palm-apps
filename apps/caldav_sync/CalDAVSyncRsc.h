/*
 * CalDAVSyncRsc.h - resource ids and pure-integer constants only (no
 * PalmOS.h, no structs/typedefs) - included directly by MainEn.rcp, whose
 * pilrc parser chokes on real C if it sees CalDAVSync.h's full include
 * chain. Same split BitWarden/Address35 use. Hand-written, kept in step
 * with MainEn.rcp by hand (not pilrc-generated).
 */
#ifndef CALDAVSYNCRSC_H
#define CALDAVSYNCRSC_H

/* Not a real SDK global - every app in this suite hand-defines its own
 * creator ID under this name. */
#define appCreator 'CDDV'

/* ---- shared Edit menu (Undo/Cut/Copy/Paste/Select All) for every modal
 * text-entry dialog - same system menu-item IDs (10000-10007) Address35's
 * mnuDlgEdit uses. ------------------------------------------------------- */
#define mnuDlgEdit			1650

/* ---- frmMain: slot list/summary + Sync Now + Edit... ------------------ */
#define frmMain				1000
#define popSlotSelector		1001	/* "1".."8" - MainForm.c relabels dynamically with each slot's alias */
#define lstSlotSelector		1002
#define btnMainEditSlot		1004
#define fldMainTypeDisplay		1005	/* read-only: selected slot's server type */
#define fldMainStatusDisplay	1006	/* read-only: selected slot's last-sync status */
#define btnMainSyncNow		1003
#define btnMainClearDB		1007	/* wipes only the selected slot's own synced DateBook35DAV.pdb entries */

#define menuMain			1600
#define mniMainSyncAll		101
#define mniMainPreferences	102
#define mniMainDeleteDB		103
#define mniMainRecompute	104
#define mniMainHelp			105
#define mniMainDuplicate	106
#define mniMainResort		107

/* ---- frmPrefs (global Preferences) ------------------------------------ */
#define frmPrefs			1100
#define popPrefsPast		1101
#define lstPrefsPast		1102
#define popPrefsFuture		1103
#define lstPrefsFuture		1104
#define lblPrefsGoogleHint	1105	/* persistent "In production" reminder text */
#define popPrefsBaud		1107	/* WiRSa serial baud */
#define lstPrefsBaud		1108
#define btnPrefsOK			1106
#define chkPrefsScreenless	1109	/* Screenless Mode */

/* ---- frmSlotEdit: focused modal, one slot at a time ------------------- */
#define frmSlotEdit			1300
#define fldSlotAlias		1201
#define popSlotType			1202	/* CalDAV / iCloud / Google / ICS Feed */
#define lstSlotType			1203
#define btnSlotEditOK		1301	/* labeled "Save" */
#define btnSlotEditCancel	1302
#define btnSlotEditDelete	1303	/* resets this slot AND wipes its synced DateBook35DAV.pdb entries */

/* CalDAV-generic group - Server URL/Path are ordinary FIELDs that intercept
 * fldEnterEvent to open frmLongText instead of inline typing, matching
 * Address35's EditForm.c "tap to open a sub-dialog" rows (keeps every row
 * the same widget type for even vertical spacing). Username/Password stay
 * normal editable FIELDs. */
#define fldSlotUrl			1210
#define fldSlotPath			1211
#define fldSlotUser			1212
#define fldSlotSecret		1213
#define lblSlot2FAHint		1214
#define chkSlotSecretShow	1215	/* toggles fldSlotSecret between masked/plain */

/* iCloud group - UUID gets the same tap-opens-frmLongText treatment. */
#define fldSlotICloudUser	1220
#define fldSlotICloudSecret	1221
#define fldSlotICloudShard	1222
#define fldSlotICloudUUID	1223
#define fldSlotICloudCal	1224
#define chkSlotICloudSecretShow	1225

/* Google group - Calendar ID can be a long address-like string. Client ID
 * and Client Secret are entered here too (not on WiRSa) - see
 * CalDAVSync.h's own comment on CDDVSlot.googleClientId/googleClientSecret. */
#define fldSlotGoogleCal	1230
#define btnSlotGoogleSignIn	1231
#define fldSlotGoogleClientId	1232
#define fldSlotGoogleClientSecret	1233

/* ICS Feed group */
#define fldSlotIcsUrl		1240
#define lblSlotIcsUrl		1241

/* Static descriptive LABELs for each adaptive group, named so
 * ApplyServerTypeVisibility (SlotEditForm.c) can show/hide them with their
 * FIELD/BUTTON - all 4 groups share one y-band, so a label left out of its
 * group's show/hide list stays permanently visible and overlaps the others. */
#define lblSlotUrl			1280
#define lblSlotPath			1281
#define lblSlotUser			1282
#define lblSlotSecret		1283
#define lblSlotICloudUser	1284
#define lblSlotICloudSecret	1285
#define lblSlotICloudShard	1286
#define lblSlotICloudUUID	1287
#define lblSlotICloudCal	1288
#define lblSlotGoogleCal	1289
#define lblSlotGoogleClientId	1290
#define lblSlotGoogleClientSecret	1291

#define chkSlotIgnore		1250
/* Per-slot sync window is a single "Sync: [from] to [to]" row where each
 * popup's own first item IS "Default" (inherit global Preferences), so no
 * separate enable/disable checkbox is needed. */
#define lblSyncFrom			1256
#define popSlotPastOverride	1252
#define lstSlotPastOverride	1253
#define lblSyncTo			1257
#define popSlotFutureOverride	1254
#define lstSlotFutureOverride	1255

/* ---- frmLongText: shared multi-line editor for any long/URL-like field,
 * mirrors Address35's frmNoteDlg/EditNoteDialog. ------------------------ */
#define frmLongText			1400
#define fldLongText			1401
#define sclLongText			1402
#define btnLongTextDone		1403
#define btnLongTextCancel	1404

/* ---- frmHelp: read-only scrollable reference for every input label,
 * reached from Record > Help. ------------------------------------------- */
#define frmHelp				1450
#define sclHelp				1452
#define btnHelpDone			1453

/* ---- frmSyncProgress: status+bar modal shown for the duration of a sync,
 * mirrors BitWarden's own frmSyncProgress. Status text and bar are hand-
 * drawn (SyncProgress.c), no resource objects needed for either. -------- */
#define frmSyncProgress		1460
#define btnSyncCancel		1461

#define altGoogleProductionWarning	1703
/* ---- frmDupSlot: Record > Duplicate Slot... (DupSlot.c) ---------------- */
#define frmDupSlot			1480
#define popDupFrom			1481
#define lstDupFrom			1482
#define popDupTo			1483
#define lstDupTo			1484
#define btnDupOK			1485
#define btnDupCancel		1486
#define altInfo				1709	/* ^1 - plain information */
#define altConfirmDupOverwrite	1708	/* ^1 = target slot's alias, ^2 = source alias */

#define altGoogleOAuthInstructions	1707	/* IP/PIN + google_auth.py invocation, OAuthRelay.c */

/* ---- shared alerts ----------------------------------------------------- */
#define altErr				1700
#define altConfirmDeleteDB	1701
#define altConfirmRecompute	1702
#define altConfirmResort	1710
#define altSyncOk			1704
#define altConfirmClearSlotDB	1705
#define altConfirmDeleteSlot	1706	/* notes event details are also deleted */

#endif
