/* host implementations of the PalmOS + A35 functions vcard.c depends on */
#include "PalmOS.h"
#include "AddressDB35.h"
#include "VFSMgr.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

Int16 StrLen(const Char *s){ return (Int16)strlen(s); }
Char *StrCopy(Char *d,const Char *s){ strcpy(d,s); return d; }
Char *StrCat(Char *d,const Char *s){ strcat(d,s); return d; }
Char *StrChr(const Char *s,Int16 c){ return strchr((char*)s,c); }
Char *StrStr(const Char *s,const Char *sub){ return strstr((char*)s,sub); }
Int16 StrCaselessCompare(const Char *a,const Char *b){ return (Int16)strcasecmp(a,b); }
Int16 StrCompare(const Char *a,const Char *b){ return (Int16)strcmp(a,b); }
Int16 StrNCompare(const Char *a,const Char *b,Int16 n){ return (Int16)strncmp(a,b,(size_t)n); }
Err   MemMove(void *d,const void *s,Int32 n){ memmove(d,s,(size_t)n); return 0; }
Err   MemSet(void *d,Int32 n,UInt8 v){ memset(d,v,(size_t)n); return 0; }

/* PalmOS StrNCopy copies up to n chars; if src shorter it NUL-pads to n. */
Char *StrNCopy(Char *d,const Char *s,Int16 n){
	Int16 i=0; for(;i<n && s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]=0; return d;
}
/* PalmOS StrNCat: n = total dst buffer size; append until dst hits n-1. */
Char *StrNCat(Char *d,const Char *s,Int16 n){
	Int16 dl=(Int16)strlen(d),i=0;
	while(s[i] && dl<n-1) d[dl++]=s[i++];
	d[dl]=0; return d;
}

/* ---- A35 record model (matches AddressDB35.h structs) ---- */
void A35RecordInit(A35Record *r){ r->version=1; r->misc=0; r->fieldCount=0; }
Boolean A35AddField(A35Record *r,UInt8 t,UInt8 l,const void *v,UInt16 len){
	if(r->fieldCount>=a35MaxFields) return false;
	r->fields[r->fieldCount].type=t; r->fields[r->fieldCount].label=l;
	r->fields[r->fieldCount].len=len; r->fields[r->fieldCount].value=(const UInt8*)v;
	r->fieldCount++; return true;
}
Boolean A35AddStr(A35Record *r,UInt8 t,UInt8 l,const char *s){
	if(!s||!s[0]) return true;			/* real one skips empties + stores +1 (NUL) */
	return A35AddField(r,t,l,s,(UInt16)(strlen(s)+1));
}
const A35Field *A35FindField(const A35Record *r,UInt8 t,UInt16 occ){
	UInt16 i,seen=0;
	for(i=0;i<r->fieldCount;i++) if(r->fields[i].type==t){ if(seen==occ) return &r->fields[i]; seen++; }
	return NULL;
}
const char *A35GetStr(const A35Record *r,UInt8 t){
	const A35Field *f=A35FindField(r,t,0);
	return f? (const char*)f->value : "";
}
void A35PackRelation(const A35Relation *rel,UInt8 *o){
	o[0]=(UInt8)(rel->targetUniqueID>>24); o[1]=(UInt8)(rel->targetUniqueID>>16);
	o[2]=(UInt8)(rel->targetUniqueID>>8);  o[3]=(UInt8)rel->targetUniqueID; o[4]=rel->relType;
}
Boolean A35UnpackRelation(const UInt8 *p,UInt16 len,A35Relation *o){
	if(len<a35RelationPackLen) return false;
	o->targetUniqueID=((UInt32)p[0]<<24)|((UInt32)p[1]<<16)|((UInt32)p[2]<<8)|p[3];
	o->relType=p[4]; return true;
}

/* ---- tiny contact + tag tables for relation/tag round-trips ---- */
static struct { UInt32 uid; char name[64]; } gC[8];
static int gNC=0;
static char gTags[16][16]; static int gNTags=0;

void TESTaddContact(UInt32 uid,const char*name){ gC[gNC].uid=uid; strcpy(gC[gNC].name,name); gNC++; }
Int16 DmNumRecords(DmOpenRef db){ (void)db; return (Int16)gNC; }
Err DmRecordInfo(DmOpenRef db,UInt16 i,UInt16*a,UInt32*uid,LocalID*c){ (void)db;(void)a;(void)c; if(uid)*uid=gC[i].uid; return 0; }
Boolean A35FindByUniqueID(DmOpenRef db,UInt32 uid,UInt16 *idx){ int i; (void)db; for(i=0;i<gNC;i++) if(gC[i].uid==uid){*idx=(UInt16)i;return true;} return false; }
void A35RecordDisplayName(DmOpenRef db,UInt16 i,char *o,UInt16 len){ (void)db; StrNCopy(o,gC[i].name,(Int16)len); o[len-1]=0; }

UInt16 A35TagAdd(DmOpenRef db,const char *n){ int i; (void)db; for(i=0;i<gNTags;i++) if(!strcasecmp(gTags[i],n)) return (UInt16)(i+1); if(gNTags<16){ StrNCopy(gTags[gNTags],n,15); gTags[gNTags][15]=0; gNTags++; return (UInt16)gNTags; } return 0; }

/* Faithfully pack `in` to a byte blob then unpack -> `out`, exactly like the real
 * DB (fields abut, NO NUL terminator). This makes out's field values point into
 * the blob non-terminated, reproducing the device layout so the harness catches
 * "read-past-the-value" bugs (e.g. A35GetStr used as a C string). */
static UInt8 gPackBlob[16384];
void A35PackUnpackHost(const A35Record *in, A35Record *out){
	UInt8 *d=gPackBlob; int off=10,i;
	d[0]=1; d[1]=0; d[2]=(UInt8)(in->fieldCount>>8); d[3]=(UInt8)in->fieldCount;
	for(i=0;i<in->fieldCount;i++){ const A35Field*f=&in->fields[i];
		d[off]=f->type; d[off+1]=f->label; d[off+2]=(UInt8)(f->len>>8); d[off+3]=(UInt8)f->len;
		if(f->len) memcpy(d+off+4,f->value,f->len); off+=4+f->len; }
	out->version=1; out->misc=0; out->fieldCount=in->fieldCount; off=10;
	for(i=0;i<in->fieldCount;i++){ A35Field*f=&out->fields[i];
		f->type=d[off]; f->label=d[off+1]; f->len=(UInt16)((d[off+2]<<8)|d[off+3]);
		f->value=d+off+4; off+=4+f->len; }
}
Boolean A35TagName(DmOpenRef db,UInt16 id,char *o){ (void)db; if(id<1||id>gNTags) return false; strcpy(o,gTags[id-1]); return true; }

/* ---- VFS stubs: the streaming reader is compiled but never called here ---- */
Err VFSFileOpen(UInt16 v,const char *p,UInt16 m,FileRef *f){ (void)v;(void)p;(void)m;(void)f; return 1; }
Err VFSFileSize(FileRef f,UInt32 *s){ (void)f; if(s)*s=0; return 1; }
Err VFSFileClose(FileRef f){ (void)f; return 0; }
Err VFSFileRead(FileRef f,UInt32 n,void *b,UInt32 *r){ (void)f;(void)n;(void)b; if(r)*r=0; return 1; }
