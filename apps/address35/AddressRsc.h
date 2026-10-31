/*
 * AddressRsc.h - Address35 resource ids (forms, controls, menus, alerts).
 * Kept in step with Address35En.rcp.
 */
#ifndef ADDRESSRSC_H
#define ADDRESSRSC_H

/* ---- Main (list) form -------------------------------------------------- */
#define frmMain					1000
#define tblMainList				1001
#define sclMainScroll			1002
#define popMainCategory			1003
#define lstMainCategory			1004
#define btnMainNew				1005
#define fldMainFind				1006		/* list-view Find text entry -- filters live, see MainFormHandleEvent's fldChangedEvent case */
#define btnMainCancel			1008		/* clear the Find filter */

#define mnuMain					1000
#define mniMainNew				1001
#define mniMainDelete			1002
#define mniMainBeam				1003
#define mniMainImport			1004
#define mniMainExport			1005
#define mniMainPrefs			1010
#define mniMainAbout			1011
#define mniMainBrowseCard		1012
#define mniMainFont				1013
#define mniMainDav				1014
#define mniMainDavImport		1015

/* ---- Edit form: inline-editable, section-ordered scrolling list -------- *
 * kEditRows physical rows: a type SELECTOR (label) + an editable value field,
 * a window onto a section-sorted field array (EditForm.c). Each row's type
 * SELECTOR opens the searchable A35PickList (Graffiti/keyboard find), not a
 * bound native popup.                                                       */
#define frmEdit					1100
#define kEditRows				8
#define selEditRowBase			1140	/* type selector  1140..1147 */
#define fldEditRowBase			1150	/* value field    1150..1157 */
#define lstEditGender			1149	/* value pick-list: Gender   */
#define lstEditPronouns			1158	/* value pick-list: Pronouns */
#define lstEditPrefix			1168	/* value pick-list: Prefix   */
#define lstEditSocial			1159	/* value pick-list: network  */
#define colEditRowBase			1160	/* ":" separators 1160..1167 */
#define sclEditFields			1102	/* whole-list scrollbar      */
#define btnEditDone				1103
#define btnEditDelete			1104
#define btnEditAddField			1105	/* "+ Field"                 */
#define popEditCategory			1106
#define lstEditCategory			1107
#define btnEditDelField			1108	/* "- Field"                 */

/* ---- Remove-fields dialog (full-screen, real checkbox controls) -------- */
#define frmDelField				1600
#define sclDelField				1602
#define btnDelFieldOK			1603
#define btnDelFieldCancel		1604
#define kDelRows				10
#define cbDelRowBase			1610	/* checkboxes 1610..1619            */

#define mnuEdit					1100
#define mniEditDelete			1101
#define mniEditBeam				1102

/* ---- Record (read-only detail) form ------------------------------------ */
#define frmRecord				1200
#define btnRecordDone			1201
#define btnRecordEdit			1202
#define tblRecord				1203
#define sclRecord				1204
#define popRecordCategory		1205	/* change category without entering Edit */
#define lstRecordCategory		1206

#define mnuRecord				1200
#define mnuDlgEdit				1300		/* Edit-only menu for modal field dialogs */
#define mniRecordEdit			1201
#define mniRecordDelete			1202
#define mniRecordBeam			1203
#define mniRecordSetMe			1204
#define mniRecordDuplicate		1205
#define mniRecordCopy			1207
#define mniRecordCopyAll		1208

/* ---- shared searchable list picker (Picker.c) -------------------------- */
#define frmPick					1500
#define lstPick					1501
#define btnPickCancel			1502
#define sclPick					1503
#define lblPickFind				1504
#define fldPickFind				1505

