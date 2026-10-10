/*
 * BusyDialog.c - centered busy/progress feedback, modeled on Address35's
 * A35BusyBegin/A35BusyEnd + A35ProgressBegin/A35ProgressBar/A35ProgressEnd
 * (apps/address35/AddrCard.c) but original code and, unlike Address35's
 * progress dialog, no separate FORM resource - both the text-only toast and
 * the progress bar are drawn screen-direct (WinSaveBits/WinRestoreBits), one
 * less moving part for an app this size. Callers run a tight synchronous
 * loop (no event pump) and are expected to call EvtResetAutoOffTimer
 * periodically themselves during any operation long enough to risk auto-off
 * - these functions don't do that on their own, they're pure draw calls.
 */
#include "LazyFTP.h"
#include "FtpProtocol.h"	/* ftpPathLen - sized for gXferBody */

#define lzBusyPadX   10
#define lzBusyH      24
#define lzBusyMarg    4

static WinHandle     gBusySaved;
static RectangleType gBusySaveR;

SEC001 void LzTPBusyBegin(const char *title)
{
	RectangleType r;
	WinHandle prevDraw;
	Err err;
	Int16 w, len = (Int16) StrLen(title);

	prevDraw = WinGetDrawWindow();
	WinSetDrawWindow(WinGetDisplayWindow());

	FntSetFont(boldFont);
	w = FntCharsWidth((Char *) title, len);
	r.extent.x = (Int16) (w + 2 * lzBusyPadX);
	r.extent.y = lzBusyH;
	r.topLeft.x = (Int16) ((160 - r.extent.x) / 2);
	r.topLeft.y = (Int16) ((160 - r.extent.y) / 2);

	gBusySaveR.topLeft.x = (Int16) (r.topLeft.x - lzBusyMarg);
	gBusySaveR.topLeft.y = (Int16) (r.topLeft.y - lzBusyMarg);
	gBusySaveR.extent.x = (Int16) (r.extent.x + 2 * lzBusyMarg);
	gBusySaveR.extent.y = (Int16) (r.extent.y + 2 * lzBusyMarg);

	gBusySaved = WinSaveBits(&gBusySaveR, &err);
	WinEraseRectangle(&r, 3);
	WinDrawRectangleFrame(dialogFrame, &r);
	WinDrawChars((Char *) title, len, r.topLeft.x + lzBusyPadX, r.topLeft.y + 6);

	WinSetDrawWindow(prevDraw);
}

SEC001 void LzTPBusyEnd(void)
{
	if (gBusySaved) {
		WinHandle prevDraw = WinGetDrawWindow();
		WinSetDrawWindow(WinGetDisplayWindow());
		WinRestoreBits(gBusySaved, gBusySaveR.topLeft.x, gBusySaveR.topLeft.y);
		WinSetDrawWindow(prevDraw);
		gBusySaved = NULL;
	}
}

SEC001 void LzTPBusyStatusHook(const char *status)
{
	/* Real-hardware crash found this session, with a precise repro: let the
	 * underlying SLIP link's own idle timeout (a Network Preferences
	 * setting, independent of anything this app configures) AND the
	 * device's own auto-off both fire while lazyFTP is sitting connected,
	 * wake the device, then trigger any command - it fails, this hook
	 * fires ("Reconnecting..."), and the moment PalmOS's OWN system-level
	 * network redial UI takes over the screen (necessary because the
	 * whole SLIP link is down, not just this app's TCP session), it
	 * crashes. This app's own hand-drawn overlay (WinSaveBits-based, see
	 * LzTPBusyBegin) is a screen-direct save/restore that has no way to
	 * know or cooperate with the OS needing the SAME screen region for its
	 * own dial-up UI at literally the same moment - two independent
	 * pieces of code each assuming they own the display is a classic
	 * PalmOS crash class, not something safe to paper over by trying to
	 * keep our own text up through it. Only safe option: get our own
	 * overlay OUT OF THE WAY completely before the redial can happen,
	 * rather than trying to coexist with it - `status` is intentionally
	 * unused now (was drawing "Reconnecting..." text here before this
	 * fix). LzTPProgressEnd() is also called (a safe no-op if the busy
	 * toast, not the progress form, is what's actually active right now)
	 * since FtpAutoReconnect can fire from inside an upload/download's own
	 * STOR/RETR command negotiation too, before any chunk has been sent -
	 * see LzTPProgressBar's own guard for the other half of this fix. */
	(void) status;
	LzTPBusyEnd();
	LzTPProgressEnd();
}

