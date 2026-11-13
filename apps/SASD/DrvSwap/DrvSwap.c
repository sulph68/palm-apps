/*
 * DrvSwap - frees the name of the running SASD slot driver so a new SASD.prc can be installed without a hard reset.
 *
 * A slot driver is in use from the moment the Palm starts, so HotSync cannot replace it (the database cannot be deleted or overwritten). What Bkp1's restore does
 * to a database it cannot delete is done here and nothing else: the installed "SASD" is renamed to "~D-<n>", its type and creator become '~~~~' (so it is no longer a slot
 * driver) and it is marked recyclable (the Palm deletes it at the next soft reset). The running driver keeps running until that reset; the card stays mounted.
 *
 * Use: run DrvSwap once, HotSync / install the new SASD.prc, soft-reset. Run it only BEFORE installing the new one: it renames whatever database is called SASD.
 */
#include <PalmOS.h>

#define alertInfo		1000
#define dbName			"SASD"
#define slotDriverType	'libs'		/* sysFileTSlotDriver */

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	if (cmd == sysAppLaunchCmdNormalLaunch) {

		UInt16 cardNo = 0, attrs;
		UInt32 typeAndCrid = '~~~~', type = 0;
		LocalID id = DmFindDatabase(cardNo, dbName);
		char tmpName[16], num[12];
		UInt16 x = 0;
		Err e;

		if (!id) {
			FrmCustomAlert(alertInfo, "No driver named SASD is installed: nothing to do. You can install the new one now.", " ", " ");
			return 0;
		}

		e = DmDatabaseInfo(cardNo, id, NULL, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &type, NULL);
		if (e != errNone || type != slotDriverType) {
			FrmCustomAlert(alertInfo, "The database named SASD is not a slot driver. It was not changed.", " ", " ");
			return 0;
		}

		do {
			StrCopy(tmpName, "~D-");
			StrIToA(num, x++);
			StrCat(tmpName, num);
		} while (DmFindDatabase(cardNo, tmpName));

		attrs |= dmHdrAttrRecyclable;
		e = DmSetDatabaseInfo(cardNo, id, tmpName, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &typeAndCrid, &typeAndCrid);
		if (e != errNone) {
			StrIToA(num, e);
			FrmCustomAlert(alertInfo, "The running SASD driver could not be renamed (error ", num, "). Nothing was changed.");
			return 0;
		}

		FrmCustomAlert(alertInfo, "The running SASD driver was renamed. Now install the new SASD.prc (HotSync or the card), then soft-reset the Palm.", " ", " ");
	}
	return 0;
}
