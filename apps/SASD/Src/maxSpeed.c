#include "maxSpeed.h"


#define DEFAULT_SPEED					115200
#define M68K_DEFAULT_SPEED_LIMIT		230400

//they can all do Fcpu / 16 spped in theory

//but the uart iface chip limits that:
// * sipex promises 120kbps, and can do 207 to our voltages
// * MAX can do 625
// * LTC can ???
//pilot 1000/5000 have max
//pilot pro/pers have sipex
//III has sipex
//at least some IIIx have sipex
//my iiic has max

//our IIIc has shown to work at 625000 and pilot 1000 has shown function at at 518144


/*

	mult	506				618				1012
	freq	16580608		20250624		33161216
	avail:	
			518144			632832			1036288
			345429.3		421888			690858.6
			259072			316416			518144
			207257.6		253132.8		414515.2
			172714.6		210944			345429.3
			148041.1		180809			296082.3
			129536			158208			259072
			115143.1		140629.3		230286.2
							126566.4		207257.6
							115060.4		188416
											172714.6
											159428.9
											148041.1
											138171.7
											129536
											121916.2
											115143.1

*/

UInt32 getMaxSpeed(void)
{
	UInt32 processorType, speedLimit, sysclock, uartClock;
	
	if (errNone == FtrGet(sysFtrCreator, sysFtrNumProcessorID, &processorType))  switch ((UInt16)(processorType >> 16)) {
		
		case sysFtrNumProcessor328 >> 16:
		case sysFtrNumProcessorEZ >> 16:
		case sysFtrNumProcessorVZ >> 16:
		case sysFtrNumProcessorSuperVZ >> 16:
			if (!PrefGetAppPreferencesV10('PpSS', 0, &speedLimit, sizeof(speedLimit)))		//if we have a pref, use it, else default limit is used
				speedLimit = M68K_DEFAULT_SPEED_LIMIT;
			
			sysclock = (*(volatile UInt32*)0x12E);
			uartClock = sysclock / 16;
			
			//make sure we never try to go faster than uartClock / 2
			if (speedLimit > uartClock / 2)
				speedLimit = uartClock / 2;
		
			//calc the speed we can attain (max speed available from an integer divider of sysclk)
			return uartClock / ((uartClock + speedLimit - 1) / speedLimit);
	}

	return DEFAULT_SPEED;
}
