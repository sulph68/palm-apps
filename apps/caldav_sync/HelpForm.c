/*
 * HelpForm.c - read-only scrollable reference for every input label across
 * all 4 server types (Record > Help). One line each: "Label: what to
 * provide", section titles (COMMON/CALDAV/ICLOUD/GOOGLE/ICS FEED) and each
 * label prefix in boldFont, description text in plain stdFont - a single
 * PalmOS FIELD can't mix two fonts within one line (and always underlines
 * MULTIPLELINES text), so this hand-draws the whole content area directly
 * instead, the same custom-draw idiom DateDay.c's icon drawing uses
 * (FntSetFont/FntGetFont save-restore around each run).
 *
 * Real pixel-width word-wrap, not hand-guessed line breaks: FntWordWrap
 * returns how many characters of a string fit within a given pixel width,
 * breaking at a word boundary where possible (and hard-breaking on its own
 * if a single word is too long to fit at all) - mirrors lazyFTP's own
 * Listing.c ComputeNameWrap exactly, including its documented gotcha:
 * FntWordWrap breaks AT a space but does not consume it, so the next
 * line's start must skip any run of spaces sitting right at the break
 * point or it starts with a leading space.
 *
 * A wrapped item's continuation rows are hanging-indented by
 * kHelpHangIndent, independent of that item's own label width, so every
 * continuation line in the whole form lines up the same regardless of
 * which label wrapped.
 */
#include "HelpForm.h"

typedef enum { helpHeader, helpItem, helpBlank } HelpEntryKind;

typedef struct {
	HelpEntryKind kind;
	const char *label;	/* header text, or the bold "Label:" prefix */
	const char *desc;	/* plain description text (helpItem only) */
} HelpEntryType;

static const HelpEntryType gHelpEntries[] = {
	{ helpHeader, "COMMON", NULL },
	{ helpItem,   "Alias:", "a short name for this slot, shown in the slot list." },
	{ helpItem,   "Type:", "CalDAV, iCloud, Google, or ICS Feed." },
	{ helpItem,   "Ignore:", "skip this slot when running Sync All." },
	{ helpItem,   "Sync from/to:", "optional per-slot override of the global sync window." },
	{ helpBlank,  NULL, NULL },
	{ helpHeader, "CALDAV", NULL },
	{ helpItem,   "Server:", "the CalDAV server's base URL (https://...)." },
	{ helpItem,   "Path:", "the calendar's path/collection on that server." },
	{ helpItem,   "User:", "your CalDAV username." },
	{ helpItem,   "Passwd:", "your CalDAV password, or an app password if 2FA is on." },
	{ helpBlank,  NULL, NULL },
	{ helpHeader, "ICLOUD", NULL },
	{ helpItem,   "AppleID:", "your full Apple ID email address." },
	{ helpItem,   "Passwd:", "an app-specific password from appleid.apple.com, not your regular Apple ID password." },
	{ helpItem,   "Host:", "your CalDAV shard host, e.g. p32-caldav.icloud.com - tap to open the full editor." },
	{ helpItem,   "DSID:", "your iCloud account's numeric DSID." },
	{ helpItem,   "Cal UUID:", "the target calendar's UUID - tap to open the full editor." },
	{ helpBlank,  NULL, NULL },
	{ helpHeader, "GOOGLE", NULL },
	{ helpItem,   "Calendar ID:", "usually your Gmail address, or one copied from Google Calendar's own Settings page." },
	{ helpItem,   "Sign in with Google:", "starts the one-time browser sign-in flow." },
	{ helpBlank,  NULL, NULL },
	{ helpHeader, "ICS FEED", NULL },
	{ helpItem,   "URL:", "the public .ics feed URL to subscribe to (read-only, no login)." },
};
#define kHelpEntryCount (sizeof(gHelpEntries) / sizeof(gHelpEntries[0]))

#define kHelpLabelGap   3	/* px gap between a bold label and its description */
#define kHelpHangIndent 6	/* px indent for a wrapped item's continuation rows */
#define kMaxHelpRows    48	/* generous margin over this fixed content's real row count */

typedef struct {
	const char *bold; Int16 boldLen;
	const char *plain; Int16 plainLen;
	Int16 indent;
} HelpRowType;

static const RectangleType gHelpContentR = { { 4, 17 }, { 142, 116 } };

static HelpRowType gHelpRows[kMaxHelpRows];
static Int16 gHelpRowCount = 0;
static Int16 gHelpTopLine = 0;
static Int16 gHelpVisibleLines = 1;
static Int16 gHelpLineHeight = 1;

static void HelpLineHeight(void)
{
	FontID prevFont = FntSetFont(stdFont);
	Int16 lhStd = FntLineHeight();
	Int16 lhBold;
	FntSetFont(boldFont);
	lhBold = FntLineHeight();
	FntSetFont(prevFont);
	gHelpLineHeight = (lhBold > lhStd) ? lhBold : lhStd;
	gHelpVisibleLines = (Int16) (gHelpContentR.extent.y / gHelpLineHeight);
	if (gHelpVisibleLines < 1) gHelpVisibleLines = 1;
}

