	.file	"DateDay.c"
gcc2_compiled.:
__gnu_compiled_c:
.data
	.even
gDetailsApptIndex:
	.word 65535
	.even
gDayEditH:
	.long 0
	.even
gDayEditRow:
	.word -1
	.even
gNoteEditH:
	.long 0
	.even
gDayEditStyleRow:
	.word -1
	.even
gDayEditStyleIcons:
	.word 0
.section	sec001,"x"
	.even
DayEnterEditStyle:
	link.w %a6,#0
	move.l %d4,-(%sp)
	move.l %d3,-(%sp)
	move.l 8(%a6),%d4
	move.w 12(%a6),%d3
	move.w #2,-(%sp)
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1CF
#NO_APP
	addq.l #8,%sp
	tst.w %d0
	jbeq .L3
	move.w %d3,gDayEditStyleRow@END.w(%a5)
	move.w %d0,gDayEditStyleIcons@END.w(%a5)
.L3:
	move.b #6,-(%sp)
	move.w #2,-(%sp)
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	clr.w -(%sp)
	move.w #2,-(%sp)
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1D0
#NO_APP
	move.l -8(%a6),%d3
	move.l -4(%a6),%d4
	unlk %a6
	rts
	.even
DayRestoreEditStyle:
	link.w %a6,#0
	movm.l #0x1030,-(%sp)
	move.l 8(%a6),%d3
	lea gDayEditStyleRow@END.w(%a5),%a3 /*FRED7b*/
	move.w (%a3),%d0
	jblt .L4
	move.b #9,-(%sp)
	move.w #2,-(%sp)
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	lea gDayEditStyleIcons@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),-(%sp)
	move.w #2,-(%sp)
	move.w (%a3),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D0
#NO_APP
	move.w #-1,(%a3)
	clr.w (%a2)
.L4:
	movm.l -12(%a6),#0xc08
	unlk %a6
	rts
	.even
DaySetRowCopy:
	link.w %a6,#0
	movm.l #0x1820,-(%sp)
	move.w 8(%a6),%d0
	move.l 10(%a6),%d4
	cmp.w #15,%d0
	jbhi .L10
	lea gDayRowH@END.w(%a5),%a2 /*FRED7b*/
	move.w %d0,%d3
	ext.l %d3
	lsl.l #2,%d3
	move.l (%a2,%d3.l),%d0
	jbeq .L8
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
.L8:
	move.l %d4,(%a2,%d3.l)
.L10:
	move.l %d4,%a0
	movm.l -12(%a6),#0x418
	unlk %a6
	rts
	.even
DayViewFreeRowCopies:
	link.w %a6,#0
	movm.l #0x1830,-(%sp)
	clr.w %d4
	.even
.L15:
	lea gDayRowH@END.w(%a5),%a3 /*FRED7b*/
	move.w %d4,%a2
	move.l %a2,%d3
	lsl.l #2,%d3
	move.l (%a3,%d3.l),%d0
	jbeq .L16
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a3,%d3.l)
	addq.l #4,%sp
.L16:
	lea gDayRowHasLoc@END.w(%a5),%a0 /*FRED7b*/
	clr.b (%a2,%a0.l)
	lea gDayRowDescHash@END.w(%a5),%a0 /*FRED7b*/
	move.l %a2,%d0
	add.l %a2,%d0 /*JANE2*/
	clr.w (%a0,%d0.l)
	addq.w #1,%d4
	cmp.w #15,%d4
	jble .L15
	lea gDayEditH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%d0
	jbeq .L18
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
.L18:
	move.w #-1,gDayEditRow@END.w(%a5)
	movm.l -16(%a6),#0xc18
	unlk %a6
	rts
	.even
DayCopyDescPlain:
	link.w %a6,#-32
	movm.l #0x1820,-(%sp)
	moveq.l #0,%d4
	pea -30(%a6)
	pea -26(%a6)
	move.w 8(%a6),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	lea (14,%sp),%sp
	tst.w %d0
	jbeq .L20
	sub.l %a0,%a0
	bra .L23
	.even
