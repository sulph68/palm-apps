/*
 * NodeDb.c - what the Palm keeps of each node: one database per node ("MeshChat-<node id>"), all of the same shape.
 * Records: 0 header, 1 the node's settings, 2 node list, 3 conversations, 4 messages, 5 text pool.
 * The app's own settings stay in the preferences (Data.c); they hold no node data.
 * Code segment sec001 (mc_seg.h): no 32-bit multiply or divide, no local tables.
 */
#include "MeshChat.h"

#define dbType		'McDb'
#define dbCreator	'McHt'
#define dbVersion	1
#define dbRecs		6
#define lastNodePref	8		/* preference id of the node shown last */

typedef struct {
	UInt16	version;
	UInt32	id;
	UInt32	cursor;
	Stamp	lastSync;
	Int8	batt;
	UInt16	nNodes, nConv, nMsg, pool;
	RelayCfg relay;
} DbHead;

UInt32 gNodeId;			/* the node whose data is in memory; 0 until it is known */

/* "MeshChat-" and the node number as 8 hex digits. */
MC_SEC static void DbName(char *out, UInt32 id)
{
	UInt16 i;
	UInt8 n;

	StrCopy(out, "MeshChat-");
	for (i = 0; i < 8; i++) {
		n = (UInt8) ((id >> (28 - 4 * i)) & 0xF);
		out[9 + i] = (char) (n < 10 ? '0' + n : 'a' + (n - 10));
	}
	out[17] = '\0';
}

/* The node number out of a database name; 0 if it is not one of ours. */
MC_SEC static UInt32 DbNameId(const char *name)
{
	UInt32 id = 0;
	UInt16 i;
	char c;

	if (StrNCompare(name, "MeshChat-", 9) != 0 || StrLen(name) != 17) return 0;
	for (i = 0; i < 8; i++) {
		c = name[9 + i];
		if (c >= '0' && c <= '9') c = (char) (c - '0');
		else if (c >= 'a' && c <= 'f') c = (char) (c - 'a' + 10);
		else return 0;
		id = (id << 4) | (UInt32) c;
	}
	return id;
}

