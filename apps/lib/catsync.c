/*
 * catsync.c - additive category-name sync across the era apps and the ROM apps.
 * See catsync.h. Lives in the MAIN code segment of each app (no section tag).
 */
#include <PalmOS.h>
#include "catsync.h"

typedef struct { UInt32 type; UInt32 creator; } CatDb;

/* Every database whose category block we keep in sync. The ROM MemoPad ('memo')
 * doubles as CryptoNote's memo side, so it appears once. */
static const CatDb kCatDbs[] = {
	{ 'DATA', 'Ad35' },	/* Address35DB   */
	{ 'DATA', 'Td35' },	/* ToDo35DB      */
	{ 'DATA', 'Db35' },	/* Datebook35DB  */
	{ 'Data', 'CrNt' },	/* CryptoNoteDB  */
	{ 'DATA', 'addr' },	/* ROM Address   */
	{ 'DATA', 'date' },	/* ROM Date Book */
	{ 'DATA', 'todo' },	/* ROM To Do     */
	{ 'DATA', 'memo' },	/* ROM Memo Pad  */
};
#define kNumCatDbs	(sizeof(kCatDbs) / sizeof(kCatDbs[0]))
#define kMaxUnion	24		/* cap on distinct names collected */

static Boolean CatInList(char names[][dmCategoryLength], UInt16 n, const char *nm) CS_SEC;
static Boolean CatInList(char names[][dmCategoryLength], UInt16 n, const char *nm)
{
	UInt16 i;

	for (i = 0; i < n; i++)
		if (StrCompare(names[i], nm) == 0)
			return true;
	return false;
}

void CatSync(void)
{
	/* static (not stack): CatSync runs once, and this keeps the stack small */
	static char names[kMaxUnion][dmCategoryLength];		/* the union */
	static char slot[dmRecNumCategories][dmCategoryLength];	/* one DB's 16 names */
	UInt16 nNames = 0, i, idx, j;

	/* 1. collect the union of every non-empty, non-Unfiled category name */
	for (i = 0; i < kNumCatDbs; i++) {
		DmOpenRef db = DmOpenDatabaseByTypeCreator(kCatDbs[i].type,
			kCatDbs[i].creator, dmModeReadOnly);
		if (db == NULL)
			continue;
		for (idx = 1; idx < dmRecNumCategories; idx++) {		/* skip 0 = Unfiled */
			char buf[dmCategoryLength];
			CategoryGetName(db, idx, buf);
			if (buf[0] != '\0' && nNames < kMaxUnion &&
				!CatInList(names, nNames, buf))
				StrCopy(names[nNames++], buf);
		}
		DmCloseDatabase(db);
	}
	if (nNames == 0)
		return;

	/* 2. add any missing union name to each DB, using its free slots only */
	for (i = 0; i < kNumCatDbs; i++) {
		DmOpenRef db = DmOpenDatabaseByTypeCreator(kCatDbs[i].type,
			kCatDbs[i].creator, dmModeReadWrite);
		if (db == NULL)
			continue;
		for (idx = 0; idx < dmRecNumCategories; idx++)		/* snapshot this DB */
			CategoryGetName(db, idx, slot[idx]);
		for (j = 0; j < nNames; j++) {
			Boolean have = false;
			for (idx = 0; idx < dmRecNumCategories; idx++)
				if (slot[idx][0] != '\0' &&
					StrCompare(slot[idx], names[j]) == 0) { have = true; break; }
			if (have)
				continue;
			for (idx = 1; idx < dmRecNumCategories; idx++)	/* first free slot */
				if (slot[idx][0] == '\0') {
					CategorySetName(db, idx, names[j]);
					StrCopy(slot[idx], names[j]);	/* don't reuse this slot */
					break;
				}
		}
		DmCloseDatabase(db);
	}
}
