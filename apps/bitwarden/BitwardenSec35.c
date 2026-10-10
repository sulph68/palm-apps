/*
 * BitwardenSec35.c - Sec35 install/passcode/bounce gate. Mirrors
 * apps/cryptonote/CryptoSec35.c's own CryptoSec35Installed/
 * CryptoSwitchToSec35/CryptoLockedBounce shape (same enccore.h constants,
 * same mechanism) - the one difference is BwSec35LockedBounce doesn't need a
 * per-record "is this one wrapped" check the way CryptoNote's does, since
 * every BitWarden record is Sec35-wrapped after the one-time format-version
 * wipe (Store.c's BwStoreInit) - there's no legacy unwrapped data to
 * distinguish.
 */
#include "BitWarden.h"
#include "enccore.h"

Boolean BwSec35Installed(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	return (Boolean) (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication,
		ENC_CREATOR, true, &card, &dbID) == errNone);
}

void BwSwitchToSec35(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, ENC_CREATOR, true,
			&card, &dbID) != errNone)
		return;
	FtrSet(ENC_CREATOR, kSecReturnFtr, appCreator);
	SysUIAppSwitch(card, dbID, kSecLaunchUnlock, NULL);
}

Boolean BwSec35LockedBounce(void)
{
	if (!EncKeyPresent()) {
		BwSwitchToSec35();
		return true;
	}
	return false;
}
