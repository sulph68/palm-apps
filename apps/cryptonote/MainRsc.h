/* Main form */

#define frmMainForm					1000

#define popMainCategory				1001
#define lstMainCategory				1002
#define tblMainRecordList			1003
#define sclMainScrollBar			1004
#define btnMainNewMemo				1005
#define fldMainFind					1006
#define btnMainClear				1007

#define mnuMainMenu					1000
#define itemRecordBeamCategory		1002
#define itemRecordEncryptCategory	1003
#define itemRecordDecryptCategory	1004
#define itemOptionsPrefs			1005
#define itemOptionsAbout			1006
#define itemOptionsSecurity			1007
#define MainProgressMessageGadget	1008
#define MainProgressBarGadget 		1009
	
#define bmpMainKeyBitmap			1000

/* Edit form */

#define frmEditForm					2000

#define popEditCategory				2001
#define lstEditCategory				2002
#define fldEditText					2003
#define sclEditScrollBar			2004
#define btnEditTerminate			2005
#define btnEditDetails				2006
#define lblEditMaxSize				2007
#define fbmpEditKey					2012   /* CryptoNote: encrypted-card-file key indicator */

#define mnuEditMenu					2000
#define mnuEditMenuNoPwd			2500

#define itemRecordNewMemo			2001
#define itemRecordDeleteMemo		2002
#define itemRecordBeamMemo			2003
#define itemRecordEncryptMemo		2004
#define itemRecordRemovePasswd 		2005
#define itemRecordImportToCrypto	2006	/* card mode only -- see EditForm.c */
#define itemOptionsGoTopOfPage		2007
#define itemOptionsGoBottomOfPage	2008
#define itemOptionsFont				2009

#define strEditTitleMemo			2000
#define bmpBarFontBitmap			2050

/* Details form */

#define frmDetailsForm				3000
#define popDetailsCategory			3001
#define lstDetailsCategory			3002
#define chkDetailsPrivate			3003
#define btnDetailsOK				3004
#define btnDetailsCancel			3005
#define btnDetailsDelete			3006

#define strDetailsHelpString		3000

/* Password form */

#define frmPasswordForm				4000

#define fldPasswordText				4001
#define btnPasswordOK				4002
#define btnPasswordCancel			4003
#define btnPasswordForget			4004

#define strPasswordHelpString		4000

/* Prefs form */

#define frmPrefsForm				5000
#define popPrefsSortOrder			5001
#define lstPrefsSortOrder			5002
#define btnPrefsOK					5003
#define btnPrefsCancel				5004
#define chkPrefsShowLines			5005
#define chkPrefsUseGlobalPassword	5006
#define selPrefsGlobalPassword		5007
#define btnPrefsForgotten			5008
#define chkPrefsUseSingleFont		5009
#define popPrefsRotateOrder			5010
#define lstPrefsRotateOrder			5011
#define chkPrefsMoveRecordOnTop		5012
#define chkPrefsShowHidden			5013


#define strPrefsHelpString			5000

/* About form */

#define frmAboutForm				6000

#define bmpCryptoPadIcon			6001

#define strAboutHelpString			6000


/* Delete form */
#define frmDeleteForm				7000

#define btnDeleteOK					7001
#define btnDeleteCancel				7002 
#define chkDeleteBackup				7003

#define strDeleteHelpString			7000

/* QueryGlobalPassword form */

#define frmQueryGlobalPasswordForm	8000

#define fldQueryGlobalPasswordText	8001
#define btnQueryGlobalPasswordOK	8002
#define btnQueryGlobalPasswordCancel 8003

/* SetGlobalPassword form */

#define frmSetGlobalPasswordForm	9000

#define fldSetAndChangeGlobalPasswordText	9001
#define btnSetAndChangeGlobalPasswordOK	9002
#define btnSetAndChangeGlobalPasswordCancel	9003

#define strSetGlobalPasswordHelpString 9000

/* ChangeGlobalPassword form */

