#ifndef _MY_EXG_DB_H_
#define _MY_EXG_DB_H_

#include <PalmOS.h>


//code from RetroFix :)

Err MyExgDBWrite(ExgDBWriteProcPtr writeProcP, void *userDataP, const char *nameP, LocalID LID, UInt16 cardNo);
Err MyExgDBRead(ExgDBReadProcPtr readProcP, ExgDBDeleteProcPtr deleteProcP, void *userDataP, LocalID *dbIDP, UInt16 cardNo, Boolean *needResetP, Boolean keepDates);



#endif