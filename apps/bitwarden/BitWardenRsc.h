/*
 * BitWardenRsc.h - resource ids and pure-integer constants only (no
 * PalmOS.h, no structs/typedefs) - included directly by BitWardenEn.rcp,
 * whose pilrc parser chokes on real C (`#include <PalmOS.h>` with angle
 * brackets in particular) if it ever sees BitWarden.h's own full include
 * chain. Same split Address35 already uses (AddressRsc.h vs. Main.h) -
 * kept in step with BitWardenEn.rcp.
 */
#ifndef BITWARDENRSC_H
#define BITWARDENRSC_H

/* ---- resource ids ---------------------------------------------------- */
#define frmMain				1000
#define popMainFolder		1002	/* top-right: All / Folders / Orgs, dotted-separated */
#define lstMainFolder		1003
#define popMainType			1004	/* bottom-left: All / Login / Card / Note / Identity */
#define lstMainType			1008
#define fldMainFilter		1005	/* bottom row, between the two: search text */
#define tblMain				1006
#define sclMain				1009
#define btnMainClear		1007	/* bottom-right: clears fldMainFilter */

/* Per-item-type row icons (icon/mkicon_types.py, 11x7 1-bit Tbmp) - drawn
 * by MainDrawRow before each row's title, user-supplied art. */
#define bmpTypeLogin		1800
#define bmpTypeNote			1801
#define bmpTypeCard			1802
#define bmpTypeIdentity		1803

/* Record view is FOUR separate, purpose-built forms (one per itemType), not
 * one shared layout with relabeled/hidden rows. Login/Card/Identity all
 * share the SAME dynamic mechanism (RecordForm.c's own AddFieldLine/
 * BuildXxxFields/FieldsDrawLine): only POPULATED fields shown, one row per
 * field ("Label: value", empty fields omitted entirely - a LABEL can't
 * conditionally disappear the way a table row can just not exist), Notes
 * folded in as more rows of the same list rather than a separate field,
 * table+scrollbar spanning nearly the whole form, Show/Done sharing one
 * bottom button row. MainForm.c picks which form to open via
 * BwStoreGetItemType(); RecordForm.c's own RecordFormHandleEvent handles
 * all four, dispatching on FrmGetActiveForm()'s own ID internally. Note is
 * the one exception - a single item has only one field worth showing, so it
 * keeps its own plain FIELD widget, just with the same bottom Show/Hide+Done
 * row as the other three for visual consistency (masks the note text; there
 * being nothing genuinely secret about a note is beside the point - explicit
 * design instruction). */
#define frmRecordLogin		1100
#define tblRecordLogin		1101
#define sclRecordLogin		1109
#define btnRecordShow		1102
#define btnRecordDone		1103

#define frmRecordNote		1150
#define fldRecordNoteText	1151
#define btnRecordNoteDone	1152
#define btnRecordNoteShow	1153
#define sclRecordNote		1154

#define frmRecordCard			1160
#define tblRecordCard			1161
#define sclRecordCard			1162
#define btnRecordCardShow		1163
#define btnRecordCardDone		1166

#define frmRecordIdentity		1170
#define tblRecordId				1171
#define sclRecordId				1172
#define btnRecordIdShow			1173
#define btnRecordIdDone			1176

#define frmPrefs			1200
#define fldPrefsServer		1201
#define btnPrefsSyncNow		1202
#define btnPrefsForgetTrust	1203
#define btnPrefsOk			1204
#define btnPrefsCancel		1205
#define lblPrefsFingerprint	1206	/* just "Yes"/"No" now - see btnPrefsShowFingerprint */
#define fldPrefsEmail		1207
#define popPrefsTtl			1208
#define lstPrefsTtl			1209
#define btnPrefsShowFingerprint	1214	/* 1210-1213 already used by this
									 * form's own decorative LABELs (Server:/
									 * Email:/Trusted:/Lock after:) */
#define popPrefsBaud		1216	/* Baud rate - must match whatever WiRSa's own
								 * serial-speed menu setting currently is; see
								 * Serial.c's own comment on why this isn't
								 * hardcoded */
#define lstPrefsBaud		1217	/* 1215 reserved for this row's own decorative LABEL */

