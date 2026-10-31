/*
 * DupSlot.c - Record > Duplicate Slot... (see DupSlot.h).
 *
 * Every slot field is Sec35 AES-CTR ciphertext whose nonce is (field,
 * slot number) - the secret's is (0, slot number) - see CDDVSec35.h. So
 * the bytes can't be copied as they are: each field is decrypted with the
 * source slot's number and encrypted again with the target's (CTR: XOR with
 * one keystream, then the other). The copy is built in a heap buffer and
 * only stored once every field went through, so a Sec35 lock half way
 * leaves both slots as they were.
 *
 * Everything here is SEC001, like MainForm.c that calls it (a SEC00x
 * function calling an untagged one in the same file misbehaves on this
 * toolchain - cddv_seg.h). The dialog runs on FrmDoDialog: the popups need
 * no event handler of ours (FrmHandleEvent sets their labels).
 */
#include "CalDAVSync.h"
#include "DupSlot.h"
#include "CDDVSec35.h"
#include "ContactDb.h"

#define kDupLabelLen	(cddvAliasLen + 4)		/* "8: " + alias + NUL */

static Boolean DupRekey(UInt32 field, UInt8 from, UInt8 to, UInt8 *buf, UInt16 len) SEC001;
static void DupAlias(const CDDVPrefsType *p, UInt8 i, char *out) SEC001;
static Boolean DupCopy(CDDVPrefsType *p, UInt8 from, UInt8 to) SEC001;
static void *DupObj(FormPtr frm, UInt16 id) SEC001;

static Boolean DupRekey(UInt32 field, UInt8 from, UInt8 to, UInt8 *buf, UInt16 len)
{
	if (len == 0)
		return true;
	return (Boolean) (CDDVCryptBytes(field, (UInt32) from, buf, len) &&
		CDDVCryptBytes(field, (UInt32) to, buf, len));
}

/* slot i's alias in plain text ("" if empty) */
static void DupAlias(const CDDVPrefsType *p, UInt8 i, char *out)
{
	const CDDVSlot *s = &p->slots[i];
	out[0] = '\0';
	if (s->aliasLen == 0)
		return;
	if (s->fieldsEncrypted) {
		CDDVDecryptSlotField(i, cddvFieldAlias, (const UInt8 *) s->alias, s->aliasLen, out, cddvAliasLen);
	} else {
		StrNCopy(out, s->alias, cddvAliasLen - 1);
		out[cddvAliasLen - 1] = '\0';
	}
}

static Boolean DupCopy(CDDVPrefsType *p, UInt8 from, UInt8 to)
{
	CDDVSlot *c = (CDDVSlot *) MemPtrNew(sizeof(CDDVSlot));
	char alias[cddvAliasLen];
	Boolean ok = true;

	if (c == NULL)
		return false;
	MemMove(c, &p->slots[from], sizeof(CDDVSlot));

	if (c->fieldsEncrypted) {
		ok = (Boolean) (ok && DupRekey(cddvFieldServer, from, to, (UInt8 *) c->server, c->serverLen));
		ok = (Boolean) (ok && DupRekey(cddvFieldPath, from, to, (UInt8 *) c->path, c->pathLen));
		ok = (Boolean) (ok && DupRekey(cddvFieldUuid, from, to, (UInt8 *) c->uuid, c->uuidLen));
		ok = (Boolean) (ok && DupRekey(cddvFieldUser, from, to, (UInt8 *) c->user, c->userLen));
		ok = (Boolean) (ok && DupRekey(cddvFieldAlias, from, to, (UInt8 *) c->alias, c->aliasLen));
		ok = (Boolean) (ok && DupRekey(cddvFieldGoogleClientId, from, to,
			(UInt8 *) c->googleClientId, c->googleClientIdLen));
		ok = (Boolean) (ok && DupRekey(cddvFieldGoogleClientSecret, from, to,
			(UInt8 *) c->googleClientSecret, c->googleClientSecretLen));
	}
	ok = (Boolean) (ok && DupRekey(0, from, to, c->secret, c->secretLen));	/* secret: nonce (0, slot) */

	/* " copy" on the alias, so the two slots can be told apart */
	if (ok) {
		DupAlias(p, from, alias);
		if (StrLen(alias) + 5 < cddvAliasLen) {
			StrCat(alias, " copy");
			if (c->fieldsEncrypted) {
				ok = CDDVEncryptSlotField(to, cddvFieldAlias, (UInt8 *) c->alias, sizeof(c->alias),
					alias, &c->aliasLen);
			} else {
				StrCopy(c->alias, alias);
				c->aliasLen = (UInt16) StrLen(alias);
			}
		}
		MemSet(alias, sizeof(alias), 0);
	}

	if (ok) {
		/* settings only: no sync history */
		c->lastSyncTime = 0;
		c->lastSyncOk = false;
		c->lastSyncEventCount = 0;
		c->lastSyncErrMsg[0] = '\0';
		MemMove(&p->slots[to], c, sizeof(CDDVSlot));
	}
	MemSet(c, sizeof(CDDVSlot), 0);
	MemPtrFree(c);
	return ok;
}

