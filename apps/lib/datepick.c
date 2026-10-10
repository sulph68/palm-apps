/*
 * datepick.c - see datepick.h. A full-range date picker laid out to look like
 * the ROM SelectDay "Go to Date" dialog: year label + prev/next arrows, a 6x2
 * grid of month-name push buttons, a custom-drawn day calendar, Cancel/Today.
 * Behaves like SelectDay: tapping a day (or Today) confirms and closes;
 * year arrows / month buttons just navigate. datelib gives day-of-week /
 * days-in-month for any year, so there is no 2031 cap.
 */
#include <PalmOS.h>
#include "datelib.h"
#include "datepick.h"
#include "hackcheck.h"   /* Hc_CurrentRealDate for the Today button */

#define YMIN 1904
#define YMAX 9999
#define HDR_H 10   /* day rows start gadget_top+10, matching the ROM SelectDay */

/* App-side globals are fine (unlike the hack): keep picker state here. */
static DlDateTime gPick;      /* the currently-selected date (shown inverted)   */
static DlDateTime gToday;     /* the real current date (marked with parentheses) */
static Int16      gResult;   /* -1 running, 0 cancel, 1 chosen */
static char gYearBuf[8], gTitleBuf[40];
/* Valid REAL-year range = the active era's window [1904, 2031] shifted by the
 * plugin offset; a picked date must map back into a DateType window year. */
static Int32 gYMin = YMIN, gYMax = YMAX;

DATELIB_SECTION static void ClampDay(void)
{
    Int16 dim = DaysInMonth(gPick.month, gPick.year);
    if (gPick.day < 1)   gPick.day = 1;
    if (gPick.day > dim) gPick.day = dim;
}

DATELIB_SECTION static void ShowYear(FormType *f)
{
    StrIToA(gYearBuf, gPick.year);
    FrmCopyLabel(f, DP_LBL_YEAR, gYearBuf);
}

DATELIB_SECTION static void ShowMonth(FormType *f)
{
    FrmSetControlGroupSelection(f, DP_MON_GROUP, DP_MON_JAN + gPick.month - 1);
}

DATELIB_SECTION static void CalGeom(FormType *f, RectangleType *g, Int16 *cw, Int16 *ch)
{
    FrmGetObjectBounds(f, FrmGetObjectIndex(f, DP_GADGET), g);
    *cw = g->extent.x / 7;          /* 140/7 = 20, as the ROM SelectDay gadget */
    *ch = 10;                        /* fixed 10px rows to match the ROM pitch  */
}

DATELIB_SECTION static void DrawCal(FormType *f)
{
    RectangleType g;
    Int16 cw, ch, i, d, fw, dim, fh;
    Int16 selCx, selCy;
    const char *hd = "SMTWTFS";
    char c[2];

    CalGeom(f, &g, &cw, &ch);
    WinEraseRectangle(&g, 0);

    FntSetFont(boldFont);
    c[1] = '\0';
    for (i = 0; i < 7; i++) {
        c[0] = hd[i];
        WinDrawChars(c, 1, g.topLeft.x + i * cw + (cw - FntCharsWidth(c, 1)) / 2,
                     g.topLeft.y);
    }
    FntSetFont(stdFont);
    /* Db35: no separator line under the S M T W T F S header -- the ROM
       SelectDay does not draw one. */

    fw  = DayOfWeek(gPick.month, 1, gPick.year);
    dim = DaysInMonth(gPick.month, gPick.year);
    fh  = FntCharHeight();
    selCx = selCy = -1;
    for (d = 1; d <= dim; d++) {
        Int16 idx = fw + d - 1, col = idx % 7, row = idx / 7;
        Int16 cx = g.topLeft.x + col * cw, cy = g.topLeft.y + HDR_H + row * ch;
        Int16 dw, dx, dy;
        char b[6];
        Boolean isToday = (gPick.year == gToday.year && gPick.month == gToday.month
                           && d == gToday.day);
        StrIToA(b, d);
        dw = FntCharsWidth(b, StrLen(b));
        dx = cx + cw - dw - 5;               /* day numbers are RIGHT-aligned  */
        dy = cy + (ch - fh) / 2;
        WinDrawChars(b, StrLen(b), dx, dy);
        if (isToday) {
            /* Today marker: the day itself is right-aligned exactly like every
               other number (above); the parentheses are then drawn at fixed
               positions near the cell edges -- matching the ROM SelectDay to the
               pixel, which string spacing alone cannot hit. */
            WinDrawChars("(", 1, cx + 1, dy);
            WinDrawChars(")", 1, cx + cw - 4, dy);
        }
        if (d == gPick.day) { selCx = cx; selCy = cy; }
    }
    /* Draw the selected-day highlight LAST, after every digit, so the row below
       (e.g. "11" under a selected "4") can't clip its rounded bottom edge. A
       broad, rounded, solid-inverted cell like the ROM SelectDay. */
    if (selCx >= 0) {
        RectangleType cr;
        cr.topLeft.x = selCx;       cr.topLeft.y = selCy;
        cr.extent.x  = cw;          cr.extent.y  = ch + 1;
        WinInvertRectangle(&cr, 3);
    }
}