/* Full-screen fingerprint viewer, reached from Preferences' "Show" button -
 * same text-display shape as frmTrust's own cert-data area (plain,
 * non-underlined, scrollable), just OK instead of Trust/Cancel. */
#define frmFingerprint			1250
#define fldFingerprintData		1251
#define sclFingerprint			1252
#define btnFingerprintOk		1253

#define frmUnlock			1300
#define fldUnlockPassword	1301
#define btnUnlockOk			1302
#define btnUnlockCancel		1303
#define lblUnlockPrompt		1304
#define chkUnlockShow		1305

/* Vaultwarden 2FA challenge (PROTOCOL.md's TWO_FACTOR_REQUIRED/
 * TWO_FACTOR_SUBMIT) - run from BwSyncRun's own receive loop (Sync.c) mid-
 * sync, same modal-popup shape as frmUnlock but with an extra FIELD above
 * the code entry for the provider hint WiRSa/mock sent ("Authenticator app
 * code", "Email code sent to j...n@example.com", ...) - not EDITABLE (same
 * technique as frmTrust's own fldTrustQuestion: a plain display FIELD
 * populated via FldSetTextPtr, not a LABEL, since the hint string is
 * dynamic and can run longer than a LABEL's own fixed compiled buffer would
 * safely hold - see the documented FrmCopyLabel/FrmCopyTitle buffer-size
 * gotcha). The code itself is NOT masked (unlike the master password) - a
 * 2FA code isn't as sensitive and Show/Hide has no purpose here. */
#define frmTwoFactor			1350
#define fldTwoFactorLabel		1351	/* provider hint, e.g. "Authenticator app code" */
#define lblTwoFactorPrompt		1352
#define fldTwoFactorCode		1353
#define btnTwoFactorOk			1354
#define btnTwoFactorCancel		1355

/* Options > Security > 2FA - lets the user pick how the 2FA code field
 * above (fldTwoFactorCode) takes input: normal text/Graffiti, or a
 * numeric keypad popup (much faster for a 6-8 digit authenticator code).
 * Same shape as Sec35's own frmPrefs entry-mode picker (sec35.c/sec35.rcp)
 * - LABEL + POPUPTRIGGER + LIST + OK/Cancel, bottom-aligned per this
 * suite's own standing dialog rule. */
#define frmTwoFactorPrefs		1360
#define lblTwoFactorPrefsType	1361
#define popTwoFactorPrefsType	1362
#define lstTwoFactorPrefsType	1363
#define btnTwoFactorPrefsOk		1364
#define btnTwoFactorPrefsCancel	1365

/* Numeric keypad for 2FA code entry (Numeric Keypad mode only) - mirrors
 * Sec35's own frmKeypad layout (10 digit GADGETs + OK/Cancel) but without
 * the phone-letter decorations (not useful for a TOTP/backup code, unlike
 * a PIN some users associate with letters). Full-screen-ish like Sec35's,
 * deliberately breaking the bottom-aligned convention for the same reason
 * Sec35's own comment gives: a real picker, not a data-entry form. */
#define frmTwoFactorKeypad		1370
#define fldTwoFactorKeypadDisplay	1371
#define gadTwoFactorKey1		1372
#define gadTwoFactorKey2		1373
#define gadTwoFactorKey3		1374
#define gadTwoFactorKey4		1375
#define gadTwoFactorKey5		1376
#define gadTwoFactorKey6		1377
#define gadTwoFactorKey7		1378
#define gadTwoFactorKey8		1379
#define gadTwoFactorKey9		1380
#define gadTwoFactorKeyDel		1381
#define gadTwoFactorKey0		1382
#define btnTwoFactorKeypadOk	1383
#define btnTwoFactorKeypadCancel	1384

/* Full-screen (real device feedback: as a small modal popup the Yes/No
 * buttons didn't fit on screen at all - unreachable, blocking every sync).
 * Both text areas are FIELDs populated via FldSetTextPtr (an externally-
 * owned buffer, not the compiled resource's own fixed buffer) rather than
 * FrmCopyLabel - sidesteps the documented FrmCopyLabel buffer-size gotcha
 * entirely instead of just sizing a placeholder generously (the actual
 * root cause of the vanished buttons: the real fingerprint text is 39
 * chars, the old LABEL placeholder was only 24 - a real overflow). */
