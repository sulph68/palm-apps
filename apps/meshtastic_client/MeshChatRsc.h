/*
 * MeshChatRsc.h - resource IDs for MeshChat.rcp. Hand-written; keep in step with the .rcp by
 * hand (pilrc can't parse <PalmOS.h>).
 */
#ifndef MESHCHATRSC_H
#define MESHCHATRSC_H

#define frmMain					1000
#define lstMainCat				1005
#define popMainCat				1004
#define gadMainBatt				1002
#define fldMainInfo				1003
#define gadMainLine				1006
#define tblMain					1007
#define sclMain					1008
#define lblMainFind				1009
#define fldMainFind				1014
#define btnMainClear			1013
#define btnMainConn				1015
#define btnMainSync				1016
#define btnMainNodeSync			1017
#define mnuMain					1000
#define mniMainSync				1020
#define mniMainConnect			1030
#define mniMainDisconnect		1031
#define mniMainClear			1021
#define mniMainPrefs			1022
#define mniMainAbout			1023
#define mniMainSetup			1024
#define mniMainPair				1025

#define frmChat					1100
#define sclChat					1102
#define fldChatEntry			1103
#define gadChatCnt				1105
#define fldChatInfo				1106
#define gadChatBatt				1107
#define gadChatLine				1108
#define gadChatList				1109
#define btnChatSend				1110
#define btnChatBack				1113
#define popChatConv			1114
#define lstChatConv			1115
#define gadChatSep			1116
#define mnuChat					3200
#define mniChatInfo				3201
#define mniChatDm				3202
#define mniChatAll				3203
#define mniChatDetail			3204
#define mniChatClear			3205
#define mniChatRead				3206	/* Mark all read: the open conversation */
#define mniSendAlert			3210
#define mniSendInfo				3211
#define mniSendPos				3212
#define mniSendTele				3213
#define mniSendTrace			3214
#define mniSendShare			3215
#define mniSendWay				3216

#define frmMsg					3400
#define fldMsgText				3401
#define sclMsg					3402
#define btnMsgDone				3403
#define fldMsgHead				3404

#define frmPos					3500
#define fldPosLat				3501
#define fldPosLon				3502
#define fldPosName				3503
#define lblPosName				3504
#define btnPosSend				3505
#define btnPosCancel			3506

#define frmNode					2700
#define ckNodeFav				2701
#define gadNodeLine				2702
#define tblNodeInfo				2714
#define sclNodeInfo				2713
#define btnNodeBack				2710
#define btnNodeMsg				2711

#define frmBusy					3300
#define fldBusyText				3301
#define btnBusyCancel			3302
#define frmConnect				3310
#define fldConnText				3311
#define btnConnCancel			3312

#define frmSetup				1400
#define lstSetup				1401
#define fldSetupNode			1402
#define btnSetupDone			1411
#define btnSetupCancel			1412
#define btnSetupReboot			1413
#define gadSetupLine			1403
#define tblSetup				1404

#define frmOwner				1500
#define fldOwnerLong			1501
#define fldOwnerShort			1502
#define btnOwnerOk				1510
#define btnOwnerCancel			1511

#define frmLora					1600
#define popLoraRegion			1601
#define lstLoraRegion			1602
#define popLoraPreset			1603
#define lstLoraPreset			1604
#define popLoraHop				1605
#define lstLoraHop				1606
#define fldLoraTx				1607
#define fldLoraNote				1608
#define btnLoraTxSet			1612
#define btnLoraOk				1610
#define btnLoraCancel			1611

#define frmDevice				1700
#define popDevRole				1701
#define lstDevRole				1702
#define popDevBt				1703
#define lstDevBt				1704
#define fldDevPin				1705
#define btnDevPinSet			1712
#define btnDevOk				1710
#define btnDevCancel			1711
#define lblDevRole				1713
#define lblDevBt				1714
#define lblDevPin				1715
#define lblDevHint				1716

#define frmChList				1800
#define lstChan					1801
#define btnChEdit				1810
#define btnChBack				1811

#define frmChEdit				1900
#define ckChUplink				1980
#define ckChDownlink			1981
#define frmMqtt					1950
#define fldMqAddr				1951
#define fldMqUser				1952
#define fldMqPass				1953
#define fldMqRoot				1954
#define ckMqEnabled				1955
#define ckMqEncrypt				1956
#define ckMqTls					1957
#define ckMqJson				1958
#define ckMqProxy				1959
#define ckMqMap					1960
#define btnMqOk					1961
#define btnMqCancel				1962
#define fldChName				1901
#define popChRole				1902
#define lstChRole				1903
#define fldChKey				1904
#define fldChKeyNote			1905
#define ckChKeyShow				1906	/* reveals fldChKey (unlabelled, no room) */
#define btnChNewKey				1910
#define btnChOk					1911
#define btnChCancel				1912

#define frmActions				2000
#define fldActNode				2001
#define fldActPalm				2002
#define fldActDiff				2003
#define btnActSetTime			2010
#define btnActReboot			2011
#define btnActDone				2012

#define frmRelay				2100
#define popRelayNode			2119
#define lstRelayNode			2122
#define popRelayMode			2102
#define lstRelayMode			2103
#define popRelayEvery			2104
#define lstRelayEvery			2105
#define btnRelayScan			2110
#define btnRelayDone			2113
#define lblRelayEvery			2107
#define popRelayFlush			2116
#define lstRelayFlush			2117
#define lblRelayFlush			2118

#define lstScan					2201
#define btnScanPair				2210
#define btnRelayUnpair			2114
#define gadRelayLine			2108
#define lblRelayFound			2109
#define lblRelayDbm				2115
#define mnuRelay				2120
#define mniRelayClear			2121

#define frmClear				2400
#define popClearAge				2401
#define lstClearAge				2402
#define fldClearInfo			2403
#define btnClearOk				2410
#define btnClearCancel			2411

#define frmPrefs				2500
#define popPrefBaud				2501
#define lstPrefBaud				2502
#define ckPrefAuto				2503
#define popPrefTest				2504
#define lstPrefTest				2505
#define fldPrefNote1			2506
#define fldPrefNote2			2507
#define ckPrefHist				2508
#define lblPrefBaud				2509
#define lblPrefHist				2512
#define btnPrefOk				2510
#define btnPrefCancel			2511

#define frmKeypad				2600
#define fldKeyDisplay			2602
#define gadKey0					2610
#define gadKey1					2611
#define gadKey2					2612
#define gadKey3					2613
#define gadKey4					2614
#define gadKey5					2615
#define gadKey6					2616
#define gadKey7					2617
#define gadKey8					2618
#define gadKey9					2619
#define gadKeyDel				2621
#define btnKeyOk				2622
#define btnKeyCancel			2623

#define bmpKey					3500

#define altInfo					3000
#define altAsk					3001
#define altUnpair				3009
#define altConnecting			3002
#define altSend					3003

#define mnuEdit					3100
#define mniEditUndo				3101
#define mniEditCut				3102
#define mniEditCopy				3103
#define mniEditPaste			3104
#define mniEditSelectAll		3105
#define mniEditKeyboard			3106
#define mniEditGraffiti			3107

#endif
