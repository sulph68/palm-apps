/*
 * WiRSaControlRsc.h - resource IDs for WiRSaControl.rcp. Hand-written; keep
 * in step with the .rcp by hand (pilrc can't parse <PalmOS.h>).
 */
#ifndef WIRSACONTROLRSC_H
#define WIRSACONTROLRSC_H

/* ---- frmMain: status ---------------------------------------------------- */
#define frmMain					1000
#define fldMainMode				1001
#define fldMainLink				1002
#define fldMainWifi				1003
#define fldMainIp				1004
#define fldMainSd				1005
#define lblMainSd				1020
#define lblMainVsd				1021
#define fldMainFtp				1006
#define fldMainNote				1007
#define fldMainBatt				1008
#define btnMainRefresh			1010
#define btnMainWifi				1011
#define btnMainModes			1012
#define btnMainSetup			1013

#define mnuMain					1000
#define mniMainFind				1020
#define mniMainReboot			1021
#define mniMainFactory			1022
#define mniMainAbout			1023
#define mniMainPrefs			1024
#define mniMainLogs				1025
#define mniMainSystem			1026

/* ---- frmModes ------------------------------------------------------------ */
#define frmModes				1100
#define popModesMode			1101
#define lstModesMode			1102
#define fldModesNote			1103
#define pbtModesFtpStart		1104
#define pbtModesFtpStop			1105
#define fldModesFtp				1106
#define btnModesStart			1110
#define btnModesCancel			1111

/* ---- frmWifi ------------------------------------------------------------- */
#define frmWifi					1200
#define btnWifiScan				1201
#define lstWifiNets				1202
#define fldWifiSsid				1203
#define fldWifiPass				1204
#define chkWifiShow				1205
#define btnWifiConnect			1210
#define btnWifiCancel			1211

/* ---- frmSetup ------------------------------------------------------------ */
#define frmSetup				1300
#define popSetupBaud			1301
#define lstSetupBaud			1302
#define popSetupLog				1303
#define lblSetupOrient			1304
#define popSetupOrient			1305
#define lstSetupOrient			1306
#define btnSetupSlip			1311
#define btnSetupTimers			1312
#define lstSetupLog				1314
#define lblSetupDisk			1315
#define popSetupDisk			1316
#define lstSetupDisk			1317
#define btnSetupFtp				1313
#define btnSetupAp				1318
#define lblSetupSleep			1330
#define popSetupSleep			1331
#define lstSetupSleep			1332
#define btnSetupOk				1320
#define btnSetupCancel			1321

/* ---- frmSlip ------------------------------------------------------------- */
#define frmSlip					1400
#define fldSlipGw				1401
#define fldSlipClient			1402
#define fldSlipMask				1403
#define fldSlipDns				1404
#define btnSlipOk				1410
#define btnSlipCancel			1411

/* ---- frmTimers: Exit Timers --------------------------------------------- */
#define frmTimers				1500
#define fldTimersUnused			1501
#define fldTimersAfter			1502
#define popTimersSlip			1503
#define lstTimersSlip			1504
#define lblTimersSlip			1505
#define btnTimersOk				1510
#define btnTimersCancel			1511

/* ---- frmFtp / frmAp ----------------------------------------------------- */
#define frmFtp					1800
#define fldFtpUser				1801
#define fldFtpPass				1802
#define btnFtpOk				1810
#define btnFtpCancel			1811
#define frmAp					1900
#define fldApSsid				1901
#define fldApPass				1902
#define btnApOk					1910
#define btnApCancel				1911

/* ---- frmPrefs: this app's own preferences ------------------------------- */
#define frmPrefs				1600
#define popPrefsBaud			1601
#define lstPrefsBaud			1602
#define btnPrefsOk				1610
#define btnPrefsCancel			1611

/* ---- frmLog: WiRSa's session log ----------------------------------------- */
#define frmLog					1700
#define fldLogText				1701
#define sclLog					1702
#define btnLogDone				1710
#define btnLogRefresh			1711
#define btnLogClear				1712

/* ---- shared Edit menu for forms with text fields ------------------------- */
#define mnuDlgEdit				1900
#define mniEditUndo				1900
#define mniEditCut				1901
#define mniEditCopy				1902
#define mniEditPaste			1903
#define mniEditSelectAll		1904
#define mniEditKeyboard			1906
#define mniEditGraffiti			1907

/* ---- alerts -------------------------------------------------------------- */
#define altError				2000	/* ^1 */
#define altInfo					2001	/* ^1 */
#define altConfirmMode			2002	/* ^1 ; Continue / Cancel */
#define altFactory				2003	/* Reset / Cancel */
#define altAbout				2004

/* ---- string lists (index = mode order M H D S A B) ----------------------- */
#define strModeNames			3000
#define strModeNotes			3001
#define strModeConfirm			3002
#define strMainNotes			3003

#endif
