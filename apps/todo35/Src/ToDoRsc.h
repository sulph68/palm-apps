/******************************************************************************
 *
 * Copyright (c) 1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: ToDoRsc.h
 *
 *****************************************************************************/

// List View
#define ListView							1000
#define ListCategoryTrigger			1003
#define ListCategoryList				1004
#define ListNewToDoButton				1005
#define ListDetailsButton				1006
#define ListShowButton					1007
#define ListUpButton						1008
#define ListDownButton					1009
#define ListTable							1010
#define ListPriorityList				1011
#define ListDueDateList					1012
#define ListItemsCategoryList			1013

// Details Dialog Box
#define DetailsDialog					1200
#define DetailsPriority1Trigger		1203
#define DetailsCategoryTrigger		1210
#define DetailsCategoryList			1211
#define DetailsDueDateTrigger			1214
#define DetailsDueDateList				1215
#define DetailsSecretCheckbox			1217
#define DetailsOkButton					1218
#define DetailsCancelButton			1219
#define DetailsDeleteButton			1220
#define DetailsNoteButton				1221
#define DetailsHelpString				1222
#define DetailsPrioritiesGroup		1
// Td35: timed + repeating ToDo (see lib/todoext.h)
#define DetailsTimeLabel				1228
#define DetailsRepeatLabel				1229
#define DetailsTimeSelector			1230	// SELECTOR -> ROM Set Time dialog
#define DetailsRepeatSelector			1231	// SELECTOR -> Repeat dialog (1250)

// Td35: Repeat dialog (full parity: type + every-N + weekly days + monthly + end)
#define RepeatDialog					1250
#define RepeatTypeGroup					3
#define RepeatByGroup					4
#define RepeatNoneButton				1252
#define RepeatDailyButton				1253
#define RepeatWeeklyButton				1254
#define RepeatMonthlyButton			1255
#define RepeatYearlyButton				1256
#define RepeatEveryLabel				1257
#define RepeatFreqField					1258
#define RepeatDaysLabel					1259
#define RepeatWeeksLabel				1260
#define RepeatMonthsLabel				1261
#define RepeatYearsLabel				1262
#define RepeatEndLabel					1263
#define RepeatEndTrigger				1264
#define RepeatEndList					1265
#define RepeatOnLabel					1266
#define RepeatDow1Button				1267	// 1267..1273 = Sun..Sat
#define RepeatByLabel					1274
#define RepeatByDayButton				1275
#define RepeatByDateButton				1276
#define RepeatNoneHelpLabel			1277
#define RepeatOkButton					1278
#define RepeatCancelButton				1279
#define RepeatDescField					1280	// read-only sentence describing the repeat

// Options Dialog
#define OptionsDialog					1400
#define OptionsSortByTrigger			1404
#define OptionsSortByList				1405
#define OptionsShowCompleted			1406
#define OptionsShowDueItems			1407
#define OptionsChangeDueDate			1408
#define OptionsShowDueDates			1409
#define OptionsShowPriorities			1411
#define OptionsShowCategories			1412
#define OptionsOkButton					1413
#define OptionsCancelButton			1414

// Detele Completed Dialog
#define DeleteCompletedDialog			1500
#define DeleteCompletedSaveBackup	1504
#define DeleteCompletedOk				1506
#define DeleteCompletedCancel			1507

// Delete To Do Dialog
#define DeleteToDoDialog				1600
#define DeleteToDoSaveBackup			1604
#define DeleteToDoOk						1606
#define DeleteToDoCancel				1607

// Delete Note Alert
#define DeleteNoteAlert					2001
#define DeleteNoteYes					0
#define DeleteNoteNo             	1

// Select An Item Alert
#define SelectItemAlert					2002

// Td35: encrypted items but Sec35 (decryption authority) not installed
#define NoSec35Alert					2003

// Td35: duplicating a repeating item - "this occurrence" vs. "the whole repeat setup".
// Custom FORM (not a native ALERT - alerts auto-size buttons from label text with
// no width override; this dialog needs specific button geometry).
#define DuplicateRepeatDlg				1260
#define btnDupThisOnly					1261
#define btnDupAll							1262
#define btnDupCancel						1263
#define DuplicateThisOccurrence		0
#define DuplicateAllOccurrences		1
#define DuplicateCancel					2

// Menus
#define ListViewMenu						1000
#define NoteViewMenu						1001

// Menu commands
#define DeleteCmd							100
#define CreateNoteCmd					101
#define DeleteNoteCmd					102
#define DeleteCompletedCmd				103
#define SendRecordCmd					104
#define SendCategoryCmd					105
#define DuplicateCmd						106
#define UndoCmd							200
#define CutCmd								201
#define CopyCmd							202
#define PasteCmd							203
#define SelectAllCmd						204
#define EditSeparator					205
#define KeyboardCmd						206
#define FontCmd							300
#define PhoneLookupCmd					301
#define SecurityCmd						302
#define AboutCmd							303

//Command bars
#define GeneralMenuCtl					100 //hasCCP, has extras

// Strings
#define FindToDoHeaderStr				100
#define DueDateTitleStr					101
#define SetTimeTitleStr					102	// Td35: Set Time dialog title (tSTR0066)
#define BeamDescriptionStr				1000
#define BeamFilenameStr					1001

