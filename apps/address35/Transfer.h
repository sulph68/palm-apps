/*
 * Transfer.h - Address35 vCard transport (beam via Exchange Manager; SD card in
 * a later increment). Send lives in the app segment; the RECEIVE/import path is
 * SEC_RECV = MAIN, because sysAppLaunchCmdExgReceiveData is a no-globals
 * sublaunch that does not load secondary code segments (see
 * [[beam-receive-sublaunch-segment]]).
 */
#ifndef TRANSFER_H
#define TRANSFER_H

#include <PalmOS.h>
#include "a35_seg.h"

#define a35VcardMime	"text/x-vcard"
#define a35VcardExt		"vcf"

#ifdef __cplusplus
extern "C" {
#endif

/* Beam one contact (index) as a single vCard. */
void A35BeamRecord(DmOpenRef db, UInt16 index) SEC001;
/* Beam every contact in a category (dmAllCategories = everything) as a stream of
 * back-to-back vCards. Silently does nothing if the category is empty. */
void A35BeamCategory(DmOpenRef db, UInt16 category) SEC001;

/* Register for beamed vCards / .vcf files. Call from StartApplication (main). */
void A35ExgRegister(void) SEC_RECV;
/* Handle sysAppLaunchCmdExgReceiveData: accept, read, import every vCard. MAIN. */
Err  A35ReceiveData(DmOpenRef db, ExgSocketPtr sock) SEC_RECV;
/* Handle sysAppLaunchCmdExgAskUser: accept + category picker; stores the pick in appData. */
Err  A35AcceptBeamDialog(DmOpenRef dbP, ExgAskParamPtr askInfoP) SEC_RECV;

#ifdef __cplusplus
}
#endif

#endif /* TRANSFER_H */
