/*
 * SyncProgress.c - status+progress-bar modal shown for the duration of a
 * sync (user request, 2026-09-17). Mirrors BitWarden's own
 * BwProgressShow/SetStatus/Bar/Hide (Dialogs.c, read as reference,
 * original code here) - same centered-modal shape (this suite's one
 * documented exception to the bottom-aligned-dialog rule), same
 * hand-drawn sawtooth bar (no true total to compute a real percentage
 * against - WiRSa's own SYNC_DONE only reports the final count once
 * everything has already streamed).
 *
 * The auto-off-timer safety net this needs during a long wait already
 * lives at the transport layer (Serial.c's ReadExact polls in short
 * slices and calls EvtResetAutoOffTimer between them, same proven
 * pattern BitWarden's own Serial.c uses) - this file doesn't need to
 * know about it.
 */
#include "SyncProgress.h"

static FormPtr gProgressFrm = NULL;
static FormPtr gProgressPrevFrm = NULL;
static Boolean gProgressBarShown = false;
static Boolean gProgressCancelled = false;

#define cddvProgressBarX	6
#define cddvProgressBarY	30
#define cddvProgressBarW	144
#define cddvProgressBarH	10
#define cddvProgressCycleItems	20

void SyncProgressShow(const char *title)
{
	gProgressPrevFrm = FrmGetActiveForm();
	gProgressFrm = FrmInitForm(frmSyncProgress);
	if (!gProgressFrm) return;
	gProgressBarShown = false;
	gProgressCancelled = false;
	FrmSetActiveForm(gProgressFrm);
	FrmDrawForm(gProgressFrm);
	SyncProgressSetStatus(title);
}

/* Direct bounds check against the raw pen event rather than dispatching it
 * through FrmHandleEvent - mirrors BitWarden's own BwProgressPenDownInBounds/
 * NotePenDown/NotePenUp exactly (Dialogs.c), including its own real-device
 * bug history: feeding arbitrary events (including appStopEvent) into a
 * form handler from inside a tight synchronous drain loop crashed
 * lazyFTP's own equivalent, and checking only where the penUpEvent LANDED
 * (not where the matching penDownEvent had STARTED) let a stray touch or a
 * drag merely passing through the button's small rectangle count as a tap
 * - real PalmOS controls require both down and up on the same control,
 * reimplemented here explicitly since bypassing FrmHandleEvent loses that
 * safety net. */
static Boolean SyncProgressPenDownInBounds(Int16 x, Int16 y)
{
	RectangleType r;
	UInt16 idx;
	if (!gProgressFrm) return false;
	idx = FrmGetObjectIndex(gProgressFrm, btnSyncCancel);
	if (idx == frmInvalidObjectId) return false;
	FrmGetObjectBounds(gProgressFrm, idx, &r);
	return RctPtInRectangle(x, y, &r);
}

static Boolean gProgressPenDownInCancel = false;

void SyncProgressNotePenDown(Int16 x, Int16 y)
{
	gProgressPenDownInCancel = (Boolean) (!gProgressCancelled && SyncProgressPenDownInBounds(x, y));
}

void SyncProgressNotePenUp(Int16 x, Int16 y)
{
	Boolean downWasIn = gProgressPenDownInCancel;
	gProgressPenDownInCancel = false;
	if (!downWasIn || gProgressCancelled) return;
	if (SyncProgressPenDownInBounds(x, y)) gProgressCancelled = true;
}

Boolean SyncProgressCancelled(void)
{
	return gProgressCancelled;
}

void SyncProgressSetStatus(const char *status)
{
	RectangleType r;
	if (!gProgressFrm) return;
	r.topLeft.x = 6; r.topLeft.y = 12;
	r.extent.x = 148; r.extent.y = 16;
	WinEraseRectangle(&r, 0);
	FntSetFont(stdFont);
	WinDrawChars(status, StrLen(status), 8, 14);
}

/* `done` = items received so far - a sawtooth (fills 0->full every
 * cddvProgressCycleItems items, then snaps back to 0), an honest "still
 * working" signal instead of a bar that stops meaning anything once a
 * sync goes past a fixed item count. Call with done=0 once to reveal the
 * bar frame right as streaming starts. */
void SyncProgressBar(UInt16 done)
{
	RectangleType bar, fill;
	Int16 w;
	UInt16 phase;
	if (!gProgressFrm) return;

	if (!gProgressBarShown) {
		bar.topLeft.x = cddvProgressBarX; bar.topLeft.y = cddvProgressBarY;
		bar.extent.x = cddvProgressBarW; bar.extent.y = cddvProgressBarH;
		WinDrawRectangleFrame(simpleFrame, &bar);
		gProgressBarShown = true;
	}

	phase = done % cddvProgressCycleItems;
	w = (Int16) ((cddvProgressBarW - 2) * phase / cddvProgressCycleItems);

	fill.topLeft.x = cddvProgressBarX + 1; fill.topLeft.y = cddvProgressBarY + 1;
	fill.extent.x = cddvProgressBarW - 2; fill.extent.y = cddvProgressBarH - 2;
	WinEraseRectangle(&fill, 0);

	if (w > 0) {
		fill.extent.x = w;
		WinDrawRectangle(&fill, 0);
	}
}

void SyncProgressHide(void)
{
	if (!gProgressFrm) return;
	FrmEraseForm(gProgressFrm);
	FrmDeleteForm(gProgressFrm);
	gProgressFrm = NULL;
	gProgressBarShown = false;
	if (gProgressPrevFrm) { FrmSetActiveForm(gProgressPrevFrm); FrmDrawForm(gProgressPrevFrm); }
}
