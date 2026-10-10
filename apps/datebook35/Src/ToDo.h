/******************************************************************************
 *
 * Copyright (c) 1997-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: ToDo.h
 *
 * Description:
 *		Header for To Do.
 *
 * History:
 *   	8/28/97  roger - Created
 *		10/4/99	jmp - Added SetDBBackupBit() prototype.
 *
 *****************************************************************************/

#include <IMCUtils.h>
#include <ExgMgr.h>

#include "ToDoDB.h"

/***********************************************************************
 *
 *	Internal Constants
 *
 ***********************************************************************/
#define toDoVersionNum					3
#define toDoPrefsVersionNum			3
#define todoPrefID						0x00
#define toDoDBName					"ToDo35DB"
#define toDo35Creator					'Td35'
#define toDoDBType						'DATA'


/************************************************************
 * Function Prototypes
 *************************************************************/
#ifdef __cplusplus
extern "C" {
#endif


// From ToDoTransfer.c
typedef Err ImportVEventF(DmOpenRef dbP, void * inputStream, GetCharF inputFunc, 
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead);
 

extern void ToDoSendRecord (DmOpenRef dbP, Int16 recordNum);
extern void ToDoSendCategory (DmOpenRef dbP, UInt16 categoryNum);
extern Err ToDoReceiveData(DmOpenRef dbP, ExgSocketPtr exgSocketP);
extern Boolean ToDoImportVToDo(DmOpenRef dbP, void * inputStream, GetCharF inputFunc, 
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, UInt32 * uniqueIDP);
extern Boolean ToDoImportVCal(DmOpenRef dbP, void * inputStream, GetCharF inputFunc, 
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, ImportVEventF vEventFunc);
extern void ToDoExportVCal(DmOpenRef dbP, Int16 index, ToDoDBRecordPtr recordP, 
	void * outputStream, PutStringF outputFunc, Boolean writeUniqueIDs);
	
extern void SetDBBackupBit(DmOpenRef dbP);


#ifdef __cplusplus 
}
#endif