/* title may be NULL to just tear the overlay down without showing anything
 * new yet - used right before FtpConnect's retry loop touches the network
 * again (NetLibClose/NetLibOpen), so our own screen-direct overlay is fully
 * out of the way first, same reasoning as LzTPBusyStatusHook just above:
 * a real crash already happened once from this exact overlay colliding
 * with PalmOS's own system-level redial UI trying to use the same screen
 * region at the same moment - clearing before any network call that could
 * possibly trigger that UI avoids it by construction rather than hoping it
 * doesn't happen. */
SEC001 void LzTPBusyRetitle(const char *title)
{
	LzTPBusyEnd();
	if (title) LzTPBusyBegin(title);
}

/* ---- transfer progress bar: a REAL modal form (frmXferProgress), same
 * design/geometry as Sec35's own frmProgress (sec35.c's ProgressBegin/
 * ProgressBar/ProgressEnd) - drawn into a form so the window manager saves
 * the pixels behind it and restores them on erase (no leftover artifacts,
 * unlike this file's own earlier screen-direct WinSaveBits/WinRestoreBits
 * approach, which this replaces). Loops driving this (StoreDbWriteProc/
 * RetrieveDbReadProc in FtpProtocol.c, via FtpSetProgressHook) are tight
 * synchronous callbacks with no event pump, so - matching Sec35's own
 * function exactly, including WHY - every call also resets the auto-off
 * timer (throttled to once per 15s, not every call: the trap itself has
 * real cost multiplied across potentially thousands of chunks) and drains
 * the event queue without dispatching anything. This is the SAME class of
 * long-blocking-loop-without-auto-off-reset bug this session's real crash
 * investigation found and fixed in FtpDataReceive - carrying the fix
 * forward here from the start rather than needing to rediscover it again
 * for this new code path. */
static UInt32  gXferLastAutoOffTick;
static FormPtr gXferProgForm;
static FormPtr gXferPrevForm;
static Int16   gXferLastPct = -1;
static Boolean gXferCancelled = false;
static char    gXferBody[ftpPathLen];	/* copied, not just referenced - see
					   LzTPProgressSetPhase, which needs to
					   redraw it after a fresh FrmDrawForm */

static void XferDrawBody(void)
{
	if (!gXferBody[0]) return;
	{
		FontID old = FntSetFont(stdFont);
		Int16 len = (Int16) StrLen(gXferBody);
		Int16 w = FntCharsWidth((Char *) gXferBody, len);
		Int16 x = (Int16) ((156 - w) / 2);
		if (x < 2) x = 2;
		WinDrawChars((Char *) gXferBody, len, x, 16);
		FntSetFont(old);
	}
}

SEC001 void LzTPProgressBegin(const char *title, const char *body)
{
	gXferPrevForm = FrmGetActiveForm();
	gXferProgForm = FrmInitForm(frmXferProgress);
	gXferLastAutoOffTick = 0;	/* forces an immediate first reset in LzTPProgressBar */
	gXferLastPct = -1;
	gXferCancelled = false;

	if (body) StrNCopy(gXferBody, body, sizeof(gXferBody) - 1);
	else gXferBody[0] = '\0';
	gXferBody[sizeof(gXferBody) - 1] = '\0';

	FrmSetActiveForm(gXferProgForm);
	FrmSetTitle(gXferProgForm, (Char *) title);
	FrmDrawForm(gXferProgForm);
	XferDrawBody();

	LzTPProgressBar(0, 1);
}

