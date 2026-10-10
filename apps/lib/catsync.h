/*
 * catsync.h - keep category names in sync across the era apps + ROM apps.
 *
 * Call CatSync() once at each app's launch. It unions the category names found
 * in every participating database's standard category AppInfo block and adds any
 * that are missing to each - purely ADDITIVE (uses the system CategoryGetName /
 * CategorySetName APIs, never renames or renumbers), so record<->category
 * assignments are never disturbed. Databases that aren't installed are skipped.
 */
#ifndef CATSYNC_H
#define CATSYNC_H

#include <PalmOS.h>

/* Multi-segment apps whose main segment is full build catsync with
 * -DCATSYNC_MULTISEG so it lands in "sec001" (only ROM traps are reached from
 * here, so a foreground-launch secondary segment is fine). */
#ifdef CATSYNC_MULTISEG
#define CS_SEC	__attribute__ ((section ("sec001")))
#else
#define CS_SEC
#endif

void CatSync(void) CS_SEC;

#endif /* CATSYNC_H */