static void HelpLayout(void)
{
	Int16 rowCount = 0;
	Int16 i;
	FontID prevFont = FntGetFont();

	for (i = 0; i < (Int16) kHelpEntryCount && rowCount < kMaxHelpRows; i++) {
		const HelpEntryType *e = &gHelpEntries[i];

		if (e->kind == helpHeader) {
			gHelpRows[rowCount].bold = e->label;
			gHelpRows[rowCount].boldLen = (Int16) StrLen(e->label);
			gHelpRows[rowCount].plain = NULL;
			gHelpRows[rowCount].plainLen = 0;
			gHelpRows[rowCount].indent = 0;
			rowCount++;
			continue;
		}
		if (e->kind == helpBlank) {
			gHelpRows[rowCount].bold = NULL;
			gHelpRows[rowCount].boldLen = 0;
			gHelpRows[rowCount].plain = NULL;
			gHelpRows[rowCount].plainLen = 0;
			gHelpRows[rowCount].indent = 0;
			rowCount++;
			continue;
		}

		{
			Int16 labelLen = (Int16) StrLen(e->label);
			Int16 labelW;
			const char *desc = e->desc;
			Int16 descLen;
			Boolean firstRow = true;

			FntSetFont(boldFont);
			labelW = FntCharsWidth(e->label, labelLen);
			FntSetFont(stdFont);

			while (*desc == ' ') desc++;
			descLen = (Int16) StrLen(desc);

			for (;;) {
				Int16 avail = firstRow
					? (Int16) (gHelpContentR.extent.x - labelW - kHelpLabelGap)
					: (Int16) (gHelpContentR.extent.x - kHelpHangIndent);
				Int16 n;

				if (avail < 10) avail = 10;
				n = (Int16) FntWordWrap(desc, (UInt16) avail);
				if (n <= 0) n = 1;
				if (n > descLen) n = descLen;
				if (rowCount >= kMaxHelpRows) break;

				gHelpRows[rowCount].bold = firstRow ? e->label : NULL;
				gHelpRows[rowCount].boldLen = (Int16) (firstRow ? labelLen : 0);
				gHelpRows[rowCount].plain = desc;
				gHelpRows[rowCount].plainLen = n;
				gHelpRows[rowCount].indent = (Int16) (firstRow ? 0 : kHelpHangIndent);
				rowCount++;

				desc += n;
				descLen = (Int16) (descLen - n);
				while (descLen > 0 && *desc == ' ') { desc++; descLen--; }
				firstRow = false;
				if (descLen <= 0) break;
			}
		}
	}

	FntSetFont(prevFont);
	gHelpRowCount = rowCount;
}

static void HelpDrawVisible(void)
{
	FontID prevFont = FntGetFont();
	Int16 y = gHelpContentR.topLeft.y;
	Int16 i;

	WinEraseRectangle((RectangleType *) &gHelpContentR, 0);

	for (i = gHelpTopLine; i < gHelpRowCount && i < gHelpTopLine + gHelpVisibleLines; i++) {
		const HelpRowType *row = &gHelpRows[i];
		Int16 x = (Int16) (gHelpContentR.topLeft.x + row->indent);

		if (row->boldLen > 0) {
			FntSetFont(boldFont);
			WinDrawChars(row->bold, row->boldLen, x, y);
			x = (Int16) (x + FntCharsWidth(row->bold, row->boldLen) + kHelpLabelGap);
		}
		if (row->plainLen > 0) {
			FntSetFont(stdFont);
			WinDrawChars(row->plain, row->plainLen, x, y);
		}
		y = (Int16) (y + gHelpLineHeight);
	}

	FntSetFont(prevFont);
}

static void HelpSyncScroll(FormPtr frm)
{
	ScrollBarPtr scl = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclHelp));
	Int16 maxValue = (Int16) (gHelpRowCount - gHelpVisibleLines);
	if (maxValue < 0) maxValue = 0;
	SclSetScrollBar(scl, gHelpTopLine, 0, maxValue, gHelpVisibleLines);
}

static void HelpScrollTo(FormPtr frm, Int16 newTop)
{
	Int16 maxValue = (Int16) (gHelpRowCount - gHelpVisibleLines);
	if (maxValue < 0) maxValue = 0;
	if (newTop < 0) newTop = 0;
	if (newTop > maxValue) newTop = maxValue;
	if (newTop == gHelpTopLine) return;
	gHelpTopLine = newTop;
	HelpDrawVisible();
	HelpSyncScroll(frm);
}

SEC001 void HelpFormShow(void)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmHelp);
	Boolean done = false;

	if (!frm) return;

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);

	gHelpTopLine = 0;
	HelpLineHeight();
	HelpLayout();
	HelpDrawVisible();
	HelpSyncScroll(frm);

	while (!done) {
		EventType ev;
		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent && ev.data.ctlSelect.controlID == btnHelpDone) {
			done = true;
			continue;
		}
		if (ev.eType == sclRepeatEvent && ev.data.sclRepeat.scrollBarID == sclHelp) {
			HelpScrollTo(frm, ev.data.sclRepeat.newValue);
			continue;
		}
		if (ev.eType == keyDownEvent &&
			(ev.data.keyDown.chr == vchrPageUp || ev.data.keyDown.chr == vchrPageDown)) {
			Int16 page = (Int16) (gHelpVisibleLines - 1);
			if (page < 1) page = 1;
			HelpScrollTo(frm, (Int16) (gHelpTopLine + (ev.data.keyDown.chr == vchrPageDown ? page : -page)));
			continue;
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (SysHandleEvent(&ev)) continue;
		FrmHandleEvent(frm, &ev);
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev) FrmSetActiveForm(prev);
}