#define frmTrust			1400
#define fldTrustQuestion	1401	/* bold - "Trust this server's certificate?" */
#define btnTrustYes			1402
#define btnTrustNo			1403
#define fldTrustCertData	1404	/* normal font, no underline, scrolls */
#define sclTrust			1405

#define frmSyncProgress		1500
#define ctlSyncProgress		1501
#define btnSyncCancel		1502

#define mniSync				1601
#define mniPreferences		1602
#define mniLockNow			1603
#define mniDeleteAccount	1605
#define mniAbout			1604
#define mniSecurityTwoFactor	1606	/* Security pulldown > 2FA - opens frmTwoFactorPrefs */
#define mniArchive			1607	/* Options > Archived Items - toggles gArchiveFilter, see MainForm.c */

#define altErr				1700
#define altErr2				1701
#define altAbout			1702
#define altConfirmForgetTrust	1703
#define altRomIncompatible	1704
#define altConfirmDeleteAccount	1705
#define altSyncOk			1706

#define appCreator			'BWrd'

/* ---- item types (mirrors PROTOCOL.md's SYNC_ITEM itemType) ------------ */
#define bwTypeLogin		1
#define bwTypeNote		2
#define bwTypeCard		3
#define bwTypeIdentity	4

/* Field indices - used both by Store.c (per-field CTR nonce) and by
 * whatever's decrypting a full record for display. The first bwField512Count
 * of these are bwMaxField(512)-sized (the original Login-shaped fields,
 * generously sized for anything realistic); everything from bwField512Count
 * up to bwFieldCount is bwMaxExtField(128)-sized instead - Card/Identity's
 * own dedicated fields (added to show every field Bitwarden's real Identity/
 * Card types have - see PROTOCOL.md's SYNC_ITEM section - rather than the
 * old 3-generic-slots reuse hack). Store.c's own read/write loops use
 * bwField512Count as the size-tier boundary; keep new small fields appended
 * AFTER bwFieldCountry (before bwFieldCount), never inserted earlier, or
 * every field index after the insertion point shifts and silently corrupts
 * already-synced on-disk records (the nonce is (recordUID, fieldIndex) -
 * see Store.c's EncCrypt calls). */
#define bwFieldName		0
#define bwFieldUser		1
#define bwFieldPass		2
#define bwFieldUri		3
#define bwFieldNotes	4
#define bwFieldTotp		5
#define bwField512Count	6

#define bwFieldCardHolder		6
#define bwFieldCardBrand		7
#define bwFieldCardNumber		8
#define bwFieldCardExpMonth	9
#define bwFieldCardExpYear		10
#define bwFieldCardCode		11
#define bwFieldIdTitle			12
#define bwFieldIdFirstName		13
#define bwFieldIdMiddleName	14
#define bwFieldIdLastName		15
#define bwFieldIdUsername		16
#define bwFieldIdCompany		17
#define bwFieldIdSsn			18
#define bwFieldIdPassportNumber	19
#define bwFieldIdLicenseNumber	20
#define bwFieldIdEmail			21
#define bwFieldIdPhone			22
#define bwFieldIdAddress1		23
#define bwFieldIdAddress2		24
#define bwFieldIdAddress3		25
#define bwFieldIdCity			26
#define bwFieldIdState			27
#define bwFieldIdPostalCode	28
#define bwFieldIdCountry		29
#define bwFieldCount			30

#define bwFolderNone	0xFFFF	/* item has no personal folder */
#define bwOrgNone		0xFFFF	/* item is personal, not organization-owned -
								 * an item is either folderIndex-meaningful
								 * (personal) or orgIndex-meaningful (org),
								 * never both, matching PROTOCOL.md/Bitwarden's
								 * own model. */
#define bwGuidLen		37	/* 36-char GUID + NUL */
#define bwMaxField		512	/* see BitWarden.h's BwItemFull for why */
#define bwMaxExtField	128	/* Card/Identity's own dedicated fields - short
							 * enough data (names, addresses, ID numbers) that
							 * 512 bytes each would be pure waste (this app
							 * only ever holds ONE decrypted record at a time,
							 * but that one instance is real static RAM). */

#endif
