/*
 * sectest.c - exercises enccore.{c,h} on-device.
 *
 *  "Self Test"  : self-contained crypto/key unit checks (no Sec35 needed).
 *  "Encrypt"    : reads the SHARED session key that Sec35 unlocked (Feature),
 *                 writes a known plaintext record to a small DB, and encrypts
 *                 it in place (AES-CTR, nonce = record uid).
 *  "Decrypt"    : reads that record back, decrypts with the shared key, and
 *                 checks it round-trips to the original plaintext.
 *
 * This is the cross-app test: Sec35 owns the passcode/unlock and caches the key;
 * SecTest (standing in for a real app) only fetches the key and does crypto. If
 * Sec35 is locked / no password, Encrypt/Decrypt report LOCKED.
 */
#include <PalmOS.h>
#include "enccore.h"
#include "aes.h"

#define frmMain		1000
#define btnRun		1001
#define btnEnc		1003
#define btnDec		1004
#define btnAddr		1005

/* Address35 on-disk format (mirror of AddressDB35.c) - just enough to inspect. */
#define kA35Type	'DATA'
#define kA35Creator	'Ad35'
#define kA35HdrSize	10
#define a35fPhone	7
#define a35fEmail	8
#define a35fNote	15

#define kDbName		"SecTestDB"
#define kDbType		'DATA'
#define kDbCreator	'SecT'
#define kTextLen	40
#define kBigTtl		1000000000UL

typedef struct { UInt8 encrypted; UInt8 pad; char text[kTextLen]; } TestRec;

