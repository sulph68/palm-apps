/******************************************************************************
 *
 * Copyright (c) 1997-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateTransfer.h
 *
 * Description:
 *	  This file defines the datebook transfer functions.
 *
 * History:
 *		September 12, 1997	Created by Roger Flores
 *
 *****************************************************************************/

typedef Boolean ImportVToDoF(DmOpenRef dbP, void * inputStream, GetCharF inputFunc, 
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead);
 

/************************************************************
 * Function Prototypes
 *************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

extern void DateSendRecord (DmOpenRef dbP, UInt16 recordNum) SEC003;

extern void DateSendCategory (DmOpenRef dbP, UInt16 categoryNum) SEC003;

/* Db35: RECEIVE path is in main (SEC_RECV) -- reached during the no-globals
 * ExgReceiveData sublaunch, when secondary segments are not loaded. SEND stays
 * in sec003. Each prototype's tag must match its definition (forward-decl gotcha). */
extern Err DateReceiveData(DmOpenRef dbP, ExgSocketPtr exgSocketP) SEC_RECV;

extern void DateExportVCal(DmOpenRef dbP, Int16 index, ApptDBRecordPtr recordP,
	void * outputStream, PutStringF outputFunc, Boolean writeUniqueIDs) SEC003;

extern Boolean DateImportVCal(DmOpenRef dbP, void * inputStream, GetCharF inputFunc,
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, ImportVToDoF vToDoFunc) SEC_RECV;


#ifdef __cplusplus 
}
#endif