.L20:
	move.l -10(%a6),%d0
	jbeq .L21
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	clr.l %d3
	move.w %d0,%d3
	move.l %d3,%a0
	pea 1(%a0)
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l %a0,%d4
	addq.l #8,%sp
	jbeq .L21
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	move.l %d3,-(%sp)
	move.l -10(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.b (%a2,%d3.l)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (20,%sp),%sp
.L21:
	move.l -30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l %d4,%a0
.L23:
	movm.l -44(%a6),#0x418
	unlk %a6
	rts
.section	sec002,"x"
	.even
DayCopyNotePlain:
	link.w %a6,#-32
	movm.l #0x1820,-(%sp)
	moveq.l #0,%d4
	pea -30(%a6)
	pea -26(%a6)
	move.w 8(%a6),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	lea (14,%sp),%sp
	tst.w %d0
	jbeq .L25
	sub.l %a0,%a0
	bra .L28
	.even
.L25:
	move.l -6(%a6),%d0
	jbeq .L26
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	clr.l %d3
	move.w %d0,%d3
	move.l %d3,%a0
	pea 1(%a0)
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l %a0,%d4
	addq.l #8,%sp
	jbeq .L26
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	move.l %d3,-(%sp)
	move.l -6(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.b (%a2,%d3.l)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (20,%sp),%sp
.L26:
	move.l -30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l %d4,%a0
.L28:
	movm.l -44(%a6),#0x418
	unlk %a6
	rts
.section	sec001,"x"
	.even
DayMaskCopy:
	link.w %a6,#0
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	pea 7.w
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l %a0,%d3
	addq.l #4,%sp
	jbeq .L30
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	move.b #42,-(%sp)
	pea 6.w
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	clr.b 6(%a2)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
.L30:
	move.l %d3,%a0
	move.l -8(%a6),%d3
	move.l -4(%a6),%a2
	unlk %a6
	rts
	.even
Sec35Installed:
	link.w %a6,#-40
	pea -36(%a6)
	pea -38(%a6)
	move.b #1,-(%sp)
	move.l #1399153459,-(%sp)
	move.l #1634758764,-(%sp)
	pea -32(%a6)
	move.b #1,-(%sp)
#APP
	trap #15; dc.w 0xA078
#NO_APP
	tst.w %d0
	seq %d0
	ext.w %d0
	neg.w %d0
	unlk %a6
	rts
	.even
SwitchToSec35:
	link.w %a6,#-40
	pea -36(%a6)
	pea -38(%a6)
	move.b #1,-(%sp)
	move.l #1399153459,-(%sp)
	move.l #1634758764,-(%sp)
	pea -32(%a6)
	move.b #1,-(%sp)
#APP
	trap #15; dc.w 0xA078
#NO_APP
	lea (24,%sp),%sp
	tst.w %d0
	jbne .L32
	move.l #1147286325,-(%sp)
	move.w #1001,-(%sp)
	move.l #1399153459,-(%sp)
#APP
	trap #15; dc.w 0xA27C
#NO_APP
	clr.l -(%sp)
	move.w #36864,-(%sp)
	move.l -36(%a6),-(%sp)
	move.w -38(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA0A7
#NO_APP
.L32:
	unlk %a6
	rts
	.even
DayLockedBounce:
	link.w %a6,#0
	move.l %a2,-(%sp)
	move.l __text__sec003@END.w(%a5),%a2
	jsr EncKeyPresent(%a2)
	tst.b %d0
	jbne .L35
	move.l ApptDB@END.w(%a5),-(%sp)
	jsr ApptDbHasEncrypted(%a2)
	addq.l #4,%sp
	tst.b %d0
	jbeq .L35
	bsr.w Sec35Installed
	tst.b %d0
	jbeq .L36
	bsr.w SwitchToSec35
.L36:
	moveq.l #1,%d0
	bra .L37
	.even
.L35:
	clr.w %d0
.L37:
	move.l -4(%a6),%a2
	unlk %a6
	rts
.data
	.even
gDayTodoCount:
	.word 0
	.even
gDayBdayCount:
	.word 0
	.even
gDayDavCount:
	.word 0
.section	sec001,"x"
.LC0:
	.ascii "Can't reopen DB\0"
.LC1:
	.ascii "Src/DateDay.c\0"
	.even
.globl DoSecurity
DoSecurity:
	link.w %a6,#0
	movm.l #0x1030,-(%sp)
	lea PrivateRecordVisualStatus@END.w(%a5),%a2 /*FRED7b*/
	cmp.b #2,(%a2)
	seq %d3
	neg.b %d3
#APP
	trap #15; dc.w 0xA3CF
#NO_APP
	move.b %d0,(%a2)
	and.w #0xFF,%d3
	cmp.b #2,%d0
	jbne .L40
	cmp.w #1,%d3
	jbne .L41
	bra .L39
	.even
.L40:
	tst.w %d3
	jbeq .L39
.L41:
	lea ApptDB@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA04A
#NO_APP
	addq.l #4,%sp
	moveq.l #19,%d0
	cmp.b #2,(%a2)
	jbne .L42
	moveq.l #3,%d0
.L42:
	move.w %d0,-(%sp)
	move.l #1147286325,-(%sp)
	move.l #1145132097,-(%sp)
#APP
	trap #15; dc.w 0xA075
#NO_APP
	move.l %a0,(%a3)
	lea (10,%sp),%sp
	jbne .L39
	lea .LC0(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #486,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	.even
.L39:
	movm.l -12(%a6),#0xc08
	unlk %a6
	rts
.LC2:
	.ascii "No displayable occurrences of event\0"
	.even
SetDateToNextOccurrence:
	link.w %a6,#-48
	movm.l #0x1f38,-(%sp)
	moveq.l #1,%d7
	pea -44(%a6)
	move.w #4,-(%sp)
	move.l #1147286325,-(%sp)
#APP
	trap #15; dc.w 0xA27B
#NO_APP
	lea (10,%sp),%sp
	tst.w %d0
	seq %d3
	neg.b %d3
	jbeq .L50
	move.w #4,-(%sp)
	move.l #1147286325,-(%sp)
#APP
	trap #15; dc.w 0xA27A
#NO_APP
	addq.l #6,%sp
.L50:
	pea -48(%a6)
	moveq.l #-40,%d5
	add.l %a6,%d5 /*JANE2*/
	move.l %d5,-(%sp)
	move.w 8(%a6),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d6
	move.l (%a0),%a2
	jsr ApptGetRecord(%a2)
	lea (14,%sp),%sp
	tst.l -32(%a6)
	jbne .L51
	move.l -40(%a6),%a0
	move.l 4(%a0),Date@END.w(%a5)
	bra .L52
	.even
.L51:
	tst.b %d3
	jbeq .L53
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.l -44(%a6),%d1
	move.l %d1,%d0
	clr.w %d0
	swap %d0
	move.w %d0,(%a0)
	lsr.l #8,%d1
	move.b %d1,2(%a0)
	move.b -41(%a6),3(%a0)
	bra .L52
	.even
.L61:
	lea .LC2(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #571,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	move.l %d4,(%a3)
	clr.b %d7
	lea (10,%sp),%sp
	bra .L52
	.even
.L53:
	pea -14(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	jsr ETimSecondsToDateTime(%a2)
	move.w -4(%a6),%d0
	add.w #-1904,%d0
	swap %d0
	clr.w %d0
	and.l #65535,%d4
	or.l %d0,%d4
	clr.l %d0
	move.b -5(%a6),%d0
	lsl.l #8,%d0
	and.w #255,%d4
	or.l %d0,%d4
	move.b -7(%a6),%d4
	lea Date@END.w(%a5),%a3 /*FRED7b*/
	move.l %d4,(%a3)
	move.l %a3,-(%sp)
	move.l %d5,-(%sp)
	jsr ApptNextRepeat(%a2)
	lea (16,%sp),%sp
	tst.b %d0
	jbne .L52
	move.l -32(%a6),%a0
	move.l 2(%a0),(%a3)
	bra .L56
	.even
.L58:
	move.l (%a3),-(%sp)
	move.l %a4,%a2
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l -40(%a6),%a0
	move.l 4(%a0),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbeq .L61
	pea -1.w
	move.l %a3,-(%sp)
	jsr EDateAdjust(%a4)
	addq.l #8,%sp
.L56:
	move.l %a3,-(%sp)
	move.l %d5,-(%sp)
	move.l %d6,%a0
	move.l (%a0),%a4
	jsr ApptNextRepeat(%a4)
	addq.l #8,%sp
	tst.b %d0
	jbeq .L58
.L52:
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w %d7,%d0
	and.w #1,%d0
	movm.l -80(%a6),#0x1cf8
	unlk %a6
	rts
	.even
GetTime:
	link.w %a6,#-20
	movm.l #0x1c30,-(%sp)
	move.l 8(%a6),%a2
	move.l 12(%a6),%a3
	pea -18(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ETimSecondsToDateTime,%a0 /*JANE2*/
	jsr (%a0)
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.w -8(%a6),%d0
	add.w #-1904,%d0
	addq.l #8,%sp
	cmp.w (%a0),%d0
	jbne .L63
	clr.w %d0
	move.b 2(%a0),%d0
	cmp.w -10(%a6),%d0
	jbne .L63
	clr.w %d0
	move.b 3(%a0),%d0
	cmp.w -12(%a6),%d0
	jbne .L63
	move.w -14(%a6),%d4
	bra .L64
	.even
.L63:
	move.w DayStartHour@END.w(%a5),%d4
.L64:
	cmp.w #-1,(%a2)
	jbne .L65
	moveq.l #1,%d5
	move.w %d4,%d0
	addq.w #1,%d0
	cmp.w #23,%d0
	jble .L66
	moveq.l #23,%d0
.L66:
	move.b %d0,-2(%a6)
	clr.b -1(%a6)
	move.w %d4,%d0
	addq.w #2,%d0
	cmp.w #24,%d0
	jble .L67
	moveq.l #24,%d0
.L67:
	move.b %d0,-4(%a6)
	clr.b -3(%a6)
	bra .L68
	.even
.L65:
	clr.b %d5
	move.b (%a2),-2(%a6)
	move.b 1(%a2),-1(%a6)
	move.b (%a3),-4(%a6)
	move.b 1(%a3),-3(%a6)
.L68:
	move.w 16(%a6),-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	move.w %d4,-(%sp)
	move.w DayEndHour@END.w(%a5),-(%sp)
	move.w DayStartHour@END.w(%a5),-(%sp)
	move.l %d3,-(%sp)
	move.b %d5,-(%sp)
	pea -4(%a6)
	pea -2(%a6)
#APP
	trap #15; dc.w 0xA3C6
#NO_APP
	move.b %d0,%d4
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	tst.b %d4
	jbeq .L69
	move.b -2(%a6),(%a2)
	move.b -1(%a6),1(%a2)
	move.b -4(%a6),(%a3)
	move.b -3(%a6),1(%a3)
.L69:
	clr.w %d0
	move.b %d4,%d0
	movm.l -40(%a6),#0xc38
	unlk %a6
	rts
	.even
GetFocusObjectPtr:
	link.w %a6,#0
	move.l %d4,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA178
#NO_APP
	move.w %d0,%d3
	addq.l #4,%sp
	cmp.w #65535,%d3
	jbeq .L76
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA182
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbne .L72
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	bra .L77
	.even
.L72:
	cmp.b #3,%d0
	jbeq .L73
.L76:
	sub.l %a0,%a0
	bra .L75
	.even
.L73:
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E9
#NO_APP
.L77:
.L75:
	move.l -8(%a6),%d3
	move.l -4(%a6),%d4
	unlk %a6
	rts
	.even
ShowObject:
	link.w %a6,#0
	move.l %d3,-(%sp)
	move.l 8(%a6),%d3
	move.w 12(%a6),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA185
#NO_APP
	move.l -4(%a6),%d3
	unlk %a6
	rts
	.even
HideObject:
	link.w %a6,#0
	move.l %d3,-(%sp)
	move.l 8(%a6),%d3
	move.w 12(%a6),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA184
#NO_APP
	move.l -4(%a6),%d3
	unlk %a6
	rts
	.even
DeleteRecord:
	link.w %a6,#-44
	movm.l #0x1f38,-(%sp)
	clr.w -42(%a6)
	moveq.l #3,%d6
	pea -40(%a6)
	moveq.l #-26,%d5
	add.l %a6,%d5 /*JANE2*/
	move.l %d5,-(%sp)
	move.w 8(%a6),-(%sp)
	lea ApptDB@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d7
	move.l (%a0),%a3
	jsr ApptGetRecord(%a3)
	lea (14,%sp),%sp
	tst.l -18(%a6)
	jbeq .L81
	move.l %d5,-(%sp)
	jsr ApptHasMultipleOccurences(%a3)
	addq.l #4,%sp
	tst.b %d0
	jbeq .L81
	move.w #2100,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d4
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (10,%sp),%sp
	cmp.w #2106,%d4
	jbne .L82
	clr.b %d6
	bra .L81
	.even
.L82:
	cmp.w #2103,%d4
	jbne .L84
	moveq.l #1,%d6
	bra .L81
	.even
.L84:
	cmp.w #2104,%d4
	jbne .L81
	move.l -26(%a6),%a0
	move.l 4(%a0),-(%sp)
	move.l %a3,%a2
	add.l #EDateToDays,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l Date@END.w(%a5),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbeq .L81
	moveq.l #2,%d6
	pea 10.w
	move.l -18(%a6),-(%sp)
	pea -36(%a6)
	jsr memcpy(%a3)
	lea (12,%sp),%sp
.L81:
	move.l -40(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	move.w %d6,%d0
	and.w #3,%d0
	cmp.w #2,%d0
	jbeq .L91
	jbgt .L99
	cmp.w #1,%d0
	jbeq .L90
	bra .L89
	.even
.L99:
	cmp.w #3,%d0
	jbeq .L92
	bra .L89
	.even
.L90:
	move.l Date@END.w(%a5),-(%sp)
	pea 8(%a6)
	move.l (%a4),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #ApptAddException,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,-42(%a6)
	bra .L103
	.even
.L91:
	move.l Date@END.w(%a5),-34(%a6)
	pea -1.w
	moveq.l #-36,%d3
	add.l %a6,%d3 /*JANE2*/
	pea -34(%a6)
	move.l %d7,%a0
	move.l (%a0),%a2
	jsr EDateAdjust(%a2)
	move.l %d3,-18(%a6)
	move.w #8192,-(%sp)
	move.l %d5,-(%sp)
	pea 8(%a6)
	move.l (%a4),-(%sp)
	jsr ApptChangeRecord(%a2)
	move.w %d0,-42(%a6)
	lea (22,%sp),%sp
	bra .L89
	.even
.L92:
	move.w #1600,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%a2
	move.w #1604,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%d3
	lea SaveBackup@END.w(%a5),%a3 /*FRED7b*/
	clr.w %d0
	move.b (%a3),%d0
	move.w %d0,-(%sp)
	move.w %d3,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA189
#NO_APP
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d4
	move.w %d3,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA188
#NO_APP
	move.w %d0,%d3
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (30,%sp),%sp
	cmp.w #1607,%d4
	jbne .L93
	clr.b %d6
	bra .L100
	.even
.L93:
	move.b %d3,(%a3)
	jbeq .L95
	move.w 8(%a6),-(%sp)
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA058
#NO_APP
	bra .L104
	.even
.L95:
	move.w 8(%a6),-(%sp)
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA057
#NO_APP
.L104:
	addq.l #6,%sp
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA04F
#NO_APP
	move.w %d0,-(%sp)
	move.w 8(%a6),-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA054
#NO_APP
.L103:
	lea (12,%sp),%sp
.L89:
	tst.b %d6
	jbeq .L100
	tst.w -42(%a6)
	jbeq .L100
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	clr.w %d0
	bra .L102
	.even
.L100:
	tst.b %d6
	sne %d0
	ext.w %d0
	neg.w %d0
.L102:
	movm.l -76(%a6),#0x1cf8
	unlk %a6
	rts
	.even
CreateException:
	link.w %a6,#-4
	movm.l #0x1030,-(%sp)
	move.l 8(%a6),%d3
	move.l 12(%a6),%a3
	clr.l -(%sp)
	clr.l -(%sp)
	pea -2(%a6)
	move.w (%a3),-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	move.l %a3,-(%sp)
	move.l %d3,-(%sp)
	move.l (%a2),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptNewRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d3
	lea (30,%sp),%sp
	jbne .L106
	btst #4,-1(%a6)
	jbeq .L106
	clr.l -(%sp)
	clr.l -(%sp)
	pea -2(%a6)
	move.w (%a3),-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	or.w #16,-2(%a6)
	clr.l -(%sp)
	pea -2(%a6)
	move.w (%a3),-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA051
#NO_APP
.L106:
	move.w %d3,%d0
	movm.l -16(%a6),#0xc08
	unlk %a6
	rts
.LC3:
	.ascii "\0"
	.even
CreateNote:
	link.w %a6,#-76
	movm.l #0x1f38,-(%sp)
	clr.b %d7
	lea CurrentRecord@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d0
	cmp.w #65535,%d0
	jbeq .L127
	pea -64(%a6)
	moveq.l #-26,%d6
	add.l %a6,%d6 /*JANE2*/
	move.l %d6,-(%sp)
	move.w %d0,-(%sp)
	lea ApptDB@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d5
	move.l %d5,%a1
	move.l (%a1),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -64(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (18,%sp),%sp
	tst.l -6(%a6)
	jbne .L121
	tst.l -18(%a6)
	jbeq .L111
	tst.b 8(%a6)
	jbeq .L111
	move.w #2100,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d4
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (10,%sp),%sp
	cmp.w #2106,%d4
	jbeq .L127
	cmp.w #2103,%d4
	jbeq .L126
	cmp.w #2104,%d4
	jbne .L111
	moveq.l #1,%d7
.L111:
	clr.b %d0
	jbeq .L117
.L126:
	lea Date@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-(%sp)
	move.l %a2,-(%sp)
	move.l (%a3),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #ApptAddException,%a0 /*JANE2*/
	jsr (%a0)
	lea (12,%sp),%sp
	tst.w %d0
	jbne .L119
	pea -64(%a6)
	move.l %d6,-(%sp)
	move.w (%a2),-(%sp)
	move.l (%a3),-(%sp)
	move.l %d5,%a1
	move.l (%a1),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -72(%a6),-60(%a6)
	move.l -68(%a6),-56(%a6)
	move.l -26(%a6),%a0
	move.b (%a0),-60(%a6)
	move.b 1(%a0),-59(%a6)
	move.l -60(%a6),-72(%a6)
	move.l -56(%a6),-68(%a6)
	move.b 2(%a0),-58(%a6)
	move.b 3(%a0),-57(%a6)
	move.l -60(%a6),-72(%a6)
	move.l -56(%a6),-68(%a6)
	moveq.l #-72,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l (%a4),-68(%a6)
	clr.b -(%sp)
	pea 26.w
	moveq.l #-52,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l %d4,-52(%a6)
	move.l -10(%a6),-36(%a6)
	move.l -22(%a6),-48(%a6)
	lea .LC3(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-32(%a6)
	move.b -2(%a6),-28(%a6)
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	bsr.w CreateException
	move.w %d0,%d3
	lea (28,%sp),%sp
	move.l -64(%a6),(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.w %d3
	jbne .L119
	bra .L121
	.even
.L117:
	clr.b -(%sp)
	pea 2.w
	pea -74(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	lea (10,%sp),%sp
	tst.b %d7
	jbeq .L122
	move.l %a2,-(%sp)
	bsr.w SplitRepeatingEvent
	addq.l #4,%sp
	tst.w %d0
	jbne .L119
	move.l Date@END.w(%a5),-68(%a6)
	move.l -26(%a6),%a0
	move.b (%a0),-72(%a6)
	move.b 1(%a0),-71(%a6)
	move.b 2(%a0),-70(%a6)
	move.b 3(%a0),-69(%a6)
	lea (-72,%a6),%a1
	move.l %a1,-52(%a6)
	or.b #128,-74(%a6)
.L122:
	lea .LC3(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-32(%a6)
	or.b #16,-74(%a6)
	move.w -74(%a6),-(%sp)
	pea -52(%a6)
	pea CurrentRecord@END.w(%a5)
	move.l (%a3),-(%sp)
	move.l %d5,%a1
	move.l (%a1),%a0
	add.l #ApptChangeRecord,%a0 /*JANE2*/
	jsr (%a0)
	lea (14,%sp),%sp
	tst.w %d0
	jbne .L119
.L121:
	moveq.l #1,%d0
	bra .L125
	.even
.L119:
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
.L127:
	clr.w %d0
.L125:
	movm.l -108(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DeleteNote:
	link.w %a6,#-76
	movm.l #0x1838,-(%sp)
	tst.b 8(%a6)
	jbeq .L129
	lea Date@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-(%sp)
	lea CurrentRecord@END.w(%a5),%a3 /*FRED7b*/
	move.l %a3,-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #ApptAddException,%a0 /*JANE2*/
	jsr (%a0)
	lea (12,%sp),%sp
	tst.w %d0
	jbne .L131
	pea -64(%a6)
	pea -52(%a6)
	move.w (%a3),-(%sp)
	move.l (%a2),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -72(%a6),-60(%a6)
	move.l -68(%a6),-56(%a6)
	move.l -52(%a6),%a0
	move.b (%a0),-60(%a6)
	move.b 1(%a0),-59(%a6)
	move.l -60(%a6),-72(%a6)
	move.l -56(%a6),-68(%a6)
	move.b 2(%a0),-58(%a6)
	move.b 3(%a0),-57(%a6)
	move.l -60(%a6),-72(%a6)
	move.l -56(%a6),-68(%a6)
	moveq.l #-72,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l (%a4),-68(%a6)
	clr.b -(%sp)
	pea 26.w
	moveq.l #-26,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l %d4,-26(%a6)
	move.l -36(%a6),-10(%a6)
	move.l -48(%a6),-22(%a6)
	move.b -28(%a6),-2(%a6)
	move.l %a3,-(%sp)
	move.l %d3,-(%sp)
	bsr.w CreateException
	move.w %d0,%d3
	lea (28,%sp),%sp
	move.l -64(%a6),(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.w %d3
	jbne .L131
	bra .L133
	.even
.L129:
	clr.b -(%sp)
	pea 2.w
	pea -74(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	pea -64(%a6)
	pea -52(%a6)
	lea CurrentRecord@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),-(%sp)
	lea ApptDB@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-(%sp)
	move.l __text__@END.w(%a5),%a3
	jsr ApptGetRecord(%a3)
	lea (24,%sp),%sp
	tst.b 10(%a6)
	jbeq .L134
	move.l %a2,-(%sp)
	bsr.w SplitRepeatingEvent
	addq.l #4,%sp
	tst.w %d0
	jbne .L131
	move.l Date@END.w(%a5),-68(%a6)
	move.l -52(%a6),%a0
	move.b (%a0),-72(%a6)
	move.b 1(%a0),-71(%a6)
	move.b 2(%a0),-70(%a6)
	move.b 3(%a0),-69(%a6)
	lea (-72,%a6),%a0
	move.l %a0,-26(%a6)
	or.b #128,-74(%a6)
.L134:
	clr.l -6(%a6)
	or.b #16,-74(%a6)
	move.w -74(%a6),-(%sp)
	pea -26(%a6)
	move.l %a2,-(%sp)
	move.l (%a4),-(%sp)
	jsr ApptChangeRecord(%a3)
	lea (14,%sp),%sp
	tst.w %d0
	jbne .L131
.L133:
	moveq.l #1,%d0
	bra .L137
	.even
.L131:
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	clr.w %d0
.L137:
	movm.l -96(%a6),#0x1c18
	unlk %a6
	rts
	.even
SplitRepeatingEvent:
	link.w %a6,#-56
	movm.l #0x1e38,-(%sp)
	move.l 8(%a6),%d5
	clr.w %d6
	move.l Date@END.w(%a5),-40(%a6)
	pea -1.w
	pea -40(%a6)
	move.l __text__@END.w(%a5),%a3
	jsr EDateAdjust(%a3)
	pea -44(%a6)
	moveq.l #-36,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l %d4,-(%sp)
	move.l %d5,%a0
	move.w (%a0),-(%sp)
	lea ApptDB@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-(%sp)
	jsr ApptGetRecord(%a3)
	move.l -36(%a6),%a0
	move.l 4(%a0),-48(%a6)
	pea -48(%a6)
	move.l %d4,-(%sp)
	jsr ApptNextRepeat(%a3)
	move.l -48(%a6),-(%sp)
	move.l %a3,%a2
	add.l #EDateToDays,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	lea (30,%sp),%sp
	move.l -40(%a6),(%sp)
	jsr (%a2)
	addq.l #4,%sp
	cmp.l %d3,%d0
	jbcs .L139
	clr.l -(%sp)
	pea -52(%a6)
	clr.l -(%sp)
	move.l %d5,%a0
	move.w (%a0),-(%sp)
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	moveq.l #-10,%d3
	add.l %a6,%d3 /*JANE2*/
	pea 10.w
	move.l -28(%a6),-(%sp)
	move.l %d3,-(%sp)
	jsr memcpy(%a3)
	move.l -40(%a6),-8(%a6)
	move.l %d3,-28(%a6)
	pea -54(%a6)
	move.l %d4,-(%sp)
	move.l (%a4),-(%sp)
	jsr ApptNewRecord(%a3)
	move.w %d0,%d6
	lea (38,%sp),%sp
	move.l %d5,(%sp)
	move.l -52(%a6),-(%sp)
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA07B
#NO_APP
	lea (12,%sp),%sp
.L139:
	move.l -44(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w %d6,%d0
	movm.l -84(%a6),#0x1c78
	unlk %a6
	rts
	.even
.globl MoveEvent
MoveEvent:
	link.w %a6,#-96
	movm.l #0x1f38,-(%sp)
	clr.b %d7
	clr.b -85(%a6)
	move.l 16(%a6),%d6
	pea -74(%a6)
	lea (-62,%a6),%a2
	move.l %a2,-(%sp)
	move.l 8(%a6),%a0
	move.w (%a0),-(%sp)
	lea ApptDB@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-90(%a6)
	move.l (%a0),-(%sp)
	lea __text__@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -74(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l 22(%a6),%a0
	clr.b (%a0)
	lea (18,%sp),%sp
	tst.l -54(%a6)
	jbeq .L141
	move.w #2100,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d5
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (10,%sp),%sp
	cmp.w #2106,%d5
	jbeq .L174
	cmp.w #2103,%d5
	jbeq .L171
	cmp.w #2104,%d5
	jbne .L146
	moveq.l #1,%d7
	bra .L141
	.even
.L146:
	cmp.w #2105,%d5
	jbne .L141
	move.b #1,-85(%a6)
	tst.b 20(%a6)
	jbeq .L141
	move.l -62(%a6),%a0
	move.l 4(%a0),%d6
.L141:
	clr.b %d0
	jbeq .L150
.L171:
	move.l Date@END.w(%a5),-(%sp)
	move.l 8(%a6),-(%sp)
	move.l -90(%a6),%a0
	move.l (%a0),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #ApptAddException,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d3
	lea (12,%sp),%sp
	jbne .L152
	pea -74(%a6)
	move.l %a2,-(%sp)
	move.l 8(%a6),%a0
	move.w (%a0),-(%sp)
	move.l -90(%a6),%a0
	move.l (%a0),-(%sp)
	move.l (%a4),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -82(%a6),-70(%a6)
	move.l -78(%a6),-66(%a6)
	move.b 12(%a6),-70(%a6)
	move.b 13(%a6),-69(%a6)
	move.b 14(%a6),-68(%a6)
	move.b 15(%a6),-67(%a6)
	move.l -70(%a6),-82(%a6)
	move.l -66(%a6),-78(%a6)
	moveq.l #-82,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l %d6,-78(%a6)
	clr.b -(%sp)
	pea 26.w
	moveq.l #-36,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l -46(%a6),-20(%a6)
	move.l -42(%a6),-16(%a6)
	move.l %d4,-36(%a6)
	move.l -58(%a6),-32(%a6)
	move.b -38(%a6),-12(%a6)
	move.l 8(%a6),-(%sp)
	move.l %d3,-(%sp)
	bsr.w CreateException
	move.w %d0,%d3
	lea (28,%sp),%sp
	move.l -74(%a6),(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	bra .L173
	.even
.L150:
	clr.b -(%sp)
	pea 2.w
	pea -84(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	clr.b -(%sp)
	pea 26.w
	lea (-36,%a6),%a0
	move.l %a0,-94(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	lea (20,%sp),%sp
	tst.l -54(%a6)
	jbeq .L155
	tst.b %d7
	jbeq .L156
	move.l 8(%a6),-(%sp)
	bsr.w SplitRepeatingEvent
	move.w %d0,%d3
	addq.l #4,%sp
	jbne .L152
.L156:
	moveq.l #-10,%d3
	add.l %a6,%d3 /*JANE2*/
	pea 10.w
	move.l -54(%a6),-(%sp)
	move.l %d3,-(%sp)
	move.l (%a4),%a3
	jsr memcpy(%a3)
	move.l %d3,-28(%a6)
	move.l -54(%a6),%a0
	move.l 2(%a0),-(%sp)
	jsr EDateInt(%a3)
	lea (16,%sp),%sp
	cmp.l #65535,%d0
	jbeq .L158
	tst.b %d7
	jbne .L158
	move.l %d6,-(%sp)
	move.l %a3,%a2
	add.l #EDateToDays,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l -62(%a6),%a0
	move.l 4(%a0),-(%sp)
	jsr (%a2)
	sub.l %d0,%d3
	move.l %d3,-(%sp)
	pea -8(%a6)
	jsr EDateAdjust(%a3)
	or.b #32,-84(%a6)
	lea (16,%sp),%sp
.L158:
	move.l -54(%a6),%a0
	move.b (%a0),%d0
	cmp.b #2,%d0
	jbne .L159
	move.l -62(%a6),%a0
	move.w 4(%a0),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 7(%a0),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 6(%a0),%d0
	move.w %d0,-(%sp)
	move.l (%a4),%a2
	add.l #EDateDayOfWeek,%a2 /*JANE2*/
	jsr (%a2)
	moveq.l #1,%d3
	move.w %d3,%d1
	lsl.w %d0,%d1
	not.b %d1
	and.b %d1,-3(%a6)
	move.l %d6,%d0
	swap %d0
	ext.l %d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w -(%sp)
	move.b %d6,1(%sp)
	move.l %d6,%d0
	lsr.l #8,%d0
	clr.w -(%sp)
	move.b %d0,1(%sp)
	jsr (%a2)
	lsl.w %d0,%d3
	or.b %d3,-3(%a6)
	or.b #32,-84(%a6)
	lea (12,%sp),%sp
	bra .L160
	.even
.L159:
	cmp.b #3,%d0
	jbne .L160
	move.l %d6,%d0
	swap %d0
	ext.l %d0
	move.w %d0,%d5
	add.w #1904,%d5
	move.w %d5,-(%sp)
	clr.w %d3
	move.b %d6,%d3
	move.w %d3,-(%sp)
	move.l %d6,%d0
	lsr.l #8,%d0
	clr.w %d4
	move.b %d0,%d4
	move.w %d4,-(%sp)
	move.l (%a4),%a2
	jsr EDateDayOfMonth(%a2)
	move.b %d0,-3(%a6)
	or.b #32,-84(%a6)
	and.w #0xFF,%d0
	move.w %d0,%d1
	mulu.w #9363,%d1
	clr.w %d1
	swap %d1
	sub.w %d1,%d0
	lsr.w #1,%d0
	add.w %d1,%d0
	lsr.w #2,%d0
	addq.l #6,%sp
	cmp.b #3,%d0
	jbne .L160
	addq.w #7,%d3
	move.w %d5,-(%sp)
	move.w %d4,-(%sp)
	jsr EDateDaysInMonth(%a2)
	addq.l #4,%sp
	cmp.w %d3,%d0
	jbge .L160
	move.w #1700,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d4
	move.l -54(%a6),%a0
	addq.l #2,%sp
	move.w #1704,%d0
	cmp.b #27,7(%a0)
	jbls .L163
	addq.w #1,%d0
.L163:
	move.w %d0,-(%sp)
	move.b #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d5
	move.w #1705,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%d3
	move.b #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA18A
#NO_APP
	lea (24,%sp),%sp
	cmp.w %d3,%d0
	jbne .L165
	addq.b #7,-3(%a6)
.L165:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	addq.l #4,%sp
	cmp.w #1707,%d5
	jbeq .L174
.L160:
	tst.b -85(%a6)
	jbeq .L168
	tst.b 20(%a6)
	jbne .L155
.L168:
	clr.l -24(%a6)
	or.b #8,-84(%a6)
.L155:
	move.l %d6,-78(%a6)
	move.b 12(%a6),-82(%a6)
	move.b 13(%a6),-81(%a6)
	move.b 14(%a6),-80(%a6)
	move.b 15(%a6),-79(%a6)
	lea (-82,%a6),%a0
	move.l %a0,-36(%a6)
	or.b #128,-84(%a6)
	move.w -84(%a6),-(%sp)
	move.l -94(%a6),-(%sp)
	move.l 8(%a6),-(%sp)
	move.l -90(%a6),%a0
	move.l (%a0),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptChangeRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d3
	lea (14,%sp),%sp
.L173:
	tst.w %d3
	jbne .L152
	move.l 22(%a6),%a0
	move.b #1,(%a0)
.L174:
	clr.w %d0
	bra .L170
	.even
.L152:
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	move.w %d3,%d0
.L170:
	movm.l -128(%a6),#0x1cf8
	unlk %a6
	rts
	.even
PurgeRecords:
	link.w %a6,#-48
	movm.l #0x1f38,-(%sp)
	move.w #1500,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d3
	move.w #1507,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%d4
	lea SaveBackup@END.w(%a5),%a2 /*FRED7b*/
	clr.w %d0
	move.b (%a2),%d0
	move.w %d0,-(%sp)
	move.w %d4,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA189
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d5
	move.w %d4,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA188
#NO_APP
	move.w %d0,%d6
	move.b %d6,%d7
	move.w #1506,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	lea (34,%sp),%sp
	move.l %a0,(%sp)
#APP
	trap #15; dc.w 0xA1B3
#NO_APP
	move.w %d0,%d4
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	addq.l #8,%sp
	cmp.w #1509,%d5
	jbeq .L194
	move.b %d6,(%a2)
	pea -14(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	lea __text__@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),%a2
	jsr ETimSecondsToDateTime(%a2)
	addq.l #8,%sp
	cmp.w #2,%d4
	jbhi .L177
	move.w %d4,%d1
	addq.w #1,%d1
	move.w %d1,%d0
	lsl.w #3,%d0
	sub.w %d1,%d0
	clr.l %d2
	move.w %d0,%d2
	bra .L178
	.even
.L177:
	move.w -6(%a6),%d0
	cmp.w #1,%d0
	jble .L179
	move.w -4(%a6),-(%sp)
	subq.w #1,%d0
	move.w %d0,-(%sp)
	bra .L195
	.even
.L179:
	move.w -4(%a6),%d0
	subq.w #1,%d0
	move.w %d0,-(%sp)
	move.w #12,-(%sp)
.L195:
	jsr EDateDaysInMonth(%a2)
	move.w %d0,%d2
	ext.l %d2
	addq.l #4,%sp
.L178:
	move.w -4(%a6),%d0
	add.w #-1904,%d0
	move.w %d0,%d1
	swap %d1
	mov.w -42(%a6),%d1
	move.l %d1,%d0
	clr.l %d1
	move.b -5(%a6),%d1
	lsl.l #8,%d1
	and.w #255,%d0
	or.l %d1,%d0
	move.l %d0,-44(%a6)
	move.b -7(%a6),-41(%a6)
	neg.l %d2
	move.l %d2,-(%sp)
	pea -44(%a6)
	move.l (%a4),%a0
	add.l #EDateAdjust,%a0 /*JANE2*/
	jsr (%a0)
	move.l ApptDB@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA04F
#NO_APP
	move.w %d0,%d5
	lea (12,%sp),%sp
	jbne .L181
.L194:
	clr.w %d0
	bra .L193
	.even
.L181:
	move.w %d5,%d4
	subq.w #1,%d4
	jbmi .L183
	.even
.L185:
	move.w %d4,-(%sp)
	lea ApptDB@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA05C
#NO_APP
	move.l %a0,-48(%a6)
	addq.l #6,%sp
	jbeq .L184
	pea -48(%a6)
	pea -40(%a6)
	move.w %d4,-(%sp)
	move.l (%a3),-(%sp)
	move.l (%a4),%a2
	jsr ApptGetRecord(%a2)
	lea (14,%sp),%sp
	move.l -32(%a6),%a0
	cmp.w #0,%a0
	jbeq .L187
	move.l 2(%a0),-(%sp)
	bra .L196
	.even
.L187:
	move.l -40(%a6),%a0
	move.l 4(%a0),-(%sp)
.L196:
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l -44(%a6),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	sge %d3
	neg.b %d3
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	clr.b -(%sp)
	move.w %d4,-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA05E
#NO_APP
	lea (12,%sp),%sp
	tst.b %d3
	jbeq .L184
	tst.b %d7
	jbeq .L190
	move.w %d4,-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA058
#NO_APP
	bra .L197
	.even
.L190:
	move.w %d4,-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA057
#NO_APP
.L197:
	addq.l #6,%sp
	move.w %d5,-(%sp)
	move.w %d4,-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA054
#NO_APP
	addq.l #8,%sp
.L184:
	dbra %d4,.L185
.L183:
	moveq.l #1,%d0
.L193:
	movm.l -80(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DatebookDayOfWeek:
	link.w %a6,#0
	move.w 12(%a6),-(%sp)
	move.w 10(%a6),-(%sp)
	move.w 8(%a6),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateDayOfWeek,%a0 /*JANE2*/
	jsr (%a0)
	sub.w StartDayOfWeek@END.w(%a5),%d0
	addq.w #7,%d0
	move.w %d0,%d2
	mulu.w #9363,%d2
	clr.w %d2
	swap %d2
	move.w %d0,%d1
	sub.w %d2,%d1
	lsr.w #1,%d1
	add.w %d2,%d1
	lsr.w #2,%d1
	move.w %d1,%d2
	lsl.w #3,%d2
	sub.w %d1,%d2
	sub.w %d2,%d0
	unlk %a6
	rts
.section	sec002,"x"
.LC4:
	.ascii "Msg missing token\0"
	.even
SubstituteStr:
	link.w %a6,#0
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d5
	move.l 12(%a6),%d7
	move.w 20(%a6),%a4
	move.l %d7,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA0CD
#NO_APP
	move.l %a0,%a2
	addq.l #8,%sp
	cmp.w #0,%a2
	jbeq .L209
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	move.w %d0,%a3
	move.w %a4,%d6
	sub.w %a3,%d6
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA014
#NO_APP
	move.l %a0,%d4
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	move.w %d0,%d3
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA02D
#NO_APP
	add.w %d6,%d3
	addq.w #1,%d3
	lea (16,%sp),%sp
	cmp.w %d3,%d0
	jbhi .L201
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w %d3,-(%sp)
	clr.w -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA033
#NO_APP
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d5
	move.l %d7,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA0CD
#NO_APP
	move.l %a0,%a2
	lea (24,%sp),%sp
	cmp.w #0,%a2
	jbne .L201
	lea .LC4(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #1643,-(%sp)
	move.l __text__sec001@END.w(%a5),%d0
	add.l #.LC1,%d0 /*JANE2*/
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L201:
	tst.w %d6
	jbeq .L207
	clr.l %d3
	move.w %a3,%d3
	add.l %a2,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	addq.w #1,%d0
	move.w %d0,%a0
	move.l %a0,-(%sp)
	move.l %d3,-(%sp)
	clr.l %d0
	move.w %a4,%d0
	pea (%a2,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	lea (16,%sp),%sp
.L207:
	move.w %a4,-(%sp)
	clr.w -(%sp)
	move.l 16(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA026
#NO_APP
.L209:
	move.l %d5,%a0
	movm.l -32(%a6),#0x1cf8
	unlk %a6
	rts
	.even
SelectFont:
	link.w %a6,#0
	movm.l #0x1c00,-(%sp)
	move.b 8(%a6),%d3
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA17D
#NO_APP
	move.w %d0,%d5
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA320
#NO_APP
	move.b %d0,%d4
	addq.l #6,%sp
	cmp.b %d4,%d3
	jbeq .L211
	move.w #256,-(%sp)
	move.w %d5,-(%sp)
#APP
	trap #15; dc.w 0xA19D
#NO_APP
.L211:
	move.b %d4,%d0
	movm.l -12(%a6),#0x38
	unlk %a6
	rts
.section	sec001,"x"
	.even
RepeatSetDateTrigger:
	link.w %a6,#0
	movm.l #0x1c30,-(%sp)
	move.l 8(%a6),%d3
	move.w #1416,-(%sp)
	move.l __text__@END.w(%a5),%a3
	move.l %a3,%a2
	add.l #GetObjectPtr,%a2 /*JANE2*/
	jsr (%a2)
	move.l %a0,%d4
	move.w #1415,-(%sp)
	jsr (%a2)
	move.l %a0,%d5
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,%a2
	move.l %d3,-(%sp)
	jsr EDateInt(%a3)
	lea (12,%sp),%sp
	cmp.l #65535,%d0
	jbne .L213
	clr.w -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1B4
#NO_APP
	move.l %a0,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0C5
#NO_APP
	clr.w -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1B7
#NO_APP
	lea (20,%sp),%sp
	bra .L214
	.even
.L213:
	move.l %a2,-(%sp)
	move.b ShortDateFormat@END.w(%a5),-(%sp)
	move.l %d3,%d0
	swap %d0
	ext.l %d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	move.b %d3,-(%sp)
	lsr.l #8,%d3
	move.b %d3,-(%sp)
	jsr EraDateToDOWDMFormat(%a3)
	move.w #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1B7
#NO_APP
	lea (18,%sp),%sp
.L214:
	move.l %a2,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	movm.l -20(%a6),#0xc38
	unlk %a6
	rts
	.even
RepeatGetUIValues:
	link.w %a6,#0
	movm.l #0x1838,-(%sp)
	move.l 8(%a6),%d4
	move.l 12(%a6),%a2
	move.l RepeatDetailsP@END.w(%a5),%a3
	move.b #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA18A
#NO_APP
	move.w %d0,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA181
#NO_APP
	lea (12,%sp),%sp
	cmp.w #1406,%d0
	jbne .L216
	move.b #5,(%a2)
	bra .L217
	.even
.L216:
	cmp.w #1404,%d0
	jbhi .L218
	add.b #-122,%d0
	move.b %d0,(%a2)
	bra .L217
	.even
.L218:
	move.b #2,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA18A
#NO_APP
	move.w %d0,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA181
#NO_APP
	lea (12,%sp),%sp
	cmp.w #1426,%d0
	jbne .L220
	move.b #3,(%a2)
	bra .L217
	.even
.L220:
	move.b #4,(%a2)
.L217:
	move.l RepeatEndDate@END.w(%a5),2(%a2)
	move.w #1408,-(%sp)
	lea __text__@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA139
#NO_APP
	move.l %a0,%d0
	addq.l #6,%sp
	jbeq .L222
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA0CE
#NO_APP
	addq.l #4,%sp
	bra .L223
	.even
.L222:
	clr.w %d0
.L223:
	tst.w %d0
	jbeq .L224
	move.b %d0,6(%a2)
	bra .L225
	.even
.L224:
	move.b #1,6(%a2)
.L225:
	cmp.b #2,(%a2)
	jbne .L226
	cmp.b #2,10(%a3)
	jbne .L227
	move.b 18(%a3),8(%a2)
	bra .L229
	.even
.L227:
	lea StartDayOfWeek@END.w(%a5),%a0 /*FRED7b*/
	move.b 1(%a0),8(%a2)
	bra .L229
	.even
.L226:
	clr.b 8(%a2)
.L229:
	move.b (%a2),%d0
	cmp.b #2,%d0
	jbne .L230
	clr.b 7(%a2)
	move.w #1418,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%a3
	clr.w %d3
	addq.l #6,%sp
	.even
.L234:
	move.w %d3,%d0
	sub.w RepeatStartOfWeek@END.w(%a5),%d0
	addq.w #7,%d0
	moveq.l #7,%d2
	and.l #0xFFFF,%d0
	divu.w %d2,%d0
	move.l %d0,%d1
	swap %d1
	add.w %a3,%d1
	move.w %d1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA188
#NO_APP
	addq.l #6,%sp
	tst.w %d0
	jbeq .L233
	moveq.l #1,%d0
	lsl.w %d3,%d0
	or.b %d0,7(%a2)
.L233:
	addq.w #1,%d3
	cmp.w #6,%d3
	jbls .L234
	bra .L237
	.even
.L230:
	cmp.b #3,%d0
	jbne .L238
	cmp.b #3,10(%a3)
	jbne .L239
	move.b 17(%a3),7(%a2)
	bra .L237
	.even
.L239:
	move.w 6(%a3),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 9(%a3),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 8(%a3),%d0
	move.w %d0,-(%sp)
	move.l (%a4),%a0
	add.l #EDateDayOfMonth,%a0 /*JANE2*/
	jsr (%a0)
	move.b %d0,7(%a2)
	bra .L237
	.even
.L238:
	clr.b 7(%a2)
.L237:
	movm.l -20(%a6),#0x1c18
	unlk %a6
	rts
	.even
RepeatSetUIValues:
	link.w %a6,#0
	movm.l #0x1f38,-(%sp)
	move.l 12(%a6),%a4
	clr.w %d4
	move.b (%a4),%d4
	add.w #1402,%d4
	cmp.b #3,(%a4)
	jbls .L243
	subq.w #1,%d4
.L243:
	move.w %d4,-(%sp)
	move.b #1,-(%sp)
	move.l 8(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	addq.l #8,%sp
	tst.b (%a4)
	jbeq .L244
	move.w #1408,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA153
#NO_APP
	move.l %a0,%d4
	addq.l #6,%sp
	jbeq .L245
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CE
#NO_APP
	move.w %d0,%d1
	bra .L264
	.even
.L245:
	pea 3.w
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	clr.w %d1
.L264:
	addq.l #8,%sp
	clr.w %d0
	move.b 6(%a4),%d0
	cmp.w %d1,%d0
	jbeq .L247
	clr.l %d0
	move.b 6(%a4),%d0
	move.l %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0C9
#NO_APP
	move.l %d4,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA198
#NO_APP
	lea (20,%sp),%sp
	tst.b %d0
	jbeq .L247
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA136
#NO_APP
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA135
#NO_APP
	addq.l #8,%sp
.L247:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
.L244:
	cmp.b #2,(%a4)
	jbne .L249
	lea StartDayOfWeek@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d7
	lea RepeatStartOfWeek@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,%d6
	move.w (%a1),%d0
	cmp.w (%a0),%d0
	jbeq .L250
	move.w #1418,-(%sp)
	lea __text__@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,%d5
	move.w #1418,%d4
	addq.l #6,%sp
	.even
.L254:
	move.w %d4,%d3
	addq.w #1,%d3
	move.w %d3,-(%sp)
	move.l (%a3),%a2
	add.l #GetObjectPtr,%a2 /*JANE2*/
	jsr (%a2)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,-(%sp)
	move.w %d4,-(%sp)
	jsr (%a2)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	lea (14,%sp),%sp
	move.w %d3,%d4
	cmp.w #1423,%d4
	jbls .L254
	move.l %d5,-(%sp)
	move.w #1424,-(%sp)
	move.l (%a3),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	move.l %d6,%a0
	move.l %d7,%a1
	move.w (%a1),(%a0)
	addq.l #8,%sp
.L250:
	clr.w %d3
	.even
.L259:
	clr.w %d1
	move.b 7(%a4),%d1
	asr.w %d3,%d1
	move.w %d3,%d0
	sub.w RepeatStartOfWeek@END.w(%a5),%d0
	addq.w #7,%d0
	moveq.l #7,%d4
	and.l #0xFFFF,%d0
	divu.w %d4,%d0
	move.l %d0,%d2
	swap %d2
	and.w #1,%d1
	move.w %d1,-(%sp)
	add.w #1418,%d2
	move.w %d2,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA112
#NO_APP
	addq.l #6,%sp
	addq.w #1,%d3
	cmp.w #6,%d3
	jbls .L259
.L249:
	cmp.b #4,(%a4)
	jbne .L261
	move.w #1427,-(%sp)
	bra .L265
	.even
.L261:
	move.w #1426,-(%sp)
.L265:
	move.b #2,-(%sp)
	move.l 8(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	addq.l #8,%sp
	tst.b (%a4)
	jbeq .L263
	move.l 2(%a4),-(%sp)
	bsr.w RepeatSetDateTrigger
.L263:
	movm.l -32(%a6),#0x1cf8
	unlk %a6
	rts
.section	sec002,"x"
.LC5:
	.ascii "Unknown repeat type\0"
.LC6:
	.ascii "^m\0"
.LC7:
	.ascii "^d\0"
.LC8:
	.ascii "^f\0"
.LC9:
	.ascii "^w\0"
.LC10:
	.ascii "week ordinal not substituted\0"
.LC11:
	.ascii "^x\0"
	.even
RepeatDrawDescription:
	link.w %a6,#-48
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d3
	clr.w %d7
	move.l RepeatDetailsP@END.w(%a5),%a4
	move.w #1428,-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-42(%a6)
	move.l %a0,%a1
	move.l (%a1),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-38(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA136
#NO_APP
	pea -10(%a6)
	move.l %d3,-(%sp)
	lea __text__sec001@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-46(%a6)
	move.l (%a0),%a2
	jsr RepeatGetUIValues(%a2)
	clr.w %d0
	move.b -4(%a6),%d0
	move.w %d0,%a3
	lea (14,%sp),%sp
	clr.w %d0
	move.b -10(%a6),%d0
	cmp.w #5,%d0
	jbhi .L290
	and.l #0xFF,%d0
	lea .L291(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L291:
	.word .L268-.L291
	.word .L269-.L291
	.word .L272-.L291
	.word .L284-.L291
	.word .L281-.L291
	.word .L287-.L291
	.even
.L268:
	move.w #142,%d5
	bra .L267
	.even
.L269:
	move.w #128,%d5
	bra .L346
	.even
.L272:
	move.w #129,%d5
	moveq.l #1,%d0
	cmp.w %a3,%d0
	jbne .L273
	add.w #15,%d5
.L273:
	clr.w %d4
	.even
.L278:
	clr.w %d0
	move.b -3(%a6),%d0
	asr.w %d4,%d0
	btst #0,%d0
	jbeq .L277
	addq.w #1,%d7
.L277:
	addq.w #1,%d4
	cmp.w #6,%d4
	jbls .L278
	subq.w #1,%d5
	add.w %d7,%d5
	bra .L267
	.even
.L281:
	move.w #137,%d5
	bra .L346
	.even
.L284:
	move.w #136,%d5
	moveq.l #1,%d0
	cmp.w %a3,%d0
	jbne .L267
	add.w #15,%d5
	bra .L267
	.even
.L287:
	move.w #138,%d5
.L346:
	moveq.l #1,%d1
	cmp.w %a3,%d1
	jbne .L267
	add.w #15,%d5
	bra .L267
	.even
.L290:
	lea .LC5(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #2098,-(%sp)
	add.l #.LC1,%a2 /*JANE2*/
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
.L267:
	move.w %d5,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA016
#NO_APP
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l %a0,-34(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	move.l %d3,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0C5
#NO_APP
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (34,%sp),%sp
	move.w #154,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	lea (10,%sp),%sp
	moveq.l #1,%d4
	cmp.b #1,8(%a4)
	jbls .L293
	.even
.L295:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d3
	addq.l #1,%d3
	addq.l #6,%sp
	addq.w #1,%d4
	clr.w %d0
	move.b 8(%a4),%d0
	cmp.w %d4,%d0
	jbhi .L295
.L293:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	sub.w %d3,%a0
	move.w %a0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC6(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %a0,%a2
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (24,%sp),%sp
	cmp.w #1,%d7
	jbeq .L298
	cmp.b #3,-10(%a6)
	jbne .L297
.L298:
	move.w #155,%d5
	bra .L299
	.even
.L297:
	move.w #156,%d5
.L299:
	move.w %d5,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	lea (10,%sp),%sp
	cmp.b #2,-10(%a6)
	jbne .L300
	clr.w %d5
	move.b -2(%a6),%d5
	move.b -3(%a6),%d7
	move.l %d3,-30(%a6)
	bra .L301
	.even
.L303:
	clr.w %d4
	bra .L347
	.even
.L308:
	move.w %d5,%d0
	addq.w #8,%d0
	moveq.l #7,%d1
	and.l #0xFFFF,%d0
	divu.w %d1,%d0
	move.l %d0,%d5
	swap %d5
	addq.w #1,%d4
	cmp.w #6,%d4
	jbhi .L305
.L347:
	clr.w %d0
	move.b %d7,%d0
	asr.w %d5,%d0
	btst #0,%d0
	jbeq .L308
	moveq.l #-2,%d0
	rol.w %d5,%d0
	and.b %d0,%d7
.L305:
	move.l -30(%a6),%d3
	clr.w %d4
	cmp.w %d4,%d5
	jbls .L311
	.even
.L313:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d3
	addq.l #1,%d3
	addq.l #6,%sp
	addq.w #1,%d4
	cmp.w %d4,%d5
	jbhi .L313
.L311:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	sub.w %d3,%a0
	move.w %a0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC7(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %a0,%a2
	lea (20,%sp),%sp
.L301:
	lea .LC7(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0CD
#NO_APP
	addq.l #8,%sp
	cmp.w #0,%a0
	jbne .L303
	bra .L316
	.even
.L300:
	move.w 6(%a4),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 9(%a4),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 8(%a4),%d0
	move.w %d0,-(%sp)
	move.l -42(%a6),%a1
	move.l (%a1),%a0
	add.l #EDateDayOfWeek,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d5
	clr.w %d4
	addq.l #6,%sp
	cmp.w %d4,%d5
	jbls .L318
	.even
.L320:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d3
	addq.l #1,%d3
	addq.l #6,%sp
	addq.w #1,%d4
	cmp.w %d4,%d5
	jbhi .L320
.L318:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	sub.w %d3,%a0
	move.w %a0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC7(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %a0,%a2
	lea (20,%sp),%sp
.L316:
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	moveq.l #2,%d0
	cmp.w %a3,%d0
	jbne .L322
	clr.w %d0
	move.b -10(%a6),%d0
	move.w #16,-(%sp)
	moveq.l #-26,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
	move.w %d0,-(%sp)
	move.w #100,-(%sp)
#APP
	trap #15; dc.w 0xA2F5
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC8(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %a0,%a2
	lea (28,%sp),%sp
	bra .L323
	.even
.L322:
	move.w #139,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	moveq.l #1,%d4
	lea (10,%sp),%sp
	cmp.w %a3,%d4
	jbcc .L325
	.even
.L327:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d3
	addq.l #1,%d3
	addq.l #6,%sp
	addq.w #1,%d4
	cmp.w %a3,%d4
	jbcs .L327
.L325:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	sub.w %d3,%a0
	move.w %a0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC8(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %a0,%a2
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (24,%sp),%sp
.L323:
	cmp.b #3,-10(%a6)
	jbne .L329
	move.w #140,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	lea (10,%sp),%sp
	clr.w %d0
	move.b -3(%a6),%d0
	move.w %d0,%d1
	mulu.w #9363,%d1
	clr.w %d1
	swap %d1
	sub.w %d1,%d0
	lsr.w #1,%d0
	add.w %d1,%d0
	lsr.w #2,%d0
	clr.w %d4
	tst.b %d0
	jbeq .L331
	.even
.L333:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d3
	addq.l #1,%d3
	addq.l #6,%sp
	addq.w #1,%d4
	clr.w %d0
	move.b -3(%a6),%d0
	move.w %d0,%d1
	mulu.w #9363,%d1
	clr.w %d1
	swap %d1
	sub.w %d1,%d0
	lsr.w #1,%d0
	add.w %d1,%d0
	lsr.w #2,%d0
	and.w #0xFF,%d0
	cmp.w %d4,%d0
	jbhi .L333
.L331:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	sub.w %d3,%a0
	move.w %a0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC9(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %a0,%a2
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (24,%sp),%sp
	bra .L335
	.even
.L329:
	lea .LC9(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0CD
#NO_APP
	addq.l #8,%sp
	cmp.w #0,%a0
	jbeq .L335
	lea .LC10(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #2204,-(%sp)
	move.l -46(%a6),%a1
	move.l (%a1),%d0
	add.l #.LC1,%d0 /*JANE2*/
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L335:
	move.w #141,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%d3
	lea (10,%sp),%sp
	moveq.l #1,%d4
	cmp.b #1,9(%a4)
	jbls .L342
	.even
.L344:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d3
	addq.l #1,%d3
	addq.l #6,%sp
	addq.w #1,%d4
	clr.w %d0
	move.b 9(%a4),%d0
	cmp.w %d4,%d0
	jbhi .L344
.L342:
	move.w #32,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	sub.w %d3,%a0
	move.w %a0,-(%sp)
	move.l %d3,-(%sp)
	lea .LC11(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.l %a2,-(%sp)
	bsr.w SubstituteStr
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l -34(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l -38(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA137
#NO_APP
	lea (28,%sp),%sp
	move.l -34(%a6),(%sp)
	move.l -38(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	move.l -38(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA135
#NO_APP
	movm.l -80(%a6),#0x1cf8
	unlk %a6
	rts
.section	sec001,"x"
	.even
RepeatSelectEndDate:
	link.w %a6,#-8
	movm.l #0x1c38,-(%sp)
	move.l 8(%a6),%a0
	move.w 20(%a0),%d0
	jbne .L349
	pea RepeatEndDate@END.w(%a5)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateSetForever,%a0 /*JANE2*/
	jsr (%a0)
	bra .L356
	.even
.L349:
	cmp.w #1,%d0
	jbne .L350
	move.l RepeatDetailsP@END.w(%a5),%d4
	lea RepeatEndDate@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
	lea __text__@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%a0
	add.l #EDateInt,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #4,%sp
	cmp.l #65535,%d0
	jbne .L352
	move.l %d4,%a0
	move.w 6(%a0),%d0
	add.w #1904,%d0
	move.w %d0,-6(%a6)
	clr.w -2(%a6)
	move.b 8(%a0),-1(%a6)
	clr.w -4(%a6)
	move.b 9(%a0),-3(%a6)
	bra .L353
	.even
.L352:
	move.w (%a3),%a0
	lea (1904,%a0),%a0 /*FRED4a*/
	move.w %a0,-6(%a6)
	clr.w -2(%a6)
	move.b 2(%a3),-1(%a6)
	clr.w -4(%a6)
	move.b 3(%a3),-3(%a6)
.L353:
	move.w #104,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d5
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,-(%sp)
	pea -6(%a6)
	pea -4(%a6)
	pea -2(%a6)
	clr.b -(%sp)
	move.l (%a2),%a4
	jsr DatePickSelectDay(%a4)
	lea (28,%sp),%sp
	tst.b %d0
	jbeq .L354
	move.b -3(%a6),3(%a3)
	move.b -1(%a6),2(%a3)
	move.w -6(%a6),%d0
	add.w #-1904,%d0
	move.w %d0,(%a3)
	move.l (%a3),-(%sp)
	move.l %a4,%a2
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l %d4,%a0
	move.l 6(%a0),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jble .L354
	move.b #3,-(%sp)
#APP
	trap #15; dc.w 0xA234
#NO_APP
	move.l %a3,-(%sp)
	jsr EDateSetForever(%a4)
	addq.l #6,%sp
.L354:
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
.L356:
	addq.l #4,%sp
.L350:
	move.l RepeatEndDate@END.w(%a5),-(%sp)
	bsr.w RepeatSetDateTrigger
	movm.l -32(%a6),#0x1c38
	unlk %a6
	rts
	.even
RepeatChangeRepeatOn:
	link.w %a6,#0
	movm.l #0x1c30,-(%sp)
	move.l RepeatDetailsP@END.w(%a5),%a3
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d4
	move.w #1418,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%a2
	clr.b %d5
	clr.w %d3
	addq.l #6,%sp
	bra .L358
	.even
.L360:
	addq.w #1,%d3
.L358:
	cmp.w #6,%d3
	jbhi .L359
	move.w %a2,%d0
	add.w %d3,%d0
	move.w %d0,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA188
#NO_APP
	addq.l #6,%sp
	tst.w %d0
	jbeq .L360
	moveq.l #1,%d5
.L359:
	tst.b %d5
	jbne .L364
	move.w 6(%a3),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 9(%a3),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 8(%a3),%d0
	move.w %d0,-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr EDateDayOfWeek(%a2)
	clr.w %d1
	move.b 18(%a3),%d1
	sub.w %d1,%d0
	addq.w #7,%d0
	move.w %d0,%d1
	mulu.w #9363,%d1
	clr.w %d1
	swap %d1
	move.w %d0,%d2
	sub.w %d1,%d2
	lsr.w #1,%d2
	add.w %d1,%d2
	lsr.w #2,%d2
	move.w %d2,%d1
	lsl.w #3,%d1
	sub.w %d2,%d1
	sub.w %d1,%d0
	move.w #1,-(%sp)
	add.w #1418,%d0
	move.w %d0,-(%sp)
	jsr GetObjectPtr(%a2)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA112
#NO_APP
	lea (12,%sp),%sp
.L364:
	move.l %d4,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #RepeatDrawDescription,%a0 /*JANE2*/
	jsr (%a0)
	movm.l -20(%a6),#0xc38
	unlk %a6
	rts
	.even
RepeatChangeType:
	link.w %a6,#-12
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%a0
	move.b 9(%a0),%d0
	move.b %d0,%d5
	add.b #-122,%d5
	cmp.b #2,%d5
	jbls .L366
	addq.b #1,%d5
.L366:
	lea RepeatingEventType@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d7
	move.b (%a0),%d6
	cmp.b %d6,%d5
	jbeq .L365
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d4
	move.l RepeatDetailsP@END.w(%a5),%a3
	cmp.b 10(%a3),%d5
	jbne .L368
	pea 10(%a3)
	move.l %d4,-(%sp)
	bsr.w RepeatSetUIValues
	move.l 12(%a3),RepeatEndDate@END.w(%a5)
	bra .L410
	.even
.L368:
	move.b %d5,-10(%a6)
	pea -8(%a6)
	move.l __text__@END.w(%a5),%a4
	move.l %a4,%a2
	add.l #EDateSetForever,%a2 /*JANE2*/
	jsr (%a2)
	pea RepeatEndDate@END.w(%a5)
	jsr (%a2)
	move.b #1,-4(%a6)
	lea StartDayOfWeek@END.w(%a5),%a0 /*FRED7b*/
	move.b 1(%a0),-2(%a6)
	addq.l #8,%sp
	cmp.b #2,%d5
	jbne .L370
	move.w 6(%a3),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 9(%a3),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 8(%a3),%d0
	move.w %d0,-(%sp)
	jsr EDateDayOfWeek(%a4)
	moveq.l #1,%d1
	lsl.w %d0,%d1
	move.b %d1,-3(%a6)
	addq.l #6,%sp
	bra .L371
	.even
.L370:
	cmp.b #3,%d5
	jbne .L372
	move.w 6(%a3),%a0
	lea (1904,%a0),%a0 /*FRED4a*/
	move.w %a0,-(%sp)
	clr.w %d0
	move.b 9(%a3),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 8(%a3),%d0
	move.w %d0,-(%sp)
	jsr EDateDayOfMonth(%a4)
	move.b %d0,-3(%a6)
	addq.l #6,%sp
	bra .L371
	.even
.L372:
	clr.b -3(%a6)
.L371:
	pea -10(%a6)
	move.l %d4,-(%sp)
	bsr.w RepeatSetUIValues
.L410:
	addq.l #8,%sp
	clr.w %d0
	move.b %d6,%d0
	cmp.w #5,%d0
	jbhi .L374
	and.l #0xFF,%d0
	lea .L391(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L391:
	.word .L375-.L391
	.word .L376-.L391
	.word .L377-.L391
	.word .L384-.L391
	.word .L384-.L391
	.word .L390-.L391
	.even
.L375:
	move.w #1429,-(%sp)
	bra .L411
	.even
.L376:
	move.w #1409,-(%sp)
	bra .L411
	.even
.L377:
	move.w #1410,-(%sp)
	move.l %d4,-(%sp)
	bsr.w HideObject
	move.w #1417,%d3
	addq.l #6,%sp
	.even
.L381:
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
	bsr.w HideObject
	addq.l #6,%sp
	addq.w #1,%d3
	cmp.w #1424,%d3
	jbls .L381
	bra .L374
	.even
.L384:
	move.w #1411,-(%sp)
	move.l %d4,-(%sp)
	bsr.w HideObject
	move.w #1425,%d3
	addq.l #6,%sp
	.even
.L388:
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
	bsr.w HideObject
	addq.l #6,%sp
	addq.w #1,%d3
	cmp.w #1427,%d3
	jbls .L388
	bra .L374
	.even
.L390:
	move.w #1412,-(%sp)
.L411:
	move.l %d4,-(%sp)
	bsr.w HideObject
	addq.l #6,%sp
.L374:
	tst.b %d6
	jbne .L393
	move.w #1407,-(%sp)
	move.l %d4,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	bra .L412
	.even
.L393:
	tst.b %d5
	jbne .L394
	move.w #1407,-(%sp)
	move.l %d4,-(%sp)
	lea HideObject(%pc),%a2 /*SPECIAL_FRED*/
.L412:
	jsr (%a2)
	move.w #1408,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	move.w #1413,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	move.w #1415,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	lea (24,%sp),%sp
.L394:
	clr.w %d0
	move.b %d5,%d0
	cmp.w #5,%d0
	jbhi .L396
	and.l #0xFF,%d0
	lea .L408(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L408:
	.word .L397-.L408
	.word .L398-.L408
	.word .L399-.L408
	.word .L406-.L408
	.word .L406-.L408
	.word .L407-.L408
	.even
.L397:
	move.w #1429,-(%sp)
	bra .L413
	.even
.L398:
	move.w #1409,-(%sp)
	bra .L413
	.even
.L399:
	move.w #1410,-(%sp)
	move.l %d4,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w #1417,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	move.w #1417,%d3
	lea (12,%sp),%sp
	.even
.L403:
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
	bsr.w ShowObject
	addq.l #6,%sp
	addq.w #1,%d3
	cmp.w #1424,%d3
	jbls .L403
	bra .L396
	.even
.L406:
	move.w #1411,-(%sp)
	move.l %d4,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w #1425,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	move.w #1426,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	move.w #1427,-(%sp)
	move.l %d4,-(%sp)
	jsr (%a2)
	lea (24,%sp),%sp
	bra .L396
	.even
.L407:
	move.w #1412,-(%sp)
.L413:
	move.l %d4,-(%sp)
	bsr.w ShowObject
	addq.l #6,%sp
.L396:
	move.l %d7,%a0
	move.b %d5,(%a0)
	move.l %d4,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #RepeatDrawDescription,%a0 /*JANE2*/
	jsr (%a0)
.L365:
	movm.l -44(%a6),#0x1cf8
	unlk %a6
	rts
	.even
RepeatApply:
	link.w %a6,#-12
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	move.l RepeatDetailsP@END.w(%a5),%a2
	moveq.l #-10,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
	bsr.w RepeatGetUIValues
	pea 10.w
	move.l %d3,-(%sp)
	pea 10(%a2)
	move.l __text__@END.w(%a5),%a0
	add.l #memcpy,%a0 /*JANE2*/
	jsr (%a0)
	move.l -20(%a6),%d3
	move.l -16(%a6),%a2
	unlk %a6
	rts
	.even
RepeatDescRectHandler:
	link.w %a6,#-8
	move.l %d3,-(%sp)
	move.w 12(%a6),%d3
	cmp.w #1,%d3
	jbls .L418
	cmp.w #2,%d3
	jbeq .L421
	bra .L416
	.even
.L418:
	move.w #1428,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	pea -8(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA138
#NO_APP
	move.w #-4,-(%sp)
	pea -8(%a6)
#APP
	trap #15; dc.w 0xA1C6
#NO_APP
	lea (16,%sp),%sp
	tst.w %d3
	jbne .L419
	pea -8(%a6)
	move.w #1,-(%sp)
#APP
	trap #15; dc.w 0xA21B
#NO_APP
	bra .L416
	.even
.L419:
	clr.w -(%sp)
	pea -8(%a6)
#APP
	trap #15; dc.w 0xA219
#NO_APP
	bra .L416
	.even
.L421:
	clr.w %d0
	bra .L424
	.even
.L416:
	moveq.l #1,%d0
.L424:
	move.l -12(%a6),%d3
	unlk %a6
	rts
	.even
RepeatInit:
	link.w %a6,#0
	movm.l #0x1830,-(%sp)
	move.l 8(%a6),%d3
	move.l RepeatDetailsP@END.w(%a5),%a3
	clr.w %d0
	move.b 10(%a3),%d0
	cmp.w #5,%d0
	jbhi .L426
	and.l #0xFF,%d0
	lea .L438(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L438:
	.word .L427-.L438
	.word .L428-.L438
	.word .L429-.L438
	.word .L436-.L438
	.word .L436-.L438
	.word .L437-.L438
	.even
.L427:
	move.w #1429,-(%sp)
	move.l %d3,-(%sp)
	bsr.w ShowObject
	addq.l #6,%sp
	bra .L426
	.even
.L428:
	move.w #1407,-(%sp)
	move.l %d3,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w #1408,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1409,-(%sp)
	bra .L440
	.even
.L429:
	move.w #1407,-(%sp)
	move.l %d3,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w #1408,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1410,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1417,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1413,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1415,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1418,%d4
	lea (36,%sp),%sp
	.even
.L433:
	move.w %d4,-(%sp)
	move.l %d3,-(%sp)
	bsr.w ShowObject
	addq.l #6,%sp
	addq.w #1,%d4
	cmp.w #1424,%d4
	jbls .L433
	bra .L426
	.even
.L436:
	move.w #1407,-(%sp)
	move.l %d3,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w #1408,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1411,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1425,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1426,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1427,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	lea (36,%sp),%sp
	move.w #1413,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1415,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	lea (12,%sp),%sp
	bra .L426
	.even
.L437:
	move.w #1407,-(%sp)
	move.l %d3,-(%sp)
	lea ShowObject(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w #1408,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1412,-(%sp)
.L440:
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1413,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	move.w #1415,-(%sp)
	move.l %d3,-(%sp)
	jsr (%a2)
	lea (30,%sp),%sp
.L426:
	lea RepeatDescRectHandler(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #1440,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	addq.l #6,%sp
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA3D8
#NO_APP
	clr.w RepeatStartOfWeek@END.w(%a5)
	move.l 12(%a3),RepeatEndDate@END.w(%a5)
	move.b 10(%a3),RepeatingEventType@END.w(%a5)
	pea 10(%a3)
	move.l %d3,-(%sp)
	bsr.w RepeatSetUIValues
	movm.l -16(%a6),#0xc18
	unlk %a6
	rts
	.even
.globl RepeatHandleEvent
RepeatHandleEvent:
	link.w %a6,#0
	movm.l #0x1820,-(%sp)
	move.l 8(%a6),%a2
	clr.b %d4
	move.w (%a2),%d2
	cmp.w #9,%d2
	jbne .L442
	move.w 8(%a2),%d0
	add.w #-1402,%d0
	cmp.w #29,%d0
	jbhi .L462
	and.l #0xFFFF,%d0
	lea .L460(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L460:
	.word .L450-.L460
	.word .L450-.L460
	.word .L450-.L460
	.word .L450-.L460
	.word .L450-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L457-.L460
	.word .L457-.L460
	.word .L457-.L460
	.word .L457-.L460
	.word .L457-.L460
	.word .L457-.L460
	.word .L457-.L460
	.word .L462-.L460
	.word .L459-.L460
	.word .L459-.L460
	.word .L462-.L460
	.word .L462-.L460
	.word .L444-.L460
	.word .L445-.L460
	.even
.L444:
	bsr.w RepeatApply
.L445:
	lea RepeatDetailsP@END.w(%a5),%a0 /*FRED7b*/
	move.l (%a0),DetailsP@END.w(%a5)
	clr.l (%a0)
	move.w #1300,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
.L483:
	moveq.l #1,%d4
	bra .L462
	.even
.L450:
	move.l %a2,-(%sp)
	bsr.w RepeatChangeType
	bra .L483
	.even
.L457:
	move.l %a2,-(%sp)
	bsr.w RepeatChangeRepeatOn
	bra .L483
	.even
.L459:
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
	bra .L477
	.even
.L442:
	cmp.w #4,%d2
	jbne .L463
	move.w 12(%a2),%d1
	btst #3,%d1
	jbeq .L464
	move.w 8(%a2),%d3
	move.w %d3,%d0
	add.w #-512,%d0
	cmp.w #255,%d0
	jbls .L463
	cmp.w #267,%d3
	jbeq .L463
.L464:
	and.w #1544,%d1
	jbne .L463
	move.w 8(%a2),%d3
	move.w %d3,-(%sp)
#APP
	moveq #2,%d2; trap #15; dc.w 0xA2AB
#NO_APP
	addq.l #2,%sp
	btst #5,%d0
	jbne .L466
	move.w %d3,-(%sp)
#APP
	moveq #2,%d2; trap #15; dc.w 0xA2AB
#NO_APP
	and.b #192,%d0
	addq.l #2,%sp
	jbeq .L483
.L466:
	move.w #1408,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a2,-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA13B
#NO_APP
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #RepeatDrawDescription,%a0 /*JANE2*/
	jsr (%a0)
	bra .L483
	.even
.L463:
	cmp.w #14,%d2
	jbne .L468
	cmp.w #1416,14(%a2)
	jbne .L462
	move.l %a2,-(%sp)
	bsr.w RepeatSelectEndDate
	bra .L483
	.even
.L468:
	cmp.w #24,%d2
	jbne .L471
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
	bsr.w RepeatInit
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	move.w #1408,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA179
#NO_APP
	move.l %d3,-(%sp)
.L477:
	move.l __text__sec002@END.w(%a5),%a0
	add.l #RepeatDrawDescription,%a0 /*JANE2*/
	jsr (%a0)
	bra .L483
	.even
.L471:
	cmp.w #26,%d2
	jbne .L473
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	bra .L483
	.even
.L473:
	cmp.w #28,%d2
	jbne .L462
	lea RepeatDetailsP@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%d0
	jbeq .L462
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA012
#NO_APP
	clr.l (%a2)
.L462:
	move.w %d4,%d0
	and.w #1,%d0
	movm.l -12(%a6),#0x418
	unlk %a6
	rts
	.even
DetailsSetSpanTriggers:
	link.w %a6,#-4
	movm.l #0x1c38,-(%sp)
	move.l 8(%a6),%a2
	move.w #1303,-(%sp)
	move.l __text__@END.w(%a5),%a3
	move.l %a3,%d3
	add.l #GetObjectPtr,%d3 /*JANE2*/
	move.l %d3,%a1
	jsr (%a1)
	move.l %a0,%d5
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
	subq.l #2,%sp
	move.b 2(%a2),(%sp)
	move.b 3(%a2),1(%sp)
	move.l 6(%a2),-(%sp)
	move.l __text__sec002@END.w(%a5),%a4
	add.l #DateSpanFormatLabel,%a4 /*JANE2*/
	jsr (%a4)
	move.l %d4,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	move.l 6(%a2),-4(%a6)
	clr.l %d0
	move.b 1(%a2),%d0
	move.l %d0,-(%sp)
	pea -4(%a6)
	jsr EDateAdjust(%a3)
	lea (32,%sp),%sp
	move.w #1305,-(%sp)
	move.l %d3,%a1
	jsr (%a1)
	move.l %a0,%d5
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
	subq.l #2,%sp
	move.b 4(%a2),(%sp)
	move.b 5(%a2),1(%sp)
	move.l -4(%a6),-(%sp)
	jsr (%a4)
	move.l %d4,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	movm.l -28(%a6),#0x1c38
	unlk %a6
	rts
	.even
DetailsDeleteAppointment:
	link.w %a6,#0
	move.l %d3,-(%sp)
	move.w CurrentRecord@END.w(%a5),%d3
	move.l __text__@END.w(%a5),%a0
	add.l #ClearEditState,%a0 /*JANE2*/
	jsr (%a0)
	tst.b %d0
	jbne .L486
	move.w %d3,-(%sp)
	bsr.w DeleteRecord
	tst.b %d0
	sne %d0
	ext.w %d0
	neg.w %d0
	bra .L488
	.even
.L486:
	moveq.l #1,%d0
.L488:
	move.l -4(%a6),%d3
	unlk %a6
	rts
	.even
DetailsGet:
	link.w %a6,#0
	move.l %a2,-(%sp)
	move.l 8(%a6),%a2
	move.w #1315,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA111
#NO_APP
	move.b %d0,(%a2)
	moveq.l #1,%d0
	move.l -4(%a6),%a2
	unlk %a6
	rts
.LC12:
	.ascii "weekly appt. repeats on no days\0"
	.even
DetailsApply:
	link.w %a6,#-104
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d7
	clr.w -74(%a6)
	clr.w -76(%a6)
	clr.b -79(%a6)
	clr.b -80(%a6)
	clr.b -81(%a6)
	clr.b -82(%a6)
	move.l %d7,-(%sp)
	bsr.w DetailsGet
	addq.l #4,%sp
	tst.b %d0
	jbeq .L560
	move.l %d7,%a0
	move.l 2(%a0),%d0
	move.l 6(%a0),%d1
	move.l %d0,-66(%a6)
	move.l %d1,-62(%a6)
	pea -56(%a6)
	lea (-26,%a6),%a1
	move.l %a1,-92(%a6)
	move.l %a1,-(%sp)
	lea CurrentRecord@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-96(%a6)
	move.w (%a0),-(%sp)
	lea ApptDB@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,-100(%a6)
	move.l (%a1),-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d6
	move.l (%a0),%a4
	jsr ApptGetRecord(%a4)
	move.w #1323,-(%sp)
	move.l %a4,%a3
	add.l #GetObjectPtr,%a3 /*JANE2*/
	jsr (%a3)
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA155
#NO_APP
	lea (20,%sp),%sp
	tst.b %d0
	jbeq .L492
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA154
#NO_APP
	addq.l #4,%sp
.L492:
	move.w #1323,-(%sp)
	jsr (%a3)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA139
#NO_APP
	move.l %a0,-88(%a6)
	addq.l #6,%sp
	jbne .L493
	lea .LC3(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-88(%a6)
.L493:
	move.l -10(%a6),%a0
	cmp.w #0,%a0
	jbne .L494
	lea .LC3(%pc),%a0 /*SPECIAL_FRED*/
.L494:
	move.l %a0,-(%sp)
	move.l -88(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA0C8
#NO_APP
	addq.l #8,%sp
	tst.w %d0
	sne %d0
	neg.b %d0
	move.b %d0,-83(%a6)
	clr.b -(%sp)
	pea 2.w
	pea -58(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l -26(%a6),%a0
	lea (10,%sp),%sp
	move.l %d7,%a1
	move.w 2(%a1),%d1
	cmp.w (%a0),%d1
	jbeq .L495
	move.l 4(%a0),-62(%a6)
	lea (-66,%a6),%a0
	move.l %a0,-52(%a6)
	or.b #128,-58(%a6)
	move.w #8,-76(%a6)
	move.b #1,-79(%a6)
.L495:
	move.l -26(%a6),%a0
	move.l %d7,%a1
	move.w 4(%a1),%d0
	cmp.w 2(%a0),%d0
	jbeq .L496
	move.l 4(%a0),-62(%a6)
	lea (-66,%a6),%a0
	move.l %a0,-52(%a6)
	or.b #128,-58(%a6)
	or.w #8,-76(%a6)
	move.b #1,-79(%a6)
.L496:
	lea Date@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
	move.l %a4,%a3
	add.l #EDateInt,%a3 /*JANE2*/
	jsr (%a3)
	move.l %d0,%d3
	move.l %d7,%a1
	move.l 6(%a1),-(%sp)
	jsr (%a3)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbeq .L497
	move.l %d7,%a0
	move.l 6(%a0),-62(%a6)
	lea (-66,%a6),%a1
	move.l %a1,-52(%a6)
	or.b #128,-58(%a6)
	or.w #4,-76(%a6)
	move.b #1,-79(%a6)
.L497:
	move.l %d7,%a0
	move.b 1(%a0),%d0
	cmp.b -2(%a6),%d0
	jbeq .L498
	move.b %d0,-28(%a6)
	or.b #1,-58(%a6)
	or.w #8,-76(%a6)
	move.b #1,-79(%a6)
.L498:
	move.l -18(%a6),%a0
	cmp.w #0,%a0
	jbne .L558
	move.l %d7,%a1
	tst.b 10(%a1)
	jbeq .L500
	moveq.l #10,%d0
	add.l %d7,%d0 /*JANE2*/
	move.l %d0,-44(%a6)
	bra .L561
	.even
.L558:
	move.l %d7,%a1
	move.b 10(%a1),%d0
	jbne .L501
	clr.l -44(%a6)
	or.w #32,-76(%a6)
	or.b #32,-58(%a6)
	bra .L562
	.even
.L501:
	cmp.b (%a0),%d0
	jbne .L504
	move.l 2(%a0),-(%sp)
	jsr (%a3)
	move.l %d0,%d3
	move.l %d7,%a0
	move.l 12(%a0),-(%sp)
	jsr (%a3)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbne .L504
	move.l -18(%a6),%a0
	move.b 6(%a0),%d0
	move.l %d7,%a1
	cmp.b 16(%a1),%d0
	jbne .L504
	cmp.b #2,10(%a1)
	jbne .L500
	move.b 7(%a0),%d0
	cmp.b 17(%a1),%d0
	jbeq .L500
.L504:
	moveq.l #10,%d1
	add.l %d7,%d1 /*JANE2*/
	move.l %d1,-44(%a6)
.L561:
	or.b #32,-58(%a6)
	or.w #32,-76(%a6)
.L562:
	move.b #1,-79(%a6)
.L500:
	clr.l -(%sp)
	pea -70(%a6)
	pea -72(%a6)
	move.l -96(%a6),%a0
	move.w (%a0),-(%sp)
	move.l -100(%a6),%a1
	move.l (%a1),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	move.l -70(%a6),-(%sp)
	lea __text__sec002@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-104(%a6)
	move.l %a0,%a1
	move.l (%a1),%a0
	add.l #LocGadgetCommit,%a0 /*JANE2*/
	jsr (%a0)
	or.w #8,-76(%a6)
	move.b -71(%a6),%d0
	lsr.l #4,%d0
	moveq.l #1,%d1
	and.l %d1,%d0
	move.l %d7,%a0
	clr.w %d1
	move.b (%a0),%d1
	lea (22,%sp),%sp
	cmp.w %d0,%d1
	jbeq .L505
	tst.b PrivateRecordVisualStatus@END.w(%a5)
	jbeq .L506
	or.w #2,-76(%a6)
	bra .L507
	.even
.L506:
	move.l %d7,%a1
	tst.b (%a1)
	jbeq .L507
	move.w #10006,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	addq.l #2,%sp
.L507:
	move.b #1,-80(%a6)
.L505:
	tst.l -18(%a6)
	jbeq .L519
	btst #5,-58(%a6)
	jbne .L509
	tst.b -79(%a6)
	jbne .L510
	tst.b -80(%a6)
	jbne .L510
	tst.b 12(%a6)
	jbeq .L509
	tst.l -6(%a6)
	jbne .L509
.L510:
	move.w #2100,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d5
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (10,%sp),%sp
	cmp.w #2106,%d5
	jbeq .L512
	cmp.w #2103,%d5
	jbne .L514
	or.w #16,-76(%a6)
	move.b #1,-81(%a6)
	btst #2,-75(%a6)
	jbne .L509
	move.l (%a2),-62(%a6)
	bra .L509
	.even
.L514:
	cmp.w #2104,%d5
	jbne .L509
	move.b #1,-82(%a6)
	btst #2,-75(%a6)
	jbne .L518
	move.l (%a2),-62(%a6)
.L518:
	lea (-66,%a6),%a0
	move.l %a0,-52(%a6)
	or.b #128,-58(%a6)
	move.b #1,-79(%a6)
.L509:
	move.l -18(%a6),%a0
	cmp.w #0,%a0
	jbeq .L519
	move.b -58(%a6),%d0
	btst #5,%d0
	jbeq .L519
	move.b (%a0),%d1
	move.l %d7,%a1
	cmp.b 10(%a1),%d1
	jbne .L521
	move.w 16(%a1),%d1
	cmp.w 6(%a0),%d1
	jbeq .L519
.L521:
	move.b #1,-82(%a6)
	btst #2,-75(%a6)
	jbne .L519
	or.w #4,-76(%a6)
	move.l Date@END.w(%a5),-62(%a6)
	lea (-66,%a6),%a0
	move.l %a0,-52(%a6)
	or.b #128,%d0
	move.b %d0,-58(%a6)
.L519:
	tst.b -81(%a6)
	jbne .L535
	move.l -18(%a6),%a0
	cmp.w #0,%a0
	jbeq .L523
	btst #5,-58(%a6)
	jbne .L523
	btst #2,-75(%a6)
	jbeq .L523
	move.l 2(%a0),-(%sp)
	move.l %d6,%a1
	move.l (%a1),%a3
	move.l %a3,%a4
	add.l #EDateInt,%a4 /*JANE2*/
	jsr (%a4)
	addq.l #4,%sp
	cmp.l #65535,%d0
	jbeq .L524
	move.l -18(%a6),%a0
	move.l 2(%a0),-(%sp)
	move.l %a3,%a2
	add.l #EDateToDays,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l -26(%a6),%a0
	move.l 4(%a0),-(%sp)
	jsr (%a2)
	move.l %d7,%a0
	move.l 6(%a0),12(%a0)
	sub.l %d0,%d3
	move.l %d3,-(%sp)
	pea 12(%a0)
	jsr EDateAdjust(%a3)
	lea (16,%sp),%sp
.L524:
	move.l %d7,%a1
	move.l 12(%a1),-(%sp)
	jsr (%a4)
	move.l %d0,%d3
	move.l -18(%a6),%a0
	move.l 2(%a0),-(%sp)
	jsr (%a4)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbeq .L523
	moveq.l #10,%d0
	add.l %d7,%d0 /*JANE2*/
	move.l %d0,-44(%a6)
	or.b #32,-58(%a6)
	or.w #32,-76(%a6)
	move.b #1,-79(%a6)
.L523:
	tst.b -81(%a6)
	jbne .L535
	tst.l -18(%a6)
	jbeq .L526
	btst #2,-75(%a6)
	jbeq .L526
	clr.l -40(%a6)
	or.b #8,-58(%a6)
	move.b #1,-79(%a6)
.L526:
	tst.b -81(%a6)
	jbne .L535
	move.l %d7,%a0
	cmp.b #2,10(%a0)
	jbne .L527
	btst #2,-75(%a6)
	jbeq .L527
	tst.b 17(%a0)
	jbne .L529
	lea .LC12(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-(%sp)
	move.w #3296,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L529:
	move.l %d7,%a1
	clr.w %d1
	move.b 17(%a1),%d1
	move.w %d1,%d0
	subq.w #1,%d0
	and.w %d0,%d1
	jbne .L527
	move.l -26(%a6),%a0
	move.w 4(%a0),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 7(%a0),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 6(%a0),%d0
	move.w %d0,-(%sp)
	move.l __text__@END.w(%a5),%a2
	add.l #EDateDayOfWeek,%a2 /*JANE2*/
	jsr (%a2)
	move.w %d0,%d3
	move.w -62(%a6),%d1
	add.w #1904,%d1
	move.w %d1,-(%sp)
	clr.w %d0
	move.b -59(%a6),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b -60(%a6),%d0
	move.w %d0,-(%sp)
	jsr (%a2)
	move.w %d0,%d1
	lea (12,%sp),%sp
	cmp.w %d3,%d1
	jbeq .L527
	moveq.l #1,%d0
	lsl.w %d1,%d0
	move.l %d7,%a0
	move.b %d0,17(%a0)
	or.b #32,-58(%a6)
	moveq.l #10,%d0
	add.l %d7,%d0 /*JANE2*/
	move.l %d0,-44(%a6)
.L527:
	tst.b -81(%a6)
	jbne .L535
	move.l %d7,%a0
	cmp.b #3,10(%a0)
	jbne .L535
	btst #2,-75(%a6)
	jbne .L536
	tst.l -18(%a6)
	jbne .L535
.L536:
	move.w -62(%a6),%a1
	lea (1904,%a1),%a1 /*FRED4a*/
	move.w %a1,-(%sp)
	clr.w %d0
	move.b -59(%a6),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b -60(%a6),%d0
	move.w %d0,-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr EDateDayOfMonth(%a2)
	move.w %d0,%d6
	move.w %d6,%d1
	mulu.w #9363,%d1
	clr.w %d1
	swap %d1
	sub.w %d1,%d0
	lsr.w #1,%d0
	add.w %d1,%d0
	lsr.w #2,%d0
	addq.l #6,%sp
	cmp.w #3,%d0
	jbne .L537
	clr.w %d3
	move.b -59(%a6),%d3
	addq.w #7,%d3
	move.w -62(%a6),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b -60(%a6),%d0
	move.w %d0,-(%sp)
	jsr EDateDaysInMonth(%a2)
	addq.l #4,%sp
	cmp.w %d3,%d0
	jbge .L537
	move.w #1700,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d4
	addq.l #2,%sp
	move.l -18(%a6),%a0
	move.w #1704,%d0
	cmp.w #0,%a0
	jbeq .L538
	cmp.b #3,(%a0)
	jbne .L538
	cmp.b #27,7(%a0)
	jbls .L538
	addq.w #1,%d0
.L538:
	move.w %d0,-(%sp)
	move.b #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d5
	move.w #1705,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%d3
	move.b #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA18A
#NO_APP
	lea (24,%sp),%sp
	cmp.w %d3,%d0
	jbne .L540
	addq.w #7,%d6
.L540:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	addq.l #4,%sp
	cmp.w #1707,%d5
	jbeq .L512
.L537:
	move.l %d7,%a0
	move.b %d6,17(%a0)
	moveq.l #10,%d0
	add.l %d7,%d0 /*JANE2*/
	move.l %d0,-44(%a6)
	or.b #32,-58(%a6)
.L535:
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA172
#NO_APP
	addq.l #8,%sp
	tst.b -81(%a6)
	jbeq .L542
	move.l Date@END.w(%a5),-(%sp)
	move.l -96(%a6),-(%sp)
	move.l -100(%a6),%a0
	move.l (%a0),-(%sp)
	move.l -104(%a6),%a1
	move.l (%a1),%a0
	add.l #ApptAddException,%a0 /*JANE2*/
	jsr (%a0)
	lea (12,%sp),%sp
	tst.w %d0
	jbne .L559
	pea -56(%a6)
	move.l -92(%a6),-(%sp)
	move.l -96(%a6),%a0
	move.w (%a0),-(%sp)
	move.l -100(%a6),%a1
	move.l (%a1),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	clr.b -(%sp)
	pea 26.w
	moveq.l #-52,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	lea (24,%sp),%sp
	tst.b -83(%a6)
	jbne .L544
	move.l -10(%a6),-36(%a6)
	bra .L545
	.even
.L544:
	move.l -88(%a6),-36(%a6)
.L545:
	move.l -6(%a6),-32(%a6)
	lea (-66,%a6),%a0
	move.l %a0,-52(%a6)
	move.l %d7,%a1
	move.b 1(%a1),-28(%a6)
	move.l -96(%a6),-(%sp)
	move.l %d3,-(%sp)
	bsr.w CreateException
	move.w %d0,-74(%a6)
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (12,%sp),%sp
	bra .L546
	.even
.L542:
	tst.b -79(%a6)
	jbne .L548
	tst.b -83(%a6)
	jbeq .L546
.L548:
	tst.b -82(%a6)
	jbeq .L549
	move.l -96(%a6),-(%sp)
	bsr.w SplitRepeatingEvent
	move.w %d0,-74(%a6)
	addq.l #4,%sp
.L549:
	tst.w -74(%a6)
	jbne .L559
	tst.b -83(%a6)
	jbeq .L551
	move.l -88(%a6),-36(%a6)
	or.b #4,-58(%a6)
.L551:
	move.w -58(%a6),-(%sp)
	pea -52(%a6)
	pea CurrentRecord@END.w(%a5)
	move.l -100(%a6),%a0
	move.l (%a0),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptChangeRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,-74(%a6)
	lea (14,%sp),%sp
.L546:
	tst.w -74(%a6)
	jbeq .L552
.L559:
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	move.l __text__@END.w(%a5),%a0
	add.l #ClearEditState,%a0 /*JANE2*/
	jsr (%a0)
	clr.w -76(%a6)
	bra .L553
	.even
.L552:
	tst.b -80(%a6)
	jbeq .L553
	clr.l -(%sp)
	clr.l -(%sp)
	pea -72(%a6)
	lea CurrentRecord@END.w(%a5),%a3 /*FRED7b*/
	move.w (%a3),-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	lea (18,%sp),%sp
	move.l %d7,%a1
	tst.b (%a1)
	jbeq .L555
	or.w #16,-72(%a6)
	bra .L556
	.even
.L555:
	and.w #65519,-72(%a6)
.L556:
	or.w #64,-72(%a6)
	clr.l -(%sp)
	pea -72(%a6)
	move.w (%a3),-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA051
#NO_APP
.L553:
	move.l 14(%a6),%a0
	move.w -76(%a6),(%a0)
	moveq.l #1,%d0
	bra .L557
	.even
.L512:
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
.L560:
	clr.w %d0
.L557:
	movm.l -136(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DetailsExit:
	link.w %a6,#0
	move.l %a2,-(%sp)
	move.w #127,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea DetailsP@END.w(%a5),%a2 /*FRED7b*/
	lea (10,%sp),%sp
	move.l (%a2),%a0
	cmp.w #0,%a0
	jbeq .L564
	tst.l 20(%a0)
	jbeq .L565
	clr.l -(%sp)
	move.w #1323,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	move.l (%a2),%a0
	move.l 20(%a0),-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	lea (12,%sp),%sp
.L565:
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA012
#NO_APP
	clr.l (%a2)
.L564:
	move.l -4(%a6),%a2
	unlk %a6
	rts
	.even
DetailsInit:
	link.w %a6,#-44
	movm.l #0x1f38,-(%sp)
	lea DetailsP@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%d0
	jbne .L567
	pea 24.w
#APP
	trap #15; dc.w 0xA013
#NO_APP
	move.l %a0,%a4
	move.l %a4,(%a2)
	move.l %a4,%d4
	clr.b -(%sp)
	pea 24.w
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	pea -32(%a6)
	moveq.l #-26,%d7
	add.l %a6,%d7 /*JANE2*/
	move.l %d7,-(%sp)
	lea CurrentRecord@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),-(%sp)
	lea ApptDB@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d6
	move.l %d6,%a1
	move.l (%a1),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	clr.l -(%sp)
	pea -36(%a6)
	pea -38(%a6)
	move.w (%a2),-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	lea (42,%sp),%sp
	move.l -36(%a6),(%sp)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #LocGadgetLoad,%a0 /*JANE2*/
	jsr (%a0)
	move.w (%a2),-(%sp)
	bsr.w DayCopyDescPlain
	move.l %a0,20(%a4)
	move.b -37(%a6),%d0
	lsr.l #4,%d0
	and.b #1,%d0
	move.b %d0,(%a4)
	move.l -26(%a6),%a0
	move.l (%a0),%d0
	move.l 4(%a0),%d1
	move.l %d4,%a1
	move.l %d0,2(%a1)
	move.l %d1,6(%a1)
	lea (10,%sp),%sp
	move.l -18(%a6),%d0
	jbeq .L588
	tst.b -2(%a6)
	jbeq .L568
	lea gDetailsApptIndex@END.w(%a5),%a2 /*FRED7b*/
	cmp.w #65535,(%a2)
	jbeq .L568
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.l %d1
	move.w (%a2),%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.b (%a0,%d0.l),-28(%a6)
	move.b 1(%a0,%d0.l),-27(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d5
	move.l %d4,%a1
	move.l (%a0),6(%a1)
	move.l -26(%a6),%a0
	addq.l #8,%sp
	move.w (%a0),%d0
	cmp.w -28(%a6),%d0
	jbeq .L577
	clr.w %d3
	move.b -2(%a6),%d3
	cmp.w #30,%d3
	jble .L570
	moveq.l #30,%d3
.L570:
	move.w #1,%a3
	bra .L571
	.even
.L573:
	addq.w #1,%a3
.L571:
	cmp.w %a3,%d3
	jblt .L577
	move.l %d5,%a0
	move.l (%a0),-42(%a6)
	move.w %a3,%d0
	neg.w %d0
	move.w %d0,%a1
	move.l %a1,-(%sp)
	pea -42(%a6)
	move.l %d6,%a0
	move.l (%a0),%a2
	jsr EDateAdjust(%a2)
	move.l -42(%a6),-(%sp)
	move.l %d7,-(%sp)
	jsr ApptRepeatsOnDate(%a2)
	lea (16,%sp),%sp
	tst.b %d0
	jbeq .L573
	move.l -42(%a6),6(%a4)
	bra .L577
	.even
.L568:
	tst.l %d0
	jbne .L578
.L588:
	tst.b -2(%a6)
	jbeq .L578
	move.l Date@END.w(%a5),-(%sp)
	move.l %d6,%a1
	move.l (%a1),%a2
	add.l #EDateToDays,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l -26(%a6),%a0
	move.l 4(%a0),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbeq .L578
	move.l -26(%a6),%a0
	move.l 4(%a0),6(%a4)
	bra .L577
	.even
.L578:
	move.l Date@END.w(%a5),6(%a4)
.L577:
	move.w #-1,gDetailsApptIndex@END.w(%a5)
	move.b -2(%a6),1(%a4)
	move.l -18(%a6),%d0
	jbeq .L580
	pea 10.w
	move.l %d0,-(%sp)
	pea 10(%a4)
	move.l __text__@END.w(%a5),%a0
	add.l #memcpy,%a0 /*JANE2*/
	jsr (%a0)
	lea (12,%sp),%sp
	bra .L581
	.even
.L580:
	clr.b 10(%a4)
	pea 12(%a4)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateSetForever,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #4,%sp
.L581:
	move.l -32(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	bra .L582
	.even
.L567:
	move.l %d0,%d4
.L582:
	move.w #1323,-(%sp)
	lea __text__@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),%a2
	add.l #GetObjectPtr,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d4,%a1
	move.l 20(%a1),-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	move.l %d4,-(%sp)
	bsr.w DetailsSetSpanTriggers
	move.w #1313,-(%sp)
	jsr (%a2)
	move.l %a0,%d5
	move.w #127,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	lea (26,%sp),%sp
	clr.w %d3
	move.l %d4,%a0
	tst.b 10(%a0)
	jbeq .L584
	.even
.L586:
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	move.w %d0,%a0
	lea 1(%a2,%a0.l),%a2 /*FRED7b*/
	addq.l #4,%sp
	addq.w #1,%d3
	move.l %d4,%a1
	clr.w %d0
	move.b 10(%a1),%d0
	cmp.w %d3,%d0
	jbhi .L586
.L584:
	move.l %a2,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	move.w #1315,-(%sp)
	move.l (%a3),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %d4,%a1
	clr.w %d0
	move.b (%a1),%d0
	move.w %d0,-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA112
#NO_APP
	move.l %d4,%a0
	movm.l -76(%a6),#0x1cf8
	unlk %a6
	rts
.lcomm details.132,4
	.even
.globl DetailsHandleEvent
DetailsHandleEvent:
	link.w %a6,#-4
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	move.l 8(%a6),%a0
	clr.w -2(%a6)
	clr.b %d3
	move.w (%a0),%d0
	cmp.w #9,%d0
	jbne .L590
	move.w 8(%a0),%d0
	add.w #-1303,%d0
	cmp.w #16,%d0
	jbhi .L612
	and.l #0xFFFF,%d0
	lea .L610(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L610:
	.word .L601-.L610
	.word .L612-.L610
	.word .L606-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L608-.L610
	.word .L612-.L610
	.word .L612-.L610
	.word .L592-.L610
	.word .L594-.L610
	.word .L595-.L610
	.word .L597-.L610
	.even
.L592:
	pea -2(%a6)
	clr.b -(%sp)
	move.l details.132@END.w(%a5),-(%sp)
	bsr.w DetailsApply
	lea (10,%sp),%sp
	tst.b %d0
	jbeq .L596
	bsr.w DetailsExit
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19E
#NO_APP
	addq.l #2,%sp
	bra .L596
	.even
.L594:
	bsr.w DetailsExit
	move.w -2(%a6),-(%sp)
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19D
#NO_APP
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19E
#NO_APP
.L624:
	moveq.l #1,%d3
	bra .L612
	.even
.L595:
	bsr.w DetailsExit
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19E
#NO_APP
	bsr.w DetailsDeleteAppointment
	addq.l #2,%sp
	tst.b %d0
	jbeq .L596
	move.w #1,-2(%a6)
.L596:
	move.w -2(%a6),-(%sp)
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19D
#NO_APP
	bra .L624
	.even
.L597:
	pea PendingUpdate@END.w(%a5)
	move.b #1,-(%sp)
	move.l details.132@END.w(%a5),-(%sp)
	bsr.w DetailsApply
	lea (10,%sp),%sp
	tst.b %d0
	jbeq .L624
	bsr.w DetailsExit
#APP
	trap #15; dc.w 0xA1A1
#NO_APP
	clr.b -(%sp)
	bsr.w CreateNote
	addq.l #2,%sp
	tst.b %d0
	jbeq .L599
	move.w #10950,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L624
	.even
.L599:
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L624
	.even
.L601:
	lea details.132@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%a0
	pea 2(%a0)
	pea 6(%a0)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DateSpanEditStart,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #8,%sp
	tst.b %d0
	jbeq .L624
	move.l (%a2),%a0
	cmp.b #3,10(%a0)
	jbne .L603
	move.w 6(%a0),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 9(%a0),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 8(%a0),%d0
	move.w %d0,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateDayOfMonth,%a0 /*JANE2*/
	jsr (%a0)
	move.l (%a2),%a0
	move.b %d0,17(%a0)
	addq.l #6,%sp
.L603:
	move.l (%a2),%a0
	cmp.w #-1,2(%a0)
	jbeq .L604
	cmp.w #-1,4(%a0)
	jbne .L604
	move.b 2(%a0),4(%a0)
	move.b 3(%a0),5(%a0)
	move.l (%a2),%a0
	move.b 4(%a0),%d0
	cmp.b #22,%d0
	jbhi .L604
	addq.b #1,%d0
	move.b %d0,4(%a0)
.L604:
	move.l (%a2),-(%sp)
	bsr.w DetailsSetSpanTriggers
	bra .L624
	.even
.L606:
	lea details.132@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%a0
	pea 4(%a0)
	pea 1(%a0)
	subq.l #2,%sp
	move.b 2(%a0),(%sp)
	move.b 3(%a0),1(%sp)
	move.l 6(%a0),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DateSpanEditEnd,%a0 /*JANE2*/
	jsr (%a0)
	lea (14,%sp),%sp
	tst.b %d0
	jbeq .L624
	move.l (%a2),-(%sp)
	bsr.w DetailsSetSpanTriggers
	bra .L624
	.even
.L608:
	move.l details.132@END.w(%a5),-(%sp)
	bsr.w DetailsGet
	addq.l #4,%sp
	tst.b %d0
	jbeq .L624
	lea DetailsP@END.w(%a5),%a0 /*FRED7b*/
	move.l (%a0),RepeatDetailsP@END.w(%a5)
	clr.l (%a0)
	clr.l -(%sp)
	move.w #1323,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	bsr.w DetailsExit
	move.w #1400,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L624
	.even
.L590:
	cmp.w #15,%d0
	jbne .L613
	cmp.w #1321,8(%a0)
	jbne .L613
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #LocFieldTapped,%a0 /*JANE2*/
	jsr (%a0)
	bra .L624
	.even
.L613:
	cmp.w #21,%d0
	jbne .L615
	move.w 8(%a0),-(%sp)
	bsr.w DetailsDoCommand
	move.b %d0,%d3
	bra .L612
	.even
.L615:
	cmp.w #24,%d0
	jbne .L617
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	bsr.w DetailsInit
	move.l %a0,details.132@END.w(%a5)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	bra .L624
	.even
.L617:
	cmp.w #28,%d0
	jbne .L612
	lea DetailsP@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%d0
	jbeq .L612
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA012
#NO_APP
	clr.l (%a2)
.L612:
	clr.w %d0
	move.b %d3,%d0
	move.l -12(%a6),%d3
	move.l -8(%a6),%a2
	unlk %a6
	rts
	.even
NoteViewDrawTitleAndForm:
	link.w %a6,#-68
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d3
	pea -34(%a6)
	pea -26(%a6)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -10(%a6),%d4
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA3A0
#NO_APP
	move.l %a0,-66(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	pea -42(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA17B
#NO_APP
	move.w -38(%a6),%d5
	move.w %d5,%d3
	subq.w #8,%d3
	move.w #10,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA0CC
#NO_APP
	move.l %a0,%d0
	lea (34,%sp),%sp
	jbne .L626
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	addq.l #4,%sp
	move.w %d0,%a2
	bra .L627
	.even
.L626:
	move.w %d0,%a2
	sub.w %d4,%a2
.L627:
	clr.w %d6
#APP
	trap #15; dc.w 0xA168
#NO_APP
	addq.w #4,%d0
	move.w %d0,-(%sp)
	move.w %d5,-(%sp)
	clr.w -(%sp)
	clr.w -(%sp)
	pea -50(%a6)
#APP
	trap #15; dc.w 0xA1C4
#NO_APP
#APP
	trap #15; dc.w 0xA168
#NO_APP
	addq.w #2,%d0
	move.w %d0,-(%sp)
	move.w %d5,-(%sp)
	clr.w -(%sp)
	clr.w -(%sp)
	pea -58(%a6)
#APP
	trap #15; dc.w 0xA1C4
#NO_APP
	move.b #21,-(%sp)
#APP
	trap #15; dc.w 0xA3A4
#NO_APP
	move.b %d0,-(%sp)
#APP
	trap #15; dc.w 0xA398
#NO_APP
	move.b %d0,-61(%a6)
	move.b #22,-(%sp)
#APP
	trap #15; dc.w 0xA3A4
#NO_APP
	move.b %d0,-(%sp)
#APP
	trap #15; dc.w 0xA399
#NO_APP
	move.w %d0,%a4
	lea (32,%sp),%sp
	move.b #21,-(%sp)
#APP
	trap #15; dc.w 0xA3A4
#NO_APP
	move.b %d0,-(%sp)
#APP
	trap #15; dc.w 0xA39A
#NO_APP
	move.w %d0,%a3
	move.b #1,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d7
	clr.w -(%sp)
	pea -50(%a6)
#APP
	trap #15; dc.w 0xA219
#NO_APP
	move.w #3,-(%sp)
	pea -58(%a6)
#APP
	trap #15; dc.w 0xA218
#NO_APP
	pea -60(%a6)
	clr.l -(%sp)
	move.w %d3,-(%sp)
	move.w %a2,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA34E
#NO_APP
	lea (34,%sp),%sp
	cmp.w %a2,%d0
	jbeq .L628
	move.b #24,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	move.w %d0,%d6
	pea -60(%a6)
	clr.l -(%sp)
	sub.w %d6,%d3
	move.w %d3,-(%sp)
	move.w %a2,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA34E
#NO_APP
	move.w %d0,%a2
	lea (18,%sp),%sp
.L628:
	move.w %d5,%d3
	sub.w -60(%a6),%d3
	sub.w %d6,%d3
	addq.w #1,%d3
	lsr.w #1,%d3
	move.w #1,-(%sp)
	move.w %d3,-(%sp)
	move.w %a2,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA22A
#NO_APP
	lea (10,%sp),%sp
	tst.w %d6
	jbeq .L629
	move.w #24,-(%sp)
	clr.l -(%sp)
	lea (-30,%a6),%a2
	move.l %a2,-(%sp)
#APP
	moveq #8,%d2; trap #15; dc.w 0xA2AB
#NO_APP
	and.l #0xFFFF,%d0
	clr.b (%a2,%d0.l)
	move.w #1,-(%sp)
	add.w -60(%a6),%d3
	move.w %d3,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	addq.l #4,%sp
	move.w %d0,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA22A
#NO_APP
	lea (20,%sp),%sp
.L629:
	tst.l -66(%a6)
	jbeq .L630
#APP
	trap #15; dc.w 0xA3A1
#NO_APP
.L630:
	move.l -34(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.b -61(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA398
#NO_APP
	move.w %a4,%d0
	move.b %d0,-(%sp)
#APP
	trap #15; dc.w 0xA399
#NO_APP
	move.w %a3,%d0
	move.b %d0,-(%sp)
#APP
	trap #15; dc.w 0xA39A
#NO_APP
	move.b %d7,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	movm.l -100(%a6),#0x1cf8
	unlk %a6
	rts
	.even
NoteViewUpdateScrollBar:
	link.w %a6,#-8
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a2
	add.l #GetObjectPtr,%a2 /*JANE2*/
	jsr (%a2)
	move.l %a0,%d3
	move.w #10908,-(%sp)
	jsr (%a2)
	move.l %a0,%a2
	pea -2(%a6)
	pea -4(%a6)
	pea -6(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA2DA
#NO_APP
	lea (20,%sp),%sp
	move.w -2(%a6),%d0
	cmp.w -4(%a6),%d0
	jbcc .L632
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA2E8
#NO_APP
	move.w -4(%a6),%d1
	sub.w -2(%a6),%d1
	add.w %d0,%d1
	addq.l #4,%sp
	bra .L633
	.even
.L632:
	move.w -6(%a6),%d1
	sne %d0
	ext.w %d0
	and.w %d0,%d1
.L633:
	move.w -2(%a6),%d0
	subq.w #1,%d0
	move.w %d0,-(%sp)
	move.w %d1,-(%sp)
	clr.w -(%sp)
	move.w -6(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA2B6
#NO_APP
	move.l -16(%a6),%d3
	move.l -12(%a6),%a2
	unlk %a6
	rts
.LC13:
	.ascii "Invalid record\0"
	.even
NoteViewLoadRecord:
	link.w %a6,#-32
	movm.l #0x1030,-(%sp)
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr GetObjectPtr(%a2)
	move.l %a0,%d3
	move.b NoteFont@END.w(%a5),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA141
#NO_APP
	pea -30(%a6)
	pea -26(%a6)
	lea CurrentRecord@END.w(%a5),%a3 /*FRED7b*/
	move.w (%a3),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	jsr ApptGetRecord(%a2)
	lea (22,%sp),%sp
	tst.l -6(%a6)
	jbne .L638
	lea .LC13(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #4057,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L638:
	move.l -30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea gNoteEditH@END.w(%a5),%a2 /*FRED7b*/
	addq.l #4,%sp
	move.l (%a2),%d0
	jbeq .L642
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
	addq.l #4,%sp
.L642:
	move.w (%a3),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayCopyNotePlain,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d0
	move.l %d0,(%a2)
	addq.l #2,%sp
	jbeq .L643
	move.l %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
.L643:
	movm.l -44(%a6),#0xc08
	unlk %a6
	rts
.section	sec002,"x"
	.even
NoteViewSave:
	link.w %a6,#-28
	movm.l #0x1c38,-(%sp)
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a4
	jsr GetObjectPtr(%a4)
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA155
#NO_APP
	move.b %d0,%d3
	addq.l #6,%sp
	jbeq .L645
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA154
#NO_APP
	addq.l #4,%sp
.L645:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA14B
#NO_APP
	addq.l #4,%sp
	tst.w %d0
	seq %d5
	neg.b %d5
	tst.b %d3
	jbeq .L646
	tst.b %d5
	jbne .L646
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA139
#NO_APP
	move.l %a0,%d3
	clr.b -(%sp)
	pea 26.w
	lea (-26,%a6),%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	lea (14,%sp),%sp
	tst.l %d3
	jbne .L647
	move.l __text__sec001@END.w(%a5),%d3
	add.l #.LC3,%d3 /*JANE2*/
.L647:
	move.l %d3,-6(%a6)
	clr.b -(%sp)
	pea 2.w
	pea -28(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	or.b #16,-28(%a6)
	move.w -28(%a6),-(%sp)
	move.l %a2,-(%sp)
	lea CurrentRecord@END.w(%a5),%a3 /*FRED7b*/
	move.l %a3,-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
	jsr ApptChangeRecord(%a4)
	move.w (%a3),-(%sp)
	move.l (%a2),-(%sp)
	jsr DirtyRecord(%a4)
	lea (30,%sp),%sp
.L646:
	clr.l -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	lea gNoteEditH@END.w(%a5),%a2 /*FRED7b*/
	addq.l #8,%sp
	move.l (%a2),%d0
	jbeq .L648
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
	addq.l #4,%sp
.L648:
	tst.b %d5
	jbeq .L649
	clr.b -(%sp)
	clr.b -(%sp)
	move.l __text__sec001@END.w(%a5),%a0
	add.l #DeleteNote,%a0 /*JANE2*/
	jsr (%a0)
.L649:
	movm.l -52(%a6),#0x1c38
	unlk %a6
	rts
.section	sec001,"x"
	.even
NoteViewDeleteNote:
	link.w %a6,#-32
	movm.l #0x1e20,-(%sp)
	clr.b %d6
	clr.b %d5
	pea -30(%a6)
	pea -26(%a6)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	lea __text__@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (18,%sp),%sp
	tst.l -18(%a6)
	jbeq .L651
	move.w #2100,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d4
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (10,%sp),%sp
	cmp.w #2106,%d4
	jbeq .L663
	cmp.w #2103,%d4
	jbne .L654
	moveq.l #1,%d6
	bra .L657
	.even
.L654:
	cmp.w #2104,%d4
	jbne .L657
	moveq.l #1,%d5
	bra .L657
	.even
.L651:
	move.w #2001,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	addq.l #2,%sp
	tst.w %d0
	jbne .L663
.L657:
	move.w #10901,-(%sp)
	move.l (%a2),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA154
#NO_APP
	clr.l -(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	lea gNoteEditH@END.w(%a5),%a2 /*FRED7b*/
	lea (14,%sp),%sp
	move.l (%a2),%d0
	jbeq .L659
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
	addq.l #4,%sp
.L659:
	move.b %d5,-(%sp)
	move.b %d6,-(%sp)
	bsr.w DeleteNote
	addq.l #4,%sp
	tst.b %d0
	jbne .L660
	bsr.w NoteViewLoadRecord
.L663:
	clr.w %d0
	bra .L661
	.even
.L660:
	moveq.l #1,%d0
.L661:
	movm.l -52(%a6),#0x478
	unlk %a6
	rts
	.even
NoteViewDoCommand:
	link.w %a6,#0
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	move.w 8(%a6),%d0
	moveq.l #1,%d3
	cmp.w #10300,%d0
	jbeq .L666
	cmp.w #10301,%d0
	jbeq .L667
	bra .L668
	.even
.L666:
	lea NoteFont@END.w(%a5),%a2 /*FRED7b*/
	move.b (%a2),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #SelectFont,%a0 /*JANE2*/
	jsr (%a0)
	move.b %d0,(%a2)
	bra .L665
	.even
.L667:
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	lea InPhoneLookup@END.w(%a5),%a2 /*FRED7b*/
	move.b #1,(%a2)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA2EF
#NO_APP
	clr.b (%a2)
	bra .L665
	.even
.L668:
	clr.b %d3
.L665:
	move.w %d3,%d0
	and.w #1,%d0
	move.l -8(%a6),%d3
	move.l -4(%a6),%a2
	unlk %a6
	rts
	.even
NoteViewScroll:
	link.w %a6,#0
	movm.l #0x1c00,-(%sp)
	move.w 8(%a6),%d4
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA2E8
#NO_APP
	move.w %d0,%d5
	addq.l #6,%sp
	tst.w %d4
	jbge .L671
	clr.b -(%sp)
	move.w %d4,%d0
	neg.w %d0
	move.w %d0,-(%sp)
	bra .L677
	.even
.L671:
	tst.w %d4
	jble .L672
	move.b #1,-(%sp)
	move.w %d4,-(%sp)
.L677:
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA14C
#NO_APP
	addq.l #8,%sp
.L672:
	tst.w %d5
	jbeq .L676
	tst.w %d4
	jblt .L675
.L676:
	tst.b 10(%a6)
	jbeq .L674
.L675:
	bsr.w NoteViewUpdateScrollBar
.L674:
	movm.l -12(%a6),#0x38
	unlk %a6
	rts
	.even
NoteViewPageScroll:
	link.w %a6,#0
	move.l %d4,-(%sp)
	move.l %d3,-(%sp)
	move.b 8(%a6),%d4
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	move.b %d4,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA14D
#NO_APP
	addq.l #8,%sp
	tst.b %d0
	jbeq .L679
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA14E
#NO_APP
	subq.w #1,%d0
	addq.l #4,%sp
	tst.b %d4
	jbne .L680
	neg.w %d0
.L680:
	move.b #1,-(%sp)
	move.w %d0,-(%sp)
	bsr.w NoteViewScroll
.L679:
	move.l -8(%a6),%d3
	move.l -4(%a6),%d4
	unlk %a6
	rts
	.even
NoteViewInit:
	link.w %a6,#-4
	move.l %d3,-(%sp)
	bsr.w NoteViewLoadRecord
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	pea -2(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA14F
#NO_APP
	or.b #4,-1(%a6)
	pea -2(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA150
#NO_APP
	move.l -8(%a6),%d3
	unlk %a6
	rts
	.even
.globl NoteViewHandleEvent
NoteViewHandleEvent:
	link.w %a6,#0
	movm.l #0x1830,-(%sp)
	move.l 8(%a6),%a3
	clr.b %d3
	move.w (%a3),%d0
	cmp.w #4,%d0
	jbne .L683
	move.w 12(%a3),%d1
	btst #3,%d1
	jbeq .L684
	move.w 8(%a3),%d2
	move.w %d2,%d0
	add.w #-512,%d0
	cmp.w #255,%d0
	jbls .L685
	cmp.w #267,%d2
	jbne .L684
.L685:
	move.l __text__sec002@END.w(%a5),%a0
	add.l #NoteViewSave,%a0 /*JANE2*/
	jsr (%a0)
	move.l __text__@END.w(%a5),%a2
	jsr ClearEditState(%a2)
	pea Date@END.w(%a5)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	jsr EDateSecondsToDate(%a2)
	bra .L716
	.even
.L684:
	and.w #1544,%d1
	jbeq .L691
	move.w 8(%a3),%d0
	cmp.w #11,%d0
	jbne .L688
	clr.b -(%sp)
	bra .L717
	.even
.L688:
	cmp.w #12,%d0
	jbne .L691
	move.b #1,-(%sp)
.L717:
	bsr.w NoteViewPageScroll
.L723:
	moveq.l #1,%d3
	bra .L691
	.even
.L683:
	cmp.w #9,%d0
	jbne .L692
	move.w 8(%a3),%d0
	cmp.w #10902,%d0
	jbeq .L694
	cmp.w #10905,%d0
	jbeq .L695
	bra .L691
	.even
.L694:
	move.l __text__sec002@END.w(%a5),%a0
	add.l #NoteViewSave,%a0 /*JANE2*/
	jsr (%a0)
.L716:
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L723
	.even
.L695:
	bsr.w NoteViewDeleteNote
	tst.b %d0
	jbeq .L723
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L723
	.even
.L692:
	cmp.w #21,%d0
	jbne .L700
	move.w 8(%a3),-(%sp)
	bsr.w NoteViewDoCommand
	move.b %d0,%d3
	bra .L691
	.even
.L700:
	cmp.w #24,%d0
	jbne .L702
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%a2
	move.l %a2,-(%sp)
	bsr.w NoteViewInit
	bra .L718
	.even
.L702:
	cmp.w #25,%d0
	jbne .L704
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%a2
	lea CurrentRecord@END.w(%a5),%a0 /*FRED7b*/
	move.w 10(%a3),(%a0)
	move.w (%a0),-(%sp)
	bsr.w SetDateToNextOccurrence
	move.l %a2,-(%sp)
	bsr.w NoteViewInit
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d4
	move.w 12(%a3),%d3
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA147
#NO_APP
	move.w %d3,%d0
	add.w 14(%a3),%d0
	move.w %d0,-(%sp)
	move.w %d3,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA142
#NO_APP
.L718:
	move.l %a2,-(%sp)
	bsr.w NoteViewDrawTitleAndForm
	bsr.w NoteViewUpdateScrollBar
	move.w #10901,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA179
#NO_APP
	bra .L723
	.even
.L704:
	cmp.w #17,%d0
	jbne .L706
#APP
	trap #15; dc.w 0xA173
#NO_APP
	bsr.w NoteViewUpdateScrollBar
	bra .L723
	.even
.L706:
	cmp.w #26,%d0
	jbne .L708
	btst #0,10(%a3)
	jbeq .L709
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.b NoteFont@END.w(%a5),-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA141
#NO_APP
	bsr.w NoteViewUpdateScrollBar
	bra .L723
	.even
.L709:
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
	bsr.w NoteViewDrawTitleAndForm
	bra .L723
	.even
.L708:
	cmp.w #28,%d0
	jbne .L712
	move.w #10901,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA153
#NO_APP
	addq.l #6,%sp
	cmp.w #0,%a0
	jbeq .L691
	move.l __text__sec002@END.w(%a5),%a0
	add.l #NoteViewSave,%a0 /*JANE2*/
	jsr (%a0)
	bra .L691
	.even
.L712:
	cmp.w #34,%d0
	jbne .L691
	clr.b -(%sp)
	move.w 16(%a3),%d0
	sub.w 14(%a3),%d0
	move.w %d0,-(%sp)
	bsr.w NoteViewScroll
.L691:
	clr.w %d0
	move.b %d3,%d0
	movm.l -16(%a6),#0xc18
	unlk %a6
	rts
.data
gDaySkipRestoreEditState:
	.byte 0
.section	sec001,"x"
	.even
DayViewRestoreEditState:
	link.w %a6,#-8
	movm.l #0x1030,-(%sp)
	lea gDaySkipRestoreEditState@END.w(%a5),%a0 /*FRED7b*/
	tst.b (%a0)
	jbeq .L725
	clr.b (%a0)
	bra .L724
	.even
.L725:
	tst.b ItemSelected@END.w(%a5)
	jbeq .L724
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a3
	jsr GetObjectPtr(%a3)
	move.l %a0,%a2
	clr.l -(%sp)
	pea -4(%a6)
	clr.l -(%sp)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	pea -6(%a6)
	move.l -4(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1F2
#NO_APP
	lea (32,%sp),%sp
	tst.b %d0
	jbeq .L728
	move.w -6(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA3D2
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbeq .L727
.L728:
	jsr ClearEditState(%a3)
	bra .L724
	.even
.L727:
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	move.w #1020,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA179
#NO_APP
	move.w #2,-(%sp)
	move.w -6(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1ED
#NO_APP
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1E9
#NO_APP
	move.l %a0,%d3
	lea DayEditPosition@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA146
#NO_APP
	lea (30,%sp),%sp
	move.w DayEditSelectionLength@END.w(%a5),%d1
	jbeq .L729
	move.w (%a2),%d0
	add.w %d0,%d1
	move.w %d1,-(%sp)
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA142
#NO_APP
	addq.l #8,%sp
.L729:
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA143
#NO_APP
.L724:
	movm.l -20(%a6),#0xc08
	unlk %a6
	rts
.LC14:
	.ascii "Wrong record\0"
	.even
DayViewClearEditState:
	link.w %a6,#-8
	movm.l #0x1820,-(%sp)
	tst.b ItemSelected@END.w(%a5)
	jbne .L731
	move.w #-1,CurrentRecord@END.w(%a5)
	clr.w %d0
	bra .L738
	.even
.L731:
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA17E
#NO_APP
	move.l %a0,%d3
	move.w #1020,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	move.l %a0,%d4
	clr.l -(%sp)
	pea -4(%a6)
	clr.l -(%sp)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	lea (32,%sp),%sp
	pea -6(%a6)
	move.l -4(%a6),-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1F2
#NO_APP
	lea (12,%sp),%sp
	tst.b %d0
	jbne .L733
	lea .LC14(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #4655,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L733:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.l %d4,-(%sp)
	bsr.w DayRestoreEditStyle
	move.l __text__@END.w(%a5),%a0
	add.l #ClearEditState,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #8,%sp
	tst.b %d0
	jbne .L737
	move.w -6(%a6),-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	clr.w %d0
	bra .L738
	.even
.L737:
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d3
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	cmp.w (%a2),%d3
	sne %d0
	ext.w %d0
	neg.w %d0
.L738:
	movm.l -20(%a6),#0x418
	unlk %a6
	rts
.LC15:
	.ascii "Record not on day\0"
	.even
DayViewFindAppointment:
	link.w %a6,#0
	move.l %d4,-(%sp)
	move.l %d3,-(%sp)
	move.w 8(%a6),%d4
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.w %d3
	addq.l #4,%sp
	lea NumAppts@END.w(%a5),%a1 /*FRED7b*/
	cmp.w (%a1),%d3
	jbcc .L741
	.even
.L743:
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	cmp.w 4(%a0,%d0.l),%d4
	jbeq .L748
	addq.w #1,%d3
	cmp.w (%a1),%d3
	jbcs .L743
.L741:
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	addq.l #4,%sp
	cmp.w #99,NumApptsOnly@END.w(%a5)
	jbhi .L746
	lea .LC15(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #4741,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	bra .L749
	.even
.L748:
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	move.w %d3,%d0
	bra .L747
	.even
.L746:
	move.l __text__@END.w(%a5),%a0
	add.l #ClearEditState,%a0 /*JANE2*/
	jsr (%a0)
.L749:
	clr.w %d0
.L747:
	move.l -8(%a6),%d3
	move.l -4(%a6),%d4
	unlk %a6
	rts
	.even
DayViewSetTopAppointment:
	link.w %a6,#-16
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	lea TopVisibleAppt@END.w(%a5),%a2 /*FRED7b*/
	clr.w (%a2)
	tst.w gDayTodoCount@END.w(%a5)
	jbne .L750
	tst.w gDayBdayCount@END.w(%a5)
	jbne .L750
	pea -16(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ETimSecondsToDateTime,%a0 /*JANE2*/
	jsr (%a0)
	move.w -6(%a6),%d0
	add.w #-1904,%d0
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	addq.l #8,%sp
	cmp.w (%a0),%d0
	jbne .L750
	clr.w %d0
	move.b 2(%a0),%d0
	cmp.w -8(%a6),%d0
	jbne .L750
	clr.w %d0
	move.b 3(%a0),%d0
	cmp.w -10(%a6),%d0
	jbne .L750
	move.b -11(%a6),-2(%a6)
	move.b -13(%a6),-1(%a6)
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.w %d2
	addq.l #4,%sp
	lea NumAppts@END.w(%a5),%a1 /*FRED7b*/
	cmp.w (%a1),%d2
	jbcc .L756
	move.w -2(%a6),%d3
	.even
.L758:
	clr.l %d1
	move.w %d2,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	cmp.w 2(%a0,%d0.l),%d3
	jble .L757
	move.w %d2,(%a2)
.L757:
	addq.w #1,%d2
	cmp.w (%a1),%d2
	jbcs .L758
.L756:
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
.L750:
	move.l -24(%a6),%d3
	move.l -20(%a6),%a2
	unlk %a6
	rts
	.even
DayDrawTodoBox:
	link.w %a6,#-8
	move.w 10(%a6),%d0
	move.w 8(%a6),-8(%a6)
	move.w %d0,-6(%a6)
	clr.l %d0
	move.w -2(%a6),%d0
	or.l #327680,%d0
	move.l %d0,-4(%a6)
	move.w #5,-2(%a6)
	pea -8(%a6)
	move.w #1,-(%sp)
#APP
	trap #15; dc.w 0xA21B
#NO_APP
	unlk %a6
	rts
	.even
DayViewDrawTime:
	link.w %a6,#-4
	movm.l #0x1030,-(%sp)
	move.l 16(%a6),%a3
	move.w 12(%a6),-(%sp)
	move.l 8(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d3
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	and.l #0xFFFF,%d3
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.b (%a0,%d0.l),-2(%a6)
	move.b 1(%a0,%d0.l),-1(%a6)
	move.w 4(%a0,%d0.l),%d3
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w %d3,%d0
	add.w #4096,%d0
	lea (14,%sp),%sp
	cmp.w #255,%d0
	jbhi .L763
	cmp.w gDayTodoCount@END.w(%a5),%d0
	jbcc .L764
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	and.l #0xFFFF,%d0
	tst.b (%a0,%d0.l)
	jbeq .L764
	move.l %a3,-(%sp)
	move.b #2,-(%sp)
	clr.b -(%sp)
	move.b TimeFormat@END.w(%a5),-(%sp)
	lea gDayTodoStart@END.w(%a5),%a0 /*FRED7b*/
	add.l %d0,%d0 /*JANE2*/
	subq.l #2,%sp
	move.b (%a0,%d0.l),(%sp)
	move.b 1(%a0,%d0.l),1(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #DrawTime,%a0 /*JANE2*/
	jsr (%a0)
	bra .L762
	.even
.L764:
	move.w 2(%a3),%d0
	addq.w #3,%d0
	move.w %d0,-(%sp)
	move.w (%a3),%d0
	add.w 4(%a3),%d0
	add.w #-13,%d0
	move.w %d0,-(%sp)
	bsr.w DayDrawTodoBox
	bra .L762
	.even
.L763:
	move.l %a3,-(%sp)
	move.b #2,-(%sp)
	clr.b -(%sp)
	move.b TimeFormat@END.w(%a5),-(%sp)
	subq.l #2,%sp
	move.b -2(%a6),(%sp)
	move.b -1(%a6),1(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #DrawTime,%a0 /*JANE2*/
	jsr (%a0)
.L762:
	movm.l -16(%a6),#0xc08
	unlk %a6
	rts
	.even
DayViewDrawIcons:
	link.w %a6,#-32
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d6
	move.w 12(%a6),%d5
	move.l 16(%a6),%a3
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d3
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	and.l #0xFFFF,%d3
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%d3
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w %d3,%d4
	add.w #4096,%d4
	lea (14,%sp),%sp
	cmp.w #255,%d4
	jbhi .L767
	clr.l %d3
	move.w %d4,%d3
	move.l %d3,%d0
	lsl.l #6,%d0
	lea gDayTodoDesc@END.w(%a5),%a0 /*FRED7b*/
	move.l %d0,%d7
	add.l %a0,%d7 /*JANE2*/
	move.w (%a3),%a2
	clr.w %d6
	move.b #3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d5
	lea gDayTodoCount@END.w(%a5),%a4 /*FRED7b*/
	addq.l #2,%sp
	cmp.w (%a4),%d4
	jbcc .L768
	lea gDayTodoNote@END.w(%a5),%a0 /*FRED7b*/
	tst.b (%a0,%d3.l)
	jbeq .L768
	move.b #11,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	move.w %d0,%d6
	addq.w #1,%d6
	addq.l #2,%sp
.L768:
	cmp.w (%a4),%d4
	jbcc .L769
	lea gDayTodoRepeat@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbeq .L769
	move.b #21,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	addq.w #1,%d6
	add.w %d0,%d6
	addq.l #2,%sp
.L769:
	move.b %d5,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	addq.l #2,%sp
	cmp.w (%a4),%d4
	jbcc .L770
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbeq .L770
	move.w 2(%a3),%d0
	addq.w #3,%d0
	move.w %d0,-(%sp)
	move.w (%a3),%d0
	addq.w #1,%d0
	move.w %d0,-(%sp)
	bsr.w DayDrawTodoBox
	move.w (%a3),%a2
	lea (9,%a2),%a2 /*FRED4a*/
	addq.l #4,%sp
.L770:
	move.b ApptDescFont@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d5
	move.w (%a3),%d0
	add.w 4(%a3),%d0
	sub.w %d6,%d0
	sub.w %a2,%d0
	move.w %d0,-(%sp)
	move.w 2(%a3),-(%sp)
	move.w %a2,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	addq.l #4,%sp
	move.w %d0,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA351
#NO_APP
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d7
	move.b %d5,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.w (%a3),%d3
	add.w 4(%a3),%d3
	sub.w %d6,%d3
	move.w 2(%a3),%a2
	move.b #3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d5
	lea (18,%sp),%sp
	cmp.w (%a4),%d4
	jbcc .L771
	lea gDayTodoNote@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbeq .L771
	move.b #11,-27(%a6)
	move.w %a2,-(%sp)
	move.w %d3,-(%sp)
	move.w #1,-(%sp)
	pea -27(%a6)
#APP
	trap #15; dc.w 0xA220
#NO_APP
	move.b -27(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	addq.w #1,%d3
	add.w %d0,%d3
	lea (12,%sp),%sp
.L771:
	cmp.w (%a4),%d4
	jbcc .L772
	lea gDayTodoRepeat@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbeq .L772
	move.b #21,-27(%a6)
	move.w %a2,-(%sp)
	move.w %d3,-(%sp)
	move.w #1,-(%sp)
	pea -27(%a6)
#APP
	trap #15; dc.w 0xA220
#NO_APP
	lea (10,%sp),%sp
.L772:
	move.b %d5,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.w 2(%a3),%a2
	add.w %d7,%a2
	subq.w #1,%a2
	move.w %a2,-(%sp)
	move.w (%a3),%d0
	add.w 4(%a3),%d0
	subq.w #1,%d0
	tst.w %d6
	jbeq .L774
	subq.w #1,%d0
	sub.w %d6,%d0
.L774:
	move.w %d0,-(%sp)
	move.w %a2,-(%sp)
	move.w (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA214
#NO_APP
	bra .L766
	.even
.L767:
	cmp.w #61695,%d3
	jbls .L775
	cmp.w #65535,%d3
	jbeq .L775
	move.w %d3,%d0
	add.w #3840,%d0
	cmp.w gDayBdayCount@END.w(%a5),%d0
	jbcc .L776
	and.l #0xFFFF,%d0
	lsl.l #6,%d0
	lea gDayBdayLines@END.w(%a5),%a0 /*FRED7b*/
	lea (%a0,%d0.l),%a2
	bra .L777
	.even
.L776:
	lea .LC3(%pc),%a2 /*SPECIAL_FRED*/
.L777:
	move.b ApptDescFont@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d5
	move.w 4(%a3),-(%sp)
	move.w 2(%a3),-(%sp)
	move.w (%a3),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	addq.l #4,%sp
	move.w %d0,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA351
#NO_APP
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d3
	move.b %d5,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.w 2(%a3),%a2
	add.w %d3,%a2
	subq.w #1,%a2
	move.w %a2,-(%sp)
	move.w (%a3),%d1
	move.w %d1,%d0
	add.w 4(%a3),%d0
	subq.w #1,%d0
	move.w %d0,-(%sp)
	move.w %a2,-(%sp)
	move.w %d1,-(%sp)
#APP
	trap #15; dc.w 0xA214
#NO_APP
	bra .L766
	.even
.L775:
	pea -32(%a6)
	pea -26(%a6)
	move.w %d3,-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w 14(%a6),-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1CF
#NO_APP
	move.w (%a3),%d3
	add.w 4(%a3),%d3
	sub.w %d0,%d3
	move.w 2(%a3),%a2
	move.b #3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d5
	lea (24,%sp),%sp
	tst.l -6(%a6)
	jbeq .L778
	move.b #11,-27(%a6)
	move.w %a2,-(%sp)
	move.w %d3,-(%sp)
	move.w #1,-(%sp)
	pea -27(%a6)
#APP
	trap #15; dc.w 0xA220
#NO_APP
	move.b -27(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	addq.w #1,%d3
	add.w %d0,%d3
	lea (12,%sp),%sp
.L778:
	tst.l -18(%a6)
	jbeq .L779
	move.b #21,-27(%a6)
	move.w %a2,-(%sp)
	move.w %d3,-(%sp)
	move.w #1,-(%sp)
	pea -27(%a6)
#APP
	trap #15; dc.w 0xA220
#NO_APP
	move.b -27(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	lea (12,%sp),%sp
.L779:
	move.b %d5,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.l -32(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
.L766:
	movm.l -64(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayViewCheckForConflicts:
	link.w %a6,#-20
	movm.l #0x1e20,-(%sp)
	clr.b -(%sp)
	pea 10.w
	lea (-10,%a6),%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	moveq.l #1,%d6
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.w %d3
	lea (14,%sp),%sp
	lea NumAppts@END.w(%a5),%a1 /*FRED7b*/
	cmp.w (%a1),%d3
	jbcc .L782
	.even
.L784:
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	cmp.w #65535,4(%a0,%d0.l)
	jbeq .L783
	cmp.w #-1,(%a0,%d0.l)
	jbeq .L783
	clr.w %d2
	.even
.L791:
	clr.l %d0
	move.w %d3,%d0
	move.l %d0,%d1
	lsl.l #2,%d1
	add.l %d0,%d1 /*JANE2*/
	add.l %d0,%d1 /*JANE2*/
	clr.l %d0
	move.w %d2,%d0
	add.l %d0,%d0 /*JANE2*/
	move.w -10(%a6,%d0.l),%d4
	cmp.w (%a0,%d1.l),%d4
	jbgt .L790
	move.b 2(%a0,%d1.l),(%a2,%d0.l)
	move.b 3(%a0,%d1.l),1(%a2,%d0.l)
	move.w %d2,%d0
	addq.w #1,%d0
	cmp.w %d0,%d6
	jbcc .L783
	move.w %d0,%d6
	bra .L783
	.even
.L790:
	addq.w #1,%d2
	cmp.w #4,%d2
	jbls .L791
.L783:
	addq.w #1,%d3
	cmp.w (%a1),%d3
	jbcs .L784
.L782:
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	addq.l #4,%sp
	cmp.w #1,%d6
	jbne .L796
	tst.b ShowTimeBars@END.w(%a5)
	jbne .L796
	clr.w %d6
.L796:
	lea TimeBarColumns@END.w(%a5),%a0 /*FRED7b*/
	cmp.w (%a0),%d6
	jbeq .L797
	move.w %d6,(%a0)
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d4
	move.w %d6,%d3
	add.w %d3,%d3
	move.w %d3,-(%sp)
	clr.w -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1EC
#NO_APP
	pea -18(%a6)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1DA
#NO_APP
	move.w #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1DC
#NO_APP
	move.w %d0,%d5
	move.w #1,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1F1
#NO_APP
	move.w -14(%a6),%d1
	sub.w %d3,%d1
	sub.w %d5,%d1
	sub.w %d0,%d1
	move.w %d1,-(%sp)
	move.w #2,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1EC
#NO_APP
	lea (34,%sp),%sp
	move.l %d4,(%sp)
#APP
	trap #15; dc.w 0xA1E0
#NO_APP
.L797:
	move.w %d6,%d0
	movm.l -40(%a6),#0x478
	unlk %a6
	rts
	.even
DayViewDrawTimeBars:
	link.w %a6,#-68
	movm.l #0x1f38,-(%sp)
	tst.w TimeBarColumns@END.w(%a5)
	jbeq .L798
	clr.b -(%sp)
	pea 10.w
	lea (-20,%a6),%a0
	move.l %a0,-62(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.w #1,-50(%a6)
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA17E
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA198
#NO_APP
	lea (16,%sp),%sp
	tst.b %d0
	jbeq .L798
	move.w #1020,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	move.l %a0,-56(%a6)
	pea -28(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1DA
#NO_APP
	clr.w %d7
	lea (20,%sp),%sp
	.even
.L804:
	move.w %d7,%a0
	add.l %a0,%a0 /*JANE2*/
	lea (-10,%a6),%a1
	move.l %a1,-66(%a6)
	move.w -26(%a6),(%a0,%a1.l)
	addq.w #1,%d7
	cmp.w #4,%d7
	jble .L804
#APP
	trap #15; dc.w 0xA395
#NO_APP
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,-52(%a6)
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a3
	clr.w -48(%a6)
	addq.l #8,%sp
	move.w -48(%a6),%d0
	cmp.w NumAppts@END.w(%a5),%d0
	jbcc .L807
	.even
.L809:
	move.w -48(%a6),%a2
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	add.l %a2,%d0 /*JANE2*/
	move.w 4(%a3,%d0.l),%d1
	cmp.w #65535,%d1
	jbeq .L808
	add.w #4096,%d1
	cmp.w #255,%d1
	jbls .L808
	cmp.w #-1,(%a3,%d0.l)
	jbeq .L808
	clr.w %d7
	.even
.L818:
	tst.w %d7
	jbne .L819
	move.b #2,-(%sp)
	bra .L851
	.even
.L819:
	move.b #29,-(%sp)
.L851:
#APP
	trap #15; dc.w 0xA3A4
#NO_APP
	move.b %d0,-(%sp)
#APP
	trap #15; dc.w 0xA398
#NO_APP
	addq.l #4,%sp
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	add.l %a2,%d0 /*JANE2*/
	move.w %d7,%a4
	move.l %a4,%d1
	add.l %a4,%d1 /*JANE2*/
	move.w -20(%a6,%d1.l),%d2
	cmp.w (%a3,%d0.l),%d2
	jbgt .L817
	move.l -62(%a6),%a0
	move.b 2(%a3,%d0.l),(%a0,%d1.l)
	move.b 3(%a3,%d0.l),1(%a0,%d1.l)
	pea -30(%a6)
	move.w -48(%a6),-(%sp)
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1EF
#NO_APP
	lea (10,%sp),%sp
	tst.b %d0
	jbeq .L822
	pea -38(%a6)
	move.w #2,-(%sp)
	move.w -30(%a6),-(%sp)
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1CD
#NO_APP
	move.w -52(%a6),%d0
	asr.w #1,%d0
	move.w -36(%a6),%d6
	add.w %d0,%d6
	move.b #1,-57(%a6)
	lea (12,%sp),%sp
	bra .L823
	.even
.L822:
	move.w -48(%a6),%d0
	cmp.w TopVisibleAppt@END.w(%a5),%d0
	jbcc .L808
	move.w -26(%a6),%d6
	clr.b -57(%a6)
.L823:
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	move.l %d0,%d1
	add.l %a2,%d1 /*JANE2*/
	move.w 2(%a3,%d1.l),%d2
	cmp.w (%a3,%d1.l),%d2
	jbeq .L808
	move.w -48(%a6),%d3
	addq.w #1,%d3
	cmp.w NumAppts@END.w(%a5),%d3
	jbcc .L828
	move.w %d3,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	add.l %a0,%d0 /*JANE2*/
	move.w (%a3,%d0.l),%d0
	cmp.w %d2,%d0
	jbge .L828
	.even
.L829:
	addq.w #1,%d3
	cmp.w NumAppts@END.w(%a5),%d3
	jbcc .L828
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	add.l %a2,%d0 /*JANE2*/
	move.w %d3,%a0
	move.l %a0,%d1
	lsl.l #2,%d1
	add.l %a0,%d1 /*JANE2*/
	add.l %a0,%d1 /*JANE2*/
	move.w (%a3,%d1.l),%d1
	cmp.w 2(%a3,%d0.l),%d1
	jblt .L829
.L828:
	pea -30(%a6)
	move.w %d3,-(%sp)
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1EF
#NO_APP
	lea (10,%sp),%sp
	tst.b %d0
	jbeq .L833
	pea -38(%a6)
	move.w #2,-(%sp)
	move.w -30(%a6),-(%sp)
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1CD
#NO_APP
	move.w -52(%a6),%d0
	asr.w #1,%d0
	move.w -36(%a6),%d4
	add.w %d0,%d4
	move.b #1,-58(%a6)
	lea (12,%sp),%sp
	bra .L834
	.even
.L833:
	cmp.w TopVisibleAppt@END.w(%a5),%d3
	jbcs .L808
	move.w -26(%a6),%d4
	add.w -22(%a6),%d4
	subq.w #1,%d4
	clr.b -58(%a6)
.L834:
	move.w %d7,%d0
	add.w %d0,%d0
	move.w -28(%a6),%a2
	add.w %d0,%a2
	move.l %a4,%d3
	add.l %a4,%d3 /*JANE2*/
	move.l -66(%a6),%a0
	cmp.w (%a0,%d3.l),%d6
	jble .L837
	move.w %a2,-46(%a6)
	move.w (%a0,%d3.l),-44(%a6)
	clr.l %d0
	move.w -40(%a6),%d0
	bset #17,%d0
	move.l %d0,-42(%a6)
	move.w %d6,%a1
	sub.w (%a0,%d3.l),%a1
	move.w %a1,-40(%a6)
	clr.w -(%sp)
	pea -46(%a6)
#APP
	trap #15; dc.w 0xA219
#NO_APP
	addq.l #6,%sp
.L837:
	move.w %d4,%a1
	addq.w #1,%a1
	move.l -66(%a6),%a0
	move.w %a1,(%a0,%d3.l)
	move.w %d4,%d0
	subq.w #2,%d0
	move.w %d0,-(%sp)
	move.w %a2,%d5
	addq.w #1,%d5
	move.w %d5,-(%sp)
	move.w %d6,%d3
	addq.w #1,%d3
	move.w %d3,-(%sp)
	move.w %d5,-(%sp)
#APP
	trap #15; dc.w 0xA215
#NO_APP
	addq.l #8,%sp
	tst.b -57(%a6)
	jbeq .L838
	move.w %d3,%d6
.L838:
	tst.b -58(%a6)
	jbeq .L839
	subq.w #1,%d4
.L839:
	move.w %d4,-(%sp)
	move.w %a2,-(%sp)
	move.w %d6,-(%sp)
	move.w %a2,-(%sp)
#APP
	trap #15; dc.w 0xA213
#NO_APP
	addq.l #8,%sp
	tst.b -57(%a6)
	jbeq .L840
	move.w %d6,-(%sp)
	move.w %d5,-(%sp)
	move.w %d6,-(%sp)
	move.w %a2,-(%sp)
#APP
	trap #15; dc.w 0xA213
#NO_APP
	addq.l #8,%sp
.L840:
	tst.b -58(%a6)
	jbeq .L841
	move.w %d4,-(%sp)
	move.w %d5,-(%sp)
	move.w %d4,-(%sp)
	move.w %a2,-(%sp)
#APP
	trap #15; dc.w 0xA213
#NO_APP
	addq.l #8,%sp
.L841:
	move.w %d7,%d0
	addq.w #1,%d0
	cmp.w -50(%a6),%d0
	jble .L808
	move.w %d0,-50(%a6)
	bra .L808
	.even
.L817:
	addq.w #1,%d7
	cmp.w #4,%d7
	jble .L818
.L808:
	addq.w #1,-48(%a6)
	move.w -48(%a6),%d2
	cmp.w NumAppts@END.w(%a5),%d2
	jbcs .L809
.L807:
	clr.w %d7
	cmp.w -50(%a6),%d7
	jbge .L846
	.even
.L848:
	move.w -26(%a6),%d2
	add.w -22(%a6),%d2
	move.w %d7,%a0
	move.l %a0,%d1
	add.l %a0,%d1 /*JANE2*/
	lea (-10,%a6),%a1
	cmp.w (%a1,%d1.l),%d2
	jble .L847
	move.w %d7,%d0
	add.w %d0,%d0
	add.w -28(%a6),%d0
	move.w %d0,-46(%a6)
	move.w (%a1,%d1.l),-44(%a6)
	move.w #2,-42(%a6)
	sub.w (%a1,%d1.l),%d2
	move.w %d2,-40(%a6)
	clr.w -(%sp)
	pea -46(%a6)
#APP
	trap #15; dc.w 0xA219
#NO_APP
	addq.l #6,%sp
.L847:
	addq.w #1,%d7
	cmp.w -50(%a6),%d7
	jblt .L848
.L846:
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
#APP
	trap #15; dc.w 0xA396
#NO_APP
.L798:
	movm.l -100(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayViewInsertAppointment:
	link.w %a6,#-4
	movm.l #0x1820,-(%sp)
	move.w 10(%a6),%d4
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.w %d2
	addq.l #4,%sp
	lea NumAppts@END.w(%a5),%a1 /*FRED7b*/
	cmp.w (%a1),%d2
	jbcc .L854
	.even
.L856:
	clr.l %d1
	move.w %d2,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%d1
	cmp.w #57343,%d1
	jbhi .L855
	cmp.w %d1,%d4
	jbhi .L855
	addq.w #1,%d1
	move.w %d1,4(%a0,%d0.l)
.L855:
	addq.w #1,%d2
	cmp.w (%a1),%d2
	jbcs .L856
.L854:
	clr.l %d1
	move.w 8(%a6),%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w %d4,4(%a0,%d0.l)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	clr.l -(%sp)
	pea -4(%a6)
	clr.l -(%sp)
	move.w %d4,-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	move.l -4(%a6),-(%sp)
	move.w 12(%a6),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1F4
#NO_APP
	movm.l -16(%a6),#0x418
	unlk %a6
	rts
	.even
DayViewGetDescription:
	link.w %a6,#-52
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d3
	move.w 12(%a6),%d6
	move.l 18(%a6),%d7
	sub.l %a3,%a3
	move.l %d7,%a0
	clr.l (%a0)
	move.w %d6,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d5
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.l %d1
	move.w %d5,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),-44(%a6)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (14,%sp),%sp
	move.w -44(%a6),%d1
	cmp.w #57343,%d1
	jbls .L860
	cmp.w #65535,%d1
	jbeq .L894
	move.w %d1,%d0
	add.w #8192,%d0
	cmp.w #4095,%d0
	jbhi .L861
	move.l 26(%a6),-(%sp)
	move.l 22(%a6),-(%sp)
	move.l %d7,-(%sp)
	move.w %d1,-(%sp)
	move.w %d6,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayDavGetDescHandle,%a0 /*JANE2*/
	jsr (%a0)
	bra .L893
	.even
.L861:
	clr.w %d0
	bra .L893
	.even
.L860:
	cmp.w #65535,%d1
	jbne .L863
.L894:
	tst.b 16(%a6)
	jbeq .L892
	cmp.w #99,NumApptsOnly@END.w(%a5)
	jbhi .L895
	clr.b -(%sp)
	pea 26.w
	moveq.l #-26,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l -42(%a6),-34(%a6)
	move.l -38(%a6),-30(%a6)
	clr.l %d1
	move.w %d5,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.b (%a0,%d0.l),-34(%a6)
	move.b 1(%a0,%d0.l),-33(%a6)
	move.l -34(%a6),-42(%a6)
	move.l -30(%a6),-38(%a6)
	move.l -42(%a6),%d0
	move.l %d0,%d1
	clr.w %d1
	swap %d1
	lsr.w #8,%d1
	lea (14,%sp),%sp
	cmp.b #22,%d1
	jbhi .L866
	addq.b #1,%d1
	and.l #0xFF,%d1
	lsl.l #8,%d1
	and.w #255,%d0
	or.l %d1,%d0
	move.l %d0,-42(%a6)
	clr.w %d0
	swap %d0
	move.b %d0,-39(%a6)
	bra .L867
	.even
.L866:
	and.w #255,%d0
	or.w #5888,%d0
	move.l %d0,-42(%a6)
	move.b #55,-39(%a6)
.L867:
	move.w %d5,%d2
	addq.w #1,%d2
	lea NumAppts@END.w(%a5),%a1 /*FRED7b*/
	cmp.w (%a1),%d2
	jbcc .L868
	clr.l %d1
	move.w %d2,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d1
	cmp.w -40(%a6),%d1
	jbge .L868
	move.b (%a0,%d0.l),-40(%a6)
	move.b 1(%a0,%d0.l),-39(%a6)
	clr.l %d1
	move.w %d5,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.b -40(%a6),2(%a0,%d0.l)
	move.b -39(%a6),3(%a0,%d0.l)
.L868:
	move.w (%a1),%d0
	cmp.w %d2,%d0
	jbeq .L870
	jbls .L869
	clr.l %d1
	move.w %d2,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	cmp.w -40(%a6),%d0
	jble .L869
.L870:
	move.w #1,%a3
	move.w %d6,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	addq.l #6,%sp
.L869:
	tst.b ApptDescFont@END.w(%a5)
	jbeq .L871
	move.w #1,%a3
	move.w %d6,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	addq.l #6,%sp
.L871:
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l Date@END.w(%a5),-38(%a6)
	lea (-42,%a6),%a0
	move.l %a0,-26(%a6)
	lea .LC3(%pc),%a1 /*SPECIAL_FRED*/
	move.l %a1,-10(%a6)
	pea -44(%a6)
	move.l %d4,-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptNewRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d3
	lea (16,%sp),%sp
	jbeq .L872
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	move.w %d3,%d0
	bra .L893
	.even
.L872:
	move.w %d6,-(%sp)
	move.w -44(%a6),-(%sp)
	move.w %d5,-(%sp)
	bsr.w DayViewInsertAppointment
	addq.l #6,%sp
	move.w %a3,%d0
	tst.b %d0
	jbne .L863
	bsr.w DayViewDrawTimeBars
.L863:
	pea -48(%a6)
	moveq.l #-26,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l %d4,-(%sp)
	move.w -44(%a6),-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
	move.l __text__@END.w(%a5),%a3
	move.l %a3,%a4
	add.l #ApptGetRecord,%a4 /*JANE2*/
	jsr (%a4)
	lea (14,%sp),%sp
	tst.l -10(%a6)
	jbne .L874
	clr.b -(%sp)
	pea 2.w
	pea -50(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	or.b #4,-50(%a6)
	lea .LC3(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-10(%a6)
	move.w -50(%a6),-(%sp)
	move.l %d4,-(%sp)
	pea -44(%a6)
	move.l (%a2),-(%sp)
	jsr ApptChangeRecord(%a3)
	move.w %d0,%d3
	lea (24,%sp),%sp
	jbne .L893
	pea -48(%a6)
	move.l %d4,-(%sp)
	move.w -44(%a6),-(%sp)
	move.l (%a2),-(%sp)
	jsr (%a4)
	lea (14,%sp),%sp
.L874:
	move.w -44(%a6),-(%sp)
	move.l (%a2),-(%sp)
	move.l __text__sec003@END.w(%a5),%a2
	jsr ApptRecEncrypted(%a2)
	addq.l #6,%sp
	tst.b %d0
	jbeq .L876
	jsr EncKeyPresent(%a2)
	tst.b %d0
	seq %d3
	neg.b %d3
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.b %d3
	jbeq .L877
.L898:
	bsr.w DayMaskCopy
	move.l %a0,%d4
	jbeq .L895
	move.l %d4,-(%sp)
	move.w %d6,-(%sp)
	bsr.w DaySetRowCopy
	move.l %d7,%a1
	move.l %a0,(%a1)
	move.l 22(%a6),%a0
	clr.w (%a0)
	move.l 26(%a6),%a1
	move.w #7,(%a1)
	bra .L892
	.even
.L877:
	tst.b 16(%a6)
	jbeq .L879
	lea gDayEditH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%d0
	jbeq .L882
	cmp.w gDayEditRow@END.w(%a5),%d6
	jbeq .L880
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
	addq.l #4,%sp
.L882:
	move.w -44(%a6),-(%sp)
	bsr.w DayCopyDescPlain
	move.l %a0,(%a2)
	move.w %d6,gDayEditRow@END.w(%a5)
	addq.l #2,%sp
.L880:
	move.l (%a2),%d0
	jbeq .L898
	move.l %d7,%a0
	move.l %d0,(%a0)
	move.l 22(%a6),%a1
	clr.w (%a1)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA02D
#NO_APP
	move.l 26(%a6),%a0
	move.w %d0,(%a0)
	addq.l #4,%sp
	bra .L897
	.even
.L879:
	move.w -44(%a6),-(%sp)
	bsr.w DayCopyDescPlain
	move.l %a0,%d4
	addq.l #2,%sp
	jbeq .L898
	cmp.w #47,%d5
	jbhi .L887
	lea gDayLocText@END.w(%a5),%a2 /*FRED7b*/
	clr.l %d3
	move.w %d5,%d3
	lsl.l #8,%d3
	tst.b (%a2,%d3.l)
	jbeq .L887
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	pea (%a2,%d3.l)
	move.l %a0,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayBuildLocSuffix,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (16,%sp),%sp
	tst.l %d3
	jbeq .L887
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	move.l %d3,%d4
	addq.l #4,%sp
.L887:
	move.l %d4,-(%sp)
	move.w %d6,-(%sp)
	bsr.w DaySetRowCopy
	move.l %d7,%a1
	move.l %a0,(%a1)
	move.l 22(%a6),%a0
	clr.w (%a0)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA02D
#NO_APP
	move.l 26(%a6),%a1
	move.w %d0,(%a1)
	bra .L892
	.even
.L895:
	move.w #65535,%d0
	bra .L893
	.even
.L876:
	tst.b 16(%a6)
	jbne .L890
	cmp.w #47,%d5
	jbhi .L890
	lea gDayLocText@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d5,%d0
	lsl.l #8,%d0
	tst.b (%a0,%d0.l)
	jbeq .L890
	pea (%a0,%d0.l)
	move.l -10(%a6),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayBuildLocSuffix,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	addq.l #8,%sp
	jbeq .L890
	move.l %d3,-(%sp)
	move.w %d6,-(%sp)
	bsr.w DaySetRowCopy
	move.l %d7,%a1
	move.l %a0,(%a1)
	move.l 22(%a6),%a0
	clr.w (%a0)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA02D
#NO_APP
	move.l 26(%a6),%a1
	move.w %d0,(%a1)
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	bra .L892
	.even
.L890:
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.w -8(%a6),%d0
	sub.w %a0,%d0
	move.l 22(%a6),%a1
	move.w %d0,(%a1)
	move.l -10(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	addq.w #1,%d0
	move.l 26(%a6),%a0
	move.w %d0,(%a0)
	move.l %d7,%a1
	move.l -48(%a6),(%a1)
	move.l (%a1),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (16,%sp),%sp
.L897:
	tst.l 30(%a6)
	jbeq .L892
	pea -52(%a6)
	move.l 30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA14F
#NO_APP
	or.b #8,-51(%a6)
	pea -52(%a6)
	move.l 30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA150
#NO_APP
.L892:
	clr.w %d0
.L893:
	movm.l -84(%a6),#0x1cf8
	unlk %a6
	rts
.LC16:
	.ascii "Error saving appointment\0"
	.even
DayViewSaveDescription:
	link.w %a6,#-36
	movm.l #0x1e38,-(%sp)
	move.l 8(%a6),%d4
	move.w 12(%a6),%d5
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d3
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	and.l #0xFFFF,%d3
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),-30(%a6)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (14,%sp),%sp
	cmp.w #65535,-30(%a6)
	jbne .L901
	lea .LC16(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #5725,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L901:
	lea gDayEditH@END.w(%a5),%a4 /*FRED7b*/
	tst.l (%a4)
	jbeq .L905
	lea gDayEditRow@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d6
	cmp.w (%a0),%d5
	jbne .L905
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E9
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA155
#NO_APP
	move.b %d0,%d5
	addq.l #8,%sp
	jbeq .L906
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA139
#NO_APP
	move.l %a0,%d3
	clr.b -(%sp)
	pea 26.w
	lea (-26,%a6),%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	lea (14,%sp),%sp
	move.l %d3,%a0
	cmp.w #0,%a0
	jbne .L907
	lea .LC3(%pc),%a0 /*SPECIAL_FRED*/
.L907:
	move.l %a0,-10(%a6)
	clr.b -(%sp)
	pea 2.w
	pea -28(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	or.b #4,-28(%a6)
	move.w -28(%a6),-(%sp)
	move.l %a2,-(%sp)
	pea -30(%a6)
	lea ApptDB@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr ApptChangeRecord(%a2)
	move.w -30(%a6),-(%sp)
	move.l (%a3),-(%sp)
	jsr DirtyRecord(%a2)
	lea (30,%sp),%sp
.L906:
	clr.l -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA158
#NO_APP
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a4)
	move.l %d6,%a0
	move.w #-1,(%a0)
	move.b %d5,RecordDirty@END.w(%a5)
	bra .L910
	.even
.L905:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E9
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA155
#NO_APP
	move.b %d0,%d5
	addq.l #8,%sp
	jbeq .L908
	move.w -30(%a6),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #DirtyRecord,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #6,%sp
.L908:
	move.b %d5,RecordDirty@END.w(%a5)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA148
#NO_APP
	addq.l #4,%sp
	tst.w %d0
	jbeq .L909
	clr.w -(%sp)
	clr.w -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA142
#NO_APP
	clr.w -(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA147
#NO_APP
	clr.w DayEditPosition@END.w(%a5)
	clr.w DayEditSelectionLength@END.w(%a5)
	bra .L910
	.even
.L909:
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA145
#NO_APP
	lea DayEditPosition@END.w(%a5),%a2 /*FRED7b*/
	move.w %d0,(%a2)
	pea -32(%a6)
	pea -34(%a6)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA13A
#NO_APP
	move.w -34(%a6),%d1
	move.w -32(%a6),%d0
	sub.w %d1,%d0
	move.w %d0,DayEditSelectionLength@END.w(%a5)
	jbeq .L910
	move.w %d1,(%a2)
.L910:
	clr.w %d0
	movm.l -64(%a6),#0x1c78
	unlk %a6
	rts
	.even
DayViewGetDescriptionHeight:
	link.w %a6,#-36
	movm.l #0x1f30,-(%sp)
	move.w 8(%a6),%d7
	move.w 10(%a6),%d6
	move.w 12(%a6),%d5
	move.l 18(%a6),%a3
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.w %d7,%a1
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%d3
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	clr.w -34(%a6)
	move.w %d3,%d0
	add.w #8192,%d0
	addq.l #8,%sp
	cmp.w #4095,%d0
	jbhi .L914
	move.w %d5,-(%sp)
	move.w %d6,-(%sp)
	move.w %d3,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayDavCalcHeight,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d4
	move.b ApptDescFont@END.w(%a5),(%a3)
	bra .L915
	.even
.L914:
	cmp.w #65535,%d3
	jbne .L916
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d4
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	clr.b (%a3)
	bra .L915
	.even
.L916:
	cmp.w #57343,%d3
	jbhi .L918
	pea -30(%a6)
	moveq.l #-26,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l %d4,-(%sp)
	move.w %d3,-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	clr.l -(%sp)
	clr.l -(%sp)
	pea -32(%a6)
	move.w %d3,-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	lea (32,%sp),%sp
	btst #4,-31(%a6)
	jbeq .L919
	cmp.b #1,PrivateRecordVisualStatus@END.w(%a5)
	jbne .L919
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d4
	clr.b (%a3)
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	addq.l #4,%sp
	bra .L920
	.even
.L919:
	pea -34(%a6)
	move.w %d5,-(%sp)
	move.w %d6,-(%sp)
	move.l %d4,-(%sp)
	move.w %d7,-(%sp)
	move.w %d3,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayApptCalcUnmaskedHeight,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d4
	move.b ApptDescFont@END.w(%a5),(%a3)
	lea (16,%sp),%sp
.L920:
	move.l -30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	bra .L915
	.even
.L918:
	lea ApptDescFont@END.w(%a5),%a2 /*FRED7b*/
	move.b (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d4
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b (%a2),(%a3)
.L915:
	move.l 14(%a6),%a0
	move.w -34(%a6),(%a0)
	move.w %d4,%d0
	movm.l -64(%a6),#0xcf8
	unlk %a6
	rts
	.even
DayViewUpdateScrollers:
	link.w %a6,#0
	movm.l #0x1e00,-(%sp)
	move.l 8(%a6),%d5
	move.w 12(%a6),%d0
	tst.w TopVisibleAppt@END.w(%a5)
	sne %d4
	neg.b %d4
	clr.b %d6
	tst.b 14(%a6)
	jbne .L924
	addq.w #1,%d0
	cmp.w NumAppts@END.w(%a5),%d0
	jbcc .L923
.L924:
	moveq.l #1,%d6
.L923:
	move.w #1018,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,%d3
	move.w #1019,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.b %d6,-(%sp)
	move.b %d4,-(%sp)
	move.w %d0,-(%sp)
	move.w %d3,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA196
#NO_APP
	movm.l -16(%a6),#0x78
	unlk %a6
	rts
	.even
DayViewInitRow:
	link.w %a6,#0
	movm.l #0x1f20,-(%sp)
	move.l 8(%a6),%d6
	move.w 12(%a6),%d5
	clr.l %d3
	move.w 14(%a6),%d3
	move.w 16(%a6),%d4
	move.b #1,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D3
#NO_APP
	move.w %d4,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DB
#NO_APP
	move.w %d3,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DE
#NO_APP
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d7
	move.w 4(%a0,%d0.l),%d3
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	clr.b %d4
	lea (32,%sp),%sp
	cmp.w #61439,%d3
	jbls .L926
	cmp.w #65535,%d3
	sne %d4
	neg.b %d4
.L926:
	move.w %d7,-(%sp)
	move.w #1,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D0
#NO_APP
	move.l 18(%a6),-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1F4
#NO_APP
	lea (20,%sp),%sp
	tst.b %d4
	jbeq .L927
	move.b #1,-(%sp)
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	clr.w -(%sp)
	bra .L931
	.even
.L927:
	tst.w 22(%a6)
	jbne .L929
	move.b #6,-(%sp)
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	clr.w -(%sp)
	bra .L931
	.even
.L929:
	move.b #9,-(%sp)
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	move.w 22(%a6),-(%sp)
.L931:
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D0
#NO_APP
	lea (20,%sp),%sp
	move.b 24(%a6),-(%sp)
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA31F
#NO_APP
	move.w %d5,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	movm.l -24(%a6),#0x4f8
	unlk %a6
	rts
	.even
DayViewLoadTable:
	link.w %a6,#-84
	movm.l #0x1f38,-(%sp)
	clr.b -67(%a6)
	move.w #-1,gDayEditStyleRow@END.w(%a5)
	clr.w gDayEditStyleIcons@END.w(%a5)
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,-72(%a6)
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA17E
#NO_APP
	move.l %a0,-66(%a6)
	move.w #1020,-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l -66(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	move.l %a0,%d7
	pea -34(%a6)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DA
#NO_APP
	move.w -28(%a6),-52(%a6)
	move.w #2,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DC
#NO_APP
	move.w %d0,-54(%a6)
	lea CurrentRecord@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-80(%a6)
	move.w (%a0),%d0
	lea (32,%sp),%sp
	cmp.w #65535,%d0
	jbeq .L933
	move.w %d0,-(%sp)
	bsr.w DayViewFindAppointment
	move.w %d0,%d5
	lea TopVisibleAppt@END.w(%a5),%a0 /*FRED7b*/
	addq.l #2,%sp
	cmp.w (%a0),%d5
	jbcc .L933
	move.w %d5,(%a0)
.L933:
	move.w TopVisibleAppt@END.w(%a5),%d6
	move.w %d6,%d5
	sub.l %a4,%a4
	clr.w -50(%a6)
	clr.w -56(%a6)
	clr.w -58(%a6)
	lea NumAppts@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-84(%a6)
	cmp.w (%a0),%d6
	jbcc .L936
	.even
.L937:
	pea -35(%a6)
	pea -38(%a6)
	move.w -52(%a6),-(%sp)
	move.w -54(%a6),-(%sp)
	move.w %d5,-(%sp)
	bsr.w DayViewGetDescriptionHeight
	move.w %d0,-60(%a6)
	move.b -35(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d4
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.w -50(%a6),%d0
	add.w %d4,%d0
	lea (18,%sp),%sp
	cmp.w -52(%a6),%d0
	jbgt .L938
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1E6
#NO_APP
	move.b %d0,%d3
	addq.l #6,%sp
	jbeq .L939
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1EB
#NO_APP
	move.w %d0,-62(%a6)
	addq.l #6,%sp
	bra .L940
	.even
.L939:
	clr.w -62(%a6)
.L940:
	tst.b %d3
	seq %d3
	neg.b %d3
	clr.l -42(%a6)
	sub.l %a3,%a3
	clr.b -73(%a6)
	clr.w -76(%a6)
	move.w %d5,%d6
	ext.l %d6
	move.l %d6,%d0
	lsl.l #2,%d0
	add.l %d6,%d0 /*JANE2*/
	add.l %d6,%d0 /*JANE2*/
	move.l -72(%a6),%a0
	move.w 4(%a0,%d0.l),%d4
	move.w %d4,%d0
	add.w #4096,%d0
	cmp.w #255,%d0
	jbhi .L941
	lea gDayTodoUID@END.w(%a5),%a0 /*FRED7b*/
.L1011:
	and.l #0xFFFF,%d0
.L1010:
	lsl.l #2,%d0
	move.l (%a0,%d0.l),-42(%a6)
	and.w #0xFF,%d3
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1F3
#NO_APP
	addq.l #6,%sp
	cmp.l -42(%a6),%d0
	jbeq .L944
	or.w #1,%d3
	bra .L944
	.even
.L941:
	cmp.w #61695,%d4
	jbls .L945
	cmp.w #65535,%d4
	jbeq .L945
	lea gDayBdayUID@END.w(%a5),%a0 /*FRED7b*/
	add.w #3840,%d4
	clr.l %d0
	move.w %d4,%d0
	bra .L1010
	.even
.L945:
	move.w %d4,%d0
	add.w #8192,%d0
	cmp.w #4095,%d0
	jbhi .L949
	lea gDayDavUID@END.w(%a5),%a0 /*FRED7b*/
	bra .L1011
	.even
.L949:
	cmp.w #65535,%d4
	jbeq .L953
	clr.l -(%sp)
	pea -42(%a6)
	clr.l -(%sp)
	move.w %d4,-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	and.w #0xFF,%d3
	lea (18,%sp),%sp
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1F3
#NO_APP
	addq.l #6,%sp
	cmp.l -42(%a6),%d0
	jbeq .L954
	or.w #1,%d3
.L954:
	clr.l -(%sp)
	clr.l -(%sp)
	pea -44(%a6)
	move.w %d4,-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	sub.l %a3,%a3
	lea (18,%sp),%sp
	btst #4,-43(%a6)
	jbeq .L956
	cmp.b #1,PrivateRecordVisualStatus@END.w(%a5)
	seq %d0
	neg.b %d0
	move.w %d0,%a3
.L956:
	clr.b -73(%a6)
	cmp.w #47,%d5
	jbgt .L957
	lea gDayLocText@END.w(%a5),%a0 /*FRED7b*/
	lsl.l #8,%d6
	tst.b (%a0,%d6.l)
	sne %d1
	neg.b %d1
	move.b %d1,-73(%a6)
.L957:
	pea -48(%a6)
	pea -26(%a6)
	move.w %d4,-(%sp)
	move.l (%a2),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	lea (14,%sp),%sp
	tst.w %d0
	jbne .L944
	move.l -10(%a6),%a0
	cmp.w #0,%a0
	jbeq .L959
	tst.b (%a0)
	jbeq .L959
	.even
.L962:
	move.w -76(%a6),%d0
	lsl.w #5,%d0
	add.w %d0,-76(%a6)
	clr.w %d0
	move.b (%a0)+,%d0
	add.w %d0,-76(%a6)
	tst.b (%a0)
	jbne .L962
.L959:
	move.l -48(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	bra .L944
	.even
.L953:
	tst.b %d3
	jbne .L944
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1F3
#NO_APP
	addq.l #6,%sp
	tst.l %d0
	sne %d3
	and.b #1,%d3
.L944:
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA3D2
#NO_APP
	addq.l #6,%sp
	move.w %a3,%d1
	cmp.b %d1,%d0
	jbeq .L968
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	addq.l #6,%sp
.L968:
	move.w %a3,%d0
	move.b %d0,-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA3D1
#NO_APP
	addq.l #8,%sp
	moveq.l #15,%d1
	cmp.w %a4,%d1
	jblt .L969
	lea gDayRowHasLoc@END.w(%a5),%a3 /*FRED7b*/
	move.w %a4,%a2
	move.b -73(%a6),%d0
	cmp.b (%a2,%a3.l),%d0
	jbeq .L970
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	addq.l #6,%sp
.L970:
	move.b -73(%a6),(%a2,%a3.l)
	lea gDayRowDescHash@END.w(%a5),%a3 /*FRED7b*/
	move.l %a2,%d4
	add.l %a2,%d4 /*JANE2*/
	move.w -76(%a6),%d1
	cmp.w (%a3,%d4.l),%d1
	jbeq .L971
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	addq.l #6,%sp
.L971:
	move.w -76(%a6),(%a3,%d4.l)
.L969:
	tst.b %d3
	jbne .L1009
	move.w #1,-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1CF
#NO_APP
	move.w %d5,%a0
	move.l %a0,%d1
	lsl.l #2,%d1
	add.l %a0,%d1 /*JANE2*/
	add.l %a0,%d1 /*JANE2*/
	addq.l #8,%sp
	move.l -72(%a6),%a0
	cmp.w (%a0,%d1.l),%d0
	jbne .L1009
	move.w #2,-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1CF
#NO_APP
	addq.l #8,%sp
	cmp.w -38(%a6),%d0
	jbne .L1009
	move.w #2,-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA31E
#NO_APP
	addq.l #8,%sp
	cmp.b -35(%a6),%d0
	jbeq .L975
.L1009:
	move.b -35(%a6),-(%sp)
	move.w -38(%a6),-(%sp)
	move.l -42(%a6),-(%sp)
	move.w -60(%a6),-(%sp)
	move.w %d5,-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
	bsr.w DayViewInitRow
	lea (18,%sp),%sp
	bra .L976
	.even
.L975:
	move.w %d5,-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DE
#NO_APP
	addq.l #8,%sp
	move.w -62(%a6),%d0
	cmp.w -60(%a6),%d0
	jbeq .L977
	move.w -60(%a6),-(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DB
#NO_APP
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	lea (14,%sp),%sp
	bra .L976
	.even
.L977:
	move.w -58(%a6),%d1
	cmp.w -56(%a6),%d1
	jbeq .L976
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	addq.l #6,%sp
.L976:
	move.w -60(%a6),%d0
	add.w %d0,-56(%a6)
	move.w -62(%a6),%d1
	add.w %d1,-58(%a6)
	move.w %d5,%d6
	addq.w #1,%d5
	addq.w #1,%a4
.L938:
	move.w -60(%a6),%d0
	add.w %d0,-50(%a6)
	move.w -52(%a6),%d1
	cmp.w -50(%a6),%d1
	jbgt .L935
	move.l -80(%a6),%a0
	move.w (%a0),%d0
	cmp.w #65535,%d0
	jbeq .L936
	move.w %d0,-(%sp)
	bsr.w DayViewFindAppointment
	move.w %d0,%d5
	addq.l #2,%sp
	cmp.w %d5,%d6
	jbhi .L936
	jbne .L984
	lea TopVisibleAppt@END.w(%a5),%a0 /*FRED7b*/
	move.w (%a0),%d0
	cmp.w %d6,%d0
	jbeq .L936
	move.w -52(%a6),%d1
	cmp.w -50(%a6),%d1
	jbeq .L936
	move.w %d0,%d1
	addq.w #1,%d1
	move.w %d1,(%a0)
	move.w %d0,%d5
	addq.w #1,%d5
	bra .L983
	.even
.L984:
	move.w %d5,TopVisibleAppt@END.w(%a5)
.L983:
	sub.l %a4,%a4
	clr.w -50(%a6)
	clr.w -56(%a6)
	clr.w -58(%a6)
.L935:
	move.l -84(%a6),%a0
	cmp.w (%a0),%d5
	jbcs .L937
.L936:
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1D4
#NO_APP
	move.w %d0,%d3
	addq.l #4,%sp
	cmp.w %a4,%d3
	jbls .L993
	.even
.L991:
	clr.b -(%sp)
	move.w %a4,-(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1D3
#NO_APP
	addq.w #1,%a4
	addq.l #8,%sp
	cmp.w %a4,%d3
	jbhi .L991
	bra .L993
	.even
.L997:
	clr.w -(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1E2
#NO_APP
	move.w %d5,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	add.l %a0,%d0 /*JANE2*/
	move.l -72(%a6),%a0
	move.w 4(%a0,%d0.l),%d4
	sub.l %a3,%a3
	move.w %d4,%d0
	add.w #4096,%d0
	addq.l #6,%sp
	cmp.w #255,%d0
	jbhi .L998
	lea gDayTodoUID@END.w(%a5),%a0 /*FRED7b*/
	bra .L1012
	.even
.L998:
	cmp.w #61695,%d4
	jbls .L1000
	cmp.w #65535,%d4
	jbeq .L1000
	lea gDayBdayUID@END.w(%a5),%a0 /*FRED7b*/
	add.w #3840,%d4
	clr.l %d0
	move.w %d4,%d0
	bra .L1013
	.even
.L1000:
	move.w %d4,%d0
	add.w #8192,%d0
	cmp.w #4095,%d0
	jbhi .L1002
	lea gDayDavUID@END.w(%a5),%a0 /*FRED7b*/
.L1012:
	and.l #0xFFFF,%d0
.L1013:
	lsl.l #2,%d0
	move.l (%a0,%d0.l),-42(%a6)
	bra .L999
	.even
.L1002:
	cmp.w #65535,%d4
	jbeq .L1004
	clr.l -(%sp)
	pea -42(%a6)
	clr.l -(%sp)
	move.w %d4,-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	clr.l -(%sp)
	clr.l -(%sp)
	pea -44(%a6)
	move.w %d4,-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	sub.l %a3,%a3
	lea (36,%sp),%sp
	btst #4,-43(%a6)
	jbeq .L999
	cmp.b #1,PrivateRecordVisualStatus@END.w(%a5)
	seq %d0
	neg.b %d0
	move.w %d0,%a3
	bra .L999
	.even
.L1004:
	clr.l -42(%a6)
.L999:
	move.w %a3,%d1
	move.b %d1,-(%sp)
	clr.w -(%sp)
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA3D1
#NO_APP
	move.b -35(%a6),-(%sp)
	move.w -38(%a6),-(%sp)
	move.l -42(%a6),-(%sp)
	move.w -60(%a6),-(%sp)
	move.w %d5,-(%sp)
	clr.w -(%sp)
	move.l %d7,-(%sp)
	bsr.w DayViewInitRow
	move.w %d5,(%a4)
	move.b #1,-67(%a6)
	move.w %d3,-50(%a6)
	lea (26,%sp),%sp
.L993:
	move.w -52(%a6),%d0
	cmp.w -50(%a6),%d0
	jble .L994
	lea TopVisibleAppt@END.w(%a5),%a4 /*FRED7b*/
	move.w (%a4),%d5
	jbeq .L994
	subq.w #1,%d5
	pea -35(%a6)
	pea -38(%a6)
	move.w %d0,-(%sp)
	move.w -54(%a6),-(%sp)
	move.w %d5,-(%sp)
	bsr.w DayViewGetDescriptionHeight
	move.w %d0,-60(%a6)
	move.w -50(%a6),%d3
	add.w %d0,%d3
	lea (14,%sp),%sp
	cmp.w -52(%a6),%d3
	jbls .L997
.L994:
	tst.b -67(%a6)
	jbeq .L1008
	move.l %d7,-(%sp)
#APP
	trap #15; dc.w 0xA1E0
#NO_APP
	addq.l #4,%sp
.L1008:
	move.w -52(%a6),%d1
	cmp.w -50(%a6),%d1
	slt %d0
	neg.b %d0
	move.b %d0,-(%sp)
	move.w %d6,-(%sp)
	move.l -66(%a6),-(%sp)
	bsr.w DayViewUpdateScrollers
	move.l -72(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	movm.l -116(%a6),#0x1cf8
	unlk %a6
	rts
.LC17:
	.ascii "******\0"
	.even
DayInjectTodos:
	link.w %a6,#-88
	movm.l #0x1f38,-(%sp)
	clr.l -54(%a6)
	lea gDayTodoCount@END.w(%a5),%a3 /*FRED7b*/
	clr.w (%a3)
	lea ApptsH@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-70(%a6)
	tst.l (%a0)
	jbeq .L1014
	move.w #1,-(%sp)
	pea -50(%a6)
	lea __text__sec002@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,-74(%a6)
	move.l (%a1),%a2
	jsr ToDoGetDatabase(%a2)
	addq.l #6,%sp
	tst.w %d0
	jbne .L1014
	tst.l -50(%a6)
	jbeq .L1014
	move.w #1,-(%sp)
	pea -54(%a6)
	jsr TdxGetDatabase(%a2)
	addq.l #6,%sp
	tst.w %d0
	jbeq .L1018
	clr.l -54(%a6)
.L1018:
	pea -14(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	lea __text__@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-78(%a6)
	move.l %a0,%a1
	move.l (%a1),%a0
	add.l #ETimSecondsToDateTime,%a0 /*JANE2*/
	jsr (%a0)
	clr.b -65(%a6)
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.w (%a0),%d0
	add.w #1904,%d0
	addq.l #8,%sp
	cmp.w -4(%a6),%d0
	jbne .L1019
	clr.w %d0
	move.b 2(%a0),%d0
	cmp.w -6(%a6),%d0
	jbne .L1019
	clr.w %d0
	move.b 3(%a0),%d0
	cmp.w -8(%a6),%d0
	seq %d0
	neg.b %d0
	move.b %d0,-65(%a6)
.L1019:
	move.l (%a0),-(%sp)
	move.l -78(%a6),%a1
	move.l (%a1),%a0
	add.l #EDateInt,%a0 /*JANE2*/
	jsr (%a0)
	move.l %d0,-64(%a6)
	move.l -50(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA04F
#NO_APP
	move.w %d0,-60(%a6)
	clr.w %d6
	addq.l #8,%sp
	cmp.w %d6,%d0
	jbls .L1021
	cmp.w #23,(%a3)
	jbhi .L1021
	.even
.L1023:
	move.w %d6,-(%sp)
	move.l -50(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA05B
#NO_APP
	move.l %a0,-82(%a6)
	clr.b %d3
	addq.l #6,%sp
	cmp.w #0,%a0
	jbeq .L1022
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a4
	addq.l #4,%sp
	tst.b 4(%a4)
	jblt .L1026
	move.l (%a4),-(%sp)
	move.l -78(%a6),%a1
	move.l (%a1),%a0
	add.l #EDateInt,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #4,%sp
	cmp.l #65535,%d0
	jbne .L1027
	move.b -65(%a6),%d3
	bra .L1026
	.even
.L1027:
	cmp.l -64(%a6),%d0
	jbeq .L1072
	tst.b -65(%a6)
	jbeq .L1026
	cmp.l -64(%a6),%d0
	jbge .L1026
	moveq.l #1,%d3
.L1026:
	tst.b %d3
	jbeq .L1032
.L1072:
	clr.l -58(%a6)
	clr.l -(%sp)
	pea -58(%a6)
	clr.l -(%sp)
	move.w %d6,-(%sp)
	move.l -50(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	lea (18,%sp),%sp
	btst #6,4(%a4)
	jbeq .L1033
	lea (5,%a4),%a0
	move.b (%a0),%d3
	lsl.w #8,%d3
	clr.w %d0
	move.b 1(%a0),%d0
	or.w %d0,%d3
	move.b 2(%a0),%d7
	lsl.w #8,%d7
	clr.w %d0
	move.b 3(%a0),%d0
	or.w %d0,%d7
	cmp.w #63,%d3
	jbls .L1034
	moveq.l #63,%d3
.L1034:
	move.l #1000000000,-(%sp)
	moveq.l #-46,%d5
	add.l %a6,%d5 /*JANE2*/
	move.l %d5,-(%sp)
	move.l __text__sec003@END.w(%a5),-86(%a6)
	move.l -86(%a6),%a0
	jsr EncGetKey(%a0)
	addq.l #8,%sp
	tst.b %d0
	jbeq .L1035
	clr.l %d4
	move.w %d3,%d4
	move.l %d4,-(%sp)
	pea 9(%a4)
	lea gDayTodoCount@END.w(%a5),%a3 /*FRED7b*/
	clr.l %d0
	move.w (%a3),%d0
	lsl.l #6,%d0
	lea gDayTodoDesc@END.w(%a5),%a2 /*FRED7b*/
	pea (%a2,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	lea (12,%sp),%sp
	tst.w %d3
	jbeq .L1036
	clr.l -(%sp)
	move.l -58(%a6),-(%sp)
	move.l %d4,-(%sp)
	clr.l %d0
	move.w (%a3),%d0
	lsl.l #6,%d0
	pea (%a2,%d0.l)
	move.l %d5,-(%sp)
	move.l -86(%a6),%a1
	jsr EncCrypt(%a1)
	lea (20,%sp),%sp
.L1036:
	clr.l %d0
	move.w (%a3),%d0
	lsl.l #6,%d0
	add.l %d4,%d0 /*JANE2*/
	clr.b (%a2,%d0.l)
	clr.b -(%sp)
	pea 16.w
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	lea (10,%sp),%sp
	bra .L1037
	.even
.L1035:
	lea .LC17(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	clr.l %d0
	move.w gDayTodoCount@END.w(%a5),%d0
	lsl.l #6,%d0
	lea gDayTodoDesc@END.w(%a5),%a0 /*FRED7b*/
	pea (%a0,%d0.l)
#APP
	trap #15; dc.w 0xA0C5
#NO_APP
	addq.l #8,%sp
.L1037:
	lea gDayTodoNote@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d1
	move.w gDayTodoCount@END.w(%a5),%d1
	tst.w %d7
	sne %d0
	neg.b %d0
	move.b %d0,(%a0,%d1.l)
	bra .L1038
	.even
.L1033:
	move.w #63,-(%sp)
	move.l %a4,%d3
	addq.l #5,%d3
	move.l %d3,-(%sp)
	lea gDayTodoCount@END.w(%a5),%a2 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	lsl.l #6,%d0
	lea gDayTodoDesc@END.w(%a5),%a3 /*FRED7b*/
	pea (%a3,%d0.l)
#APP
	trap #15; dc.w 0xA2CE
#NO_APP
	clr.l %d0
	move.w (%a2),%d0
	lsl.l #6,%d0
	clr.b 63(%a3,%d0.l)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	move.w %d0,%a0
	lea gDayTodoNote@END.w(%a5),%a1 /*FRED7b*/
	clr.l %d1
	move.w (%a2),%d1
	lea (14,%sp),%sp
	tst.b 6(%a4,%a0.l)
	sne %d0
	neg.b %d0
	move.b %d0,(%a1,%d1.l)
.L1038:
	lea gDayTodoRec@END.w(%a5),%a0 /*FRED7b*/
	lea gDayTodoCount@END.w(%a5),%a2 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	add.l %d0,%d0 /*JANE2*/
	move.w %d6,(%a0,%d0.l)
	lea gDayTodoUID@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d1
	move.w (%a2),%d1
	lsl.l #2,%d1
	move.l -58(%a6),%d2
	move.l %d2,%d0
	and.l #16777215,%d0
	or.l #-536870912,%d0
	move.l %d0,(%a0,%d1.l)
	move.l -54(%a6),%d0
	jbeq .L1039
	pea -30(%a6)
	move.l %d2,-(%sp)
	move.l %d0,-(%sp)
	move.l -74(%a6),%a1
	move.l (%a1),%a0
	add.l #TdxGet,%a0 /*JANE2*/
	jsr (%a0)
	lea (12,%sp),%sp
	tst.b %d0
	jbeq .L1039
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	move.b -30(%a6),%d1
	move.b %d1,%d2
	and.b #1,%d2
	move.b %d2,(%a0,%d0.l)
	lea gDayTodoStart@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	add.l %d0,%d0 /*JANE2*/
	move.b -28(%a6),(%a0,%d0.l)
	move.b -27(%a6),1(%a0,%d0.l)
	lea gDayTodoRepeat@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	lsr.b #1,%d1
	and.b #1,%d1
	move.b %d1,(%a0,%d0.l)
	bra .L1040
	.even
.L1039:
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	clr.b (%a0,%d0.l)
	lea gDayTodoRepeat@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w (%a2),%d0
	clr.b (%a0,%d0.l)
.L1040:
	addq.w #1,gDayTodoCount@END.w(%a5)
.L1032:
	move.l -82(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
.L1022:
	addq.w #1,%d6
	cmp.w -60(%a6),%d6
	jbcc .L1021
	cmp.w #23,gDayTodoCount@END.w(%a5)
	jbls .L1023
.L1021:
	move.l -50(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA04A
#NO_APP
	addq.l #4,%sp
	move.l -54(%a6),%d0
	jbeq .L1042
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA04A
#NO_APP
	addq.l #4,%sp
.L1042:
	lea gDayTodoCount@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d0
	jbeq .L1014
	lea NumAppts@END.w(%a5),%a4 /*FRED7b*/
	add.w (%a4),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l -70(%a6),%a0
	move.l (%a0),-(%sp)
#APP
	trap #15; dc.w 0xA033
#NO_APP
	addq.l #8,%sp
	tst.w %d0
	jbne .L1014
	move.l -70(%a6),%a1
	move.l (%a1),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a3
	clr.w %d3
	clr.w %d5
	clr.w %d6
	addq.l #4,%sp
	cmp.w (%a2),%d6
	jbcc .L1046
	.even
.L1048:
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d6,%d0
	tst.b (%a0,%d0.l)
	jbne .L1047
	addq.w #1,%d3
.L1047:
	addq.w #1,%d6
	cmp.w (%a2),%d6
	jbcs .L1048
.L1046:
	tst.w %d3
	jbeq .L1051
	move.w (%a4),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l %a3,-(%sp)
	move.w %d3,%d0
	mulu.w #6,%d0
	pea (%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.w %d6
	lea (12,%sp),%sp
	lea gDayTodoCount@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d4
	cmp.w (%a0),%d6
	jbcc .L1053
	.even
.L1055:
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d6,%d0
	tst.b (%a0,%d0.l)
	jbne .L1054
	st -(%sp)
	pea 2.w
	move.w %d5,%d0
	mulu.w #6,%d0
	lea (%a3,%d0.l),%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	st -(%sp)
	pea 2.w
	pea 2(%a2)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	clr.l %d1
	move.w %d5,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w %d6,%a1
	lea (-4096,%a1),%a1 /*FRED4a*/
	move.w %a1,4(%a3,%d0.l)
	addq.w #1,%d5
	lea (20,%sp),%sp
.L1054:
	addq.w #1,%d6
	move.l %d4,%a0
	cmp.w (%a0),%d6
	jbcs .L1055
.L1053:
	add.w %d3,(%a4)
.L1051:
	clr.w %d6
	lea gDayTodoCount@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,%d4
	cmp.w (%a1),%d6
	jbcc .L1059
	.even
.L1061:
	lea gDayTodoTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d6,%d0
	tst.b (%a0,%d0.l)
	jbeq .L1060
	lea gDayTodoStart@END.w(%a5),%a4 /*FRED7b*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a4,%d0.l),%d2
	clr.w %d3
	lea NumAppts@END.w(%a5),%a0 /*FRED7b*/
	cmp.w (%a0),%d3
	jbcc .L1069
	move.w (%a3),%d0
	bra .L1073
	.even
.L1065:
	addq.w #1,%d3
	cmp.w (%a0),%d3
	jbcc .L1069
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w (%a3,%d0.l),%d0
.L1073:
	cmp.w #65535,%d0
	jbeq .L1065
	cmp.w %d0,%d2
	jbhi .L1065
	cmp.w NumAppts@END.w(%a5),%d3
	jbcc .L1069
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	cmp.w #65535,4(%a3,%d0.l)
	jbne .L1069
	cmp.w (%a3,%d0.l),%d2
	jbne .L1069
	move.w %d6,%d1
	add.w #-4096,%d1
	move.w %d1,4(%a3,%d0.l)
	bra .L1060
	.even
.L1069:
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d0
	sub.w %d3,%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.w %d3,%d0
	mulu.w #6,%d0
	pea (%a3,%d0.l)
	addq.w #1,%d3
	move.w %d3,%d0
	mulu.w #6,%d0
	subq.w #1,%d3
	pea (%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.l %d0
	move.w %d3,%d0
	move.l %d0,%d1
	lsl.l #2,%d1
	add.l %d0,%d1 /*JANE2*/
	add.l %d0,%d1 /*JANE2*/
	clr.l %d0
	move.w %d6,%d0
	add.l %d0,%d0 /*JANE2*/
	move.b (%a4,%d0.l),(%a3,%d1.l)
	move.b 1(%a4,%d0.l),1(%a3,%d1.l)
	move.b (%a4,%d0.l),2(%a3,%d1.l)
	move.b 1(%a4,%d0.l),3(%a3,%d1.l)
	move.w %d6,%d2
	add.w #-4096,%d2
	move.w %d2,4(%a3,%d1.l)
	addq.w #1,(%a2)
	lea (12,%sp),%sp
.L1060:
	addq.w #1,%d6
	move.l %d4,%a0
	cmp.w (%a0),%d6
	jbcs .L1061
.L1059:
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
.L1014:
	movm.l -120(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayInjectBdays:
	link.w %a6,#0
	movm.l #0x1c38,-(%sp)
	lea gDayBdayCount@END.w(%a5),%a4 /*FRED7b*/
	clr.w (%a4)
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	tst.l (%a2)
	jbeq .L1074
	move.w #12,-(%sp)
	lea gDayBdayRec@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d5
	move.l %d5,-(%sp)
	pea gDayBdayLines@END.w(%a5)
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.b 3(%a0),-(%sp)
	move.b 2(%a0),-(%sp)
	move.w (%a0),%a0
	lea (1904,%a0),%a0 /*FRED4a*/
	move.w %a0,-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #AddrBdayGet,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,(%a4)
	lea (16,%sp),%sp
	jbeq .L1074
	lea NumAppts@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,%d4
	add.w (%a1),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA033
#NO_APP
	addq.l #8,%sp
	tst.w %d0
	jbeq .L1077
	clr.w (%a4)
	bra .L1074
	.even
.L1077:
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a3
	move.l %d4,%a0
	move.w (%a0),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l %a3,-(%sp)
	move.w (%a4),%d0
	mulu.w #6,%d0
	pea (%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.w %d3
	lea (16,%sp),%sp
	cmp.w (%a4),%d3
	jbcc .L1079
	.even
.L1081:
	st -(%sp)
	pea 2.w
	move.w %d3,%d0
	mulu.w #6,%d0
	lea (%a3,%d0.l),%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	st -(%sp)
	pea 2.w
	pea 2(%a2)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d2
	lsl.l #2,%d2
	move.l %d2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w %d3,%a1
	lea (-3840,%a1),%a1 /*FRED4a*/
	move.w %a1,4(%a3,%d0.l)
	lea gDayBdayUID@END.w(%a5),%a0 /*FRED7b*/
	add.l %d1,%d1 /*JANE2*/
	move.l %d5,%a1
	clr.l %d0
	move.w (%a1,%d1.l),%d0
	or.l #-805306368,%d0
	move.l %d0,(%a0,%d2.l)
	lea (20,%sp),%sp
	addq.w #1,%d3
	cmp.w (%a4),%d3
	jbcs .L1081
.L1079:
	move.w gDayBdayCount@END.w(%a5),%d0
	move.l %d4,%a0
	add.w %d0,(%a0)
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
.L1074:
	movm.l -24(%a6),#0x1c38
	unlk %a6
	rts
.section	sec002,"x"
	.even
DayDavGetDescHandle:
	link.w %a6,#0
	movm.l #0x1e20,-(%sp)
	move.w 10(%a6),%d0
	add.w #8192,%d0
	cmp.w gDayDavCount@END.w(%a5),%d0
	jbcc .L1084
	mulu.w #192,%d0
	lea gDayDavLines@END.w(%a5),%a0 /*FRED7b*/
	move.l %d0,%d5
	add.l %a0,%d5 /*JANE2*/
	bra .L1085
	.even
.L1084:
	move.l __text__sec001@END.w(%a5),%d5
	add.l #.LC3,%d5 /*JANE2*/
.L1085:
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA0C7
#NO_APP
	move.w %d0,%d6
	clr.l %d4
	move.w %d6,%d4
	move.l %d4,%a0
	pea 1(%a0)
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l %a0,%d3
	addq.l #8,%sp
	jbeq .L1086
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	move.l %d4,-(%sp)
	move.l %d5,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.b (%a2,%d4.l)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l %d3,-(%sp)
	move.w 8(%a6),-(%sp)
	move.l __text__sec001@END.w(%a5),%a0
	add.l #DaySetRowCopy,%a0 /*JANE2*/
	jsr (%a0)
	move.l 12(%a6),%a1
	move.l %a0,(%a1)
	move.l 16(%a6),%a0
	clr.w (%a0)
	move.l 20(%a6),%a0
	addq.w #1,%d6
	move.w %d6,(%a0)
	clr.w %d0
	bra .L1087
	.even
.L1086:
	move.w #65535,%d0
.L1087:
	movm.l -20(%a6),#0x478
	unlk %a6
	rts
	.even
DayApptCalcUnmaskedHeight:
	link.w %a6,#0
	movm.l #0x1f38,-(%sp)
	move.w 10(%a6),%d3
	move.l 12(%a6),%a4
	move.w 16(%a6),%d4
	move.w 18(%a6),%d7
	clr.w %d5
	move.b #3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d6
	addq.l #2,%sp
	tst.l 20(%a4)
	jbeq .L1089
	move.b #11,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	move.w %d0,%d5
	addq.l #2,%sp
.L1089:
	tst.l 8(%a4)
	jbeq .L1090
	tst.w %d5
	jbeq .L1091
	addq.w #1,%d5
.L1091:
	move.b #21,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	add.w %d0,%d5
	addq.l #2,%sp
.L1090:
	move.b ApptDescFont@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	addq.l #2,%sp
	cmp.w #47,%d3
	jbgt .L1092
	lea gDayLocText@END.w(%a5),%a3 /*FRED7b*/
	ext.l %d3
	lsl.l #8,%d3
	tst.b (%a3,%d3.l)
	jbeq .L1092
	move.w 8(%a6),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__sec003@END.w(%a5),%a2
	jsr ApptRecEncrypted(%a2)
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1093
	jsr EncKeyPresent(%a2)
	tst.b %d0
	jbeq .L1092
.L1093:
	pea (%a3,%d3.l)
	move.l 16(%a4),-(%sp)
	bsr.w DayBuildLocSuffix
	move.l %a0,%d3
	addq.l #8,%sp
	jbeq .L1094
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	sub.w %d5,%d4
	move.w %d4,-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA152
#NO_APP
	move.w %d0,%d4
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	lea (18,%sp),%sp
	bra .L1096
	.even
.L1094:
.L1092:
	sub.w %d5,%d4
	move.w %d4,-(%sp)
	move.l 16(%a4),-(%sp)
#APP
	trap #15; dc.w 0xA152
#NO_APP
	move.w %d0,%d4
	addq.l #6,%sp
.L1096:
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d2
	move.w %d7,%d1
	ext.l %d1
	divs.w %d2,%d1
	cmp.w %d1,%d4
	jbge .L1097
	move.w %d4,%d1
.L1097:
	move.w %d1,%d4
	muls.w %d2,%d4
	move.b %d6,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.l 20(%a6),%a0
	move.w %d5,(%a0)
	move.w %d4,%d0
	movm.l -32(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayDavCalcHeight:
	link.w %a6,#0
	movm.l #0x1e00,-(%sp)
	move.w 12(%a6),%d6
	move.w 8(%a6),%d0
	add.w #8192,%d0
	cmp.w gDayDavCount@END.w(%a5),%d0
	jbcc .L1099
	mulu.w #192,%d0
	lea gDayDavLines@END.w(%a5),%a0 /*FRED7b*/
	move.l %d0,%d4
	add.l %a0,%d4 /*JANE2*/
	bra .L1100
	.even
.L1099:
	move.l __text__sec001@END.w(%a5),%d4
	add.l #.LC3,%d4 /*JANE2*/
.L1100:
	move.b ApptDescFont@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
	move.w 10(%a6),-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA152
#NO_APP
	move.w %d0,%d5
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d4
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.w %d6,%d1
	ext.l %d1
	divs.w %d4,%d1
	cmp.w %d1,%d5
	jbge .L1101
	move.w %d5,%d1
.L1101:
	move.w %d1,%d0
	muls.w %d4,%d0
	movm.l -16(%a6),#0x78
	unlk %a6
	rts
	.even
DayDavShowInfo:
	link.w %a6,#0
	move.w 8(%a6),%d0
	add.w #8192,%d0
	cmp.w gDayDavCount@END.w(%a5),%d0
	jbcc .L1103
	mulu.w #192,%d0
	lea gDayDavLines@END.w(%a5),%a0 /*FRED7b*/
	add.l %a0,%d0 /*JANE2*/
	bra .L1104
	.even
.L1103:
	move.l __text__sec001@END.w(%a5),%d0
	add.l #.LC3,%d0 /*JANE2*/
.L1104:
	clr.l -(%sp)
	clr.l -(%sp)
	move.l %d0,-(%sp)
	move.w #1371,-(%sp)
#APP
	trap #15; dc.w 0xA194
#NO_APP
	unlk %a6
	rts
	.even
DayInjectCalDav:
	link.w %a6,#0
	movm.l #0x1f38,-(%sp)
	lea gDayDavCount@END.w(%a5),%a2 /*FRED7b*/
	clr.w (%a2)
	lea ApptsH@END.w(%a5),%a3 /*FRED7b*/
	tst.l (%a3)
	jbeq .L1105
	move.w #16,-(%sp)
	lea gDayDavHash@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d3
	move.l %d3,-(%sp)
	lea gDayDavEnd@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,%d7
	move.l %d7,-(%sp)
	lea gDayDavStart@END.w(%a5),%a4 /*FRED7b*/
	move.l %a4,-(%sp)
	pea gDayDavLines@END.w(%a5)
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.b 3(%a0),-(%sp)
	move.b 2(%a0),-(%sp)
	move.w (%a0),%a0
	lea (1904,%a0),%a0 /*FRED4a*/
	move.w %a0,-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #DateDAVGet,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,(%a2)
	lea (24,%sp),%sp
	jbeq .L1105
	clr.w %d4
	cmp.w %d4,%d0
	jbls .L1109
	.even
.L1111:
	lea gDayDavTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	move.l %d0,%d1
	add.l %d0,%d1 /*JANE2*/
	cmp.b #255,(%a4,%d1.l)
	sne %d1
	neg.b %d1
	move.b %d1,(%a0,%d0.l)
	lea gDayDavUID@END.w(%a5),%a0 /*FRED7b*/
	lsl.l #2,%d0
	move.l %d3,%a1
	move.l (%a1,%d0.l),%d1
	and.l #16777215,%d1
	or.l #-1073741824,%d1
	move.l %d1,(%a0,%d0.l)
	addq.w #1,%d4
	cmp.w (%a2),%d4
	jbcs .L1111
.L1109:
	lea NumAppts@END.w(%a5),%a4 /*FRED7b*/
	lea gDayDavCount@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a4),%d0
	add.w (%a2),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA033
#NO_APP
	addq.l #8,%sp
	tst.w %d0
	jbeq .L1113
	clr.w (%a2)
	bra .L1105
	.even
.L1113:
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a3
	clr.w %d3
	clr.w %d6
	clr.w %d4
	addq.l #4,%sp
	cmp.w (%a2),%d4
	jbcc .L1115
	.even
.L1117:
	lea gDayDavTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbne .L1116
	addq.w #1,%d3
.L1116:
	addq.w #1,%d4
	cmp.w (%a2),%d4
	jbcs .L1117
.L1115:
	tst.w %d3
	jbeq .L1120
	move.w (%a4),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l %a3,-(%sp)
	move.w %d3,%d0
	mulu.w #6,%d0
	pea (%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.w %d4
	lea (12,%sp),%sp
	lea gDayDavCount@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d5
	cmp.w (%a0),%d4
	jbcc .L1122
	.even
.L1124:
	lea gDayDavTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbne .L1123
	st -(%sp)
	pea 2.w
	move.w %d6,%d0
	mulu.w #6,%d0
	lea (%a3,%d0.l),%a2
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	st -(%sp)
	pea 2.w
	pea 2(%a2)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	clr.l %d1
	move.w %d6,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w %d4,%a1
	lea (-8192,%a1),%a1 /*FRED4a*/
	move.w %a1,4(%a3,%d0.l)
	addq.w #1,%d6
	lea (20,%sp),%sp
.L1123:
	addq.w #1,%d4
	move.l %d5,%a0
	cmp.w (%a0),%d4
	jbcs .L1124
.L1122:
	add.w %d3,(%a4)
.L1120:
	clr.w %d4
	lea gDayDavCount@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,%d5
	cmp.w (%a1),%d4
	jbcc .L1128
	.even
.L1130:
	lea gDayDavTimed@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d4,%d0
	tst.b (%a0,%d0.l)
	jbeq .L1129
	lea gDayDavStart@END.w(%a5),%a4 /*FRED7b*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a4,%d0.l),%d2
	clr.w %d3
	lea NumAppts@END.w(%a5),%a0 /*FRED7b*/
	cmp.w (%a0),%d3
	jbcc .L1133
	move.w (%a3),%d0
	bra .L1139
	.even
.L1134:
	addq.w #1,%d3
	cmp.w (%a0),%d3
	jbcc .L1133
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w (%a3,%d0.l),%d0
.L1139:
	cmp.w #65535,%d0
	jbeq .L1134
	cmp.w %d0,%d2
	jbhi .L1134
.L1133:
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d0
	sub.w %d3,%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.w %d3,%d0
	mulu.w #6,%d0
	pea (%a3,%d0.l)
	addq.w #1,%d3
	move.w %d3,%d0
	mulu.w #6,%d0
	subq.w #1,%d3
	pea (%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	clr.l %d0
	move.w %d3,%d0
	move.l %d0,%d1
	lsl.l #2,%d1
	add.l %d0,%d1 /*JANE2*/
	add.l %d0,%d1 /*JANE2*/
	clr.l %d0
	move.w %d4,%d0
	add.l %d0,%d0 /*JANE2*/
	move.b (%a4,%d0.l),(%a3,%d1.l)
	move.b 1(%a4,%d0.l),1(%a3,%d1.l)
	move.l %d7,%a0
	move.b (%a0,%d0.l),2(%a3,%d1.l)
	move.b 1(%a0,%d0.l),3(%a3,%d1.l)
	move.w %d4,%a1
	lea (-8192,%a1),%a1 /*FRED4a*/
	move.w %a1,4(%a3,%d1.l)
	addq.w #1,(%a2)
	lea (12,%sp),%sp
.L1129:
	addq.w #1,%d4
	move.l %d5,%a0
	cmp.w (%a0),%d4
	jbcs .L1130
.L1128:
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
.L1105:
	movm.l -32(%a6),#0x1cf8
	unlk %a6
	rts
.section	sec001,"x"
	.even
DayInjectLocationText:
	link.w %a6,#0
	movm.l #0x1030,-(%sp)
	move.l ApptsH@END.w(%a5),%d0
	jbeq .L1140
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	clr.w %d3
	addq.l #4,%sp
	lea NumAppts@END.w(%a5),%a3 /*FRED7b*/
	cmp.w (%a3),%d3
	jbcc .L1143
	.even
.L1145:
	clr.l %d1
	move.w %d3,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	cmp.w #57343,4(%a2,%d0.l)
	jbls .L1147
	lea gDayLocText@END.w(%a5),%a0 /*FRED7b*/
	lsl.l #8,%d1
	clr.b (%a0,%d1.l)
	bra .L1144
	.even
.L1147:
	move.w #256,-(%sp)
	lsl.l #8,%d1
	lea gDayLocText@END.w(%a5),%a0 /*FRED7b*/
	pea (%a0,%d1.l)
	move.w 4(%a2,%d0.l),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #LocSummaryForRecord,%a0 /*JANE2*/
	jsr (%a0)
	lea (12,%sp),%sp
.L1144:
	addq.w #1,%d3
	cmp.w (%a3),%d3
	jbcc .L1143
	cmp.w #47,%d3
	jbls .L1145
.L1143:
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
.L1140:
	movm.l -12(%a6),#0xc08
	unlk %a6
	rts
	.even
DayLaunchTodo:
	link.w %a6,#-4
	move.l %d3,-(%sp)
	move.w 8(%a6),%d3
	cmp.w gDayTodoCount@END.w(%a5),%d3
	jbcc .L1150
	move.w #1,-(%sp)
	pea -4(%a6)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #ToDoGetDatabase,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #6,%sp
	tst.w %d0
	jbne .L1150
	move.l -4(%a6),%d1
	jbeq .L1150
	lea gDayTodoRec@END.w(%a5),%a0 /*FRED7b*/
	clr.l %d0
	move.w %d3,%d0
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),-(%sp)
	move.l %d1,-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #LaunchToDoWithRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.l -4(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA04A
#NO_APP
.L1150:
	move.l -8(%a6),%d3
	unlk %a6
	rts
	.even
DayViewLayoutDay:
	link.w %a6,#-32
	movm.l #0x1f38,-(%sp)
	tst.b 8(%a6)
	jbeq .L1155
	lea ApptsOnlyH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),%d0
	jbeq .L1156
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	addq.l #4,%sp
.L1156:
	pea NumApptsOnly@END.w(%a5)
	move.l %a2,-(%sp)
	move.w #1,-(%sp)
	move.l Date@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #ApptGetAppointments,%a0 /*JANE2*/
	jsr (%a0)
	lea (18,%sp),%sp
.L1155:
	lea ApptsH@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,-24(%a6)
	move.l (%a0),%d0
	jbeq .L1157
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	addq.l #4,%sp
.L1157:
	lea ApptsOnlyH@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,-28(%a6)
	tst.l (%a1)
	jbne .L1158
	lea NumAppts@END.w(%a5),%a4 /*FRED7b*/
	lea DayStartHour@END.w(%a5),%a2 /*FRED7b*/
	move.w DayEndHour@END.w(%a5),%d0
	sub.w (%a2),%d0
	addq.w #1,%d0
	move.w %d0,(%a4)
	mulu.w #6,%d0
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l -24(%a6),%a1
	move.l %a0,(%a1)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a3
	clr.w %d6
	addq.l #8,%sp
	cmp.w (%a4),%d6
	jbcc .L1160
	.even
.L1162:
	move.w %d6,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	add.l %a0,%d0 /*JANE2*/
	move.b %d6,%d1
	add.b 1(%a2),%d1
	move.b %d1,(%a3,%d0.l)
	clr.b 1(%a3,%d0.l)
	move.b %d6,%d1
	add.b 1(%a2),%d1
	addq.b #1,%d1
	move.b %d1,2(%a3,%d0.l)
	clr.b 3(%a3,%d0.l)
	move.w #65535,4(%a3,%d0.l)
	addq.w #1,%d6
	cmp.w (%a4),%d6
	jbcs .L1162
.L1160:
	bsr.w DayViewCheckForConflicts
	move.l -24(%a6),%a0
	move.l (%a0),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	bsr.w DayInjectTodos
	bsr.w DayInjectBdays
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayInjectCalDav,%a0 /*JANE2*/
	jsr (%a0)
	bsr.w DayInjectLocationText
	bra .L1154
	.even
.L1158:
	lea NumApptsOnly@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,-32(%a6)
	move.w (%a1),%d0
	add.w #48,%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA01E
#NO_APP
	move.l -24(%a6),%a2
	move.l %a0,(%a2)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a3
	clr.w NumAppts@END.w(%a5)
	clr.w %d5
	move.l -28(%a6),%a0
	move.l (%a0),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a4
	clr.w %d6
	lea (12,%sp),%sp
	move.l -32(%a6),%a1
	cmp.w (%a1),%d6
	jbcc .L1165
	.even
.L1167:
	clr.b %d7
	move.w %d5,%d4
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	move.l %a2,%d5
	cmp.w (%a2),%d4
	jbcc .L1169
	move.w %d4,%a2
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	lea (%a2,%d0.l),%a1
	cmp.w #65535,4(%a3,%a1.l)
	jbne .L1172
	move.w %d6,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	add.l %a0,%d0 /*JANE2*/
	move.w (%a3,%a1.l),%d2
	move.w (%a4,%d0.l),%d3
	bra .L1217
	.even
.L1173:
	cmp.w %d2,%d3
	jble .L1172
	cmp.w 2(%a3,%a1.l),%d3
	jbge .L1172
	move.b (%a4,%d0.l),2(%a3,%a1.l)
	move.b 1(%a4,%d0.l),3(%a3,%a1.l)
.L1172:
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	add.l %a2,%d0 /*JANE2*/
	move.w %d6,%a0
	move.l %a0,%d1
	lsl.l #2,%d1
	add.l %a0,%d1 /*JANE2*/
	add.l %a0,%d1 /*JANE2*/
	move.w (%a4,%d1.l),%d3
	cmp.w (%a3,%d0.l),%d3
	jblt .L1215
	addq.w #1,%d4
	lea NumAppts@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d5
	cmp.w (%a0),%d4
	jbcc .L1169
	move.w %d4,%a2
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	lea (%a2,%d0.l),%a1
	cmp.w #65535,4(%a3,%a1.l)
	jbne .L1172
	move.l %d1,%d0
	move.w (%a3,%a1.l),%d2
.L1217:
	cmp.w %d2,%d3
	jbne .L1173
	moveq.l #1,%d7
.L1169:
	move.w %d4,%a0
	move.l %a0,%d1
	lsl.l #2,%d1
	add.l %a0,%d1 /*JANE2*/
	add.l %a0,%d1 /*JANE2*/
	move.w %d6,%a1
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.l (%a4,%d0.l),(%a3,%d1.l)
	move.w 4(%a4,%d0.l),4(%a3,%d1.l)
	move.w %d4,%d5
	addq.w #1,%d5
	tst.b %d7
	jbne .L1177
	addq.w #1,NumAppts@END.w(%a5)
.L1177:
	cmp.w #-1,(%a4,%d0.l)
	jbeq .L1166
	move.w 2(%a4,%d0.l),%d3
	cmp.w (%a4,%d0.l),%d3
	jbeq .L1166
	moveq.l #1,%d7
	move.w %d5,%d4
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d2
	cmp.w %d4,%d2
	jbls .L1180
	move.w %d4,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	add.l %a0,%d0 /*JANE2*/
	move.w (%a3,%d0.l),%d1
	move.w %d3,%d0
	cmp.w %d1,%d0
	jbeq .L1166
	.even
.L1183:
	cmp.w %d1,%d0
	jblt .L1216
	addq.w #1,%d4
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d2
	cmp.w %d4,%d2
	jbls .L1180
	move.w %d4,%a0
	move.l %a0,%d1
	lsl.l #2,%d1
	add.l %a0,%d1 /*JANE2*/
	add.l %a0,%d1 /*JANE2*/
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.w (%a3,%d1.l),%d1
	move.w 2(%a4,%d0.l),%d0
	cmp.w %d1,%d0
	jbne .L1183
	clr.b %d7
.L1180:
	tst.b %d7
	jbeq .L1166
	move.w %d6,%a1
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.b 2(%a4,%d0.l),%d0
	cmp.b #22,%d0
	jbhi .L1187
	addq.b #1,%d0
	move.b %d0,-4(%a6)
	clr.b -3(%a6)
	bra .L1188
	.even
.L1216:
	sub.w %d4,%d2
	mulu.w #6,%d2
	move.l %d2,-(%sp)
	move.w %d4,%d0
	muls.w #6,%d0
	pea (%a3,%d0.l)
	pea 6(%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	lea (12,%sp),%sp
	bra .L1180
	.even
.L1215:
	move.l %d5,%a1
	move.w (%a1),%d0
	sub.w %d4,%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.w %d4,%d0
	muls.w #6,%d0
	pea (%a3,%d0.l)
	pea 6(%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	lea (12,%sp),%sp
	bra .L1169
	.even
.L1187:
	move.b #23,-4(%a6)
	move.b #55,-3(%a6)
.L1188:
	cmp.w (%a2),%d4
	jbcc .L1189
	move.w %d4,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	add.l %a0,%d0 /*JANE2*/
	lea 6(%a3,%d0.l),%a0 /*FRED7b*/
	move.w (%a0),%d0
	cmp.w -4(%a6),%d0
	jbge .L1189
	move.b (%a0),-4(%a6)
	move.b 1(%a0),-3(%a6)
.L1189:
	move.w %d4,%a0
	move.l %a0,%d1
	lsl.l #2,%d1
	add.l %a0,%d1 /*JANE2*/
	add.l %a0,%d1 /*JANE2*/
	move.w #65535,4(%a3,%d1.l)
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.b 2(%a4,%d0.l),(%a3,%d1.l)
	move.b 3(%a4,%d0.l),1(%a3,%d1.l)
	move.b -4(%a6),2(%a3,%d1.l)
	move.b -3(%a6),3(%a3,%d1.l)
	addq.w #1,(%a2)
.L1166:
	addq.w #1,%d6
	move.l -32(%a6),%a0
	cmp.w (%a0),%d6
	jbcs .L1167
.L1165:
	bsr.w DayViewCheckForConflicts
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA17E
#NO_APP
	move.l %a0,%d3
	move.w #1020,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D4
#NO_APP
	lea CompressDayView@END.w(%a5),%a1 /*FRED7b*/
	move.l %a1,%d7
	lea (18,%sp),%sp
	tst.b (%a1)
	jbeq .L1192
	cmp.w NumAppts@END.w(%a5),%d0
	jbls .L1191
.L1192:
	pea -12(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1DA
#NO_APP
	move.w -6(%a6),%d5
	sub.l %a4,%a4
	move.w #2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1DC
#NO_APP
	move.w %d0,-20(%a6)
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,-18(%a6)
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	lea (18,%sp),%sp
	move.l %d7,%a2
	tst.b (%a2)
	jbeq .L1193
	clr.w %d4
	bra .L1194
	.even
.L1196:
	addq.w #1,%d4
.L1194:
	cmp.w NumAppts@END.w(%a5),%d4
	jbcc .L1193
	pea -13(%a6)
	pea -16(%a6)
	move.w %d5,-(%sp)
	move.w -20(%a6),-(%sp)
	move.w %d4,-(%sp)
	bsr.w DayViewGetDescriptionHeight
	add.w %d0,%a4
	lea (14,%sp),%sp
	cmp.w %a4,%d5
	jbgt .L1196
.L1193:
	lea DayStartHour@END.w(%a5),%a0 /*FRED7b*/
	move.l %a0,%d4
	move.b 1(%a0),-2(%a6)
	clr.b -1(%a6)
	clr.w %d6
	move.l %d7,%a1
	tst.b (%a1)
	jbne .L1218
	.even
.L1202:
	lea NumAppts@END.w(%a5),%a2 /*FRED7b*/
	cmp.w (%a2),%d6
	jbcc .L1203
	move.w %d6,%a0
	move.l %a0,%d0
	lsl.l #2,%d0
	add.l %a0,%d0 /*JANE2*/
	move.l %d0,%d1
	add.l %a0,%d1 /*JANE2*/
	move.w -2(%a6),%d0
	cmp.w (%a3,%d1.l),%d0
	jblt .L1203
	lea (%a3,%d1.l),%a0
	lea (2,%a0),%a1
	cmp.w (%a1),%d0
	jbgt .L1204
	clr.w %d0
	move.b 2(%a3,%d1.l),%d0
	move.l %d4,%a2
	cmp.w (%a2),%d0
	jbcs .L1204
	move.b 2(%a3,%d1.l),-2(%a6)
	move.b 3(%a3,%d1.l),-1(%a6)
	jbne .L1206
	move.w (%a1),%d0
	cmp.w (%a0),%d0
	jbne .L1204
.L1206:
	addq.b #1,-2(%a6)
	clr.b -1(%a6)
.L1204:
	addq.w #1,%d6
	bra .L1200
	.even
.L1203:
	clr.w %d1
	move.b -2(%a6),%d1
	move.w DayEndHour@END.w(%a5),%d0
	cmp.w %d1,%d0
	jbhi .L1209
	jbne .L1208
	tst.b -1(%a6)
	jbne .L1208
.L1209:
	move.w (%a2),%d0
	sub.w %d6,%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.w %d6,%d0
	muls.w #6,%d0
	pea (%a3,%d0.l)
	pea 6(%a3,%d0.l)
#APP
	trap #15; dc.w 0xA026
#NO_APP
	addq.w #1,(%a2)
	move.w %d6,%a1
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.b -2(%a6),(%a3,%d0.l)
	move.b -1(%a6),1(%a3,%d0.l)
	move.w #65535,4(%a3,%d0.l)
	addq.b #1,-2(%a6)
	clr.b -1(%a6)
	move.w %d6,%d3
	addq.w #1,%d3
	lea (12,%sp),%sp
	cmp.w (%a2),%d3
	jbcc .L1210
	lea 6(%a3,%d0.l),%a0 /*FRED7b*/
	move.w (%a0),%d1
	cmp.w -2(%a6),%d1
	jbge .L1210
	move.b (%a0),-2(%a6)
	move.b 1(%a0),-1(%a6)
.L1210:
	move.l %a1,%d0
	lsl.l #2,%d0
	add.l %a1,%d0 /*JANE2*/
	add.l %a1,%d0 /*JANE2*/
	move.b -2(%a6),2(%a3,%d0.l)
	move.b -1(%a6),3(%a3,%d0.l)
	pea -13(%a6)
	pea -16(%a6)
	move.w %d5,-(%sp)
	move.w -20(%a6),-(%sp)
	move.w %d6,-(%sp)
	bsr.w DayViewGetDescriptionHeight
	add.w %d0,%a4
	move.w %d3,%d6
	lea (14,%sp),%sp
	bra .L1200
	.even
.L1208:
	cmp.w (%a2),%d6
	jbcc .L1191
	addq.b #1,-2(%a6)
	clr.b -1(%a6)
.L1200:
	move.l %d7,%a0
	tst.b (%a0)
	jbeq .L1202
.L1218:
	move.w %a4,%d0
	add.w -18(%a6),%d0
	cmp.w %d0,%d5
	jbge .L1202
.L1191:
	move.l -28(%a6),%a1
	move.l (%a1),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l -24(%a6),%a2
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.w NumAppts@END.w(%a5),%d0
	mulu.w #6,%d0
	move.l %d0,-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA033
#NO_APP
	bsr.w DayInjectTodos
	bsr.w DayInjectBdays
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayInjectCalDav,%a0 /*JANE2*/
	jsr (%a0)
	bsr.w DayInjectLocationText
.L1154:
	movm.l -64(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayViewNewAppointment:
	link.w %a6,#-52
	movm.l #0x1c20,-(%sp)
	move.l 8(%a6),%a2
	clr.b %d5
	move.w #-1,-4(%a6)
	move.w #-1,-6(%a6)
	clr.b -2(%a6)
	cmp.w #0,%a2
	jbeq .L1220
	move.w 8(%a2),-(%sp)
#APP
	moveq #2,%d2; trap #15; dc.w 0xA2AB
#NO_APP
	addq.l #2,%sp
	btst #5,%d0
	jbeq .L1221
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA11B
#NO_APP
	move.b 9(%a2),%d0
	move.b %d0,%d1
	add.b #-48,%d1
	move.b %d1,-4(%a6)
	clr.b -3(%a6)
	add.b #-47,%d0
	move.b %d0,-6(%a6)
	clr.b -5(%a6)
	move.w #102,-(%sp)
	pea -6(%a6)
	pea -4(%a6)
	bsr.w GetTime
	lea (14,%sp),%sp
	tst.b %d0
	jbne .L1223
	bra .L1228
	.even
.L1221:
	move.b 9(%a2),-2(%a6)
	clr.b -1(%a6)
	move.b -2(%a6),%d1
	move.b %d1,%d0
	add.b #-97,%d0
	cmp.b #25,%d0
	jbhi .L1223
	add.b #-32,%d1
	move.b %d1,-2(%a6)
.L1223:
	moveq.l #1,%d5
.L1220:
	cmp.w #99,NumApptsOnly@END.w(%a5)
	jbhi .L1228
	clr.b -(%sp)
	pea 26.w
	moveq.l #-32,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l -48(%a6),-40(%a6)
	move.l -44(%a6),-36(%a6)
	move.b -4(%a6),-40(%a6)
	move.b -3(%a6),-39(%a6)
	move.b -6(%a6),-38(%a6)
	move.b -5(%a6),-37(%a6)
	move.l -40(%a6),-48(%a6)
	move.l -36(%a6),-44(%a6)
	move.l Date@END.w(%a5),-44(%a6)
	lea (-48,%a6),%a0
	move.l %a0,-32(%a6)
	lea (-2,%a6),%a0
	move.l %a0,-16(%a6)
	pea -50(%a6)
	move.l %d3,-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr ApptNewRecord(%a2)
	lea (22,%sp),%sp
	tst.w %d0
	jbne .L1226
	move.w -50(%a6),CurrentRecord@END.w(%a5)
	move.b #1,ItemSelected@END.w(%a5)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d4
	move.w #1020,-(%sp)
	jsr GetObjectPtr(%a2)
	move.l %a0,%d3
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	move.w -50(%a6),-(%sp)
	bsr.w DayViewFindAppointment
	pea -52(%a6)
	move.w %d0,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1EF
#NO_APP
	move.w #1020,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA179
#NO_APP
	lea (32,%sp),%sp
	move.w #2,-(%sp)
	move.w -52(%a6),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1ED
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E9
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA143
#NO_APP
	move.w %d5,%d0
	and.w #1,%d0
	bra .L1227
	.even
.L1226:
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
.L1228:
	moveq.l #1,%d0
.L1227:
	movm.l -68(%a6),#0x438
	unlk %a6
	rts
	.even
DayViewDeleteAppointment:
	link.w %a6,#-4
	movm.l #0x1838,-(%sp)
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a4
	jsr GetObjectPtr(%a4)
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E8
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1229
	pea -2(%a6)
	pea -4(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.w -4(%a6),-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%a2
	lea ApptsH@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.w %a2,%a2
	move.l %a2,%d0
	lsl.l #2,%d0
	add.l %a2,%d0 /*JANE2*/
	add.l %a2,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%d4
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (30,%sp),%sp
	cmp.w CurrentRecord@END.w(%a5),%d4
	jbeq .L1232
	lea .LC14(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #7314,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	lea (10,%sp),%sp
	.even
.L1232:
	jsr ClearEditState(%a4)
	tst.b %d0
	jbne .L1236
	move.w %d4,-(%sp)
	bsr.w DeleteRecord
	addq.l #2,%sp
	tst.b %d0
	jbeq .L1229
.L1236:
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
.L1229:
	movm.l -24(%a6),#0x1c18
	unlk %a6
	rts
.LC18:
	.ascii "This event repeats. Duplicate only this occurrence or the complete repeat setup?\0"
	.even
DuplicatePromptRepeat:
	link.w %a6,#-28
	movm.l #0x1f38,-(%sp)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-28(%a6)
	move.w #2300,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d5
	move.w #2,%a4
	sub.l %a3,%a3
	lea .LC18(%pc),%a2 /*SPECIAL_FRED*/
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA174
#NO_APP
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	moveq.l #18,%d4
	move.b #1,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d7
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,%d6
	lea (12,%sp),%sp
	tst.b (%a2)
	jbeq .L1240
	.even
.L1241:
	move.w #144,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA156
#NO_APP
	move.w %d0,%d3
	move.w %d4,-(%sp)
	move.w #6,-(%sp)
	move.w %d3,-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA220
#NO_APP
	and.l #0xFFFF,%d3
	add.l %d3,%a2 /*JANE2*/
	lea (16,%sp),%sp
	cmp.b #32,(%a2)
	jbne .L1243
	.even
.L1244:
	addq.l #1,%a2
	cmp.b #32,(%a2)
	jbeq .L1244
.L1243:
	add.w %d6,%d4
	tst.b (%a2)
	jbne .L1241
.L1240:
	move.b %d7,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	addq.l #2,%sp
	bra .L1247
	.even
.L1249:
	pea -1.w
	moveq.l #-24,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA11D
#NO_APP
	move.w -24(%a6),%d0
	addq.l #8,%sp
	cmp.w #9,%d0
	jbne .L1250
	move.w -16(%a6),%d0
	cmp.w #2302,%d0
	jbeq .L1253
	jbhi .L1257
	cmp.w #2301,%d0
	jbeq .L1252
	bra .L1247
	.even
.L1257:
	cmp.w #2303,%d0
	jbeq .L1254
	bra .L1247
	.even
.L1252:
	sub.l %a4,%a4
	bra .L1248
	.even
.L1253:
	move.w #1,%a4
	bra .L1248
	.even
.L1254:
	move.w #2,%a4
	bra .L1248
	.even
.L1250:
	cmp.w #22,%d0
	jbne .L1258
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA11B
#NO_APP
	move.w #1,%a3
	bra .L1262
	.even
.L1258:
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1A0
#NO_APP
.L1262:
	addq.l #4,%sp
.L1247:
	move.w %a3,%d0
	tst.b %d0
	jbeq .L1249
.L1248:
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA172
#NO_APP
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	addq.l #8,%sp
	tst.l -28(%a6)
	jbeq .L1260
	move.l -28(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA174
#NO_APP
.L1260:
	move.w %a4,%d0
	movm.l -60(%a6),#0x1cf8
	unlk %a6
	rts
	.even
ApptDuplicateEventCore:
	link.w %a6,#-68
	movm.l #0x1e30,-(%sp)
	move.w 8(%a6),%d6
	clr.w %d5
	pea -56(%a6)
	pea -26(%a6)
	move.w %d6,-(%sp)
	lea ApptDB@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
	lea __text__@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	lea (14,%sp),%sp
	tst.l -18(%a6)
	jbeq .L1264
	bsr.w DuplicatePromptRepeat
	move.w %d0,%d5
	cmp.w #2,%d5
	jbne .L1264
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	clr.w %d0
	bra .L1270
	.even
.L1264:
	clr.b -(%sp)
	pea 8.w
	pea -64(%a6)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l -26(%a6),%a0
	move.b (%a0),-64(%a6)
	moveq.l #-64,%d3
	add.l %a6,%d3 /*JANE2*/
	move.b 1(%a0),-63(%a6)
	move.b 2(%a0),-62(%a6)
	move.b 3(%a0),-61(%a6)
	clr.b -(%sp)
	pea 26.w
	moveq.l #-52,%d4
	add.l %a6,%d4 /*JANE2*/
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l %d3,-52(%a6)
	clr.l -48(%a6)
	clr.l -40(%a6)
	move.l -10(%a6),-36(%a6)
	move.l -6(%a6),-32(%a6)
	move.b -2(%a6),-28(%a6)
	lea (20,%sp),%sp
	cmp.w #1,%d5
	jbne .L1266
	move.l -26(%a6),%a0
	move.l 4(%a0),-60(%a6)
	move.l -18(%a6),-44(%a6)
	bra .L1267
	.even
.L1266:
	move.l Date@END.w(%a5),-60(%a6)
	clr.l -44(%a6)
.L1267:
	clr.l -(%sp)
	clr.l -(%sp)
	pea -66(%a6)
	move.w %d6,-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	pea -68(%a6)
	move.l %d4,-(%sp)
	move.l (%a2),-(%sp)
	move.l (%a3),%a0
	add.l #ApptNewRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d3
	move.l -56(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (34,%sp),%sp
	tst.w %d3
	jbeq .L1268
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	clr.w %d0
	bra .L1270
	.even
.L1268:
	btst #4,-65(%a6)
	jbeq .L1269
	clr.l -(%sp)
	clr.l -(%sp)
	pea -66(%a6)
	move.w -68(%a6),-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	or.w #16,-66(%a6)
	clr.l -(%sp)
	pea -66(%a6)
	move.w -68(%a6),-(%sp)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA051
#NO_APP
.L1269:
	moveq.l #1,%d0
.L1270:
	movm.l -92(%a6),#0xc78
	unlk %a6
	rts
	.even
DayViewDuplicateEvent:
	link.w %a6,#0
	move.l %d3,-(%sp)
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E8
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1271
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.w CurrentRecord@END.w(%a5),-(%sp)
	bsr.w ApptDuplicateEventCore
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1271
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
.L1271:
	move.l -4(%a6),%d3
	unlk %a6
	rts
	.even
DetailsDuplicateEvent:
	link.w %a6,#0
	move.w CurrentRecord@END.w(%a5),-(%sp)
	bsr.w ApptDuplicateEventCore
	unlk %a6
	rts
	.even
DetailsDoCommand:
	link.w %a6,#0
	move.w 8(%a6),%d0
	cmp.w #106,%d0
	jbne .L1276
	bsr.w DetailsDuplicateEvent
	bra .L1279
	.even
.L1276:
	cmp.w #105,%d0
	jbeq .L1277
	clr.w %d0
	bra .L1278
	.even
.L1277:
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #DateSendRecord,%a0 /*JANE2*/
	jsr (%a0)
.L1279:
	moveq.l #1,%d0
.L1278:
	unlk %a6
	rts
	.even
DayViewDeleteNote:
	link.w %a6,#-48
	movm.l #0x1f20,-(%sp)
	clr.b %d7
	clr.b %d5
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr GetObjectPtr(%a2)
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E8
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1280
	pea -30(%a6)
	pea -26(%a6)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	jsr ApptGetRecord(%a2)
	clr.b %d3
	lea (14,%sp),%sp
	move.l -6(%a6),%a0
	cmp.w #0,%a0
	jbeq .L1283
	tst.b (%a0)
	jbne .L1282
.L1283:
	moveq.l #1,%d3
.L1282:
	move.l -30(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.b %d3
	jbne .L1280
	tst.l -18(%a6)
	jbeq .L1285
	move.w #2100,-(%sp)
#APP
	trap #15; dc.w 0xA16F
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA193
#NO_APP
	move.w %d0,%d4
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA170
#NO_APP
	lea (10,%sp),%sp
	cmp.w #2106,%d4
	jbeq .L1280
	cmp.w #2103,%d4
	jbne .L1288
	moveq.l #1,%d7
	bra .L1291
	.even
.L1288:
	cmp.w #2104,%d4
	jbne .L1291
	moveq.l #1,%d5
	bra .L1291
	.even
.L1285:
	move.w #2001,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	addq.l #2,%sp
	tst.w %d0
	jbne .L1280
.L1291:
	pea -32(%a6)
	pea -34(%a6)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.b %d5,-(%sp)
	move.b %d7,-(%sp)
	bsr.w DeleteNote
	lea (20,%sp),%sp
	tst.b %d0
	jbeq .L1280
	clr.b -(%sp)
	move.w -34(%a6),-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D3
#NO_APP
	move.w -34(%a6),-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1EB
#NO_APP
	move.w %d0,%d5
	move.w -34(%a6),-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d4
	move.w #2,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DC
#NO_APP
	move.w %d0,%d3
	pea -42(%a6)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DA
#NO_APP
	lea (34,%sp),%sp
	pea -43(%a6)
	pea -46(%a6)
	move.w -36(%a6),-(%sp)
	move.w %d3,-(%sp)
	move.w %d4,-(%sp)
	bsr.w DayViewGetDescriptionHeight
	lea (14,%sp),%sp
	cmp.w %d5,%d0
	jbeq .L1294
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D4
#NO_APP
	move.w %d0,%d4
	move.w -34(%a6),%d3
	addq.w #1,%d3
	addq.l #4,%sp
	cmp.w %d3,%d4
	jbls .L1294
	.even
.L1298:
	clr.b -(%sp)
	move.w %d3,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D3
#NO_APP
	addq.l #8,%sp
	addq.w #1,%d3
	cmp.w %d3,%d4
	jbhi .L1298
.L1294:
	tst.b %d7
	jbeq .L1300
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	addq.l #2,%sp
.L1300:
	bsr.w DayViewLoadTable
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	bsr.w DayViewRestoreEditState
.L1280:
	movm.l -72(%a6),#0x4f8
	unlk %a6
	rts
	.even
DayViewSelectTime:
	link.w %a6,#-40
	movm.l #0x1820,-(%sp)
	move.l 8(%a6),%d4
	move.w 12(%a6),-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d3
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	and.l #0xFFFF,%d3
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.b (%a0,%d0.l),-2(%a6)
	move.b 1(%a0,%d0.l),-1(%a6)
	move.b 2(%a0,%d0.l),-4(%a6)
	move.b 3(%a0,%d0.l),-3(%a6)
	move.w 4(%a0,%d0.l),-38(%a6)
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (14,%sp),%sp
	move.w -38(%a6),%d0
	cmp.w #57343,%d0
	jbls .L1303
	cmp.w #65535,%d0
	jbne .L1302
.L1303:
	pea -34(%a6)
	pea -30(%a6)
	move.w %d0,-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr ApptGetRecord(%a2)
	lea (14,%sp),%sp
	tst.w %d0
	jbne .L1302
	clr.b %d3
	tst.b -6(%a6)
	jbeq .L1305
	move.l Date@END.w(%a5),-(%sp)
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l -30(%a6),%a0
	move.l 4(%a0),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	sne %d3
	neg.b %d3
.L1305:
	move.l -34(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.b %d3
	jbeq .L1302
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w -38(%a6),CurrentRecord@END.w(%a5)
	move.b #1,ItemSelected@END.w(%a5)
	move.b #1,gDaySkipRestoreEditState@END.w(%a5)
	move.w #1300,-(%sp)
#APP
	trap #15; dc.w 0xA19C
#NO_APP
	bra .L1301
	.even
.L1302:
	move.w #102,-(%sp)
	pea -4(%a6)
	pea -2(%a6)
	bsr.w GetTime
	lea (10,%sp),%sp
	tst.b %d0
	jbne .L1307
	bsr.w DayViewRestoreEditState
	bra .L1301
	.even
.L1307:
	pea -35(%a6)
	move.b #1,-(%sp)
	move.l Date@END.w(%a5),-(%sp)
	subq.l #2,%sp
	move.b -4(%a6),(%sp)
	move.b -3(%a6),1(%sp)
	subq.l #2,%sp
	move.b -2(%a6),(%sp)
	move.b -1(%a6),1(%sp)
	pea -38(%a6)
	bsr.w MoveEvent
	lea (18,%sp),%sp
	tst.w %d0
	jbne .L1301
	tst.b -35(%a6)
	jbeq .L1309
	move.w -38(%a6),CurrentRecord@END.w(%a5)
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	addq.l #6,%sp
.L1309:
	bsr.w DayViewRestoreEditState
.L1301:
	movm.l -52(%a6),#0x418
	unlk %a6
	rts
	.even
DayViewSelectIconRect:
	link.w %a6,#-8
	movm.l #0x1c00,-(%sp)
	move.l 8(%a6),%d5
	move.l 14(%a6),%d3
	move.b 18(%a6),%d4
	pea -8(%a6)
#APP
	trap #15; dc.w 0xA20F
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA210
#NO_APP
	addq.l #8,%sp
	tst.b %d4
	jbeq .L1311
	move.w #2,-(%sp)
	move.w 12(%a6),-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1CE
#NO_APP
	addq.l #8,%sp
	bra .L1312
	.even
.L1311:
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	addq.l #4,%sp
.L1312:
	pea -8(%a6)
#APP
	trap #15; dc.w 0xA210
#NO_APP
	movm.l -20(%a6),#0x38
	unlk %a6
	rts
	.even
DayViewSelectIcons:
	link.w %a6,#-52
	movm.l #0x1f30,-(%sp)
	move.l 8(%a6),%a3
	move.b #1,-47(%a6)
	moveq.l #1,%d7
	clr.b %d6
	cmp.w #2,16(%a3)
	jbne .L1316
	move.l 10(%a3),%d4
	move.w 14(%a3),%d5
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1CF
#NO_APP
	move.w %d0,%d3
	addq.l #8,%sp
	jbeq .L1316
	pea -34(%a6)
	move.w #2,-(%sp)
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1CD
#NO_APP
	move.w -34(%a6),%d0
	add.w -30(%a6),%d0
	sub.w %d3,%d0
	move.w %d0,-42(%a6)
	lea (-42,%a6),%a2
	move.w -32(%a6),-40(%a6)
	move.w %d3,-38(%a6)
	move.b ApptDescFont@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
#APP
	trap #15; dc.w 0xA168
#NO_APP
	move.w %d0,-36(%a6)
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.w 4(%a3),%d1
	move.w %d1,-52(%a6)
	move.w 6(%a3),%d0
	move.w %d0,-50(%a6)
	move.l %a2,-(%sp)
	move.w %d0,-(%sp)
	move.w %d1,-(%sp)
#APP
	trap #15; dc.w 0xA1C8
#NO_APP
	lea (24,%sp),%sp
	tst.b %d0
	jbeq .L1316
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d3
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	and.l #0xFFFF,%d3
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%a3
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	pea -46(%a6)
	pea -26(%a6)
	move.w %a3,-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptGetRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.b #3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.b %d0,%d3
	lea (30,%sp),%sp
	tst.l -6(%a6)
	jbeq .L1317
	move.b #11,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	move.w %d0,-38(%a6)
	move.l %a2,-(%sp)
	move.w -50(%a6),-(%sp)
	move.w -52(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1C8
#NO_APP
	move.b %d0,%d6
	lea (10,%sp),%sp
	jbne .L1317
	move.w -42(%a6),%d0
	addq.w #1,%d0
	add.w -38(%a6),%d0
	move.w %d0,-42(%a6)
.L1317:
	tst.l -18(%a6)
	jbeq .L1319
	tst.b %d6
	jbne .L1319
	clr.b %d0
	jbne .L1319
	move.b #21,-(%sp)
#APP
	trap #15; dc.w 0xA16A
#NO_APP
	move.w %d0,4(%a2)
	move.l %a2,-(%sp)
	move.w -50(%a6),-(%sp)
	move.w -52(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1C8
#NO_APP
	lea (10,%sp),%sp
	tst.b %d0
	jbne .L1319
	move.w -42(%a6),%d0
	addq.w #1,%d0
	add.w 4(%a2),%d0
	move.w %d0,-42(%a6)
.L1319:
	move.b %d3,-(%sp)
#APP
	trap #15; dc.w 0xA164
#NO_APP
	move.l -46(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.b #1,-(%sp)
	pea -42(%a6)
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
	bsr.w DayViewSelectIconRect
	lea (22,%sp),%sp
	.even
.L1328:
	pea -47(%a6)
	pea -50(%a6)
	pea -52(%a6)
#APP
	trap #15; dc.w 0xA11E
#NO_APP
	pea -42(%a6)
	move.w -50(%a6),-(%sp)
	move.w -52(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA1C8
#NO_APP
	lea (20,%sp),%sp
	tst.b %d0
	jbeq .L1324
	tst.b %d7
	jbne .L1323
	moveq.l #1,%d7
	move.b #1,-(%sp)
	bra .L1333
	.even
.L1324:
	tst.b %d7
	jbeq .L1323
	clr.b %d7
	clr.b -(%sp)
.L1333:
	pea -42(%a6)
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
	bsr.w DayViewSelectIconRect
	lea (12,%sp),%sp
.L1323:
	tst.b -47(%a6)
	jbne .L1328
	tst.b %d7
	jbeq .L1316
	clr.b -(%sp)
	pea -42(%a6)
	move.w %d5,-(%sp)
	move.l %d4,-(%sp)
	bsr.w DayViewSelectIconRect
	move.w %a3,CurrentRecord@END.w(%a5)
	lea (12,%sp),%sp
	tst.b %d6
	jbeq .L1330
	move.w #10950,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L1331
	.even
.L1330:
	move.w #1300,-(%sp)
#APP
	trap #15; dc.w 0xA19C
#NO_APP
.L1331:
	moveq.l #1,%d0
	bra .L1332
	.even
.L1316:
	clr.w %d0
.L1332:
	movm.l -80(%a6),#0xcf8
	unlk %a6
	rts
	.even
DayViewItemSelected:
	link.w %a6,#-48
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%a2
	move.l 10(%a2),%d5
	move.w 14(%a2),%d6
	move.w 16(%a2),%d7
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d3
	lea ApptsH@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	and.l #0xFFFF,%d3
	move.l %d3,%d0
	lsl.l #2,%d0
	add.l %d3,%d0 /*JANE2*/
	add.l %d3,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%d4
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	move.w %d4,%d3
	add.w #4096,%d3
	lea (14,%sp),%sp
	cmp.w #255,%d3
	jbhi .L1335
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w %d3,-(%sp)
	bsr.w DayLaunchTodo
	bra .L1334
	.even
.L1335:
	cmp.w #61695,%d4
	jbls .L1336
	cmp.w #65535,%d4
	jbeq .L1336
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	lea gDayBdayRec@END.w(%a5),%a0 /*FRED7b*/
	add.w #3840,%d4
	clr.l %d0
	move.w %d4,%d0
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #AddrBdayGoTo,%a0 /*JANE2*/
	jsr (%a0)
	bra .L1334
	.even
.L1336:
	move.w %d4,%d0
	add.w #8192,%d0
	cmp.w #4095,%d0
	jbhi .L1337
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w %d4,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayDavShowInfo,%a0 /*JANE2*/
	jsr (%a0)
	bra .L1334
	.even
.L1337:
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA3D2
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1338
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA3CE
#NO_APP
	addq.l #2,%sp
	cmp.b #1,%d0
	jbne .L1334
	clr.b PrivateRecordVisualStatus@END.w(%a5)
	move.w #64,-(%sp)
	bsr.w DayViewUpdateDisplay
	bra .L1334
	.even
.L1338:
	cmp.w #1,%d7
	jbne .L1340
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w 14(%a2),-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1F3
#NO_APP
	move.l %d0,-48(%a6)
	lea (10,%sp),%sp
	jbne .L1341
	cmp.w #99,NumApptsOnly@END.w(%a5)
	jbhi .L1334
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d4
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	clr.b -(%sp)
	pea 26.w
	moveq.l #-26,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA027
#NO_APP
	move.l -42(%a6),-34(%a6)
	move.l -38(%a6),-30(%a6)
	clr.l %d1
	move.w %d4,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	move.l %d0,%d4
	add.l %d1,%d4 /*JANE2*/
	move.b (%a2,%d4.l),-34(%a6)
	move.b 1(%a2,%d4.l),-33(%a6)
	move.l -34(%a6),-42(%a6)
	move.l -30(%a6),-38(%a6)
	move.b 2(%a2,%d4.l),-32(%a6)
	move.b 3(%a2,%d4.l),-31(%a6)
	move.l -34(%a6),-42(%a6)
	move.l -30(%a6),-38(%a6)
	move.l Date@END.w(%a5),-38(%a6)
	lea (-42,%a6),%a0
	move.l %a0,-26(%a6)
	lea .LC3(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-10(%a6)
	pea -44(%a6)
	move.l %d3,-(%sp)
	lea ApptDB@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ApptNewRecord,%a0 /*JANE2*/
	jsr (%a0)
	move.w %d0,%d3
	lea (32,%sp),%sp
	jbne .L1343
	move.w -44(%a6),4(%a2,%d4.l)
	clr.l -(%sp)
	pea -48(%a6)
	clr.l -(%sp)
	move.w -44(%a6),-(%sp)
	move.l (%a3),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	move.l -48(%a6),-(%sp)
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1F4
#NO_APP
	move.w -44(%a6),CurrentRecord@END.w(%a5)
	move.b #1,ItemSelected@END.w(%a5)
	lea (28,%sp),%sp
.L1343:
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.w %d3
	jbeq .L1341
	move.w #10013,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	bra .L1334
	.even
.L1341:
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
	bsr.w DayViewSelectTime
	bra .L1334
	.even
.L1340:
	cmp.w #2,%d7
	jbne .L1334
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d4
	move.l (%a4),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.l %a0,%a2
	clr.l %d1
	move.w %d4,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w 4(%a2,%d0.l),CurrentRecord@END.w(%a5)
	move.w %d4,gDetailsApptIndex@END.w(%a5)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E8
#NO_APP
	lea (18,%sp),%sp
	tst.b %d0
	jbeq .L1347
	move.b #1,ItemSelected@END.w(%a5)
	lea DayViewDrawTimeBars(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	move.w %d6,-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E4
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbeq .L1334
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	jsr (%a2)
	bsr.w DayViewRestoreEditState
	bra .L1334
	.even
.L1347:
	move.w #10950,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
.L1334:
	movm.l -80(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayViewResizeDescription:
	link.w %a6,#-16
	move.l %a3,-(%sp)
	move.l %a2,-(%sp)
	move.l 8(%a6),%a2
	pea -8(%a6)
	move.l 10(%a2),-(%sp)
#APP
	trap #15; dc.w 0xA138
#NO_APP
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%a3
	move.l %a2,-(%sp)
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA1CC
#NO_APP
	lea (18,%sp),%sp
	move.w -2(%a6),%d0
	cmp.w 14(%a2),%d0
	jbgt .L1351
	clr.w -(%sp)
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,TopVisibleAppt@END.w(%a5)
	addq.l #6,%sp
.L1351:
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.w CurrentRecord@END.w(%a5),-(%sp)
	bsr.w DayViewFindAppointment
	pea -10(%a6)
	move.w %d0,-(%sp)
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA1EF
#NO_APP
	pea -12(%a6)
	pea -14(%a6)
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	move.w -10(%a6),%d0
	lea (26,%sp),%sp
	cmp.w -14(%a6),%d0
	jbeq .L1352
	move.w %d0,16(%a3)
.L1352:
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	move.l -24(%a6),%a2
	move.l -20(%a6),%a3
	unlk %a6
	rts
.LC19:
	.ascii "Unknown short date format\0"
	.even
DayViewSetTitle:
	link.w %a6,#-16
	move.l %d4,-(%sp)
	move.l %d3,-(%sp)
	move.b ShortDateFormat@END.w(%a5),%d0
	cmp.b #6,%d0
	jbls .L1354
	lea .LC19(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #8278,-(%sp)
	lea .LC1(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA084
#NO_APP
	clr.w %d0
	lea (10,%sp),%sp
	bra .L1355
	.even
.L1354:
	and.w #0xFF,%d0
.L1355:
	add.w #6002,%d0
	move.w %d0,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d4
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	move.w #14,-(%sp)
	moveq.l #-16,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
	clr.b -(%sp)
	lea Date@END.w(%a5),%a1 /*FRED7b*/
	move.w (%a1),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	move.b 3(%a1),-(%sp)
	move.b 2(%a1),-(%sp)
	move.l %a0,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EraDateTemplateToAscii,%a0 /*JANE2*/
	jsr (%a0)
	move.l %d4,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	lea (28,%sp),%sp
	move.l %d3,(%sp)
	move.l 8(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA19A
#NO_APP
	move.l -24(%a6),%d3
	move.l -20(%a6),%d4
	unlk %a6
	rts
	.even
DayViewDrawTitle:
	link.w %a6,#0
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
	bsr.w DayViewSetTitle
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	move.w (%a0),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 3(%a0),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 2(%a0),%d0
	move.w %d0,-(%sp)
	bsr.w DatebookDayOfWeek
	add.w #1003,%d0
	move.w %d0,-(%sp)
	move.b #1,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	clr.b TimeDisplayed@END.w(%a5)
	move.l -4(%a6),%d3
	unlk %a6
	rts
	.even
DayViewShowTime:
	link.w %a6,#-24
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	pea -24(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #ETimSecondsToDateTime,%a0 /*JANE2*/
	jsr (%a0)
	moveq.l #-10,%d3
	add.l %a6,%d3 /*JANE2*/
	move.l %d3,-(%sp)
	move.b TimeFormat@END.w(%a5),-(%sp)
	move.b -21(%a6),-(%sp)
	move.b -19(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA268
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA19A
#NO_APP
	move.b #1,TimeDisplayed@END.w(%a5)
	lea TimeDisplayTick@END.w(%a5),%a2 /*FRED7b*/
#APP
	trap #15; dc.w 0xA0F7
#NO_APP
	add.l #150,%d0 /*JANE2*/
	move.l %d0,(%a2)
	move.l -32(%a6),%d3
	move.l -28(%a6),%a2
	unlk %a6
	rts
	.even
DayViewHideTime:
	link.w %a6,#0
	move.l %d3,-(%sp)
	tst.b TimeDisplayed@END.w(%a5)
	jbeq .L1359
	tst.b 8(%a6)
	jbne .L1361
	move.l __text__@END.w(%a5),%a0
	add.l #TimeToWait,%a0 /*JANE2*/
	jsr (%a0)
	tst.l %d0
	jbne .L1359
.L1361:
#APP
	trap #15; dc.w 0xA1FE
#NO_APP
	move.l %a0,%d3
	move.w #1000,-(%sp)
#APP
	trap #15; dc.w 0xA17E
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA17C
#NO_APP
	addq.l #6,%sp
	cmp.l %d3,%a0
	jbne .L1359
	bsr.w DayViewDrawTitle
.L1359:
	move.l -4(%a6),%d3
	unlk %a6
	rts
	.even
DayViewDrawDate:
	link.w %a6,#0
	move.l 8(%a6),Date@END.w(%a5)
	bsr.w DayViewDrawTitle
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewSetTopAppointment
	bsr.w DayViewLoadTable
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	unlk %a6
	rts
	.even
DayViewDayOfWeekSelected:
	link.w %a6,#0
	move.l %a3,-(%sp)
	move.l %a2,-(%sp)
	move.l 8(%a6),%a2
	lea Date@END.w(%a5),%a3 /*FRED7b*/
	move.w (%a3),%d0
	add.w #1904,%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 3(%a3),%d0
	move.w %d0,-(%sp)
	clr.w %d0
	move.b 2(%a3),%d0
	move.w %d0,-(%sp)
	bsr.w DatebookDayOfWeek
	move.w 8(%a2),%d1
	add.w #-1003,%d1
	addq.l #6,%sp
	cmp.w %d0,%d1
	jbeq .L1364
	sub.w %d0,%d1
	move.w %d1,%a0
	move.l %a0,-(%sp)
	move.l %a3,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateAdjust,%a0 /*JANE2*/
	jsr (%a0)
	move.l (%a3),-(%sp)
	bsr.w DayViewDrawDate
.L1364:
	move.l -8(%a6),%a2
	move.l -4(%a6),%a3
	unlk %a6
	rts
	.even
DayViewGoToDate:
	link.w %a6,#-8
	move.l %a2,-(%sp)
	move.l %d3,-(%sp)
	move.w #101,-(%sp)
	move.l #1951618130,-(%sp)
#APP
	trap #15; dc.w 0xA05F
#NO_APP
	move.l %a0,%d3
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	lea Date@END.w(%a5),%a2 /*FRED7b*/
	clr.w -2(%a6)
	move.b 3(%a2),-1(%a6)
	clr.w -4(%a6)
	move.b 2(%a2),-3(%a6)
	move.w (%a2),%d0
	add.w #1904,%d0
	move.w %d0,-6(%a6)
	move.l %a0,-(%sp)
	pea -6(%a6)
	pea -2(%a6)
	pea -4(%a6)
	clr.b -(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #DatePickSelectDay,%a0 /*JANE2*/
	jsr (%a0)
	lea (28,%sp),%sp
	tst.b %d0
	jbeq .L1367
	move.b -1(%a6),3(%a2)
	move.b -3(%a6),2(%a2)
	move.w -6(%a6),%d0
	add.w #-1904,%d0
	move.w %d0,(%a2)
	move.l (%a2),-(%sp)
	bsr.w DayViewDrawDate
	addq.l #4,%sp
.L1367:
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l -16(%a6),%d3
	move.l -12(%a6),%a2
	unlk %a6
	rts
	.even
DayViewGotoAppointment:
	link.w %a6,#0
	move.l 8(%a6),%a0
	clr.w TopVisibleAppt@END.w(%a5)
	move.b #1,ItemSelected@END.w(%a5)
	lea CurrentRecord@END.w(%a5),%a1 /*FRED7b*/
	move.w 10(%a0),(%a1)
	move.w 12(%a0),DayEditPosition@END.w(%a5)
	move.w 14(%a0),DayEditSelectionLength@END.w(%a5)
	move.w (%a1),-(%sp)
	bsr.w SetDateToNextOccurrence
	unlk %a6
	rts
	.even
DayViewScroll:
	link.w %a6,#-12
	movm.l #0x1f38,-(%sp)
	move.b 8(%a6),%d3
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a3
	add.l #GetObjectPtr,%a3 /*JANE2*/
	jsr (%a3)
	move.l %a0,%d6
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	pea -8(%a6)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DA
#NO_APP
	move.w -2(%a6),%d7
	move.w #2,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DC
#NO_APP
	move.w %d0,%a4
	lea TopVisibleAppt@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d5
	lea (20,%sp),%sp
	cmp.b #1,%d3
	jbne .L1370
	tst.b 10(%a6)
	jbeq .L1371
	move.w #1019,-(%sp)
	jsr (%a3)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA119
#NO_APP
	addq.l #6,%sp
	tst.b %d0
	jbne .L1371
	clr.w (%a2)
	bra .L1376
	.even
.L1371:
	move.w (%a2),%d0
	addq.w #1,%d0
	cmp.w NumAppts@END.w(%a5),%d0
	jbcc .L1388
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1F0
#NO_APP
	move.w %d0,%d3
	move.w %d3,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d5
	lea (10,%sp),%sp
	tst.w %d3
	jbne .L1376
	addq.w #1,%d5
	bra .L1376
	.even
.L1370:
	tst.w %d5
	jbne .L1377
.L1388:
	clr.w %d0
	bra .L1386
	.even
.L1377:
	clr.w -(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1EB
#NO_APP
	move.w %d0,%d3
	addq.l #6,%sp
	cmp.w %d3,%d7
	jbgt .L1381
	clr.w %d3
	cmp.w %d3,%d7
	jble .L1376
	.even
.L1381:
	pea -9(%a6)
	pea -12(%a6)
	move.w %d7,-(%sp)
	move.w %a4,-(%sp)
	move.w %d5,%d4
	subq.w #1,%d4
	move.w %d4,-(%sp)
	bsr.w DayViewGetDescriptionHeight
	add.w %d0,%d3
	lea (14,%sp),%sp
	cmp.w %d3,%d7
	jbge .L1384
	cmp.w (%a2),%d5
	jbne .L1379
.L1384:
	move.w %d4,%d5
.L1379:
	cmp.w %d3,%d7
	jble .L1376
	tst.w %d5
	jbne .L1381
.L1376:
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E0
#NO_APP
	move.w %d5,TopVisibleAppt@END.w(%a5)
	bsr.w DayViewLoadTable
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
	moveq.l #1,%d0
.L1386:
	movm.l -44(%a6),#0x1cf8
	unlk %a6
	rts
	.even
DayViewPurge:
	link.w %a6,#0
	bsr.w PurgeRecords
	tst.b %d0
	jbeq .L1390
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewLoadTable
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
.L1390:
	unlk %a6
	rts
	.even
DayViewDoCommand:
	link.w %a6,#-8
	movm.l #0x1820,-(%sp)
	move.w 8(%a6),%d0
	moveq.l #1,%d4
	cmp.w #100,%d0
	jbne .L1392
	bsr.w DayViewClearEditState
	clr.l -(%sp)
	bsr.w DayViewNewAppointment
	addq.l #4,%sp
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1394
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%d3
	pea -2(%a6)
	pea -4(%a6)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.w -4(%a6),-(%sp)
	move.l %d3,-(%sp)
	bsr.w DayViewSelectTime
	bra .L1394
	.even
.L1392:
	cmp.w #101,%d0
	jbne .L1395
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1428
	bsr.w DayViewDeleteAppointment
	bra .L1394
	.even
.L1395:
	cmp.w #106,%d0
	jbne .L1399
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1428
	bsr.w DayViewDuplicateEvent
	bra .L1394
	.even
.L1399:
	cmp.w #102,%d0
	jbne .L1403
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1428
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.b #1,-(%sp)
	bsr.w CreateNote
	addq.l #8,%sp
	tst.b %d0
	jbeq .L1439
	clr.l -(%sp)
#APP
	trap #15; dc.w 0xA1C1
#NO_APP
	move.w #10950,-(%sp)
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L1394
	.even
.L1403:
	cmp.w #103,%d0
	jbne .L1409
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1428
	bsr.w DayViewDeleteNote
	bra .L1394
	.even
.L1409:
	cmp.w #104,%d0
	jbne .L1413
	bsr.w DayViewClearEditState
	bsr.w DayViewPurge
	bra .L1394
	.even
.L1413:
	cmp.w #105,%d0
	jbne .L1415
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1428
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #DateSendRecord,%a0 /*JANE2*/
	jsr (%a0)
.L1439:
	bsr.w DayViewRestoreEditState
	bra .L1394
	.even
.L1415:
	cmp.w #300,%d0
	jbne .L1419
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	lea ApptDescFont@END.w(%a5),%a2 /*FRED7b*/
	move.b (%a2),-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #SelectFont,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #8,%sp
	cmp.b (%a2),%d0
	jbeq .L1439
	move.b %d0,(%a2)
	bsr.w DayViewClearEditState
	bra .L1394
	.even
.L1419:
	cmp.w #301,%d0
	jbne .L1423
	bsr.w DayViewClearEditState
	move.w #1800,-(%sp)
#APP
	trap #15; dc.w 0xA19C
#NO_APP
	bra .L1394
	.even
.L1423:
	cmp.w #302,%d0
	jbne .L1425
	bsr.w DayViewClearEditState
	move.w #1900,-(%sp)
#APP
	trap #15; dc.w 0xA19C
#NO_APP
	bra .L1394
	.even
.L1425:
	cmp.w #303,%d0
	jbne .L1427
	bsr.w GetFocusObjectPtr
	move.l %a0,%d0
	jbeq .L1428
	lea InPhoneLookup@END.w(%a5),%a2 /*FRED7b*/
	move.b #1,(%a2)
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA2EF
#NO_APP
	clr.b (%a2)
	bra .L1394
	.even
.L1428:
	move.w #2002,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	bra .L1394
	.even
.L1427:
	cmp.w #304,%d0
	jbne .L1431
	bsr.w DayViewClearEditState
	bsr.w DoSecurity
	move.w #64,-(%sp)
	bsr.w DayViewUpdateDisplay
	bra .L1394
	.even
.L1431:
	cmp.w #305,%d0
	jbne .L1433
	bsr.w DayViewClearEditState
	move.l __text__@END.w(%a5),%a0
	add.l #ShowAboutDb35,%a0 /*JANE2*/
	jsr (%a0)
	bra .L1394
	.even
.L1433:
	cmp.w #10003,%d0
	jbne .L1435
	lea GetFocusObjectPtr(%pc),%a2 /*SPECIAL_FRED*/
	jsr (%a2)
	cmp.w #0,%a0
	jbne .L1436
	pea -6(%a6)
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA10C
#NO_APP
	addq.l #6,%sp
	cmp.w #0,%a0
	jbeq .L1436
	tst.w -6(%a6)
	jbeq .L1436
	clr.l -(%sp)
	bsr.w DayViewNewAppointment
	clr.b -(%sp)
	clr.b -(%sp)
	clr.b -(%sp)
#APP
	trap #15; dc.w 0xA281
#NO_APP
	jsr (%a2)
.L1436:
.L1435:
	clr.b %d4
.L1394:
	move.w %d4,%d0
	and.w #1,%d0
	movm.l -20(%a6),#0x418
	unlk %a6
	rts
	.even
DayViewUpdateDisplay:
	link.w %a6,#-40
	movm.l #0x1c38,-(%sp)
	move.w 8(%a6),%d3
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr GetObjectPtr(%a2)
	move.l %a0,%d5
	addq.l #2,%sp
	tst.w %d3
	jbge .L1441
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E8
#NO_APP
	addq.l #8,%sp
	tst.b %d0
	jbeq .L1442
	pea -28(%a6)
	pea -30(%a6)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	move.w -30(%a6),-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	lea (22,%sp),%sp
.L1442:
	bsr.w DayViewDrawTimeBars
	bra .L1440
	.even
.L1441:
	btst #6,%d3
	jbeq .L1444
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	bsr.w DayViewSetTopAppointment
	bra .L1461
	.even
.L1444:
	move.w %d3,%d0
	and.w #3,%d0
	jbeq .L1446
	jsr ClearEditState(%a2)
	bra .L1462
	.even
.L1446:
	btst #5,%d3
	jbeq .L1448
	pea -34(%a6)
	lea (-26,%a6),%a3
	move.l %a3,-(%sp)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	jsr ApptGetRecord(%a2)
	lea (14,%sp),%sp
	tst.l -18(%a6)
	jbeq .L1449
	lea Date@END.w(%a5),%a4 /*FRED7b*/
	move.l (%a4),-38(%a6)
	pea -38(%a6)
	move.l %a3,-(%sp)
	move.l %a2,%d4
	add.l #ApptNextRepeat,%d4 /*JANE2*/
	move.l %d4,%a0
	jsr (%a0)
	move.b %d0,%d3
	addq.l #8,%sp
	jbne .L1450
	move.l -26(%a6),%a0
	move.l 4(%a0),-38(%a6)
	pea -38(%a6)
	move.l %a3,-(%sp)
	move.l %d4,%a0
	jsr (%a0)
	move.b %d0,%d3
	addq.l #8,%sp
.L1450:
	move.l -34(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	addq.l #4,%sp
	tst.b %d3
	jbeq .L1460
	move.l -38(%a6),-(%sp)
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l (%a4),-(%sp)
	jsr (%a2)
	addq.l #8,%sp
	cmp.l %d3,%d0
	jbeq .L1454
	move.l -38(%a6),(%a4)
	move.l (%a4),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1440
	.even
.L1460:
	jsr ClearEditState(%a2)
	bra .L1454
	.even
.L1449:
	move.l -26(%a6),%a0
	move.l 4(%a0),-38(%a6)
	move.l -34(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l -38(%a6),-(%sp)
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	lea Date@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),-(%sp)
	jsr (%a2)
	lea (12,%sp),%sp
	cmp.l %d3,%d0
	jbeq .L1454
	move.l -38(%a6),(%a3)
	move.l (%a3),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1440
	.even
.L1454:
	pea -28(%a6)
	pea -30(%a6)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	clr.b -(%sp)
	move.w -30(%a6),-(%sp)
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1D3
#NO_APP
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	lea (22,%sp),%sp
	bra .L1443
	.even
.L1448:
	btst #2,%d3
	jbeq .L1457
	pea -34(%a6)
	pea -26(%a6)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	jsr ApptGetRecord(%a2)
	lea Date@END.w(%a5),%a2 /*FRED7b*/
	move.l -26(%a6),%a0
	move.l 4(%a0),(%a2)
	move.l -34(%a6),-(%sp)
#APP
	trap #15; dc.w 0xA022
#NO_APP
	move.l (%a2),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1440
	.even
.L1457:
	and.w #280,%d3
	jbeq .L1443
.L1462:
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
.L1461:
	addq.l #2,%sp
.L1443:
	bsr.w DayViewLoadTable
	move.l %d5,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bsr.w DayViewDrawTimeBars
.L1440:
	movm.l -64(%a6),#0x1c38
	unlk %a6
	rts
	.even
DayViewInit:
	link.w %a6,#-4
	movm.l #0x1f38,-(%sp)
	move.l 8(%a6),%d6
	lea Date@END.w(%a5),%a0 /*FRED7b*/
	clr.w %d0
	move.b 3(%a0),%d0
	move.w %d0,-2(%a6)
	clr.w %d0
	move.b 2(%a0),%d0
	move.w %d0,%a4
	move.w (%a0),%d7
	add.w #1904,%d7
	move.l %d6,-(%sp)
	bsr.w DayViewSetTitle
	addq.l #4,%sp
	cmp.w #1,StartDayOfWeek@END.w(%a5)
	jbne .L1464
	move.w #1003,-(%sp)
	lea __text__@END.w(%a5),%a3 /*FRED7b*/
	move.l (%a3),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,%d5
	move.w #1003,%d4
	addq.l #6,%sp
	.even
.L1468:
	move.w %d4,%d3
	addq.w #1,%d3
	move.w %d3,-(%sp)
	move.l (%a3),%a2
	add.l #GetObjectPtr,%a2 /*JANE2*/
	jsr (%a2)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA113
#NO_APP
	move.l %a0,-(%sp)
	move.w %d4,-(%sp)
	jsr (%a2)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	lea (14,%sp),%sp
	move.w %d3,%d4
	cmp.w #1008,%d4
	jbls .L1468
	move.l %d5,-(%sp)
	move.w #1009,-(%sp)
	move.l (%a3),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	addq.l #2,%sp
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA114
#NO_APP
	addq.l #8,%sp
.L1464:
	move.w %d7,-(%sp)
	move.w -2(%a6),-(%sp)
	move.w %a4,-(%sp)
	bsr.w DatebookDayOfWeek
	add.w #1003,%d0
	move.w %d0,-(%sp)
	move.b #1,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	move.w #1011,-(%sp)
	move.b #2,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA18B
#NO_APP
	move.w #1020,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA180
#NO_APP
	move.w %d0,-(%sp)
	move.l %d6,-(%sp)
#APP
	trap #15; dc.w 0xA183
#NO_APP
	move.l %a0,%d3
	lea (30,%sp),%sp
	move.l %d3,(%sp)
#APP
	trap #15; dc.w 0xA1D4
#NO_APP
	move.w %d0,%d4
	sub.l %a2,%a2
	addq.l #4,%sp
	cmp.w %a2,%d4
	jbls .L1471
	.even
.L1473:
	move.b #1,-(%sp)
	clr.w -(%sp)
	move.w %a2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	move.b #1,-(%sp)
	move.w #1,-(%sp)
	move.w %a2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	move.b #6,-(%sp)
	move.w #2,-(%sp)
	move.w %a2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D1
#NO_APP
	clr.b -(%sp)
	move.w %a2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D3
#NO_APP
	lea (38,%sp),%sp
	addq.w #1,%a2
	cmp.w %a2,%d4
	jbhi .L1473
.L1471:
	move.b #1,-(%sp)
	clr.w -(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1EA
#NO_APP
	move.b #1,-(%sp)
	move.w #1,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1EA
#NO_APP
	move.b #1,-(%sp)
	move.w #2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1EA
#NO_APP
	move.b #1,-(%sp)
	move.w #2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA3D0
#NO_APP
	lea (32,%sp),%sp
	clr.w -(%sp)
	clr.w -(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1F5
#NO_APP
	move.w #2,-(%sp)
	move.w #1,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1F5
#NO_APP
	clr.b -(%sp)
	clr.w -(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA2D8
#NO_APP
	lea DayViewGetDescription(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D8
#NO_APP
	lea (34,%sp),%sp
	lea DayViewSaveDescription(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D9
#NO_APP
	lea DayViewDrawTime(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #1,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D5
#NO_APP
	lea DayViewDrawIcons(%pc),%a0 /*SPECIAL_FRED*/
	move.l %a0,-(%sp)
	move.w #2,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA1D5
#NO_APP
	clr.w TimeBarColumns@END.w(%a5)
	move.b #1,-(%sp)
	bsr.w DayViewLayoutDay
	lea ItemSelected@END.w(%a5),%a2 /*FRED7b*/
	lea (32,%sp),%sp
	tst.b (%a2)
	jbne .L1475
	bsr.w DayViewSetTopAppointment
.L1475:
	lea PendingUpdate@END.w(%a5),%a3 /*FRED7b*/
	move.w (%a3),%d0
	jbeq .L1476
	tst.b (%a2)
	jbeq .L1476
	move.w %d0,-(%sp)
	bsr.w DayViewUpdateDisplay
	bra .L1477
	.even
.L1476:
	bsr.w DayViewLoadTable
.L1477:
	clr.b TimeDisplayed@END.w(%a5)
	clr.w (%a3)
	movm.l -36(%a6),#0x1cf8
	unlk %a6
	rts
	.even
.globl DayViewHandleEvent
DayViewHandleEvent:
	link.w %a6,#-24
	movm.l #0x1e38,-(%sp)
	move.l 8(%a6),%a3
	clr.b %d5
	move.w (%a3),%d0
	cmp.w #4,%d0
	jbne .L1479
	move.w 12(%a3),%d1
	btst #3,%d1
	jbeq .L1480
	move.w 8(%a3),%d2
	move.w %d2,%d0
	add.w #-512,%d0
	cmp.w #255,%d0
	jbls .L1481
	cmp.w #267,%d2
	jbne .L1480
.L1481:
	bsr.w DayViewClearEditState
	pea -4(%a6)
#APP
	trap #15; dc.w 0xA0F5
#NO_APP
	move.l %d0,-(%sp)
	move.l __text__@END.w(%a5),%a2
	jsr EDateSecondsToDate(%a2)
	move.l -4(%a6),-(%sp)
	add.l #EDateInt,%a2 /*JANE2*/
	jsr (%a2)
	move.l %d0,%d3
	move.l Date@END.w(%a5),-(%sp)
	jsr (%a2)
	lea (16,%sp),%sp
	cmp.l %d3,%d0
	jbne .L1483
	btst #0,12(%a3)
	jbeq .L1482
.L1483:
	move.l -4(%a6),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1640
	.even
.L1482:
	move.w #1100,-(%sp)
	bra .L1617
	.even
.L1480:
	and.w #1544,%d1
	jbeq .L1486
	move.w 8(%a3),%d0
	cmp.w #11,%d0
	jbne .L1487
	bsr.w DayViewClearEditState
	lea TopVisibleAppt@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d3
	clr.b -(%sp)
	clr.b -(%sp)
	bsr.w DayViewScroll
	addq.l #4,%sp
	tst.b %d0
	jbeq .L1489
	cmp.w (%a2),%d3
	jbne .L1640
.L1489:
	pea -1.w
	lea Date@END.w(%a5),%a2 /*FRED7b*/
	move.l %a2,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateAdjust,%a0 /*JANE2*/
	jsr (%a0)
	move.l (%a2),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1640
	.even
.L1487:
	cmp.w #12,%d0
	jbne .L1491
	bsr.w DayViewClearEditState
	lea TopVisibleAppt@END.w(%a5),%a2 /*FRED7b*/
	move.w (%a2),%d3
	clr.b -(%sp)
	move.b #1,-(%sp)
	bsr.w DayViewScroll
	addq.l #4,%sp
	tst.b %d0
	jbeq .L1493
	cmp.w (%a2),%d3
	jbne .L1640
.L1493:
	pea 1.w
	lea Date@END.w(%a5),%a2 /*FRED7b*/
	move.l %a2,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateAdjust,%a0 /*JANE2*/
	jsr (%a0)
	move.l (%a2),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1640
	.even
.L1491:
	cmp.w #278,%d0
	jbne .L1495
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1496
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #DateSendRecord,%a0 /*JANE2*/
	jsr (%a0)
	bra .L1628
	.even
.L1496:
	move.w #2002,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	bra .L1640
	.even
.L1495:
	clr.b %d5
	bra .L1506
	.even
.L1486:
	move.w 8(%a3),-(%sp)
#APP
	moveq #2,%d2; trap #15; dc.w 0xA2AB
#NO_APP
	and.w #574,%d0
	addq.l #2,%sp
	jbeq .L1506
	lea ItemSelected@END.w(%a5),%a2 /*FRED7b*/
	tst.b (%a2)
	jbne .L1506
	move.w 8(%a3),-(%sp)
#APP
	moveq #2,%d2; trap #15; dc.w 0xA2AB
#NO_APP
	addq.l #2,%sp
	btst #5,%d0
	jbeq .L1502
	move.l %a3,-(%sp)
	bsr.w DayViewNewAppointment
	move.b %d0,%d5
	bra .L1506
	.even
.L1502:
	clr.l -(%sp)
	bsr.w DayViewNewAppointment
	addq.l #4,%sp
	tst.b (%a2)
	jbeq .L1506
	move.w 8(%a3),%d1
	move.w %d1,%d0
	add.w #-97,%d0
	cmp.w #25,%d0
	jbhi .L1505
	add.w #-32,%d1
	move.w %d1,8(%a3)
.L1505:
	move.l %a3,-(%sp)
#APP
	trap #15; dc.w 0xA11B
#NO_APP
.L1640:
	moveq.l #1,%d5
	bra .L1506
	.even
.L1479:
	cmp.w #1,%d0
	jbne .L1507
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a3,-(%sp)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA17A
#NO_APP
	move.b %d0,%d5
	addq.l #8,%sp
	jbne .L1506
.L1624:
	bsr.w DayViewClearEditState
	bra .L1506
	.even
.L1507:
	cmp.w #7,%d0
	jbne .L1510
	move.w 8(%a3),%d0
	cmp.w #1002,%d0
	jbcs .L1506
	cmp.w #1015,%d0
	jbls .L1624
	cmp.w #1019,%d0
	jbhi .L1506
	cmp.w #1017,%d0
	jbcs .L1506
	bra .L1624
	.even
.L1510:
	cmp.w #9,%d0
	jbne .L1532
	move.w 8(%a3),%d0
	add.w #-1003,%d0
	cmp.w #14,%d0
	jbhi .L1506
	and.l #0xFFFF,%d0
	lea .L1550(%pc),%a0 /*SPECIAL_FRED*/
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),%d0
	jmp %pc@(2,%d0:w)
	.even
.L1550:
	.word .L1549-.L1550
	.word .L1549-.L1550
	.word .L1549-.L1550
	.word .L1549-.L1550
	.word .L1549-.L1550
	.word .L1549-.L1550
	.word .L1549-.L1550
	.word .L1506-.L1550
	.word .L1506-.L1550
	.word .L1534-.L1550
	.word .L1535-.L1550
	.word .L1536-.L1550
	.word .L1540-.L1550
	.word .L1537-.L1550
	.word .L1542-.L1550
	.even
.L1534:
	move.w #1100,-(%sp)
	bra .L1617
	.even
.L1535:
	move.w #1200,-(%sp)
	bra .L1617
	.even
.L1536:
	move.w #2200,-(%sp)
.L1617:
#APP
	trap #15; dc.w 0xA19B
#NO_APP
	bra .L1640
	.even
.L1537:
	tst.b ItemSelected@END.w(%a5)
	jbne .L1634
	move.w #2002,-(%sp)
#APP
	trap #15; dc.w 0xA192
#NO_APP
	bra .L1640
	.even
.L1540:
	clr.l -(%sp)
	bsr.w DayViewNewAppointment
	addq.l #4,%sp
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1506
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,%a2
	pea -6(%a6)
	pea -8(%a6)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1E1
#NO_APP
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1E7
#NO_APP
	move.w -8(%a6),-(%sp)
	move.l %a2,-(%sp)
	bsr.w DayViewSelectTime
	bra .L1506
	.even
.L1542:
	bsr.w DayViewGoToDate
	bra .L1640
	.even
.L1549:
	move.l %a3,-(%sp)
	bsr.w DayViewDayOfWeekSelected
	bra .L1640
	.even
.L1532:
	cmp.w #8,%d0
	jbne .L1553
	cmp.w #1016,8(%a3)
	jbne .L1506
	bsr.w DayViewRestoreEditState
	bra .L1506
	.even
.L1553:
	cmp.w #10,%d0
	jbne .L1556
	move.w 8(%a3),%d0
	cmp.w #1010,%d0
	jbeq .L1559
	jbhi .L1564
	cmp.w #1002,%d0
	jbeq .L1558
	bra .L1506
	.even
.L1564:
	cmp.w #1018,%d0
	jbeq .L1560
	cmp.w #1019,%d0
	jbeq .L1561
	bra .L1506
	.even
.L1558:
	pea -1.w
	bra .L1618
	.even
.L1559:
	pea 1.w
.L1618:
	lea Date@END.w(%a5),%a2 /*FRED7b*/
	move.l %a2,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #EDateAdjust,%a0 /*JANE2*/
	jsr (%a0)
	move.l (%a2),-(%sp)
	bsr.w DayViewDrawDate
	bra .L1506
	.even
.L1560:
	clr.b -(%sp)
	clr.b -(%sp)
	bsr.w DayViewScroll
	bra .L1506
	.even
.L1561:
	clr.b -(%sp)
	move.b #1,-(%sp)
	bsr.w DayViewScroll
	bra .L1506
	.even
.L1556:
	cmp.w #18,%d0
	jbne .L1566
	move.l 10(%a3),%a2
	bsr.w DayLockedBounce
	tst.b %d0
	jbne .L1640
	lea ItemSelected@END.w(%a5),%a4 /*FRED7b*/
	tst.b (%a4)
	jbeq .L1569
	clr.l -(%sp)
	pea -12(%a6)
	clr.l -(%sp)
	move.w CurrentRecord@END.w(%a5),-(%sp)
	move.l ApptDB@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA050
#NO_APP
	pea -8(%a6)
	move.l -12(%a6),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1F2
#NO_APP
	lea (30,%sp),%sp
	tst.b %d0
	jbeq .L1569
	move.w -8(%a6),%d0
	cmp.w 14(%a3),%d0
	jbeq .L1569
	bsr.w DayViewClearEditState
	move.b %d0,%d5
.L1569:
	tst.w 16(%a3)
	jbeq .L1640
	tst.b %d5
	jbne .L1506
	move.l %a3,-(%sp)
	bsr.w DayViewSelectIcons
	move.b %d0,%d5
	addq.l #4,%sp
	jbne .L1506
	cmp.w #2,16(%a3)
	jbne .L1506
	move.w 14(%a3),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1DD
#NO_APP
	move.w %d0,%d6
	move.l ApptsH@END.w(%a5),-(%sp)
#APP
	trap #15; dc.w 0xA021
#NO_APP
	clr.l %d1
	move.w %d6,%d1
	move.l %d1,%d0
	lsl.l #2,%d0
	add.l %d1,%d0 /*JANE2*/
	add.l %d1,%d0 /*JANE2*/
	move.w 4(%a0,%d0.l),%d4
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA035
#NO_APP
	move.w %d4,%d3
	add.w #4096,%d3
	lea (14,%sp),%sp
	cmp.w #255,%d3
	jbhi .L1576
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w %d3,-(%sp)
	bsr.w DayLaunchTodo
	bra .L1640
	.even
.L1576:
	cmp.w #61695,%d4
	jbls .L1578
	cmp.w #65535,%d4
	jbeq .L1578
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	lea gDayBdayRec@END.w(%a5),%a0 /*FRED7b*/
	add.w #3840,%d4
	clr.l %d0
	move.w %d4,%d0
	add.l %d0,%d0 /*JANE2*/
	move.w (%a0,%d0.l),-(%sp)
	move.l __text__sec003@END.w(%a5),%a0
	add.l #AddrBdayGoTo,%a0 /*JANE2*/
	bra .L1620
	.even
.L1578:
	move.w %d4,%d0
	add.w #8192,%d0
	cmp.w #4095,%d0
	jbhi .L1580
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w %d4,-(%sp)
	move.l __text__sec002@END.w(%a5),%a0
	add.l #DayDavShowInfo,%a0 /*JANE2*/
.L1620:
	jsr (%a0)
	bra .L1640
	.even
.L1580:
	cmp.w #65535,%d4
	jbeq .L1582
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1D2
#NO_APP
	move.w %d4,CurrentRecord@END.w(%a5)
	move.w %d6,gDetailsApptIndex@END.w(%a5)
	move.b #1,(%a4)
	move.b #1,gDaySkipRestoreEditState@END.w(%a5)
.L1634:
	move.w #1300,-(%sp)
#APP
	trap #15; dc.w 0xA19C
#NO_APP
	bra .L1640
	.even
.L1582:
	move.w 14(%a3),-(%sp)
	move.l %a2,-(%sp)
	bsr.w DayEnterEditStyle
	move.w 14(%a3),-(%sp)
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1DF
#NO_APP
	move.l %a2,-(%sp)
#APP
	trap #15; dc.w 0xA1E5
#NO_APP
	bra .L1506
	.even
.L1566:
	cmp.w #19,%d0
	jbne .L1585
	move.l %a3,-(%sp)
	bsr.w DayViewItemSelected
	bra .L1640
	.even
.L1585:
	cmp.w #16,%d0
	jbne .L1587
	move.l %a3,-(%sp)
	bsr.w DayViewResizeDescription
	bra .L1640
	.even
.L1587:
	cmp.w #2048,%d0
	jbne .L1589
	tst.b ItemSelected@END.w(%a5)
	jbeq .L1590
	move.w #1020,-(%sp)
	move.l __text__@END.w(%a5),%a0
	add.l #GetObjectPtr,%a0 /*JANE2*/
	jsr (%a0)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA1E9
#NO_APP
	pea -14(%a6)
	pea -16(%a6)
	move.l %a0,-(%sp)
#APP
	trap #15; dc.w 0xA13A
#NO_APP
	lea (18,%sp),%sp
	move.w -14(%a6),%d0
	cmp.w -16(%a6),%d0
	jbeq .L1591
	clr.l -(%sp)
	pea 10003.w
	move.b #2,-(%sp)
	move.w #10032,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	clr.l -(%sp)
	pea 10002.w
	move.b #2,-(%sp)
	move.w #10031,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	clr.l -(%sp)
	pea 10001.w
	move.b #2,-(%sp)
	move.w #10030,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	lea (38,%sp),%sp
	clr.l (%sp)
	pea 10000.w
	move.b #2,-(%sp)
	move.w #10033,-(%sp)
	bra .L1621
	.even
.L1591:
	clr.l -(%sp)
	pea 101.w
	move.b #2,-(%sp)
	move.w #10036,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	clr.l -(%sp)
	pea 10003.w
	move.b #2,-(%sp)
	move.w #10032,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	clr.l -(%sp)
	pea 304.w
	move.b #2,-(%sp)
	move.w #10035,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	lea (38,%sp),%sp
	clr.l (%sp)
	pea 105.w
	move.b #2,-(%sp)
	move.w #10034,-(%sp)
.L1621:
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
	bra .L1593
	.even
.L1590:
	clr.l -(%sp)
	pea 304.w
	move.b #2,-(%sp)
	move.w #10035,-(%sp)
	st -(%sp)
#APP
	trap #15; dc.w 0xA3C7
#NO_APP
.L1593:
	move.b #1,8(%a3)
	bra .L1506
	.even
.L1589:
	cmp.w #21,%d0
	jbne .L1595
	move.w 8(%a3),-(%sp)
	bsr.w DayViewDoCommand
	move.b %d0,%d5
	bra .L1506
	.even
.L1595:
	cmp.w #24,%d0
	jbne .L1597
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	bra .L1622
	.even
.L1597:
	cmp.w #25,%d0
	jbne .L1599
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	move.l %a3,-(%sp)
	bsr.w DayViewGotoAppointment
.L1622:
	move.l %d3,-(%sp)
	bsr.w DayViewInit
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA171
#NO_APP
	bsr.w DayViewDrawTimeBars
.L1628:
	bsr.w DayViewRestoreEditState
	bra .L1640
	.even
.L1599:
	cmp.w #26,%d0
	jbne .L1601
	move.w 10(%a3),-(%sp)
	bsr.w DayViewUpdateDisplay
	addq.l #2,%sp
	cmp.w #32768,10(%a3)
	jbeq .L1640
	bsr.w DayViewRestoreEditState
	bra .L1640
	.even
.L1601:
	cmp.w #27,%d0
	jbeq .L1624
	cmp.w #29,%d0
	jbne .L1606
	pea 5.w
#APP
	trap #15; dc.w 0xA0A0
#NO_APP
	pea -17(%a6)
	pea -20(%a6)
	pea -22(%a6)
#APP
	trap #15; dc.w 0xA11E
#NO_APP
	lea (16,%sp),%sp
	tst.b -17(%a6)
	jbeq .L1506
	tst.b TimeDisplayed@END.w(%a5)
	jbeq .L1608
	move.b #1,-(%sp)
	bsr.w DayViewHideTime
	bra .L1506
	.even
.L1608:
	bsr.w DayViewShowTime
	bra .L1506
	.even
.L1606:
	tst.w %d0
	jbne .L1611
	clr.b -(%sp)
	bsr.w DayViewHideTime
	bra .L1506
	.even
.L1611:
	cmp.w #28,%d0
	jbne .L1506
#APP
	trap #15; dc.w 0xA173
#NO_APP
	move.l %a0,%d3
	jbeq .L1614
	move.w #65535,-(%sp)
	move.l %d3,-(%sp)
#APP
	trap #15; dc.w 0xA179
#NO_APP
	addq.l #6,%sp
.L1614:
	bsr.w DayViewFreeRowCopies
	lea ApptsH@END.w(%a5),%a2 /*FRED7b*/
	move.l (%a2),-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
	lea ApptsOnlyH@END.w(%a5),%a2 /*FRED7b*/
	addq.l #4,%sp
	move.l (%a2),%d0
	jbeq .L1506
	move.l %d0,-(%sp)
#APP
	trap #15; dc.w 0xA02B
#NO_APP
	clr.l (%a2)
.L1506:
	clr.w %d0
	move.b %d5,%d0
	movm.l -52(%a6),#0x1c78
	unlk %a6
	rts
.lcomm gDayRowH,64
.lcomm gDayRowHasLoc,16
.lcomm gDayRowDescHash,32
.lcomm gDayTodoRec,48
.lcomm gDayTodoDesc,1536
.lcomm gDayTodoUID,96
.lcomm gDayTodoTimed,24
.lcomm gDayTodoStart,48
.lcomm gDayTodoNote,24
.lcomm gDayTodoRepeat,24
.lcomm gDayBdayLines,768
.lcomm gDayBdayRec,24
.lcomm gDayBdayUID,48
.lcomm gDayDavLines,3072
.lcomm gDayDavHash,64
.lcomm gDayDavUID,64
.lcomm gDayDavTimed,16
.lcomm gDayDavStart,32
.lcomm gDayDavEnd,32
.lcomm gDayLocText,12288
