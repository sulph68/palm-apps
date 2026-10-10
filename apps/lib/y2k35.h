/*
 * y2k35.h - shared constants for the HackMaster Y2K35 plugin and the era-aware
 * apps (Datebook35, ToDo35, ...). One definition, referenced by both the hack
 * and the apps so they agree on creator, resource ids and published Features.
 */
#ifndef Y2K35_H
#define Y2K35_H

#include "datelib.h"

#define Y2K35_CREATOR    'Y235'      /* the hack's creator id                     */
#define Y2K35_HACK_TYPE  'HACK'      /* the plugin database type                  */
#define Y2K35_TRAP_RESID 1000        /* code/TRAP #1000; X-Master publishes a
                                        Feature (creator, resid) for this patch
                                        while the hack is ENABLED (hackapi.htm)   */
#define Y2K35_FTR_OFFSET 0           /* Feature: whole-year re-base offset         */
#define Y2K35_FTR_MHI    1           /* Feature: 64-bit master clock, high word    */
#define Y2K35_FTR_MLO    2           /* Feature: 64-bit master clock, low word     */
#define Y2K35_FTR_ALIVE  999         /* Feature: liveness stamp (TimGetTicks) the
                                        trap patch writes on every call; an app
                                        triggers the trap then checks the stamp is
                                        current to prove the hack is ENABLED, with
                                        no dependence on X-Master's conventions.   */
#define Y2K35_PREF_ID    0           /* the hack's saved preference id             */

/* The hack's persisted preference (mirrors ctlpanel.c). */
typedef struct { dl_i64 master; dl_i32 offset; } Y2k35Pref;

#endif /* Y2K35_H */