MC_SEC static DmOpenRef DbOpen(UInt32 id, Boolean create, Boolean write)
{
	char name[32];
	LocalID lid;
	DmOpenRef db;
	MemHandle h;
	UInt16 i, at, attr;

	DbName(name, id);
	lid = DmFindDatabase(0, name);
	if (lid) return DmOpenDatabase(0, lid, write ? dmModeReadWrite : dmModeReadOnly);
	if (! create) return 0;
	if (DmCreateDatabase(0, name, dbCreator, dbType, false) != 0) return 0;
	lid = DmFindDatabase(0, name);
	db = lid ? DmOpenDatabase(0, lid, dmModeReadWrite) : 0;
	if (! db) return 0;
	for (i = 0; i < dbRecs; i++) {
		at = dmMaxRecordIndex;
		h = DmNewRecord(db, &at, 1);
		if (! h) { DmCloseDatabase(db); DmDeleteDatabase(0, lid); return 0; }
		DmReleaseRecord(db, at, true);
	}
	if (DmDatabaseInfo(0, lid, NULL, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == 0) {
		attr |= dmHdrAttrBackup;				/* HotSync keeps it */
		DmSetDatabaseInfo(0, lid, NULL, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	}
	return db;
}

MC_SEC static Boolean RecPut(DmOpenRef db, UInt16 idx, const void *src, UInt16 size)
{
	MemHandle h;
	void *p;

	if (size == 0) size = 1;					/* a record is never empty; the header says how much is used */
	if (! DmGetRecord(db, idx)) return false;
	h = DmResizeRecord(db, idx, size);
	if (! h) { DmReleaseRecord(db, idx, false); return false; }
	p = MemHandleLock(h);
	if (src) DmWrite(p, 0, src, size); else DmSet(p, 0, size, 0);
	MemHandleUnlock(h);
	DmReleaseRecord(db, idx, true);
	return true;
}

/* A record's bytes into dst when exactly want of them are used. */
MC_SEC static Boolean RecGet(DmOpenRef db, UInt16 idx, void *dst, UInt16 want)
{
	MemHandle h = DmQueryRecord(db, idx);
	void *p;

	if (! h) return false;
	if (want == 0) return true;
	if (MemHandleSize(h) < want) return false;
	p = MemHandleLock(h);
	MemMove(dst, p, want);
	MemHandleUnlock(h);
	return true;
}

/* The node's data from nothing: what a launch or a new node starts with. */
MC_SEC void NodeDataClear(void)
{
	UInt16 i;

	gNNodes = 0; gNConv = 0; gNMsg = 0; gPoolUsed = 0;
	MemSet(gConv, sizeof(gConv), 0);
	gCursor = 0; gLastSync = 0; gMyBatt = -1;
	gWantNodes = gWantChans = true;
	gCurConv = 0; gCurNode = 0;
	/* the node's own settings (Setup pages): shown from here until the node can be asked for them */
	MemSet(&gNodeCfg, sizeof(gNodeCfg), 0);
	StrCopy(gNodeCfg.chan[0].name, "Primary");
	gNodeCfg.hop = 3;
	StrCopy(gNodeCfg.pin, "123456");
	for (i = 0; i < MAX_CHAN; i++) gNodeCfg.chan[i].role = 0;
}

MC_SEC void CacheSave(void)
{
	DmOpenRef db;
	DbHead h;
	UInt32 id = gNodeId;

	if (! id) return;
	db = DbOpen(id, true, true);
	if (! db) return;
	MemSet(&h, sizeof(h), 0);
	h.version = dbVersion; h.id = id;
	h.cursor = gCursor; h.lastSync = gLastSync; h.batt = gMyBatt;
	h.nNodes = gNNodes; h.nConv = gNConv; h.nMsg = gNMsg; h.pool = gPoolUsed; h.relay = gRelay;
	RecPut(db, 0, &h, sizeof(h));
	RecPut(db, 1, &gNodeCfg, sizeof(gNodeCfg));
	RecPut(db, 2, gNodes, (UInt16) (gNNodes * sizeof(Node)));
	RecPut(db, 3, gConv, (UInt16) (gNConv * sizeof(Conv)));
	RecPut(db, 4, gMsg, (UInt16) (gNMsg * sizeof(Msg)));
	RecPut(db, 5, gPool, gPoolUsed);
	DmCloseDatabase(db);
	PrefSetAppPreferences(dbCreator, lastNodePref, 1, &id, sizeof(id), true);
}

/* The data of a node into memory. False if the Palm has none for it (memory is untouched). */
MC_SEC Boolean CacheLoadNode(UInt32 id)
{
	DmOpenRef db = DbOpen(id, false, false);
	DbHead h;
	NodeCfg cfg;
	Boolean ok = false;
	UInt16 sz;

	if (! db) return false;
	if (RecGet(db, 0, &h, sizeof(h)) && h.version == dbVersion && h.id == id &&
	    h.nNodes <= gMaxNode && h.nConv <= MAX_CONV && h.nMsg <= MAX_MSG && h.pool <= POOL_SIZE) {
		ok = RecGet(db, 2, gNodes, (UInt16) (h.nNodes * sizeof(Node))) &&
		     RecGet(db, 3, gConv, (UInt16) (h.nConv * sizeof(Conv))) &&
		     RecGet(db, 4, gMsg, (UInt16) (h.nMsg * sizeof(Msg))) &&
		     RecGet(db, 5, gPool, h.pool);
		if (ok) {
			sz = sizeof(cfg);
			if (RecGet(db, 1, &cfg, sz)) gNodeCfg = cfg;
			gNNodes = h.nNodes; gNConv = h.nConv; gNMsg = h.nMsg; gPoolUsed = h.pool;
			gCursor = h.cursor; gLastSync = h.lastSync; gMyBatt = h.batt; gRelay = h.relay;
			gWantNodes = gNNodes == 0;
			gWantChans = gNConv == 0;
			gCurConv = 0; gCurNode = 0;
			gNodeId = id;
			SortConv();
		}
	}
	DmCloseDatabase(db);
	return ok;
}

/* The node WiRSa is linked to is the one whose data is shown. */
MC_SEC void NodeAdopt(UInt32 id)
{
	if (! id || id == gNodeId) return;
	if (gNodeId) CacheSave();			/* the node shown until now keeps its data */
	if (! CacheLoadNode(id) && gNodeId) NodeDataClear();	/* no data of this node: it starts empty. Without one shown, what is in memory (an older copy) is its */
	gNodeId = id;
}

MC_SEC void CacheDelete(UInt32 id)
{
	char name[32];
	LocalID lid;

	DbName(name, id);
	lid = DmFindDatabase(0, name);
	if (lid) DmDeleteDatabase(0, lid);
}

/* The nodes this Palm holds data for: number and name of each. */
MC_SEC UInt16 CacheList(CacheEntry *out, UInt16 max)
{
	DmSearchStateType st;
	UInt16 card, n = 0;
	LocalID lid;
	char name[32];
	DmOpenRef db;
	DbHead h;
	Boolean fresh = true;

	while (n < max && DmGetNextDatabaseByTypeCreator(fresh, &st, dbType, dbCreator, false, &card, &lid) == 0) {
		fresh = false;
		if (DmDatabaseInfo(card, lid, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) != 0) continue;
		out[n].id = DbNameId(name);
		if (! out[n].id) continue;
		out[n].name[0] = '\0';
		db = DmOpenDatabase(card, lid, dmModeReadOnly);
		if (db) {
			if (RecGet(db, 0, &h, sizeof(h))) StrNCopy(out[n].name, h.relay.node, sizeof(out[n].name) - 1);
			DmCloseDatabase(db);
		}
		out[n].name[sizeof(out[n].name) - 1] = '\0';
		n++;
	}
	return n;
}

/* The node shown last; 0 if none. */
MC_SEC UInt32 CacheLastNode(void)
{
	UInt32 id = 0;
	UInt16 sz = sizeof(id);

	if (PrefGetAppPreferences(dbCreator, lastNodePref, &id, &sz, true) != 1 || sz != sizeof(id)) return 0;
	return id;
}
