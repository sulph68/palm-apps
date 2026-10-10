/* The sources say <VfsMgr.h>; the SDK file is VFSMgr.h (matters on a case-sensitive file system). */
#include <VFSMgr.h>

#ifdef BKP1_FAKE_VFS
#include "fakevfs.h"	/* test build: an embedded backup instead of a card - see test/ */
#endif