/* ---- Email value dialog ------------------------------------------------ */
#define frmEmailDlg				1510
#define fldEmailUser			1511
#define popEmailDomain			1512	/* trigger; driven via A35PickList   */
#define cbEmailOther			1513
#define fldEmailCustom			1514
#define btnEmailOK				1515
#define btnEmailCancel			1516
#define lblEmailAt				1517
#define lstEmailDomain			1518
#define lblEmailOther			1519	/* "Other" placeholder over custom field */
#define popEmailLabel			1520	/* trigger: Home/Work/iCloud/Other */
#define lstEmailLabel			1521
#define lblEmailLabel			1522

/* ---- Address value dialog ---------------------------------------------- */
#define frmAddrDlg				1540
#define fldAddrLine1			1541
#define fldAddrLine2			1542
#define fldAddrApt				1543
#define fldAddrCity				1544
#define fldAddrState			1545
#define fldAddrZip				1546
#define popAddrCountry			1547	/* unbound: tap -> A35PickList        */
#define btnAddrOK				1548
#define btnAddrCancel			1549

/* ---- Date value dialog (Birthday / Anniversary) ------------------------ *
 * Any-year date entry: month/day bound popups, a 4-digit year field with
 * +/- stepper buttons (full era range, not the ROM SelectDay 2031 cap), an
 * "Unknown year" checkbox, and (Anniversary) a Type popup. Stored as a
 * portable "YYYYMMDD" string so Datebook35/ToDo35 can read it.             */
#define frmDateDlg				1560
/* Date pickers are ONE push-button per field (whole value, not per-digit) +
 * up/down REPEATBUTTON arrows; holding an arrow continuously wraps the whole
 * value (month/day/year all wrap - see EditDialogs.c DateBump). */
#define pbDateMonthVal			1700	/* month, 1-2 digits                 */
#define pbDateDayVal			1701	/* day, 1-2 digits                   */
#define pbDateYearVal			1702	/* year, up to 4 digits              */
#define btnDateMonthUp			1562	/* month selected-digit + 1 (repeat) */
#define btnDateMonthDn			1563	/* month selected-digit - 1 (repeat) */
#define fldDateYear				1565	/* editable 4-digit year             */
#define btnDateYearDn			1566	/* year - 1 (repeat)                 */
#define btnDateYearUp			1567	/* year + 1 (repeat)                 */
#define cbDateNoYear			1568	/* "Unknown year"                     */
#define lblDateType				1569	/* "Type" label (Anniversary line 1)  */
#define popDateType				1570	/* bound popup: anniversary type      */
#define lstDateType				1571
#define btnDateOK				1572
#define btnDateCancel			1573
#define lblDateDate				1574	/* "Date" label                       */
#define lblDateYear				1575	/* "Year" label                       */
#define lblDateUnknown			1576	/* "Unknown" (own label so it aligns) */
#define fldDateTypeOther		1577	/* custom name when Type = "Other"     */
#define btnDateDayUp			1578	/* day + 1 (repeat)                   */
#define btnDateDayDn			1579	/* day - 1 (repeat)                   */
#define lblDateDay				1708	/* "Day" label (own row)              */

/* ---- Relation dialog --------------------------------------------------- */
#define frmRelDlg				1580
#define lblRelType				1581
#define popRelType				1582
#define lstRelType				1583
#define lblRelContact			1584
#define btnRelPick				1585	/* shows target name; tap = pick contact */
#define btnRelOK				1586
#define btnRelCancel			1587

/* ---- Contact picker (searchable list for relations) -------------------- */
#define frmContactPick			1590
#define lstContactPick			1591
#define fldContactFind			1592
#define btnContactCancel		1594
#define lblContactFind			1595

/* ---- Note editor (full-screen) ----------------------------------------- */
#define frmNoteDlg				1596
#define fldNoteText				1597
#define sclNote					1598
#define btnNoteDone				1599
#define btnNoteCancel			1600

/* ---- Phone value dialog (half-screen, avoids inline-chip redraw issues) - */
#define frmPhoneDlg				1601
#define lblPhoneCountry			1602
#define selPhoneDial			1603	/* dial-code chip; tap -> A35DialPickList */
#define lblPhoneNumber			1604
#define fldPhoneNumber			1605
#define btnPhoneOK				1606
#define btnPhoneCancel			1607