/* FIPS-197 AES-128 ECB known-answer vector (public standard vector). */
static const UInt8 katKey[16] =
	{ 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
static const UInt8 katPt[16]  =
	{ 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
	  0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
static const UInt8 katCt[16]  =
	{ 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
	  0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };

static const char kPlain[] = "SecTest data: hello 0123456789 !!";
static const char kHex[]   = "0123456789abcdef";

static char		gLine[26][40];
static UInt16	gN;

static void Add (Boolean pass, const char* msg)
{
	if (gN >= 26) return;
	StrPrintF (gLine[gN], "%s %s", pass ? "PASS" : "FAIL", msg);
	gN++;
}

static void AddRaw (const char* msg)
{
	if (gN >= 26) return;
	StrNCopy (gLine[gN], msg, 39);
	gLine[gN][39] = 0;
	gN++;
}

static void ToHex (char* out, const UInt8* in, UInt16 n)
{
	UInt16 i;
	for (i = 0; i < n; i++)
		{ out[i * 2] = kHex[in[i] >> 4]; out[i * 2 + 1] = kHex[in[i] & 15]; }
	out[n * 2] = 0;
}

/* ---- self tests (crypto/key, no Sec35) --------------------------------- */

static void RunTests (void)
{
	struct AES_ctx	ctx;
	UInt8			blk[16], dev[32];
	UInt8			k1[16], k2[16], k3[16], out[16];
	UInt8			salt[16], salt2[16], c1[40], c2[40];
	char			msg[40], msg2[40];
	UInt16			devLen, mlen, rounds = 60;

	gN = 0;

	MemMove (blk, katPt, 16);
	AES_init_ctx (&ctx, katKey);
	AES_ECB_encrypt (&ctx, blk);
	Add (MemCmp (blk, katCt, 16) == 0, "AES-128 KAT");

	devLen = EncDeviceSecret (dev, sizeof (dev));
	Add (devLen > 0, "device secret");

	MemSet (salt, 16, 0xA5); MemSet (salt2, 16, 0x3C);
	EncDeriveKey ("pass1234", salt, rounds, k1);
	EncDeriveKey ("pass1234", salt, rounds, k2);
	Add (MemCmp (k1, k2, 16) == 0, "KDF deterministic");
	EncDeriveKey ("different", salt, rounds, k3);
	Add (MemCmp (k1, k3, 16) != 0, "KDF passcode-sens");
	EncDeriveKey ("pass1234", salt2, rounds, k3);
	Add (MemCmp (k1, k3, 16) != 0, "KDF salt-sens");

	StrCopy (msg, "Hello Sec35! 0123456789abcdef XYZ");
	mlen = StrLen (msg);
	MemMove (msg2, msg, mlen);
	EncCrypt (k1, (UInt8*) msg2, mlen, 0, 0);
	Add (MemCmp (msg, msg2, mlen) != 0, "CTR encrypts");
	EncCrypt (k1, (UInt8*) msg2, mlen, 0, 0);
	Add (MemCmp (msg, msg2, mlen) == 0, "CTR round-trip");

	MemMove (msg2, msg, mlen);
	EncCrypt (k1, (UInt8*) msg2, mlen, 0, 0);
	EncCrypt (k3, (UInt8*) msg2, mlen, 0, 0);
	Add (MemCmp (msg, msg2, mlen) != 0, "CTR wrong-key");

	MemMove (c1, msg, mlen); EncCrypt (k1, c1, mlen, 0, 0);
	MemMove (c2, msg, mlen); EncCrypt (k1, c2, mlen, 0, 1);
	Add (MemCmp (c1, c2, mlen) != 0, "CTR nonce-indep");

	Add (EncCalibrateRounds (100) >= 1, "calibrate rounds");

	EncWipeKey ();
	Add (! EncKeyPresent (), "cache empty init");
	EncCacheKey (k1);
	Add (EncKeyPresent (), "cache present");
	Add (EncGetKey (out, 100000UL) && MemCmp (out, k1, 16) == 0, "cache get==key");
	EncWipeKey ();
	Add ((! EncKeyPresent ()) && (! EncGetKey (out, 100000UL)), "cache wiped");

	MemSet (k1, 16, 0); MemSet (k2, 16, 0); MemSet (k3, 16, 0);
}

/* ---- data round-trip against Sec35's shared key ------------------------ */

static DmOpenRef OpenTestDb (Boolean create)
{
	DmOpenRef db = DmOpenDatabaseByTypeCreator (kDbType, kDbCreator, dmModeReadWrite);
	if (! db && create)
		{
		if (DmCreateDatabase (0, kDbName, kDbCreator, kDbType, false) != errNone)
			return NULL;
		db = DmOpenDatabaseByTypeCreator (kDbType, kDbCreator, dmModeReadWrite);
		}
	return db;
}

static void DoEncryptData (void)
{
	UInt8		key[16];
	DmOpenRef	db;
	UInt16		idx = 0;
	UInt32		uid = 0;
	MemHandle	h;
	TestRec		rec;
	char		hex[40];

	gN = 0;

	if (! EncGetKey (key, kBigTtl))
		{ Add (false, "LOCKED - set pw in Sec35"); return; }
	Add (true, "shared key available");

	db = OpenTestDb (true);
	if (! db) { Add (false, "open/create DB"); return; }
	if (DmNumRecords (db) == 0)
		{
		h = DmNewRecord (db, &idx, sizeof (TestRec));
		if (h) { TestRec z; MemSet (&z, sizeof (z), 0);
				 DmWrite (MemHandleLock (h), 0, &z, sizeof (z));
				 MemHandleUnlock (h); DmReleaseRecord (db, idx, true); }
		}

	MemSet (&rec, sizeof (rec), 0);
	StrCopy (rec.text, kPlain);
	DmRecordInfo (db, 0, NULL, &uid, NULL);
	EncCrypt (key, (UInt8*) rec.text, kTextLen, uid, 0);	/* nonce = record uid */
	rec.encrypted = 1;

	h = DmGetRecord (db, 0);
	DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
	MemHandleUnlock (h);
	DmReleaseRecord (db, 0, true);
	DmCloseDatabase (db);

	Add (true, "stored ciphertext:");
	ToHex (hex, (UInt8*) rec.text, 8);
	AddRaw (hex);
	MemSet (key, 16, 0);
}

static void DoDecryptData (void)
{
	UInt8		key[16];
	DmOpenRef	db;
	UInt32		uid = 0;
	MemHandle	h;
	TestRec		rec;

	gN = 0;

	if (! EncGetKey (key, kBigTtl))
		{ Add (false, "LOCKED - set pw in Sec35"); return; }

	db = OpenTestDb (false);
	if (! db || DmNumRecords (db) == 0)
		{ Add (false, "no data - Encrypt first"); if (db) DmCloseDatabase (db); return; }

	h = DmQueryRecord (db, 0);
	MemMove (&rec, MemHandleLock (h), sizeof (rec));
	MemHandleUnlock (h);
	DmRecordInfo (db, 0, NULL, &uid, NULL);
	DmCloseDatabase (db);

	if (rec.encrypted)
		EncCrypt (key, (UInt8*) rec.text, kTextLen, uid, 0);
	rec.text[kTextLen - 1] = 0;

	Add (true, "decrypted text:");
	AddRaw (rec.text);
	Add (StrCompare (rec.text, kPlain) == 0, "round-trip match");
	MemSet (key, 16, 0);
}

/* ---- Address35 DB at-rest inspector (reusable encryption proof) --------- *
 * Opens the real Address35DB, reads record 0, and reports a sensitive field's
 * bytes: PLAINTEXT (readable ASCII at rest) vs. ciphertext that the SHARED Sec35
 * key decrypts back to text. Nonce convention = (recordUniqueID, fieldIndex) -
 * the same one the Address35 packer must use when encryption is wired.          */

static Boolean Printable (const UInt8* p, UInt16 n)
{
	UInt16 i;
	for (i = 0; i < n; i++)
		if (p[i] < 0x20 || p[i] > 0x7e) return false;
	return true;
}

static void DoInspectAddr35 (void)
{
	DmOpenRef	db;
	MemHandle	h;
	UInt8*		rec;
	UInt32		uid = 0;
	UInt16		count, i, off, recCount;
	UInt16		fIdx = 0xffff, fLen = 0, fOff = 0;
	UInt8		key[16], buf[64];
	char		line[96], hex[40];	/* line must fit "decrypted: " + up to 63 bytes */

	gN = 0;

	db = DmOpenDatabaseByTypeCreator (kA35Type, kA35Creator, dmModeReadOnly);
	if (! db) { Add (false, "Address35DB not found"); return; }
	recCount = DmNumRecords (db);
	StrPrintF (line, "Address35DB: %u records", recCount);
	AddRaw (line);
	if (recCount == 0) { DmCloseDatabase (db); return; }

	h = DmQueryRecord (db, 0);
	rec = (UInt8*) MemHandleLock (h);
	DmRecordInfo (db, 0, NULL, &uid, NULL);
	count = (UInt16) (((UInt16) rec[2] << 8) | rec[3]);		/* fieldCount (BE) */

	off = kA35HdrSize;										/* first sensitive field */
	for (i = 0; i < count; i++) {
		UInt8  t  = rec[off];
		UInt16 ln = (UInt16) (((UInt16) rec[off + 2] << 8) | rec[off + 3]);
		if (fIdx == 0xffff && (t == a35fEmail || t == a35fPhone || t == a35fNote)) {
			fIdx = i; fLen = ln; fOff = (UInt16) (off + 4);
		}
		off = (UInt16) (off + 4 + ln);
	}
	if (fIdx == 0xffff) {
		AddRaw ("no phone/email/note field");
		MemHandleUnlock (h); DmCloseDatabase (db); return;
	}

	StrPrintF (line, "rec0 uid=%lu fld#%u len=%u", uid, fIdx, fLen);
	AddRaw (line);
	{ UInt16 n = (fLen > 8) ? 8 : fLen; ToHex (hex, rec + fOff, n);
	  StrPrintF (line, "at-rest hex: %s", hex); AddRaw (line); }

	if (Printable (rec + fOff, (fLen < 8) ? fLen : 8)) {
		UInt16 n = (fLen > 38) ? 38 : fLen;
		MemMove (buf, rec + fOff, n); buf[n] = 0;
		StrPrintF (line, "text: %s", (char*) buf); AddRaw (line);
		Add (false, "PLAINTEXT - not encrypted");
	} else {
		Add (true, "ciphertext at rest; decrypting:");
		if (EncGetKey (key, kBigTtl)) {
			UInt16 n = (fLen > 63) ? 63 : fLen;
			MemMove (buf, rec + fOff, n);
			EncCrypt (key, buf, n, uid, fIdx);			/* nonce = (uid, fieldIndex) */
			buf[n] = 0;
			StrPrintF (line, "decrypted: %s", (char*) buf); AddRaw (line);
			Add (Printable (buf, (n < 8) ? n : 8), "decrypt -> readable text");
			MemSet (key, 16, 0);
		} else {
			Add (false, "LOCKED - unlock in Sec35 first");
		}
	}

	MemHandleUnlock (h);
	DmCloseDatabase (db);
}

/* ---- UI ---------------------------------------------------------------- */

static void DrawResults (void)
{
	UInt16	i;
	Int16	y = 48;
	FontID	f = FntSetFont (stdFont);

	for (i = 0; i < gN; i++)
		{ WinDrawChars (gLine[i], StrLen (gLine[i]), 6, y); y += 9; }
	FntSetFont (f);
}

static void ShowResults (void)
{
	RectangleType r;
	r.topLeft.x = 0; r.topLeft.y = 46; r.extent.x = 160; r.extent.y = 114;
	WinEraseRectangle (&r, 0);
	DrawResults ();
}

static Boolean MainFormHandleEvent (EventType* e)
{
	Boolean handled = false;

	switch (e->eType)
		{
		case frmOpenEvent:
			FrmDrawForm (FrmGetActiveForm ());
			handled = true;
			break;

		case ctlSelectEvent:
			switch (e->data.ctlSelect.controlID)
				{
				case btnRun: RunTests ();      ShowResults (); handled = true; break;
				case btnEnc: DoEncryptData (); ShowResults (); handled = true; break;
				case btnDec: DoDecryptData (); ShowResults (); handled = true; break;
				case btnAddr: DoInspectAddr35 (); ShowResults (); handled = true; break;
				}
			break;

		default:
			break;
		}
	return handled;
}

static Boolean AppHandleEvent (EventType* e)
{
	UInt16		formId;
	FormType*	frm;

	if (e->eType == frmLoadEvent)
		{
		formId = e->data.frmLoad.formID;
		frm = FrmInitForm (formId);
		FrmSetActiveForm (frm);
		if (formId == frmMain)
			FrmSetEventHandler (frm, MainFormHandleEvent);
		return true;
		}
	return false;
}

UInt32 PilotMain (UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	EventType e;

	if (cmd == sysAppLaunchCmdNormalLaunch)
		{
		FrmGotoForm (frmMain);
		do {
			EvtGetEvent (&e, evtWaitForever);
			if (! SysHandleEvent (&e))
				if (! AppHandleEvent (&e))
					FrmDispatchEvent (&e);
		} while (e.eType != appStopEvent);
		FrmCloseAllForms ();
		}
	return 0;
}