static void *DupObj(FormPtr frm, UInt16 id)
{
	return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

Boolean DupSlotRun(CDDVPrefsType *p, UInt8 current, UInt8 *target)
{
	char *labels, *ptrs[cddvMaxSlots], *q;
	char a[cddvAliasLen], b[cddvAliasLen];
	FormPtr frm;
	ListPtr lf, lt;
	UInt16 btn;
	Int16 from, to;
	UInt8 i;
	Boolean done = false;

	labels = (char *) MemPtrNew(cddvMaxSlots * kDupLabelLen);
	if (labels == NULL)
		return false;
	q = labels;
	for (i = 0; i < cddvMaxSlots; i++, q += kDupLabelLen) {
		DupAlias(p, i, a);
		if (a[0])
			StrPrintF(q, "%d: %s", i + 1, a);
		else
			StrPrintF(q, "%d: (empty)", i + 1);
		ptrs[i] = q;
	}

	/* default target: the first empty slot after the current one, wrapping */
	to = -1;
	for (i = 1; i < cddvMaxSlots && to < 0; i++) {
		UInt8 k = (UInt8) (current + i);
		if (k >= cddvMaxSlots)
			k = (UInt8) (k - cddvMaxSlots);
		if (p->slots[k].aliasLen == 0)
			to = k;
	}
	if (to < 0)
		to = (current + 1 < cddvMaxSlots) ? current + 1 : 0;

	frm = FrmInitForm(frmDupSlot);
	lf = (ListPtr) DupObj(frm, lstDupFrom);
	lt = (ListPtr) DupObj(frm, lstDupTo);
	LstSetListChoices(lf, ptrs, cddvMaxSlots);
	LstSetListChoices(lt, ptrs, cddvMaxSlots);
	LstSetSelection(lf, (Int16) current);
	LstSetSelection(lt, to);
	CtlSetLabel((ControlPtr) DupObj(frm, popDupFrom), ptrs[current]);
	CtlSetLabel((ControlPtr) DupObj(frm, popDupTo), ptrs[to]);
	btn = FrmDoDialog(frm);
	from = LstGetSelection(lf);
	to = LstGetSelection(lt);
	FrmDeleteForm(frm);

	if (btn == btnDupOK && from >= 0 && to >= 0) {
		DupAlias(p, (UInt8) from, a);
		DupAlias(p, (UInt8) to, b);
		if (from == to) {
			FrmCustomAlert(altErr, "Pick two different slots.", NULL, NULL);
		} else if (a[0] == '\0') {
			FrmCustomAlert(altErr, "That slot is empty: there is nothing to copy.", NULL, NULL);
		} else if (b[0] == '\0' || FrmCustomAlert(altConfirmDupOverwrite, b, a, NULL) == 0) {
			Boolean wasContacts = (Boolean) (b[0] && cddvIsContacts(p->slots[to].serverType));
			if (DupCopy(p, (UInt8) from, (UInt8) to)) {
				if (wasContacts) ContactDbMarkStale((UInt8) to);	/* old account's contacts */
				*target = (UInt8) to;
				done = true;
			} else {
				FrmCustomAlert(altErr, "Couldn't copy the slot: unlock Sec35 and try again.", NULL, NULL);
			}
		}
	}
	MemSet(a, sizeof(a), 0);
	MemSet(b, sizeof(b), 0);
	MemSet(labels, cddvMaxSlots * kDupLabelLen, 0);
	MemPtrFree(labels);
	return done;
}