/* returns true if a valid day was tapped (and selected) */
DATELIB_SECTION static Boolean TapCal(FormType *f, Int16 x, Int16 y)
{
    RectangleType g;
    Int16 cw, ch, fw, dim, col, row, d;

    CalGeom(f, &g, &cw, &ch);
    if (x < g.topLeft.x || x >= g.topLeft.x + g.extent.x) return false;
    if (y < g.topLeft.y + HDR_H)                          return false;
    col = (x - g.topLeft.x) / cw;
    row = (y - g.topLeft.y - HDR_H) / ch;
    if (col > 6) col = 6;
    fw  = DayOfWeek(gPick.month, 1, gPick.year);
    dim = DaysInMonth(gPick.month, gPick.year);
    d = row * 7 + col - fw + 1;
    if (d >= 1 && d <= dim) { gPick.day = d; return true; }
    return false;
}

DATELIB_SECTION static Boolean PickHandler(EventType *e)
{
    FormType *f = FrmGetActiveForm();
    UInt16 id;

    switch (e->eType) {
    case ctlRepeatEvent:
        id = e->data.ctlRepeat.controlID;
        if (id == DP_YEAR_PREV && gPick.year > gYMin) gPick.year--;
        else if (id == DP_YEAR_NEXT && gPick.year < gYMax) gPick.year++;
        else return false;
        ClampDay(); ShowYear(f); DrawCal(f);
        return false;   /* let the repeat control clear its own highlight */

    case penDownEvent:
        if (TapCal(f, e->screenX, e->screenY)) { gResult = 1; return true; }
        return false;   /* buttons still get their own tracking */

    case ctlSelectEvent:
        id = e->data.ctlSelect.controlID;
        if (id >= DP_MON_JAN && id <= DP_MON_JAN + 11) {
            gPick.month = id - DP_MON_JAN + 1;
            ClampDay(); DrawCal(f);          /* group highlight handled by the OS */
            return true;
        }
        if (id == DP_BTN_TODAY)  { Hc_CurrentRealDate(&gPick); gResult = 1; return true; }
        if (id == DP_BTN_CANCEL) { gResult = 0; return true; }
        break;

    default:
        break;
    }
    return false;
}

DATELIB_SECTION Boolean DatePicker(DlDateTime *io, const char *titleP)
{
    FormType  *prev = FrmGetActiveForm();   /* restore caller's form afterwards */
    FormType  *f = FrmInitForm(DATEPICK_FORM);
    EventType  e;
    Err        err;

    /* Aware tier: the app stores real years, so the picker spans the full range. */
    gYMin = YMIN;
    gYMax = YMAX;
    gPick = *io;
    if (gPick.month < 1 || gPick.month > 12) gPick.month = 1;
    if (gPick.year  < gYMin) gPick.year = gYMin;
    if (gPick.year  > gYMax) gPick.year = gYMax;
    ClampDay();
    Hc_CurrentRealDate(&gToday);   /* the "today" reference marked with parentheses */
    gResult = -1;

    if (titleP) {
        StrNCopy(gTitleBuf, titleP, sizeof(gTitleBuf) - 1);
        gTitleBuf[sizeof(gTitleBuf) - 1] = '\0';
        FrmSetTitle(f, gTitleBuf);
    }
    FrmSetActiveForm(f);
    FrmSetEventHandler(f, PickHandler);
    FrmDrawForm(f);   /* an inset MODAL form draws its own rounded dialog frame */
    ShowYear(f);
    ShowMonth(f);
    DrawCal(f);   /* FrmDoDialog never posts frmOpenEvent, so draw explicitly */
    do {
        EvtGetEvent(&e, evtWaitForever);
        if (e.eType == appStopEvent) { EvtAddEventToQueue(&e); gResult = 0; break; }
        if (!SysHandleEvent(&e))
            if (!MenuHandleEvent(NULL, &e, &err))
                FrmDispatchEvent(&e);
    } while (gResult < 0);

    FrmEraseForm(f);
    FrmDeleteForm(f);
    if (prev) FrmSetActiveForm(prev);   /* full-screen picker occluded caller */
    if (gResult == 1) { *io = gPick; return true; }
    return false;
}

DATELIB_SECTION Boolean DatePickSelectDay(const SelectDayType selectDayBy, Int16 *month,
                                          Int16 *day, Int16 *year, const Char *titleP)
{
    /* Db35: the custom DatePicker is used (NOT the ROM SelectDay trap): SelectDay
       clamps the year to its valid range (opening it at 2050 shows 2026), so it
       cannot select era years beyond 2031. This picker is laid out to be pixel-
       identical to the ROM SelectDay "Go to Date" dialog (system form 10100) but
       supports the full year range needed for eras. */
    /* Aware tier: callers pass/expect the REAL full year (DateType.year +
       firstYear), which is exactly what the picker works in. No conversion. */
    DlDateTime dt;
    dt.year = *year; dt.month = *month; dt.day = *day;
    dt.hour = dt.minute = dt.second = 0;
    if (!DatePicker(&dt, titleP)) return false;
    *month = dt.month; *day = dt.day; *year = dt.year;
    return true;
}
