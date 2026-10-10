#include "PalmOS.h"
#include "AddressDB35.h"
#include "vcard.h"
#include <stdio.h>
#include <string.h>

void TESTaddContact(UInt32 uid,const char*name);
void A35PackUnpackHost(const A35Record *in, A35Record *out);

/* stable storage for the source record's field values */
static char addrTab[] = "12 Main St\t\tApt 4\tSpringfield\tIL\t62704\tUSA";
static char note[] = "Line1\nLine2; has a semi, and a comma";
static UInt8 relPack[5];
static UInt8 tagFriends[2], tagVIP[2];
static char annivWed[] = "19960718";
static char annivOther[] = "20000101Reunion";
static char bday[] = "19850718";

static void printCard(const char *title, const char *buf){
	const char *p; printf("==== %s ====\n", title);
	for(p=buf;*p;p++){ if(*p!='\r') putchar(*p); }
	printf("---- (%d bytes) ----\n\n",(int)strlen(buf));
}

int main(void){
	A35Record r; char buf[4096];
	A35Record r2; VCardArena arena; char buf2[4096];
	UInt16 n, n2; UInt16 tid; A35Relation rel;

	/* contacts table: Jane Public is uid 1001 (relation target) */
	TESTaddContact(1001, "Jane Public");

	/* build tag ids */
	tid = A35TagAdd((DmOpenRef)1, "Friends"); tagFriends[0]=(UInt8)(tid>>8); tagFriends[1]=(UInt8)tid;
	tid = A35TagAdd((DmOpenRef)1, "VIP");     tagVIP[0]=(UInt8)(tid>>8);     tagVIP[1]=(UInt8)tid;

	rel.targetUniqueID=1001; rel.relType=a35relSpouse; A35PackRelation(&rel, relPack);

	A35RecordInit(&r);
	A35AddStr(&r, a35fPrefix, 0, "Dr.");
	A35AddStr(&r, a35fFirst, 0, "John");
	A35AddStr(&r, a35fMiddle, 0, "Q.");
	A35AddStr(&r, a35fLast, 0, "Public");
	A35AddStr(&r, a35fCompany, 0, "Acme, Inc.");
	A35AddStr(&r, a35fTitle, 0, "Engineer");
	A35AddStr(&r, a35fNickname, 0, "Johnny");
	A35AddStr(&r, a35fPhone, a35plMobile, "555-1234");
	A35AddStr(&r, a35fPhone, a35plWork, "555-9999");
	A35AddStr(&r, a35fEmail, 0, "john@acme.com");
	A35AddField(&r, a35fAddress, a35alHome, addrTab, (UInt16)strlen(addrTab));
	A35AddField(&r, a35fBirthday, 0, bday, (UInt16)strlen(bday));
	A35AddField(&r, a35fAnniversary, 2, annivWed, (UInt16)strlen(annivWed));
	A35AddField(&r, a35fAnniversary, 4, annivOther, (UInt16)strlen(annivOther));
	A35AddStr(&r, a35fSocial, a35socInstagram, "@johnq");
	A35AddStr(&r, a35fSocial, a35socOther, "Mastodon: @jq@mas.to");
	A35AddStr(&r, a35fURL, 0, "https://acme.com");
	A35AddField(&r, a35fNote, 0, note, (UInt16)strlen(note));
	A35AddStr(&r, a35fGender, 0, "Male");
	A35AddStr(&r, a35fPronouns, 0, "he/him");
	A35AddField(&r, a35fRelation, 0, relPack, 5);
	A35AddField(&r, a35fTag, 0, tagFriends, 2);
	A35AddField(&r, a35fTag, 0, tagVIP, 2);

	/* emit from a PACKED+UNPACKED view (non-terminated values), like the device */
	{ A35Record rp; A35PackUnpackHost(&r, &rp);
	  n = A35RecordToVCard((DmOpenRef)1, &rp, buf, sizeof(buf)); }
	if(!n){ printf("EMIT FAILED (overflow)\n"); return 1; }
	printCard("EMIT #1 (from packed record)", buf);

	if(!A35VCardToRecord((DmOpenRef)1, buf, n, &r2, &arena)){
		printf("PARSE FAILED\n"); return 1;
	}
	printf("parsed %d fields (source had %d)\n", r2.fieldCount, r.fieldCount);

	n2 = A35RecordToVCard((DmOpenRef)1, &r2, buf2, sizeof(buf2));
	printCard("EMIT #2 (after round-trip)", buf2);

	if(n==n2 && memcmp(buf,buf2,n)==0) printf("ROUND-TRIP STABLE: identical vCard\n");
	else { printf("ROUND-TRIP DIFF (n=%d n2=%d)\n", n, n2); }

	/* ---- import tolerance: a hand-authored vCard 3.0 with folding, 2.1-isms,
	 * --MM-DD birthday, and an unknown property ---- */
	{
		static const char *raw =
			"BEGIN:VCARD\r\n"
			"VERSION:3.0\r\n"
			"N:Doe;Jane;;;\r\n"
			"FN:Jane Doe\r\n"
			"TEL;TYPE=HOME,VOICE:555-0000\r\n"
			"TEL;TYPE=CELL:555-1111\r\n"
			"EMAIL;TYPE=INTERNET:jane@x.com\r\n"
			"BDAY:--03-14\r\n"
			"ADR;TYPE=WORK:;;100 Long Street\\nSuite 5;Metropolis;NY;10001;USA\r\n"
			"ORG:Globex;RnD\r\n"
			"NOTE:This is a very long note that should exceed the seventy five oct\r\n"
			" et fold boundary and be unfolded into one logical value.\r\n"
			"CATEGORIES:Work,Leads\r\n"
			"X-CUSTOMPROP:keepme\r\n"
			"END:VCARD\r\n";
		A35Record ri; VCardArena ar; char eb[4096]; UInt16 en; const A35Field *f;
		if(!A35VCardToRecord((DmOpenRef)1, raw, (UInt16)strlen(raw), &ri, &ar)){
			printf("\nIMPORT(3.0) PARSE FAILED\n");
		} else {
			printf("\n==== IMPORT vCard 3.0 -> re-emit ====\n");
			printf("fields=%d  first=[%s] last=[%s] company=[%s]\n",
				ri.fieldCount, A35GetStr(&ri,a35fFirst), A35GetStr(&ri,a35fLast),
				A35GetStr(&ri,a35fCompany));
			f=A35FindField(&ri,a35fPhone,0);
			printf("phone0 label=%d (0=Mobile,1=Home) value=[%.*s]\n",
				f?f->label:255, f?f->len:0, f?(char*)f->value:"");
			f=A35FindField(&ri,a35fBirthday,0);
			printf("bday=[%.*s] (expect 00000314, year unknown)\n", f?f->len:0, f?(char*)f->value:"");
			en=A35RecordToVCard((DmOpenRef)1,&ri,eb,sizeof(eb));
			printCard("re-emit", eb);
			/* CRITICAL: a parsed record must store string fields NUL-terminated
			 * (len=StrLen+1) so that once PACKED (fields abut, no terminator)
			 * A35GetStr on the saved record is clean -- else the receiver's display
			 * shows a trailing garbage glyph. */
			{ A35Record rp; const char *fn, *ln; int bad;
			  A35PackUnpackHost(&ri, &rp);
			  fn=A35GetStr(&rp,a35fFirst); ln=A35GetStr(&rp,a35fLast);
			  bad = (strcmp(fn,"Jane")!=0) || (strcmp(ln,"Doe")!=0);
			  printf("packed-record A35GetStr: first=[%s] last=[%s] -> %s\n",
			    fn, ln, bad?"FAIL (trailing garbage!)":"CLEAN"); }
		}
	}

	/* ---- macOS Contacts compatibility: multi-token TYPE=, N Suffix, iPhone
	 * TEL fallback, and Apple's item1.X-ABLabel / X-ABRELATEDNAMES pairing ---- */
	{
		static const char *mac =
			"BEGIN:VCARD\r\n"
			"VERSION:3.0\r\n"
			"N:Doe;Jane;Marie;Dr.;Jr.\r\n"
			"FN:Dr. Jane Marie Doe Jr.\r\n"
			"item1.EMAIL;type=INTERNET;type=HOME:jane@x.com\r\n"
			"item2.EMAIL;type=INTERNET;type=iCloud:jane@icloud.com\r\n"
			"TEL;type=iPhone:555-2222\r\n"
			"TEL;type=School:555-3333\r\n"
			"item3.TEL;type=pref:555-4444\r\n"
			"item3.X-ABLabel:_$!<Emergency>!$_\r\n"
			"item4.X-ABRELATEDNAMES:Jane Public\r\n"
			"item4.X-ABLabel:_$!<Spouse>!$_\r\n"
			"END:VCARD\r\n";
		A35Record rm; VCardArena arm; const A35Field *f;
		int ok = 1;
		if (!A35VCardToRecord((DmOpenRef)1, mac, (UInt16)strlen(mac), &rm, &arm)) {
			printf("\nMACOS IMPORT: PARSE FAILED\n"); ok = 0;
		} else {
			printf("\n==== macOS Contacts import checks ====\n");
			f = A35FindField(&rm, a35fSuffix, 0);
			printf("suffix=[%.*s] (expect Jr.) %s\n", f?f->len:0, f?(char*)f->value:"",
				(f && f->len>=3 && !strncmp((char*)f->value,"Jr.",3)) ? "OK" : (ok=0,"FAIL"));

			f = A35FindField(&rm, a35fEmail, 0);
			printf("email0 label=%d (expect %d=Home) [%.*s] %s\n", f?f->label:-1, a35elHome,
				f?f->len:0, f?(char*)f->value:"",
				(f && f->label==a35elHome) ? "OK" : (ok=0,"FAIL"));
			f = A35FindField(&rm, a35fEmail, 1);
			printf("email1 label=%d (expect %d=iCloud) [%.*s] %s\n", f?f->label:-1, a35elICloud,
				f?f->len:0, f?(char*)f->value:"",
				(f && f->label==a35elICloud) ? "OK" : (ok=0,"FAIL"));

			f = A35FindField(&rm, a35fPhone, 0);
			printf("phone0 (iPhone) label=%d (expect %d=Mobile) %s\n", f?f->label:-1, a35plMobile,
				(f && f->label==a35plMobile) ? "OK" : (ok=0,"FAIL"));
			f = A35FindField(&rm, a35fPhone, 1);
			printf("phone1 (School, unrecognized) label=%d (expect %d=Other) %s\n",
				f?f->label:-1, a35plOther,
				(f && f->label==a35plOther) ? "OK" : (ok=0,"FAIL"));
			{	/* the unrecognized TEL type should survive as a "Phone Type\0School" custom field */
				UInt16 i; int found = 0;
				for (i = 0; i < rm.fieldCount; i++)
					if (rm.fields[i].type == a35fCustom &&
						!strncmp((char*)rm.fields[i].value, "Phone Type", 10)) found = 1;
				printf("phone raw-label passthrough found=%d %s\n", found, found?"OK":(ok=0,"FAIL"));
			}
			f = A35FindField(&rm, a35fPhone, 2);
			printf("phone2 (item3, X-ABLabel=Emergency, unmatched) label=%d (expect %d=Other) %s\n",
				f?f->label:-1, a35plOther,
				(f && f->label==a35plOther) ? "OK" : (ok=0,"FAIL"));

			f = A35FindField(&rm, a35fRelation, 0);
			if (f) {
				A35Relation rel;
				A35UnpackRelation(f->value, f->len, &rel);
				printf("relation (X-ABRELATEDNAMES+X-ABLabel) uid=%u relType=%d (expect %d=Spouse) %s\n",
					rel.targetUniqueID, rel.relType, a35relSpouse,
					(rel.targetUniqueID==1001 && rel.relType==a35relSpouse) ? "OK" : (ok=0,"FAIL"));
			} else { printf("relation: NOT FOUND %s\n", (ok=0,"FAIL")); }

			{	/* export round-trip: Suffix + EMAIL/ADR TYPE= must come back out */
				char eb[4096]; UInt16 en; A35Record rp;
				A35PackUnpackHost(&rm, &rp);
				en = A35RecordToVCard((DmOpenRef)1, &rp, eb, sizeof(eb));
				printCard("macOS re-emit", eb);
				ok = ok && (strstr(eb, "N:Doe;Jane;Marie;Dr.;Jr.") != NULL);
				ok = ok && (strstr(eb, "EMAIL;TYPE=home:jane@x.com") != NULL);
				ok = ok && (strstr(eb, "EMAIL;TYPE=icloud:jane@icloud.com") != NULL);
			}
		}
		printf("MACOS CONTACTS COMPAT: %s\n", ok ? "ALL OK" : "SOME FAILED");
	}

	/* span check on a 2-card blob */
	{
		char two[8192]; UInt16 s;
		memcpy(two, buf, n); memcpy(two+n, buf, n); two[n+n]=0;
		s = A35VCardSpan(two, (UInt16)(n+n));
		printf("span of first card = %d (single card = %d) %s\n",
			s, n, s==n?"OK":"MISMATCH");
	}
	return 0;
}