/* ---- Social value dialog ----------------------------------------------- */
#define frmSocialDlg			1520
#define fldSocialHandle			1521
#define popSocialNet			1522
#define lstSocialNet			1523
#define cbSocialOther			1524
#define lblSocialOther			1525
#define fldSocialCustom			1526
#define btnSocialOK				1527
#define btnSocialCancel			1528

/* ---- Preferences form -------------------------------------------------- */
#define frmPrefs				1300
#define popPrefsSurname			1301
#define lstPrefsSurname			1302
#define btnPrefsOK				1303
#define btnPrefsCancel			1304
#define popPrefsPrimary			1305
#define lstPrefsPrimary			1306
#define popPrefsSecondary		1307
#define lstPrefsSecondary		1308
#define cbPrefsShowHidden		1309
#define lblPrefsDisplay			1314

/* ---- About form -------------------------------------------------------- */
#define frmAbout				1400
#define btnAboutOK				1401

/* ---- alerts ------------------------------------------------------------ */
#define altRomIncompatible		1900
#define altHackOff				1901
#define altNotYet				1902
#define altNoSelection			1904
#define altNoCard				1905
#define altImportDone			1906
#define altExportDone			1907
#define altExportScope			1908
#define altCardEmpty			1909
#define altSetMe				1910
#define altNoSec35				1911
#define altCardGroupSplit		1912
#define altCardSplitFailed		1915
#define altDavLocked			1930
#define altDavNoSync			1931
#define altDavReadOnly			1932
#define altDavInfo				1933
#define altDavImportAsk			1934
#define altDavImportDone		1935

/* Options > CardDAV... (MainForm.c MainDavDialog) */
#define frmDav					1940
#define popDavShow				1941
#define lstDavShow				1942
#define btnDavOK				1943
#define btnDavCancel			1944
#define btnDavSync				1945

/* "Reading..." is drawn screen-direct (A35BusyBegin/End in AddrCard.c) - no
 * form resource, matching CryptoNote's CardBusyShow style. "Processing
 * vCard..."/"Importing..." use the real form below instead (they have an
 * actual progress bar to show). */
#define frmCardProgress			1914

/* Duplicate-filename prompt (Split + Export share A35CardUniquePath) */
#define frmDupFile				1916
#define lblDupMsg1				1917
#define lblDupMsg2				1918
#define cbDupApplyAll			1919
#define lblDupApplyAll			1920
#define btnDupReplace			1921
#define btnDupKeepBoth			1922
#define btnDupCancel			1923

/* Shown when a card directory has more entries than kCardMax (MainForm.c)
 * can hold - CardListDir silently stops enumerating past that, so this says
 * so instead of just quietly hiding files. */
#define altCardTooMany			1924

/* SD-card browse iconography (the category-list "Card" row uses a symbolFont
 * glyph (0x1A) instead of a bitmap) */
#define bmpCardFolder			2000
#define bmpCardUp				2001

/* Phone Lookup (Lookup.c): small modal picker shown over the calling app. */
#define frmLookup				2100
#define lstLookup				2101
#define lblLookupFind			2102
#define fldLookupFind			2103
#define btnLookupPaste			2104
#define btnLookupCancel			2105
#define lstLookupItems			2106
#define popLookupSrc			2107
#define lstLookupSrc			2108
#define altLookupNone			2110
#define altDelFieldWarn			1903

/* ---- string / bitmap resources ----------------------------------------- */
#define strAboutText			1000

#endif /* ADDRESSRSC_H */

/* Record view: full-field popup (RecField.c) */
#define frmRecField				2200
#define fldRecField				2201
#define sclRecField				2202
#define btnRecFieldDone			2203
#define btnRecFieldCopy			2204
#define mnuRecField				2205
#define mniRecFieldCopy			2206
