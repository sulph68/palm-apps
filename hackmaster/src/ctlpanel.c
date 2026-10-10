/*
 * ctlpanel.c - HackMaster Y2K35 control panel (code + tFRM #2000).
 *
 * Launched by X-Master's "Configure". Each of Year/Month/Day is shown as a row
 * of single-digit push-buttons (tap one to select it); a repeating up/down
 * button (tREP) increments/decrements the selected digit (0-9, no carry). Time
 * uses the standard Set Time dialog (SelectOneTime). "Set" stores a 64-bit
 * master clock (pref + Feature), re-bases via datelib, sets the RTC, and
 * publishes the whole-year offset.
 *
 * Stand-alone code resource: NO global/static *data* (static const tables live in
 * .text and are fine). Digit push-button labels are 2-byte buffers allocated on
 * form open and freed on close; their pointers are recovered via CtlGetLabel, so
 * no globals are needed to track them.
 */
#include <PalmOS.h>
#include <Standalone.h>
#include "datelib.h"

#define HACK_CREATOR  'Y235'
#define FTR_OFFSET    0
#define FTR_MHI       1
#define FTR_MLO       2
#define PREF_ID       0
#define HM_LIST_FORM  9000

#define fldTime   2013
#define fldBased  2020
#define btnSet    2001
#define btnDone   2002
#define btnTime   2203

static const UInt16 kYear[4]  = { 2300, 2301, 2302, 2303 };
static const UInt16 kMonth[2] = { 2310, 2311 };
static const UInt16 kDay[2]   = { 2320, 2321 };
#define YUP 2400
#define YDN 2401
#define MUP 2410
#define MDN 2411
#define DUP 2420
#define DDN 2421

typedef struct { dl_i64 master; dl_i32 offset; } PrefBlk;