#define frmChangeGlobalPasswordForm	1100

#define fldChangeGlobalPasswordText	1101
#define btnChangeGlobalPasswordDelete 1102

/* ConfirmGlobalPassword form */

#define frmConfirmGlobalPasswordForm	1200

#define btnConfirmGlobalPasswordOK	1201

/* Progress form */

#define frmProgressForm				1300

#define gadProgressMessage			1301
#define gadProgressBar				1302

/* Miscellaneous */

#define altConfirmDeleteAlert				1000
#define altInvalidPasswordAlert				2000
#define altInvalidFormatAlert				3000
#define altRemovePasswdAlert				4000
#define altRomIncompatibleAlert				5000
#define altAlphabeticSortAlert				6000
#define altUseNoGlobalPasswordAlert			7000
#define altInvalidGlobalPasswordAlert 		8000
#define altEncryptRecordWithGlobalPasswordAlert 9000
#define altEncryptCategoryWithGlobalPasswordAlert 1100
#define altGlobalPasswordNotMatchAlert	 	1200
#define altConfirmDeleteForgottenPasswordAlert 1300
#define altNoGlobalPasswordToDeleteAlert	1400
#define altCategoryAlreadyEncryptedAlert	1500
#define altCategoryAlreadyDecryptedAlert	1600
#define altChangeTableFontOfCategoryAllAlert 1700
#define altUseSingleFontAlert				1800
#define altBeamCryptoDataAlert				1900
#define altMoveRecordOnTopImpossibleAlert	2100
#define altMigrateMemoPadAlert				2950


#define strCryptoHeader						10
#define strGlobalPasswordUnassigned			20
#define strGlobalPasswordAssigned			30
#define strProgressEncryption				40
#define strProgressDecryption				50
#define strDeleteForgottenPassword		 	60
#define strBeamDescription 					70
#define strBeamFilename 					80
#define strMenuSecurityItem					90
#define strSynchroniseFont					100
#define altDebugAlert						20

/* CryptoNote: card browser icons (Tbmp). Encrypted files reuse the existing
 * CryptoPad key icon (bmpMainKeyBitmap), drawn right-aligned like DB records. */
#define bmpCardFolder				2000
#define bmpCardUp					2001

/* CryptoNote: "New note on card" dialog + its resources */
#define frmCardNewNote				1400
#define fldCardNewName				1401
#define btnCardNewOK				1404
#define btnCardNewCancel			1405
#define altCardNewNoName			2200
#define altCardNewExists			2300

/* CryptoNote: card-file password dialog + encrypt/decrypt menu + alerts */
#define frmCardPassword				1500
#define fldCardPassword				1501
#define btnCardPasswordOK			1502
#define btnCardPasswordCancel		1503
#define altCardBadPassword			2400
#define altCardReencrypt			2500
#define altCardDeletePlain			2600
#define altCardTooLarge				2700
#define altCardNoBeam				2800
#define altCardEncAll				2900
#define altCardImportConfirm		2960	/* Phase 5b: Import to CryptoNote (2950-2999 gap; 3000 taken by altInvalidFormatAlert) */
#define altCardImportComplete		2961
#define altCardImportFailed			2962
#define altCardOpenFailed			2963
#define altRecordTooLargeToEdit		2964

/* Numeric keypad for Password form entry (Preferences > Password Type in
 * Sec35, shared cross-app via EncGetPasswordEntryType -- see PasswordForm.c).
 * IDs mirror Sec35's own frmKeypad (sec35.rcp/sec35.c) layout+numbering
 * 1-for-1 (offset by +9100) purely so the ported C logic (kKeyIds table etc.)
 * lines up the same way; 11000+ is well clear of both this app's own IDs and
 * the system Edit-menu block (10000-10007). */
#define frmKeypadForm				11000
#define kpd1						11001	/* digits 1-9 = kpd1..kpd1+8 */
#define kpdDel						11010
#define kpd0						11011
#define kpdOk						11012
#define kpdCancel					11013
