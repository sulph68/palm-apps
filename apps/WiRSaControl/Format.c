/*
 * Format.c - Options > Format SD Card...: erase the SD card in WiRSa (FORMAT_SD, Protocol-wire.md section 12),
 * optionally copying the virtual card's SASD install set onto it. frmFormat is a plain dialog (FrmDoDialog).
 */
#include <PalmOS.h>
#include "WiRSaControl.h"
#include "relayproto.h"
#include "relaywire.h"

/* A FAT32 card of 32 GB takes WiRSa a while; a 1 GB FAT16 card a few seconds. */
#define kFormatWaitSec	240

void FormatOpenFromMain(void)
{
	FormPtr frm = FrmInitForm(frmFormat);
	UInt16 hit, n;
	Boolean copy;
	UInt8 body[7], rep[96], r, fat, copied, total;
	char note[64], msg[160];
	RpR rd;

	FrmSetControlValue(frm, FrmGetObjectIndex(frm, chkFormatCopy), 1);
	hit = FrmDoDialog(frm);
	copy = FrmGetControlValue(frm, FrmGetObjectIndex(frm, chkFormatCopy)) != 0;
	FrmDeleteForm(frm);
	if (hit != btnFormatOk) return;

	MemMove(body, "FORMAT", 6);
	body[6] = copy ? 1 : 0;
	BusyShow("Formatting SD card...");
	r = WCtlBegin(SweepOK());
	if (r == wctlOk) {
		r = WCtlRequest(wctlTypeFormatSd, body, sizeof(body), rep, sizeof(rep), &n,
			SysTicksPerSecond() * kFormatWaitSec);
		WCtlEnd();
	}
	BusyHide();
	if (r != wctlOk) { ShowResult(r); FrmUpdateForm(frmMain, updRefresh); return; }

	rd.buf = rep; rd.len = n; rd.pos = 0; rd.ok = true;
	(void) RpR8(&rd);
	fat = RpR8(&rd);
	copied = RpR8(&rd);
	total = RpR8(&rd);
	note[0] = '\0';
	RpRStr(&rd, note, sizeof(note));
	if (! rd.ok) { ShowResult(wctlBadReply); FrmUpdateForm(frmMain, updRefresh); return; }

	StrPrintF(msg, "The SD card was formatted (FAT%u).", (UInt16) fat);
	if (copy) {
		if (note[0]) {
			StrCat(msg, " The SASD files were not copied: ");
			StrCat(msg, note);
			StrCat(msg, ".");
		} else if (total == 0)
			StrCat(msg, " The virtual card has no SASD install set, so nothing was copied.");
		else
			StrPrintF(msg + StrLen(msg), " %u of %u SASD files were copied from the virtual card.",
				(UInt16) copied, (UInt16) total);
	}
	FrmCustomAlert(altInfo, msg, NULL, NULL);
	FrmUpdateForm(frmMain, updRefresh);
}