static ControlPtr Ctl(FormPtr frm, UInt16 id)
{
    return (ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}
static FieldPtr Fld(FormPtr frm, UInt16 id)
{
    return (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

/* ---- field text (owned handle) ---- */
static void SetText(FormPtr frm, UInt16 id, const Char *s)
{
    FieldPtr  f   = Fld(frm, id);
    MemHandle old = FldGetTextHandle(f);
    MemHandle h;
    Char     *p;
    UInt16    len = StrLen(s);
    FldSetTextHandle(f, NULL);
    if (old) MemHandleFree(old);
    h = MemHandleNew(len + 1);
    if (!h) return;
    p = (Char *) MemHandleLock(h);
    StrCopy(p, s);
    MemHandleUnlock(h);
    FldSetTextHandle(f, h);
    FldDrawField(f);
}

/* ---- digit push-buttons (owned 2-byte labels) ---- */
static void InitDigit(FormPtr frm, UInt16 id)
{
    Char *b = (Char *) MemPtrNew(2);
    if (!b) return;
    b[0] = '0'; b[1] = '\0';
    CtlSetLabel(Ctl(frm, id), b);
}
static void FreeDigit(FormPtr frm, UInt16 id)
{
    void *l = (void *) CtlGetLabel(Ctl(frm, id));
    if (l) MemPtrFree(l);
}
static Char GetDigit(FormPtr frm, UInt16 id)
{
    const Char *l = CtlGetLabel(Ctl(frm, id));
    return (l && l[0]) ? l[0] : '0';
}
static void SetDigit(FormPtr frm, UInt16 id, Char ch)
{
    Char *l = (Char *) CtlGetLabel(Ctl(frm, id));
    if (!l) return;
    l[0] = ch; l[1] = '\0';
    CtlSetLabel(Ctl(frm, id), l);   /* redraw with new digit */
}

static Int32 GetVal(FormPtr frm, const UInt16 *ids, Int16 n)
{
    Int32 v = 0; Int16 i;
    for (i = 0; i < n; i++) v = v * 10 + (GetDigit(frm, ids[i]) - '0');
    return v;
}
static void SetVal(FormPtr frm, const UInt16 *ids, Int16 n, Int32 v)
{
    Int16 i;
    if (v < 0) v = 0;
    for (i = n - 1; i >= 0; i--) { SetDigit(frm, ids[i], (Char)('0' + (v % 10))); v /= 10; }
}

/* selected digit in a field (falls back to the ones digit) */
static UInt16 SelId(FormPtr frm, const UInt16 *ids, Int16 n)
{
    Int16 i;
    for (i = 0; i < n; i++)
        if (CtlGetValue(Ctl(frm, ids[i]))) return ids[i];
    return ids[n - 1];
}
static void BumpSel(FormPtr frm, const UInt16 *ids, Int16 n, Int16 delta)
{
    UInt16 id = SelId(frm, ids, n);
    Int16  d  = (GetDigit(frm, id) - '0' + delta + 10) % 10;
    SetDigit(frm, id, (Char)('0' + d));
}

#define YEAR_MIN 1904   /* Palm epoch: 1 Jan 1904 is the minimum settable date */
#define YEAR_MAX 9999

/* Clamp all fields to sane values after any spinner bump: year to
 * [1904, 9999] (1904 = the Palm epoch, the minimum date), month to 1..12, and
 * day to 1..daysInMonth (Feb honours the year's leap status). This makes the
 * minimum settable moment 1904-01-01 and blocks nonsense like month 13 or day
 * 55. Year's four digits also cap it at 9999. */
static void ClampFields(FormPtr frm)
{
    Int32 y  = GetVal(frm, kYear, 4);
    Int16 mo, d, dim;

    if (y < YEAR_MIN) y = YEAR_MIN;
    if (y > YEAR_MAX) y = YEAR_MAX;
    SetVal(frm, kYear, 4, y);

    mo = (Int16) GetVal(frm, kMonth, 2);
    if (mo < 1)  mo = 1;
    if (mo > 12) mo = 12;
    SetVal(frm, kMonth, 2, mo);

    dim = dl_daysInMonth(y, mo);
    d = (Int16) GetVal(frm, kDay, 2);
    if (d < 1)   d = 1;
    if (d > dim) d = dim;
    SetVal(frm, kDay, 2, d);
}

/* ---- time ---- */
static void SetTimeField(FormPtr frm, Int16 h, Int16 m)
{
    Char b[8];
    StrPrintF(b, "%02d:%02d", (int) h, (int) m);
    SetText(frm, fldTime, b);
}
static void ParseTime(FormPtr frm, Int16 *h, Int16 *m)
{
    Char *t = FldGetTextPtr(Fld(frm, fldTime)), *p;
    *h = 0; *m = 0;
    if (!t) return;
    *h = (Int16) StrAToI(t);
    for (p = t; *p && *p != ':'; p++) ;
    if (*p == ':') *m = (Int16) StrAToI(p + 1);
}
static void DoSetTime(FormPtr frm)
{
    Int16 h, m;
    ParseTime(frm, &h, &m);
    if (SelectOneTime(&h, &m, "Set Time")) SetTimeField(frm, h, m);
}

static void FrameBased(FormPtr frm)
{
    RectangleType r;
    FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, fldBased), &r);
    r.topLeft.x -= 3; r.topLeft.y -= 2;
    r.extent.x  += 6; r.extent.y  += 4;
    WinDrawRectangleFrame(simpleFrame, &r);
}

static void Populate(FormPtr frm)
{
    PrefBlk    p;
    UInt16     sz = sizeof(p);
    DlDateTime dt;
    Int16      i, got;
    Int32      y;

    for (i = 0; i < 4; i++) InitDigit(frm, kYear[i]);
    for (i = 0; i < 2; i++) InitDigit(frm, kMonth[i]);
    for (i = 0; i < 2; i++) InitDigit(frm, kDay[i]);

    /* Always show the CURRENT era date/time (not the value last "Applied"): the
     * re-based system clock, with the stored whole-year offset added back to
     * recover the true era year. Matches Hc_CurrentRealDate(). So if everything
     * already looks correct the user just Cancels; only Apply changes anything. */
    got = PrefGetAppPreferences(HACK_CREATOR, PREF_ID, &p, &sz, true);
    dl_secondsToDate((dl_i64)(UInt32) TimGetSeconds(), &dt);
    if (got != noPreferenceFound && sz >= sizeof(p))
        y = (Int32) dt.year + p.offset;   /* re-based clock -> real era year */
    else
        y = dt.year;                      /* no hack set yet: clock is already real */

    if (y < YEAR_MIN) y = YEAR_MIN;
    if (y > 9999) y = 9999;
    SetVal(frm, kYear, 4, y);
    SetVal(frm, kMonth, 2, dt.month);
    SetVal(frm, kDay, 2, dt.day);
    SetTimeField(frm, dt.hour, dt.minute);
    SetText(frm, fldBased, "(set to apply)");

    /* select the ones digit of each field by default */
    CtlSetValue(Ctl(frm, kYear[3]), 1);
    CtlSetValue(Ctl(frm, kMonth[1]), 1);
    CtlSetValue(Ctl(frm, kDay[1]), 1);
}

static void DoSet(FormPtr frm)
{
    Int32   year = GetVal(frm, kYear, 4);
    Int16   mo   = (Int16) GetVal(frm, kMonth, 2);
    Int16   d, h, mi, dim;
    Int32   off, basedYear;
    dl_i64  master;
    UInt32  based32;
    PrefBlk p;
    DlDateTime bd;
    Char    line[64], yb[12];

    if (year < YEAR_MIN) year = YEAR_MIN;          /* guard: >= Palm epoch 1904 */
    if (year > YEAR_MAX) year = YEAR_MAX;
    if (mo < 1) mo = 1; if (mo > 12) mo = 12;      /* clamp per-digit edits */
    d = (Int16) GetVal(frm, kDay, 2);
    dim = dl_daysInMonth(year, mo);
    if (d < 1) d = 1; if (d > dim) d = dim;
    ParseTime(frm, &h, &mi);

    off       = dl_reBaseOffset(year);
    basedYear = year - off;
    master    = dl_dateToSeconds(year, mo, d, h, mi, 0);
    based32   = (UInt32) dl_dateToSeconds(basedYear, mo, d, h, mi, 0);

    TimSetSeconds(based32);
    FtrSet(HACK_CREATOR, FTR_OFFSET, (UInt32) off);
    FtrSet(HACK_CREATOR, FTR_MHI, (UInt32) (master >> 32));
    FtrSet(HACK_CREATOR, FTR_MLO, (UInt32) (master & 0xFFFFFFFFUL));
    p.master = master; p.offset = off;
    PrefSetAppPreferences(HACK_CREATOR, PREF_ID, 1, &p, sizeof(p), true);

    dl_secondsToDate((dl_i64) based32, &bd);
    StrIToA(yb, bd.year);
    StrPrintF(line, "yr=%s %d/%d off=%ld", yb, (int) bd.month, (int) bd.day, (long) off);
    SetText(frm, fldBased, line);
}

Boolean CtlPanelHandler(EventType *e)
{
    FormPtr frm = FrmGetActiveForm();

    switch (e->eType) {
    case frmOpenEvent:
        FrmDrawForm(frm);
        Populate(frm);
        FrameBased(frm);
        return true;
    case frmCloseEvent: {
        Int16 i;
        for (i = 0; i < 4; i++) FreeDigit(frm, kYear[i]);
        for (i = 0; i < 2; i++) FreeDigit(frm, kMonth[i]);
        for (i = 0; i < 2; i++) FreeDigit(frm, kDay[i]);
        break;
    }
    case ctlRepeatEvent:
        /* Do the bump, then return FALSE so the repeat control finishes its own
         * redraw and clears the pressed/inverted highlight on pen-up. Returning
         * true here leaves the arrow stuck inverted. */
        switch (e->data.ctlRepeat.controlID) {
        case YUP: BumpSel(frm, kYear, 4, +1);  break;
        case YDN: BumpSel(frm, kYear, 4, -1);  break;
        case MUP: BumpSel(frm, kMonth, 2, +1); break;
        case MDN: BumpSel(frm, kMonth, 2, -1); break;
        case DUP: BumpSel(frm, kDay, 2, +1);   break;
        case DDN: BumpSel(frm, kDay, 2, -1);   break;
        default: break;
        }
        ClampFields(frm);     /* year>=1904, month 1-12, day 1..daysInMonth */
        return false;
    case ctlSelectEvent: {
        UInt16 id = e->data.ctlSelect.controlID;
        if (id == btnSet)  { DoSet(frm); return true; }
        if (id == btnDone) { FrmGotoForm(HM_LIST_FORM); return true; }
        if (id == btnTime) { DoSetTime(frm); return true; }
        break;
    }
    default:
        break;
    }
    return false;
}

STANDALONE_CODE_RESOURCE_ID(2000);
