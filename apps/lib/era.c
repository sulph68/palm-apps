/* era.c - see era.h. Strict C89 (gcc 2.95.3); no data section.
 *
 * Aware tier: the app stores REAL years, but the ROM date FORMATTERS
 * (DateToDOWDMFormat / DateToAscii / DateTemplateToAscii) still cap at 2031 and
 * bake the weekday from the year. So we feed them a same-calendar WINDOW year
 * (dl_matchInWindow -> correct weekday + layout, valid range) and swap the real
 * year into the digits the user sees. No plugin offset involved. */
#include <PalmOS.h>
#include "datelib.h"
#include "era.h"

/* a valid 1904..2031 year with the SAME calendar as realFullYear (itself when it
   already fits), so the ROM formatters produce the right weekday/layout. */
DATELIB_SECTION static Int16 WinYear(Int32 realFullYear)
{
    if (realFullYear >= DL_WINDOW_LOW && realFullYear <= DL_WINDOW_HIGH)
        return (Int16)realFullYear;
    return (Int16)dl_matchInWindow(realFullYear);
}

/* Replace the LAST occurrence of decimal-string 'from' in 's' with 'to' (equal
 * or different length). */
DATELIB_SECTION static void SubstLast(Char *s, const Char *from, const Char *to)
{
    Int16 flen = StrLen(from);
    Int16 tlen = StrLen(to);
    Int16 slen = StrLen(s);
    Int16 i;
    for (i = slen - flen; i >= 0; i--) {
        if (StrNCompare(s + i, from, flen) == 0) {
            if (tlen != flen)
                MemMove(s + i + tlen, s + i + flen, slen - (i + flen) + 1);
            MemMove(s + i, to, tlen);
            return;
        }
    }
}

/* Swap the window year that the ROM wrote into 'outP' for the FULL real year
   (unambiguous). Matches the 4-digit form first, else the 2-digit form. */
DATELIB_SECTION static void SubstYear(Char *outP, Int32 realFullYear, Int16 win)
{
    Char w[8], r[8];
    StrPrintF(r, "%ld", realFullYear);
    StrPrintF(w, "%ld", (Int32)win);
    if (StrStr(outP, w) != NULL) {
        SubstLast(outP, w, r);
    } else {
        StrPrintF(w, "%02ld", (Int32)(win % 100));
        SubstLast(outP, w, r);
    }
}

DATELIB_SECTION void EraDateToDOWDMFormat(UInt8 month, UInt8 day,
                        UInt16 realFullYear, DateFormatType fmt, Char *outP)
{
    Int16 win = WinYear((Int32)realFullYear);
    DateToDOWDMFormat(month, day, (UInt16)win, fmt, outP);
    SubstYear(outP, (Int32)realFullYear, win);
}

DATELIB_SECTION void EraDateToAscii(UInt8 month, UInt8 day,
                        UInt16 realFullYear, DateFormatType fmt, Char *outP)
{
    Int16 win = WinYear((Int32)realFullYear);
    DateToAscii(month, day, (UInt16)win, fmt, outP);
    SubstYear(outP, (Int32)realFullYear, win);
}

DATELIB_SECTION void EraDateTemplateToAscii(const Char *templateP, UInt8 month,
                UInt8 day, UInt16 realFullYear, Boolean fullYear,
                Char *outP, Int16 maxLen)
{
    Int16 win = WinYear((Int32)realFullYear);
    Char tmpl[96];
    Char y[8];
    Int16 i = 0, o = 0;
    /* Rewrite the ^4 (year) token to the literal real year; weekday/layout still
       come from the window year we pass to the ROM. */
    while (templateP[i] != '\0' && o < (Int16)sizeof(tmpl) - 8) {
        if (templateP[i] == chrCircumflexAccent &&
            templateP[i + 1] == dateTemplateYearNum) {
            Char mod = templateP[i + 2];
            Int16 k = 0;
            if (fullYear) {
                if (o > 0 && tmpl[o - 1] == '\'') o--;   /* drop "'2060" apostrophe */
                StrPrintF(y, "%ld", (Int32)realFullYear);
            } else {
                StrPrintF(y, "%02ld", (Int32)(realFullYear % 100));  /* 2-digit (tight) */
            }
            while (y[k] != '\0') tmpl[o++] = y[k++];
            i += 2;
            if (mod == dateTemplateShortModifier || mod == dateTemplateLongModifier ||
                mod == dateTemplateRegularModifier || mod == dateTemplateLeadZeroModifier)
                i++;
        } else {
            tmpl[o++] = templateP[i++];
        }
    }
    tmpl[o] = '\0';
    DateTemplateToAscii(tmpl, month, day, (UInt16)win, outP, maxLen);
}