/* Switches the dialog title mid-transfer (e.g. "Uploading" -> "CRC
 * Checking") and resets the bar/auto-off throttle exactly like a fresh
 * LzTPProgressBegin, so the 0%-restart reads as "new phase" rather than a
 * stall. Does NOT touch gXferCancelled - a cancel tapped right at the phase
 * boundary must still be honored. FrmDrawForm repaints the whole form (title
 * + Cancel button), which would otherwise wipe the body text drawn outside
 * the form's own object list - XferDrawBody() puts it back. */
SEC001 void LzTPProgressSetPhase(const char *title)
{
	if (!gXferProgForm) return;
	FrmSetTitle(gXferProgForm, (Char *) title);
	FrmDrawForm(gXferProgForm);
	XferDrawBody();
	gXferLastAutoOffTick = 0;
	gXferLastPct = -1;
	LzTPProgressBar(0, 1);
}

SEC001 Boolean LzTPProgressCancelled(void)
{
	return gXferCancelled;
}

SEC001 void LzTPProgressBar(UInt32 done, UInt32 total)
{
	RectangleType bar, fill;
	Int16 pct = total ? (Int16) ((UInt32) 100 * done / total) : 0;
	Int16 w;

	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;

	{
		UInt32 now = TimGetTicks();
		if (now - gXferLastAutoOffTick >= (UInt32) SysTicksPerSecond() * 15) {
			EvtResetAutoOffTimer();
			gXferLastAutoOffTick = now;
		}
	}
	{
		EventType ev;
		while (EvtGetEvent(&ev, 0), ev.eType != nilEvent) {
			/* Drain, don't dispatch through FrmHandleEvent - a prior bug in
			 * Address35's own A35ProgressBar (same no-event-pump idiom)
			 * showed that feeding arbitrary events (incl. appStopEvent)
			 * into a form handler from inside this kind of drain loop can
			 * crash on app-close. Detect a Cancel tap directly against the
			 * button's own bounds instead - same idiom KeypadHitTest uses
			 * in cryptonote/PasswordForm.c - so every other event type is
			 * still just silently discarded, unchanged from before. */
			if (gXferProgForm && ev.eType == penUpEvent) {
				RectangleType r;
				UInt16 idx = FrmGetObjectIndex(gXferProgForm, ctlXferCancel);
				if (idx != frmInvalidObjectId) {
					FrmGetObjectBounds(gXferProgForm, idx, &r);
					if (RctPtInRectangle(ev.screenX, ev.screenY, &r)) {
						gXferCancelled = true;
					}
				}
			}
		}
	}

	/* Guards against a real corruption risk, not just a cosmetic glitch:
	 * LzTPBusyStatusHook can tear this form down mid-transfer (see its own
	 * comment - a reconnect triggered by the STOR/RETR command itself,
	 * before any chunk has been sent) without this function's own caller
	 * knowing. Without this check, a subsequent progress-hook call after
	 * that teardown would draw the bar's fixed-position rectangles
	 * straight onto whatever form is ACTUALLY active now (e.g. frmMain,
	 * restored by LzTPProgressEnd) - corrupting its unrelated UI instead
	 * of just silently skipping a redraw that no longer has anywhere
	 * correct to go. */
	if (!gXferProgForm) return;

	bar.topLeft.x = 6; bar.topLeft.y = 30; bar.extent.x = 144; bar.extent.y = 10;
	if (pct == gXferLastPct) return;

	WinDrawRectangleFrame(simpleFrame, &bar);
	w = (Int16) ((UInt32) 142 * pct / 100);
	if (w > 0) {
		fill.topLeft.x = 7; fill.topLeft.y = 31; fill.extent.x = w; fill.extent.y = 8;
		WinDrawRectangle(&fill, 0);
	}
	gXferLastPct = pct;
}

SEC001 void LzTPProgressEnd(void)
{
	if (gXferProgForm) {
		FrmEraseForm(gXferProgForm);
		FrmSetActiveForm(gXferPrevForm);
		FrmDeleteForm(gXferProgForm);
		gXferProgForm = NULL;
	}
	/* A long transfer (esp. CRC verify on a big upload) can eat most of the
	 * auto-off window without ever tripping the 15s throttle above again -
	 * give the user a full fresh idle period right as the dialog closes
	 * instead of risking an auto-off moments after a completed transfer. */
	EvtResetAutoOffTimer();
}
