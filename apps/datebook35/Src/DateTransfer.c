/******************************************************************************
 *
 * Copyright (c) 1998-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateTransfer.c
 *
 * Description:
 *      DateBook routines to transfer records.
 *
 * History:
 *			Name		Date		Description
 *			----		----		-----------
 *			djk		8/9/97	Initial Revision
 *			rbb		6/10/99	Removed obsoleted code that worked around
 *									single-segment linker limitation
 *
 *****************************************************************************/

#include <PalmOS.h>

// DOLATER kwk - decide if everything in this file is assumed to only
// be dealing with single-byte (low ascii) text.
#define NON_INTERNATIONAL
#include <CharAttr.h>		// GetCharAttr()

#include "Datebook.h"
//#include "DateTime.h"
//#include  "DateTransfer.h"



#define identifierLengthMax		40
#define tempStringLengthMax		24			// AALARM is longest
#define dateDBType				'DATA'

#define dateFilenameExtension		".vcs"

/////////////////////////////////////
//  Macros
#define BitAtPosition(pos)                ((UInt32)1 << (pos))
#define GetBitMacro(bitfield, index)      ((bitfield) & BitAtPosition(index))
#define SetBitMacro(bitfield, index)      ((bitfield) |= BitAtPosition(index))
#define RemoveBitMacro(bitfield, index)   ((bitfield) &= ~BitAtPosition(index))



static UInt16 GetChar(const void * exgSocketP) SEC_RECV;
static void PutString(void * exgSocketP, const Char * const stringP) SEC003;


/***********************************************************************
 *
 *   Global variables from MainApp can NOT be referenced in our PlugIn!
 *
 ***********************************************************************/


/***********************************************************************
 *
 * FUNCTION:    ApptGetAlarmTimeVCalForm
 *
 * DESCRIPTION: This routine determines the date and time of the next alarm
 *              for the appointment passed.
 *
 * PARAMETERS:  apptRec     - pointer to an appointment record
 *
 * RETURNED:    date and time of the alarm, in seconds, or zero if there
 *              is no alarm
 *
 *	NOTE:		the only differences between this function and the 
 *             	function ApptGetAlarmTime in the App is that 
 *			 	this function does not return 0 if the alarm time has passed
 *				and it returns the first event Date as the alarm date
 *				for reapeating events
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art		6/20/95		Initial Revision
 *			djk		7/28/97		modified for vCal
 *
 ***********************************************************************/
SEC003 static UInt32 ApptGetAlarmTimeVCalForm (ApptDBRecordPtr apptRec)
{
	UInt32 advance;
	UInt32 alarmTime;
	DateTimeType apptDateTime;


	// An alarm on an untimed event triggers at midnight.
	if (TimeToInt (apptRec->when->startTime) == apptNoTime)
		{
		apptDateTime.minute = 0;
		apptDateTime.hour = 0;
		}
	else
		{
		apptDateTime.minute = apptRec->when->startTime.minutes;
		apptDateTime.hour = apptRec->when->startTime.hours;
		}
	apptDateTime.second = 0;
	apptDateTime.day = apptRec->when->date.day;
	apptDateTime.month = apptRec->when->date.month;
	apptDateTime.year = apptRec->when->date.year + firstYear;



	// Compute the time of the alarm by adjusting the date and time 
	// of the appointment by the length of the advance notice.
	advance = apptRec->alarm->advance;
	switch (apptRec->alarm->advanceUnit)
		{
		case aauMinutes:
			advance *= minutesInSeconds;
			break;
		case aauHours:
			advance *= hoursInSeconds;
			break;
		case aauDays:
			advance *= daysInSeconds;
			break;
		}

	alarmTime = TimDateTimeToSeconds (&apptDateTime) - advance;

	return alarmTime;
}


/************************************************************
 *
 * FUNCTION: TranslateAlarm
 *
 * DESCRIPTION: Translate an alarm in seconds to a DateTimeType.
 * Broken out of DateImportVEvent for linking on the device purposes.
 *
 * PARAMETERS: 
 *			newDateRecordP - the new record
 *			alarmDTinSec - date and time of the alarm in seconds
 *
 * RETURNS: nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	9/19/97	Created
 *
 *************************************************************/

SEC_RECV static void TranslateAlarm(ApptDBRecordType *newDateRecordP, UInt32 alarmDTinSec)
{

	DateTimeType eventDT;
	UInt32 alarmAdvanceSeconds;
	UInt32 alarmAdvance;

	// allocate a new AlarmInfoType
	newDateRecordP->alarm = (AlarmInfoType *) MemPtrNew (sizeof(AlarmInfoType));
			
	eventDT.year  = newDateRecordP->when->date.year + firstYear;
	eventDT.month  = newDateRecordP->when->date.month;
	eventDT.day  = newDateRecordP->when->date.day;
	
	if (TimeToInt (newDateRecordP->when->startTime) == noTime)
		{
		// The time for alarms is midnight.
		eventDT.hour  = 0;
		eventDT.minute = 0;
		}
	else
		{
		eventDT.hour  = newDateRecordP->when->startTime.hours;
		eventDT.minute = newDateRecordP->when->startTime.minutes;
		}
	eventDT.second = 0;

	alarmAdvanceSeconds = TimDateTimeToSeconds(&eventDT) - alarmDTinSec;

	// convert to minutes
	alarmAdvance = alarmAdvanceSeconds / minutesInSeconds;

	if (alarmAdvance < 100 && alarmAdvance != hoursInMinutes)
		{
		newDateRecordP->alarm->advanceUnit = aauMinutes;
		newDateRecordP->alarm->advance = (Int8) alarmAdvance;
		}
	else
		{
		// convert to hours
		alarmAdvance = alarmAdvance / hoursInMinutes;
		
		if (alarmAdvance < 100 && (alarmAdvance % hoursPerDay))
			{
			newDateRecordP->alarm->advanceUnit = aauHours;
			newDateRecordP->alarm->advance = (Int8) alarmAdvance;
			}
		else
			{
			// convert to days
			alarmAdvance = alarmAdvance / hoursPerDay;
			
			// set to the lesser of 99 and alarmAdvance
			newDateRecordP->alarm->advanceUnit = aauDays;
			
				if (alarmAdvance < 99)
					newDateRecordP->alarm->advance = (Int8) alarmAdvance;
				else
					newDateRecordP->alarm->advance = 99;
			}
		}
}


#pragma mark ----------------------------
/************************************************************
 *
 * FUNCTION: GetToken
 *
 * DESCRIPTION: Extracts first available token from given
 *		string. Tokens are assumed to be separated by "white
 *		space", as used by the IsSpace() function.
 *
 * PARAMETERS: 
 *		startP		-	str ptr from which to extract
 *		tokenP		-	str ptr where to store found token
 *
 * RETURNS: str ptr of where to start next token, or null if
 *		end of string is reached.
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			frigino	12/3/97	Stolen from rest of code & modified
 *
 *************************************************************/
SEC_RECV static char* GetToken(char* startP, char* tokenP)
{
	char		c;

	// Skip leading "blank space"
	while (TxtCharIsSpace(*startP))
		startP += TxtNextCharSize(startP, 0);

	// DOLATER kwk - figure out if we need to worry about
	// anything other than 7-bit ascii in this routine.
	
	// Get first char
	c = *startP;
	// While char is not terminator, nor is it "blank space"
	while (c != '\0' && !TxtCharIsSpace(c))
		{
		// Copy char to token
		*tokenP++ = c;
		// Advance to next char
		c = *(++startP);
		}
	// Terminate token
	*tokenP = '\0';

	// Skip trailing "blank space"
	if (c != '\0')
		while (TxtCharIsSpace(*startP))
			++startP;

	// Return next token ptr
	return ((*startP == '\0') ? NULL : startP);
}


/************************************************************
 *
 * FUNCTION: MatchDateTimeToken
 *
 * DESCRIPTION: Extract date and time from the given string
 *
 * PARAMETERS: 
 *		tokenP	-	string ptr from which to extract
 *		dateP		-	ptr where to store date (optional)
 *		timeP		-	ptr where to store time (optional)
 *
 * RETURNS: nothing
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			frigino	12/3/97	Stolen from rest of code & modified
 *
 *************************************************************/
SEC_RECV static void MatchDateTimeToken(
	const char*	tokenP,
	DateType*	dateP,
	TimeType*	timeP)
{
	char			identifier[identifierLengthMax];
	char*			timeStartP;
	int			nv;

	// Use identifier[] as a temp buffer to copy parts of the vCal DateTime
	// so we can convert them to correct form.  This date portion 
	// is 4 chars (date) + 2 chars (month) + 2 chars (day) = 8 chars long

	// Try to get date if desired by caller. It must precede the time
	if (dateP != NULL)
		{
		// Read the Year
		StrNCopy(identifier, tokenP, 4);
		identifier[4] = nullChr;
		nv = StrAToI(identifier);
		// Validate the number and use it. Db35 era: accept any year 1904..9999
		// (the stored DateType is wide), not the ROM's lastYear (2031), so a
		// beamed far-future event keeps its year on the receiving end.
		if (nv < firstYear || 9999 < nv)
			nv = firstYear;
		dateP->year = nv - firstYear;
		tokenP += StrLen(identifier) * sizeof(char);
		
		// Read the Month
		StrNCopy(identifier, tokenP, 2);
		identifier[2] = nullChr;
		nv = StrAToI(identifier); 
		// Validate the number and use it.
		if (nv < 1 || 12 < nv)
			nv = 1;
		dateP->month = nv;
		tokenP += StrLen(identifier) * sizeof(char);
		
		// Read the Day
		StrNCopy(identifier, tokenP, 2);
		identifier[2] = nullChr;
		nv = StrAToI(identifier);
		// Validate the number and use it.
		if (nv < 1 || 31 < nv)
			nv = 1;
		dateP->day = nv;
		tokenP += StrLen(identifier) * sizeof(char);
		}

	// Try to get time if desired by caller
	if (timeP != NULL)
		{
		// Check to see if there is a time value, if so read it in,
		// if not assume that the event has no time
		timeStartP = StrChr(tokenP, 'T');
		if (!timeStartP)
			{
			TimeToInt(*timeP) = apptNoTime;
			}
		else
			{
			// Move over the time/date separator
			tokenP = timeStartP + sizeOf7BitChar('T');

			// Read in the Hours
			StrNCopy(identifier, tokenP, 2);
			identifier[2] = nullChr;
			nv = StrAToI(identifier);
			// Validate the number and use it.
			if (nv < 0 || 24 <= nv)
				nv = 0;
			timeP->hours = nv;
			tokenP += StrLen(identifier) * sizeof(char);
			
			// Read in Minutes
			StrNCopy(identifier, tokenP, 2);
			identifier[2] = nullChr;
			nv = StrAToI(identifier);
			// Validate the number and use it.
			if (nv < 0 || 59 < nv)
				nv = 1;
			timeP->minutes = nv;
			}
		}
}


/************************************************************
 *
 * FUNCTION: MatchWeekDayToken
 *
 * DESCRIPTION:
 *		Matches the given string to a week day value.
 *
 *		=== THE TOKEN STRING MUST BE CONVERTED TO LOWERCASE
 *		=== BEFORE IT IS SENT TO THIS FUNCTION
 *
 * PARAMETERS: 
 *		tokenP	-	string ptr from which to extract weekday
 *
 * RETURNS: the week day value (sunday -> saturday) or 255
 *				if token didnt match.
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			frigino	12/3/97	Original
 *
 *************************************************************/
SEC_RECV static UInt8 MatchWeekDayToken(const char* tokenP)
{
	// Token must already be converted to lower-case string

	// Get 2-char token
	UInt16 weekDay = *((UInt16*)tokenP);
	// Find it
	switch (weekDay)
		{
		case 'su':
			return sunday;
			break;
		case 'mo':
			return monday;
			break;
		case 'tu':
			return tuesday;
			break;
		case 'we':
			return wednesday;
			break;
		case 'th':
			return thursday;
			break;
		case 'fr':
			return friday;
			break;
		case 'sa':
			return saturday;
			break;
		default:
			// Bad weekday token
			ErrNonFatalDisplay("Bad weekday");
			return 255;
			break;
		}
}


/************************************************************
 *
 * FUNCTION: MatchDayOrPositionToken
 *
 * DESCRIPTION:
 *		Extracts a day or position value and its sign from
 *		the given token string
 *
 * PARAMETERS: 
 *		tokenP		-	string ptr from which to extract
 *		valueP		-	ptr of where to store day/position value
 *		negativeP	-	true if valueP is negative
 *
 * RETURNS: nothing
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			frigino	12/3/97	Original
 *
 *************************************************************/
SEC_RECV static void MatchDayPositionToken(
	const char*			tokenP,
	UInt32*		valueP,
	Boolean*				negativeP)
{
	// Get token length
	UInt16 len = StrLen(tokenP);
	// Determine sign from last char if present
	*negativeP = (tokenP[len - 1] == '-');
	// Convert string value to integer. Any non-numeric chars
	// after the digits will be ignored
	*valueP = StrAToI(tokenP);
	// Return sign
}

/************************************************************
 *
 * FUNCTION: DateImportVEvent
 *
 * DESCRIPTION: Import a VCal record of type vEvent
 *
 * PARAMETERS: 
 *			ruleTextP - pointer to the imported rule string
 *
 * RETURNS: a pointer to the resulting RepeatInfoType or NULL
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk	8/9/97	Created
 *			art	10/17/97	Added parameter to return unique id
 *			art	2/2/98	Handle yearly events better (but not great).
 *			tlw	2/9/98	Use UInt16 for yearly (monthly repeatFrequency) so its not truncated.
 *			roger	8/4/98	Broke out of DateImportVEvent.
 *			grant	8/31/99	Return NULL if the repeat rule is invalid.
 *			jmp	10/21/99	Eliminated "incomplete notes" warning, and added a DOLATER comment.
 *
 *************************************************************/
 
SEC_RECV static RepeatInfoPtr
DateImportRepeatingRule(Char * ruleTextP)
{
	MemHandle repeatInfoH;
	RepeatInfoPtr repeatInfoP = NULL;
	Char * fieldP;
	Char identifier[identifierLengthMax];

#if EMULATION_LEVEL != EMULATION_NONE
// Spec allows for both a duration AND and end date for all repeat rule cases.
// This parsing will only work for Datebook beamed records for now.
//
// DOLATER:  Document this more fully!
#endif

	// If some rule was read
	if (ruleTextP != NULL)
		{
		// Allocate repeat info handle
		repeatInfoH = MemHandleNew(sizeof(RepeatInfoType));
		// Debug check
		ErrFatalDisplayIf(repeatInfoH == NULL, "Memory full");
		repeatInfoP = MemHandleLock(repeatInfoH);
		// Save repeat info into datebook record

		// Initialize all fields
		repeatInfoP->repeatType = repeatNone;
		EDateSetForever(&repeatInfoP->repeatEndDate);
		repeatInfoP->repeatFrequency = 0;
		repeatInfoP->repeatOn = 0;
		repeatInfoP->repeatStartOfWeek = 0;

		// Convert entire field to lower-case
		StrToLower(ruleTextP, ruleTextP);
		// Get the rule type and interval token (required)
		fieldP = GetToken(ruleTextP, identifier);
		// Determine rule type from first char
		switch (identifier[0])
			{
			case 'd':
				{
				// Daily
				repeatInfoP->repeatType = repeatDaily;
				// Convert interval string to integer
				repeatInfoP->repeatFrequency = StrAToI(&identifier[1]);
				// If there's more to read (optional)
				if (fieldP != NULL)
					{
					// Read duration or end-date
					fieldP = GetToken(fieldP, identifier);
					// Is literal a duration or end-date?
					if (identifier[0] == '#')
						{
						// It's a duration. Extract it.
						UInt32 duration = StrAToI(&identifier[1]);
						// If duration is not zero
						if (duration != 0)
							{
							// Compute end-date from available data and duration
							}
						}
					else
						{
						// It's an end-date. Read & convert to Palm OS date.
						MatchDateTimeToken(identifier, &repeatInfoP->repeatEndDate, NULL);
						}
					}
				}
				break;
			case 'w':
				{
//							Boolean		foundWeekday = false;
				// Weekly
				repeatInfoP->repeatType = repeatWeekly;
				// Convert interval string to integer
				repeatInfoP->repeatFrequency = StrAToI(&identifier[1]);
				// Read remaining tokens: weekdays, occurrences, duration, end date
				while (fieldP != NULL)
					{
					// Read a token
					fieldP = GetToken(fieldP, identifier);
					// Determine token type
					if (identifier[0] == '#')
						{
						// It's a duration. Extract it.
						UInt32 duration = StrAToI(&identifier[1]);
						// If duration is not zero
						if (duration != 0)
							{
							// Compute end-date from available data and duration
							}
						}
					else if (TxtCharIsDigit(identifier[0]))
						{
						// It's an end-date. Read & convert to Palm OS date.
						MatchDateTimeToken(identifier, &repeatInfoP->repeatEndDate, NULL);
						}
					else
						{
						UInt8		weekDay;
						// Try to match weekday token
						weekDay = MatchWeekDayToken(identifier);
						if (weekDay != 255)
							{
							// Set the bit for this day
							SetBitMacro(repeatInfoP->repeatOn, weekDay);
							// We found at least one weekday
//										foundWeekday = true;
							}
						}
					}
				}
				break;
			case 'm':
				{
				// Monthly
				// Convert interval string to integer
				UInt16 repeatFrequency = StrAToI(&identifier[2]);
				// Determine if monthly by day or by position
				switch (identifier[1])
					{
					case 'p':
						{
						// Monthly by position
						UInt32		position;
						Boolean				fromEndOfMonth;

						repeatInfoP->repeatType = repeatMonthlyByDay;
						repeatInfoP->repeatFrequency = repeatFrequency;
						// Read remaining tokens: weekdays, occurrences, duration, end date
						while (fieldP != NULL)
							{
							// Read a token
							fieldP = GetToken(fieldP, identifier);
							// Determine token type
							if (identifier[0] == '#')
								{
								// It's a duration. Extract it.
								UInt32 duration = StrAToI(&identifier[1]);
								// If duration is not zero
								if (duration != 0)
									{
									// Compute end-date from available data and duration
									}
								}
							else if (TxtCharIsDigit(identifier[0]))
								{
								// It's an occurrence or an end-date. Judge by length
								if (StrLen(identifier) > 2)
									{
									// It should be an end-date
									MatchDateTimeToken(identifier, &repeatInfoP->repeatEndDate, NULL);
									}
								else
									{
									// It should be an occurrence
									// Extract day/position and sign
									MatchDayPositionToken(identifier, &position,
																&fromEndOfMonth);
									// Validate position
									if (position < 1)
										position = 1;
									else if (position > 5)
										position = 5;
									}
								}
							else
								{
								// It should be a weekday
								UInt8		weekDay;
								// Try to match weekday token
								weekDay = MatchWeekDayToken(identifier);
								if (weekDay != 255)
									{
									// Calc day of week to repeat. Note that an
									// occurrence should already have been found
									if (fromEndOfMonth)
										// assume the position is 1, since datebook doesn't handle
										// things like 2nd-to-the-last Monday...
										repeatInfoP->repeatOn = domLastSun + weekDay;
									else
										repeatInfoP->repeatOn = dom1stSun + ((position - 1) * daysInWeek) + weekDay;
									}
								}
							}
						}
						break;
					case 'd':
						{
						// Monthly By day or Yearly
						//
						// Yearly repeating events are passed a monthly-by-date repeating
						// event with the frequency in months instead of years.  This is due 
						// to the fact that vCal's years rule uses days by number, which creates 
						// problems in leap years.
						if (repeatFrequency % monthsInYear)
							{
							repeatInfoP->repeatType = repeatMonthlyByDate;
							repeatInfoP->repeatFrequency = repeatFrequency;
							}
						else
							{
							repeatInfoP->repeatType = repeatYearly;
							repeatInfoP->repeatFrequency = repeatFrequency / monthsInYear;
							// Has no meaning for this case
							repeatInfoP->repeatOn = 0;
							}
						// Read remaining tokens: occurrences, duration, end date
						while (fieldP != NULL)
							{
							// Read a token
							fieldP = GetToken(fieldP, identifier);
							// Determine token type
							if (identifier[0] == '#')
								{
								// It's a duration. Extract it.
								UInt32 duration = StrAToI(&identifier[1]);
								// If duration is not zero
								if (duration != 0)
									{
									// Compute end-date from available data and duration
									}
								}
							else if (TxtCharIsAlNum(identifier[0]))
								{
								// It's an occurrence or an end-date. Judge by length
								if (StrLen(identifier) > 3)
									{
									// It should be an end-date
									MatchDateTimeToken(identifier, &repeatInfoP->repeatEndDate, NULL);
									}
								else
									{
#if 1
									// Datebook doesnt support repeating on a day which isnt the
									// same as the start day. Thus, occurrences are not used and
									// this value should be zero
									repeatInfoP->repeatOn = 0;
#else
									// It should be an occurrence
									// Check for the "LD" special case
									if (StrCompare(identifier, "LD") == 0)
										{
										// It's the "last day" occurrence. Use maximum
										repeatInfoP->repeatOn = 31;
										}
									else
										{
										UInt32		position;
										Boolean				fromEndOfMonth;

										// Extract position and sign
										MatchDayPositionToken(identifier, &position,
																	&fromEndOfMonth);
										// Validate occurrence
										if (position < 1)
											position = 1;
										else if (position > 31)
											position = 31;

										if (fromEndOfMonth)
											{
											// This wont be accurate, since not all months
											// have 31 days. Datebook doesnt support it anyway
											repeatInfoP->repeatOn = 31 - position;
											}
										else
											{
											// It's an exact day from the start of the month
											repeatInfoP->repeatOn = position;
											}
										}
#endif
									}
								}
							}
						}
						break;
					default:
						// Bad monthly sub-type
						ErrNonFatalDisplay("Bad monthly rule");
						MemHandleFree(repeatInfoH);
						repeatInfoP = NULL;
						break;
					}
				}
				break;
			case 'y':
				{
				// Yearly
				repeatInfoP->repeatType = repeatYearly;
				// Has no meaning for this case
				repeatInfoP->repeatFrequency = StrAToI(&identifier[2]);
				// Determine if yearly by day or by month
				switch (identifier[1])
					{
					case 'm':
						{
						// By month

						// Read remaining tokens: months, duration, end date
						while (fieldP != NULL)
							{
							// Read a token
							fieldP = GetToken(fieldP, identifier);
							// Determine token type
							if (identifier[0] == '#')
								{
								// It's a duration. Extract it.
								UInt32 duration = StrAToI(&identifier[1]);
								// If duration is not zero
								if (duration != 0)
									{
									// Compute end-date from available data and duration
									}
								}
							else if (TxtCharIsDigit(identifier[0]))
								{
								// It's a month occurrence or an end-date. Judge by length
								if (StrLen(identifier) > 2)
									{
									// It should be an end-date
									MatchDateTimeToken(identifier, &repeatInfoP->repeatEndDate, NULL);
									}
								else
									{
#if 1
									// Datebook doesnt support monthly repeats on a date which isnt
									// the same as the start day. Thus, occurrences are not used and
									// this value should be zero
									repeatInfoP->repeatOn = 0;
#else
									// It should be a monthly occurrence
									UInt32	month;
									// Extract month
									month = StrAToI(identifier);
									// Validate month
									if (month < 1)
										month = 1;
									else if (month > december)
										month = december;
									// Do something with month here
#endif
									}
								}
							}
						}
						break;
					case 'd':
						{
						// By day

						// Read remaining tokens: days, duration, end date
						while (fieldP != NULL)
							{
							// Read a token
							fieldP = GetToken(fieldP, identifier);
							// Determine token type
							if (identifier[0] == '#')
								{
								// It's a duration. Extract it.
								UInt32 duration = StrAToI(&identifier[1]);
								// If duration is not zero
								if (duration != 0)
									{
									// Compute end-date from available data and duration
									}
								}
							else if (TxtCharIsDigit(identifier[0]))
								{
								// It's a day occurrence or an end-date. Judge by length
								if (StrLen(identifier) > 3)
									{
									// It should be an end-date
									MatchDateTimeToken(identifier, &repeatInfoP->repeatEndDate, NULL);
									}
								else
									{
#if 1
									// Datebook doesnt support daily repeats on a days which arent
									// the same as the start day. Thus, occurrences are not used and
									// this value should be zero
									repeatInfoP->repeatOn = 0;
#else
									// It should be a daily occurrence
									UInt32	day;
									// Extract day
									day = StrAToI(identifier);
									// Validate day
									if (day < 1)
										day = 1;
									else if (day > 31)
										day = 31;
									// Do something with day here
#endif
									}
								}
							}
						}
						break;
					default:
						// Bad yearly sub-type
						ErrNonFatalDisplay("Bad yearly rule");
						MemHandleFree(repeatInfoH);
						repeatInfoP = NULL;
						break;
					}
				}
				break;
			default:
				// Unknown rule
				ErrNonFatalDisplay("Bad repeat rule");
				MemHandleFree(repeatInfoH);
				repeatInfoP = NULL;
				break;
			}
		// Free entire repeat rule string
		MemPtrFree(ruleTextP);
		}
	
	return repeatInfoP;
}


#pragma mark ----------------------------
/************************************************************
 *
 * FUNCTION: DateImportVEvent
 *
 * DESCRIPTION: Import a VCal record of type vEvent
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *
 * RETURNS: true if the input was read
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk	8/9/97	Created
 *			art	10/17/97	Added parameter to return unique id
 *			art	2/2/98	Handle yearly events better (but not great).
 *			tlw	2/9/98	Use UInt16 for yearly (monthly repeatFrequency) so its not truncated.
 *
 *************************************************************/
 
SEC_RECV static Boolean 
DateImportVEvent(DmOpenRef dbP, void * inputStream, GetCharF inputFunc, 
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, UInt32 * uniqueIDP)
{
	UInt16 c;
	Char identifier[identifierLengthMax];
	Int16 identifierEnd;
	UInt16 indexNew;
	UInt16 indexOld;
	UInt16* junk = NULL;
	UInt32 uid;
	Boolean lastCharWasQuotedPrintableContinueToNextLineChr;
	Boolean quotedPrintable;
	volatile ApptDBRecordType newDateRecord;
	Char * volatile newAttachment;
	ApptDateTimeType nWhen;
	UInt32 alarmDTinSec = 0; // ASSUMPTION: Alarm is not set for 1/1/1904 at 00:00:00
	char *tempP;
	Boolean firstLoop = true;
	volatile Err error = 0;
		
	c = inputFunc(inputStream);
	ImcReadWhiteSpace(inputStream, inputFunc, junk, &c);
	
	if (c == EOF) return false;
	
	identifierEnd = 0;
	if (!beginAlreadyRead)
		{
		// CAUTION: IsAlpha(EOF) returns true
		while (TxtCharIsAlpha(c) || c == ':' || 
			(c != linefeedChr && c != 0x0D && TxtCharIsSpace(c)))
			{
			if (!TxtCharIsSpace(c) && (identifierEnd < identifierLengthMax-1))
		 		identifier[identifierEnd++] = (char) c;
		 	
			c = inputFunc(inputStream);
			if (c == EOF) return false;
			}
		}
	identifier[identifierEnd++] = nullChr;
	
	
	// Read in the vEvent entry
	if (!(StrCaselessCompare(identifier, "BEGIN:VEVENT") == 0 || beginAlreadyRead))
		return false;
		
	// Initialize the record to NULL
	newDateRecord.when = &nWhen;
	newDateRecord.alarm = NULL;
	newDateRecord.repeat = NULL;
	newDateRecord.exceptions = NULL;
	newDateRecord.description = NULL;
	newDateRecord.note = NULL;
	newDateRecord.endDateOffset = 0;	/* Db35: multi-day span, see DTEND below */
	newAttachment = NULL;
		
	// An error happens usually due to no memory.  It's easier just to 
	// catch the error.  If an error happens, we remove the last record.  
	// Then we throw a second time so the caller receives it and displays a message.
	ErrTry
		{
		do
			{
			quotedPrintable = false;
			
			if (!beginAlreadyRead || !firstLoop)
				{
				// Advance to the next line.  If the property wasn't handled we need to make sure that
				// if the property value contains quoted printable text that newlines within that 
				// section are skipped.
				lastCharWasQuotedPrintableContinueToNextLineChr = (c == '=');
				while ((c != endOfLineChr || lastCharWasQuotedPrintableContinueToNextLineChr) && c != EOF)
					{
					lastCharWasQuotedPrintableContinueToNextLineChr = (c == '=');
					c = inputFunc(inputStream);
					} 
				
				c = inputFunc(inputStream);
				}
				
			
			// Skip the second part of a 0x0D 0x0A end of line sequence
			if (c == 0x0A)
				c = inputFunc(inputStream);
			else if (c == EOF)
				ErrThrow(exgErrBadData);
				
				
			firstLoop = false;
			
			// Read in an identifier.
			identifierEnd = 0;
			ImcReadWhiteSpace(inputStream, inputFunc, junk, &c);
			while (c != valueDelimeterChr && c != parameterDelimeterChr && 
				c != groupDelimeterChr && c != endOfLineChr && c != EOF)
				{
				if (identifierEnd < identifierLengthMax)
					identifier[identifierEnd++] = (char) c;
				
				c = inputFunc(inputStream);
				}
			identifier[identifierEnd++] = nullChr;
				
			
			// Handle Start tag 
			// ASSUMPTION: we will use the date from the start time, not the end time 
			// NOTE: This function will break if the DTSTART value is truncated
			// beyond the day
			if (StrCaselessCompare(identifier, "DTSTART") == 0)
				{
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
				tempP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars);
				if (tempP != NULL)
					{
					// Extract start date & time
					MatchDateTimeToken(tempP, &newDateRecord.when->date,
											&newDateRecord.when->startTime);
					// Assume end time same as start time for now...
					newDateRecord.when->endTime = newDateRecord.when->startTime;
					MemPtrFree(tempP);
					}
				}
				
			// Read in the end time
			// Db35: DTEND CAN land on a later calendar day than DTSTART (a
			// multi-day span, e.g. a flight) -- derive endDateOffset from the
			// difference, assuming DTSTART (which sets when->date) was
			// already parsed, same ordering assumption the rest of this
			// function already makes for other fields.
			else if (StrCaselessCompare(identifier, "DTEND") == 0)
				{
				DateType endDate;

				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
				tempP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars);
				if (tempP != NULL)
					{
					Int32 spanDays;

					// Extract end date & time
					MatchDateTimeToken(tempP, &endDate, &newDateRecord.when->endTime);

					spanDays = (Int32) DateToDays (endDate) -
						(Int32) DateToDays (newDateRecord.when->date);
					if (spanDays < 0) spanDays = 0;
					if (spanDays > 30) spanDays = 30;
					newDateRecord.endDateOffset = (UInt8) spanDays;

					MemPtrFree(tempP);
					}
				}
	
			// Read Repeat info
			else if (StrCaselessCompare(identifier, "RRULE") == 0)
				{
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c,
					identifier, &quotedPrintable);
				tempP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars);
				newDateRecord.repeat = DateImportRepeatingRule(tempP);
				}
				
			// Read Repeat exceptions
			else if (StrCaselessCompare(identifier, "EXDATE") == 0)
				{
				MemHandle						exceptionListH;
				ExceptionsListType*		exceptionListP;
				DateType*					exceptionP;
				UInt16							exceptionCount = 0;
				Err							err;

				// Skip property params
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c,
													identifier, &quotedPrintable);
				// Allocate exception list handle to hold at least the first exception
				exceptionListH = MemHandleNew(sizeof(ExceptionsListType));
				// Debug check
				ErrFatalDisplayIf(exceptionListH == NULL, "Memory full");

				// Read each exception
				while ((tempP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars)) != NULL)
					{
					// Resize handle to hold exception
					err = MemHandleResize(exceptionListH, sizeof(ExceptionsListType) +
												sizeof(DateType) * exceptionCount);
					ErrFatalDisplayIf(err != 0, "Memory full");
					// Lock exception handle
					exceptionListP = MemHandleLock(exceptionListH);
					// Calc exception ptr
					exceptionP = (DateType*)((UInt32)exceptionListP + (UInt32)sizeof(UInt16) +
									(UInt32)(sizeof(DateType) * exceptionCount));
					// Store exception into exception handle
					MatchDateTimeToken(tempP, exceptionP, NULL);
					// Increase exception count
					exceptionCount++;
					// Unlock exceptions list handle
					MemHandleUnlock(exceptionListH);
					// Free exception string
					MemPtrFree(tempP);
					}

				// Lock exception handle
				exceptionListP = MemHandleLock(exceptionListH);
				// Store final exception count
				exceptionListP->numExceptions = exceptionCount;
				// Save exception list into datebook record
				newDateRecord.exceptions = exceptionListP;
				}
			
			// 	Read in Alarm info	
			else if (StrCaselessCompare(identifier, "AALARM") == 0 || 
				StrCaselessCompare(identifier, "DALARM") == 0)
				{
				DateTimeType	alarmDT;
				DateType			alarmDate;
				TimeType			alarmTime;
				
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
				tempP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars);		
				if (tempP != NULL)
					{
					// Extract alarm date & time
					MatchDateTimeToken(tempP, &alarmDate, &alarmTime);
					// Copy values to DateTimeType struct
					alarmDT.year = alarmDate.year + firstYear;
					alarmDT.month = alarmDate.month;
					alarmDT.day = alarmDate.day;
					alarmDT.hour = alarmTime.hours;
					alarmDT.minute = alarmTime.minutes;
					alarmDT.second = 0;
					
					alarmDTinSec = TimDateTimeToSeconds(&alarmDT);
					
					MemPtrFree(tempP);
					}
				}
			
			// Read in Summary	
			else if  (StrCaselessCompare(identifier, "SUMMARY") == 0)
				{
				if (newDateRecord.description != NULL)
					{
					MemPtrFree(newDateRecord.description);
					}
				
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
				newDateRecord.description = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, tableMaxTextItemSize);
				}
			
			// Read in Description	
			else if  (StrCaselessCompare(identifier, "DESCRIPTION") == 0)
				{
				if (newDateRecord.note != NULL)
					{
					MemPtrFree(newDateRecord.note);
					}
				
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
				newDateRecord.note = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, tableMaxTextItemSize);
				}
			
			// Read in attachments.  At the end we place the attachment into the record.
			else if (StrCaselessCompare(identifier, "ATTACH") == 0)
				{
				// Note: vCal permits attachments of types other than text, specifically
				// URLs and Content ID's.  At the moment, wee will just treat both of these
				// as text strings
				
				if (newAttachment != NULL)
					{
					MemPtrFree(newAttachment);
					}
				
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
				newAttachment = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c,
												endOfLineChr, quotedPrintable, noteViewMaxLength);
				}
			
			// read in the unique identifier
			else if (StrCaselessCompare(identifier, "UID") == 0 && obeyUniqueIDs)
				{
				Char * uniqueIDStringP;
				ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
								
				uniqueIDStringP = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, imcUnlimitedChars);
				if (uniqueIDStringP != NULL)
					{
					uid = StrAToI(uniqueIDStringP);
					MemPtrFree(uniqueIDStringP);
					
					// Check the uid for reasonableness.
					if (uid < (dmRecordIDReservedRange << 12))
						uid = 0;
					}
				}
					
			// stop if at the end
			else if (StrCaselessCompare(identifier, "END") == 0)
				{
					// Advance to the next line.
				while (c != endOfLineChr && c != EOF)
					{
					c = inputFunc(inputStream);
					} 
				
				break;
				}
			
			} while (true);


		// if an alarm was read in, translate it appropriately
		// ASSUMPTION: alarm is before start time
		if (alarmDTinSec != 0)
			{
			TranslateAlarm((ApptDBRecordType*)&newDateRecord, alarmDTinSec);
			}
		
		
		// PalmIII stored descriptions as DESCRIPTION and notes as ATTACH.
		// vCal spec stores considers them to be SUMMARY and DESCRIPTION.
		// For now we write records in the old terminology but here we 
		// handle both.
		if (newDateRecord.description == NULL)
			{
			newDateRecord.description = newDateRecord.note;
			newDateRecord.note = newAttachment;
			newAttachment = NULL;
			}
		
		// Some vCal implementations send duplicate SUMMARY and DESCRIPTION fields.
		if (newDateRecord.description != NULL && newDateRecord.note != NULL &&
			StrCompare(newDateRecord.description, newDateRecord.note) == 0)
			{
			// Delete the duplicate note.
			MemPtrFree(newDateRecord.note);
			newDateRecord.note = NULL;
			}
		
		
		// Write the actual record
		if (ApptNewRecord(dbP, (ApptDBRecordType*)&newDateRecord, & indexNew))
			ErrThrow(exgMemError);

		// If uid was set then a unique id was passed to be used.
		if (uid != 0 && obeyUniqueIDs)
			{
			// We can't simply remove any old record using the unique id and
			// then add the new record because removing the old record could
			// move the new one.  So, we find any old record, change the new
			// record, and then remove the old one.
			indexOld = indexNew;
			
			// Find any record with this uid.  indexOld changes only if 
			// such a record is found.
			DmFindRecordByID (dbP, uid, &indexOld);
			
			// Change this record to this uid.  The dirty bit is set from 
			// newly making this record.
			DmSetRecordInfo(dbP, indexNew, NULL, &uid);
			
			// Now remove any old record.
			if (indexOld != indexNew)
				{
				DmRemoveRecord(dbP, indexOld);
				}
			}
		
		// Return the unique id of the record inserted.
		DmRecordInfo(dbP, indexNew, NULL, uniqueIDP, NULL);
		}
	
	ErrCatch(inErr)
		{
		error = inErr;
		} ErrEndCatch

	
	// Free any temporary buffers used to store the incoming data.
	if (newAttachment)
		MemPtrFree(newAttachment);
	if (newDateRecord.note)
		MemPtrFree(newDateRecord.note);
	if (newDateRecord.description)
		MemPtrFree(newDateRecord.description);
	if (newDateRecord.alarm)
		MemPtrFree(newDateRecord.alarm);
	if (newDateRecord.repeat)
		MemPtrFree(newDateRecord.repeat);
	if (newDateRecord.exceptions)
		MemPtrFree(newDateRecord.exceptions);
	
	if (error)
		ErrThrow(error);
	
	return true;	
}


/************************************************************
 *
 * FUNCTION: DateExportVCal
 *
 * DESCRIPTION: Export a VCALENDAR record.
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to export the records from
 *			index - the record number to export
 *			recordP - whether the begin statement has been read
 *			outputStream - pointer to where to export the record to
 *			outputFunc - function to send output to the stream
 *			writeUniqueIDs - true to write the record's unique id
 *
 * RETURNS: nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk		8/9/97		Created
 *
 *************************************************************/
SEC003 extern void DateExportVCal(DmOpenRef dbP, Int16 index, ApptDBRecordPtr recordP, 
	void * outputStream, PutStringF outputFunc, Boolean writeUniqueIDs)
{
	UInt32			uid;
	Char 			tempString[tempStringLengthMax];
	
	
	outputFunc(outputStream, "BEGIN:VEVENT" imcLineSeparatorString);

	// Handle When
	// ASSUMPTION: To represent non-timed events, we will use a 
	// DTSTART propert with a date value (instead of a date/time value)
	// and _no_ DTEND proprty.  This is permitted by ISO 8601. 
	
	{
	/* Db35: DTEND lands on a LATER calendar day than DTSTART for a
	 * spanning event (endDateOffset days after when->date, 0 = same day) --
	 * vCalendar's DTSTART/DTEND already natively support different dates,
	 * this was just never wired up before this feature existed. */
	DateType endDate = recordP->when->date;
	if (recordP->endDateOffset)
		DateAdjust (&endDate, (Int32) recordP->endDateOffset);

	if (TimeToInt(recordP->when->startTime) != apptNoTime)
		{
		outputFunc(outputStream, "DTSTART:");
		StrPrintF(tempString, "%d%02d%02dT%02d%02d00", firstYear + recordP->when->date.year,
					recordP->when->date.month,recordP->when->date.day,
					recordP->when->startTime.hours, recordP->when->startTime.minutes);

		ImcWriteNoSemicolon(outputStream, outputFunc,tempString);
		outputFunc(outputStream, imcLineSeparatorString);

		// End Time/Date
		ImcWriteNoSemicolon(outputStream, outputFunc, "DTEND:");
		StrPrintF(tempString, "%d%02d%02dT%02d%02d00", firstYear + endDate.year,
					endDate.month, endDate.day,
					recordP->when->endTime.hours, recordP->when->endTime.minutes);

		ImcWriteNoSemicolon(outputStream, outputFunc,tempString);
		outputFunc(outputStream, imcLineSeparatorString);
		}
	else
		// Handle a non-timed event -- see note above for convention
		{
		ImcWriteNoSemicolon(outputStream, outputFunc, "DTSTART:");
		StrPrintF(tempString, "%d%02d%02d", firstYear + recordP->when->date.year,
					recordP->when->date.month,recordP->when->date.day);

		ImcWriteNoSemicolon(outputStream, outputFunc,tempString);
		outputFunc(outputStream, imcLineSeparatorString);

		// Db35: an untimed event can still span multiple days -- emit a
		// date-only DTEND too when it does (still no DTEND at all for the
		// same-day case, preserving the original convention/behavior).
		if (recordP->endDateOffset)
			{
			ImcWriteNoSemicolon(outputStream, outputFunc, "DTEND:");
			StrPrintF(tempString, "%d%02d%02d", firstYear + endDate.year,
						endDate.month, endDate.day);

			ImcWriteNoSemicolon(outputStream, outputFunc,tempString);
			outputFunc(outputStream, imcLineSeparatorString);
			}
		}
	}
		
	// Handle Alarm
	if (recordP->alarm != NULL &&
		recordP->alarm->advance != apptNoAlarm)
		{
		DateTimeType alrmDT;
		
		ImcWriteNoSemicolon(outputStream, outputFunc,  "AALARM:");
		TimSecondsToDateTime(ApptGetAlarmTimeVCalForm(recordP), &alrmDT);
		
		// Because we cannot access DateBook globals due to our simple second code segment scheme, we 
		// can't access AlarmSoundRepeatInterval or AlarmSoundRepeatCount.  When we switch to a 
		// smarter multisegment scheme or a large segment scheme, resume using the globals.
//		StrPrintF(tempString, "%d%02d%02dT%02d%02d00;PT%uM;%u" ,  alrmDT.year, alrmDT.month, alrmDT.day,
//					alrmDT.hour, alrmDT.minute, AlarmSoundRepeatInterval / minutesInSeconds, AlarmSoundRepeatCount);	
		StrPrintF(tempString, "%d%02d%02dT%02d%02d00" ,  alrmDT.year, alrmDT.month, alrmDT.day,
					alrmDT.hour, alrmDT.minute);
		
		ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
		outputFunc(outputStream, imcLineSeparatorString);		
		}
		
	// Handle Repeating Events
	if (recordP->repeat && (recordP->repeat->repeatType != repeatNone))
		{
		ImcWriteNoSemicolon(outputStream, outputFunc,  "RRULE:");
		
		// Set the rule type
		switch(recordP->repeat->repeatType)
			{
			case repeatDaily:
				ImcWriteNoSemicolon(outputStream, outputFunc, "D");
				break;
			
			case repeatWeekly:
				ImcWriteNoSemicolon(outputStream, outputFunc, "W");
				break;
			
			// One small oddity is that our repeatMonthlyByDay is equivelent
			// to vCal's by-postion and our repeatMonthlyByDate is equivelent
			// to vCal's by-Day rule
			case repeatMonthlyByDay:
				ImcWriteNoSemicolon(outputStream, outputFunc, "MP");
				break;
			
			case repeatMonthlyByDate:
				ImcWriteNoSemicolon(outputStream, outputFunc, "MD");
				break;
			// vCal's years rule uses days by number, which creates problems in leap years
			// so instead we will use the month by-Day rule (equiv to our month by date rule)
			// and multiply by monthsInYear
			case repeatYearly:
				ImcWriteNoSemicolon(outputStream, outputFunc, "MD");
				break;				
			}
			
		// Set the freqency
		if (recordP->repeat->repeatType == repeatYearly)
			StrPrintF(tempString, "%d ", monthsInYear * (UInt16) recordP->repeat->repeatFrequency);
		else
			StrPrintF(tempString, "%d ", (UInt16) recordP->repeat->repeatFrequency);
			
		ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
		
		
		// if the repeat type is repeatWeekly, emit which days the event is on
		if (recordP->repeat->repeatType == repeatWeekly)
			{
			int dayIndex;
			
			for (dayIndex = 0; dayIndex < daysInWeek; dayIndex++)
				{
				if (GetBitMacro(recordP->repeat->repeatOn, dayIndex))
					{
					switch(dayIndex)
						{
						case sunday:
							StrPrintF(tempString, "SU ");
							break;
						case monday:
							StrPrintF(tempString, "MO ");
							break;
						case tuesday:
							StrPrintF(tempString, "TU ");
							break;
						case wednesday:
							StrPrintF(tempString, "WE ");
							break;
						case thursday:
							StrPrintF(tempString, "TH ");
							break;
						case friday:
							StrPrintF(tempString, "FR ");
							break;
						case saturday:
							StrPrintF(tempString, "SA ");
							break;
						}	
					ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
					}
				}	
			}
					
		// If the repeat type is a pilot monthly repeat by day (as opposed to by date),
		// emit the repetition rule
		if (recordP->repeat->repeatType == repeatMonthlyByDay)
			{
			
			// Deal with the case that are not the domLast___ cases
			if (((DayOfWeekType) recordP->repeat->repeatOn) < domLastSun)
				{
				// Figure out which week were in and emit it
				StrPrintF(tempString, "%d+ ", (recordP->repeat->repeatOn / daysInWeek) + 1);
				}
			else
				// domLast___ are all in week -1
				StrPrintF(tempString, "1- ");
				
			ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
								
			
			//	Figure out what the day of the week is and emit it
			if ((recordP->repeat->repeatOn == dom1stSun) || (recordP->repeat->repeatOn == dom2ndSun) ||
			     (recordP->repeat->repeatOn == dom3rdSun) || (recordP->repeat->repeatOn == dom4thSun) ||
			     (recordP->repeat->repeatOn == domLastSun))
				StrPrintF(tempString, "SU ");
			
			if ((recordP->repeat->repeatOn == dom1stMon) || (recordP->repeat->repeatOn == dom2ndMon) ||
			     (recordP->repeat->repeatOn == dom3rdMon) || (recordP->repeat->repeatOn == dom4thMon) ||
			     (recordP->repeat->repeatOn == domLastMon))
				StrPrintF(tempString, "MO ");
			
			if ((recordP->repeat->repeatOn == dom1stTue) || (recordP->repeat->repeatOn == dom2ndTue) ||
			     (recordP->repeat->repeatOn == dom3rdTue) || (recordP->repeat->repeatOn == dom4thTue) ||
			     (recordP->repeat->repeatOn == domLastTue))
				StrPrintF(tempString, "TU ");
			
			if ((recordP->repeat->repeatOn == dom1stWen) || (recordP->repeat->repeatOn == dom2ndWen) ||
			     (recordP->repeat->repeatOn == dom3rdWen) || (recordP->repeat->repeatOn == dom4thWen) ||
			     (recordP->repeat->repeatOn == domLastWen))
				StrPrintF(tempString, "WE ");
			
			if ((recordP->repeat->repeatOn == dom1stThu) || (recordP->repeat->repeatOn == dom2ndThu) ||
			     (recordP->repeat->repeatOn == dom3rdThu) || (recordP->repeat->repeatOn == dom4thThu) ||
			     (recordP->repeat->repeatOn == domLastThu))
				StrPrintF(tempString, "TH ");
			
			if ((recordP->repeat->repeatOn == dom1stFri) || (recordP->repeat->repeatOn == dom2ndFri) ||
			     (recordP->repeat->repeatOn == dom3rdFri) || (recordP->repeat->repeatOn == dom4thFri) ||
			     (recordP->repeat->repeatOn == domLastFri))
				StrPrintF(tempString, "FR ");
			
			if ((recordP->repeat->repeatOn == dom1stSat) || (recordP->repeat->repeatOn == dom2ndSat) ||
			     (recordP->repeat->repeatOn == dom3rdSat) || (recordP->repeat->repeatOn == dom4thSat) ||
			     (recordP->repeat->repeatOn == domLastSat))
				StrPrintF(tempString, "SA ");
				
			ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
			}
			
		// If the record is repeatMonthlyByDate, put out the # of the day
		if (recordP->repeat->repeatType == repeatMonthlyByDate)
			{
			StrPrintF(tempString, "%d ", recordP->when->date.day);
			ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
			}
			
		// Emit the end date
		if (TimeToInt(recordP->repeat->repeatEndDate) == apptNoEndDate)
			StrPrintF(tempString, "#0");
		else
			StrPrintF(tempString, "%d%02d%02dT000000", firstYear + recordP->repeat->repeatEndDate.year, 
				recordP->repeat->repeatEndDate.month,recordP->repeat->repeatEndDate.day); 
				
		ImcWriteNoSemicolon(outputStream, outputFunc,  tempString);
		outputFunc(outputStream, imcLineSeparatorString);	
		}
	
	
	// Handle exceptions to repeating
	if (recordP->exceptions != NULL)
		{
		DateType *exArray = &recordP->exceptions->exception;
		UInt16 i;
		
		ImcWriteNoSemicolon(outputStream, outputFunc,  "EXDATE:");

		for (i = 0; i < recordP->exceptions->numExceptions; i++)
			{
		 	// ASSUMPTION:  EXDATE has a date/time property, although the ISO 8601 standard allows
		 	// us to truncate this to a date (which is what we keep), we will make the reasonable
		 	// assumptio that the time of an exception is the same as the time of the event it is
		 	// an exception for.  This does not affect communication with another pilot, but will
		 	// require less robustness for other devices we communicate with
		 	StrPrintF(tempString, "%d%02d%02dT%02d%02d00" ,  exArray[i].year + firstYear, exArray[i].month , exArray[i].day,
		 			 recordP->when->startTime.hours, recordP->when->startTime.minutes);	
			
			outputFunc(outputStream, tempString);	
			
			if (i+1 < recordP->exceptions->numExceptions)
				outputFunc(outputStream,  "; ");
				
			}
		outputFunc(outputStream, imcLineSeparatorString);	
		}
	
	
	// Handle description
	if (recordP->description != NULL)
		{
		ImcWriteNoSemicolon(outputStream, outputFunc,  "DESCRIPTION");
		ImcWriteQuotedPrintable(outputStream, outputFunc,  recordP->description, false);
		outputFunc(outputStream, imcLineSeparatorString);	
		}
	
	
	// Handle note
	if (recordP->note != NULL)
		{
		ImcWriteNoSemicolon(outputStream, outputFunc,  "ATTACH");
		ImcWriteQuotedPrintable(outputStream, outputFunc,  recordP->note, false);
		outputFunc(outputStream, imcLineSeparatorString);	
		}
	
	
	// Emit an unique id
	if (writeUniqueIDs)
		{
		outputFunc(outputStream, "UID:");
		
		// Get the record's unique id and append to the string.
		DmRecordInfo(dbP, index, NULL, &uid, NULL);
		StrIToA(tempString, uid);
		outputFunc(outputStream, tempString);
		outputFunc(outputStream, imcLineSeparatorString);
		}
	
	
	outputFunc(outputStream, "END:VEVENT" imcLineSeparatorString);
}


/***********************************************************************
 *
 * FUNCTION:		DateSetGoToParams
 *
 * DESCRIPTION:	Store the information necessary to navigate to the 
 *                record inserted into the launch code's parameter block.
 *
 * PARAMETERS:		 dbP        - pointer to the database to add the record to
 *						 exgSocketP - parameter block passed with the launch code
 *						 uniqueID   - unique id of the record inserted
 *
 * RETURNED:		nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	10/17/97	Created
 *
 ***********************************************************************/
SEC_RECV static void DateSetGoToParams (DmOpenRef dbP, ExgSocketPtr exgSocketP, UInt32 uniqueID)
{
	UInt16		recordNum;
	UInt16		cardNo;
	LocalID 	dbID;

	
	if (! uniqueID) return;

	DmOpenDatabaseInfo (dbP, &dbID, NULL, NULL, &cardNo, NULL);

	// The this the the first record inserted, save the information
	// necessary to navigate to the record.
	if (! exgSocketP->goToParams.uniqueID)
		{
		DmFindRecordByID (dbP, uniqueID, &recordNum);

		exgSocketP->goToCreator = datebookCreator;
		exgSocketP->goToParams.uniqueID = uniqueID;
		exgSocketP->goToParams.dbID = dbID;
		exgSocketP->goToParams.dbCardNo = cardNo;
		exgSocketP->goToParams.recordNum = recordNum;
		}

	// If we already have a record then make sure the record index 
	// is still correct.  Don't update the index if the record is not
	// in your the app's database.
	else if (dbID == exgSocketP->goToParams.dbID &&
			   cardNo == exgSocketP->goToParams.dbCardNo)
		{
		DmFindRecordByID (dbP, exgSocketP->goToParams.uniqueID, &recordNum);

		exgSocketP->goToParams.recordNum = recordNum;
		}
}


/************************************************************
 *
 * FUNCTION: DateImportVCal
 *
 * DESCRIPTION: Import a VCal record of type vEvent and vToDo
 *
 * The Datebook handles vCalendar records.  Any vToDo records 
 * are sent to the ToDo app for importing.
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *			vToDoFunc - function to import vToDo records
 *						on the device this is a function to call ToDo to read
 *						for the shell command this is an empty function
 *
 * RETURNS: true if the input was read
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk	8/9/97		Created
 *			roger	9/12/97		Modified to work on the device
 *			art	10/17/97		Return unique id of first record inserted
 *
 *************************************************************/
 
SEC_RECV extern Boolean 
DateImportVCal(DmOpenRef dbP, void * inputStream, GetCharF inputFunc, 
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, ImportVToDoF vToDoFunc)
{
#pragma unused (beginAlreadyRead)

	UInt16 c = '\n';
	char identifier[40];
	int identifierEnd;
	UInt32 uniqueID;
	Err error = 0;
	
	c = inputFunc(inputStream);
	if (c == EOF) return false;
		
	identifierEnd = 0;
	// CAUTION: IsAlpha(EOF) returns true
	while (TxtCharIsAlpha(c) || c == ':' || (c != linefeedChr && TxtCharIsSpace(c)))
		{
		if (!TxtCharIsSpace(c) && (identifierEnd < sizeof(identifier) - sizeOf7BitChar(nullChr)))
	 		identifier[identifierEnd++] = (char) c;
	 	
		c = inputFunc(inputStream);
		if (c == EOF) return false;
		}
	identifier[identifierEnd++] = nullChr;
	
	// Read in the vcard entry
	if (StrCaselessCompare(identifier, "BEGIN:VCALENDAR") == 0)
		{
		do
			{
			identifierEnd = 0;
			c = inputFunc(inputStream);
			if (c == EOF) return false;
			// CAUTION: IsAlpha(EOF) returns true
			while (TxtCharIsAlpha(c) || c == ':' || (c != linefeedChr && TxtCharIsSpace(c) ||
			      TxtCharIsAlNum(c) || c == '.' ))
				{
				if (!TxtCharIsSpace(c) && (identifierEnd < sizeof(identifier) - sizeOf7BitChar(nullChr)))
	 				identifier[identifierEnd++] = (char) c;
	 	
				c = inputFunc(inputStream);
				if (c == EOF) return false;
				}
			identifier[identifierEnd++] = nullChr;

			// Hand it off to the correct sub-routine
			// Note: here VCalEventRead is a dummy routine that just runs until it finds an end
			if (StrCaselessCompare(identifier, "BEGIN:VTODO") == 0)
				{
				error = vToDoFunc(dbP, inputStream, inputFunc, obeyUniqueIDs, true);
				if (error)
					ErrThrow(error);
				}
			else if (StrCaselessCompare(identifier, "BEGIN:VEVENT") == 0)
				{
				DateImportVEvent(dbP, inputStream, inputFunc, obeyUniqueIDs, true, &uniqueID);
				DateSetGoToParams (dbP, inputStream, uniqueID);
				}
			// Handle the end of a calender.
			else if (StrCaselessCompare(identifier, "END:VCALENDAR") == 0)
				{
				// Advance to the next line.
				while ((c != '\n') && (c != EOF))
					{
					c = inputFunc(inputStream);
					} 
				
				break;
				}
			
			} while (true);
		
		}

	return (c != EOF);
}


/***********************************************************************
 *
 * FUNCTION:    GetChar
 *
 * DESCRIPTION: Function to get a character from the exg transport. 
 *
 * PARAMETERS:  exgSocketP - the exg connection
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   8/15/97   Initial Revision
 *
 ***********************************************************************/
SEC_RECV static UInt16 GetChar(const void * exgSocketP)
{
	UInt32 len;
	UInt8 c;
	Err err;
	
	len = ExgReceive((ExgSocketPtr) exgSocketP, &c, sizeof(c), &err);
	
	if (err || len == 0)
		return EOF;
	else
		return c;
}


/***********************************************************************
 *
 * FUNCTION:    PutString
 *
 * DESCRIPTION: Function to put a string to the exg transport. 
 *
 * PARAMETERS:  exgSocketP - the exg connection
 *					 stringP - the string to put
 *
 * RETURNED:    nothing
 *					 If the all the string isn't sent an error is thrown using ErrThrow.
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   8/15/97   Initial Revision
 *
 ***********************************************************************/
SEC003 static void PutString(void * exgSocketP, const Char * const stringP)
{
	UInt32 len;
	Err err;
	
	len = ExgSend((ExgSocketPtr) exgSocketP, stringP, StrLen(stringP), &err);
	
	// If the bytes were not sent throw an error.
	if (len == 0 && StrLen(stringP) > 0)
		ErrThrow(err);
}


/***********************************************************************
 *
 * FUNCTION:    SetDescriptionAndFilename
 *
 * DESCRIPTION: Derive and allocate a decription and filename from some text. 
 *
 * PARAMETERS:  textP - the text string to derive the names from
 *					 descriptionPP - pointer to set to the allocated description 
 *					 descriptionHP - handle to set to the allocated description 
 *					 filenamePP - pointer to set to the allocated filename 
 *					 filenameHP - handle to set to the allocated description 
 *
 * RETURNED:    a description and filename are allocated and the pointers are set
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   11/4/97   Initial Revision
 *
 ***********************************************************************/
SEC003 static void SetDescriptionAndFilename(Char * textP, Char **descriptionPP, 
	MemHandle *descriptionHP, Char **filenamePP, MemHandle *filenameHP)
{
	Char * descriptionP;
   Int16 descriptionSize;
	Int16 descriptionWidth;
	Boolean descriptionFit;
	Char * spaceP;
	Char * filenameP;
	UInt8 filenameLength;
	
	
	descriptionSize = StrLen(textP);
	WinGetDisplayExtent(&descriptionWidth, NULL);
	FntCharsInWidth (textP, &descriptionWidth, &descriptionSize, &descriptionFit);
	
	if (descriptionSize > 0)
		{
		*descriptionHP = MemHandleNew(descriptionSize+sizeOf7BitChar('\0'));
		descriptionP = MemHandleLock(*descriptionHP);
		MemMove(descriptionP, textP, descriptionSize);
		descriptionP[descriptionSize] = nullChr;
		}
	else
		{
		*descriptionHP = DmGetResource(strRsc, beamDescriptionStrID);
		descriptionP = MemHandleLock(*descriptionHP);
		}
	
		
	if (descriptionSize > 0)
		{
		// Now form a file name.  Use only the first word or two.
		spaceP = StrChr(descriptionP, spaceChr);
		if (spaceP)
			// Check for a second space
			spaceP = StrChr(spaceP + sizeOf7BitChar(spaceChr), spaceChr);
		
		// If at least two spaces were found then use only that much of the description.
		// If less than two spaces were found then use all of the description.
		if (spaceP)
			filenameLength = spaceP - descriptionP;
		else
			filenameLength = StrLen(descriptionP);
		
		
		// Allocate space and form the filename
		*filenameHP = MemHandleNew(filenameLength + StrLen(dateFilenameExtension) + sizeOf7BitChar('\0'));
		filenameP = MemHandleLock(*filenameHP);
		if (filenameP)
			{
			MemMove(filenameP, descriptionP, filenameLength);
			MemMove(&filenameP[filenameLength], dateFilenameExtension, 
				StrLen(dateFilenameExtension) + sizeOf7BitChar('\0'));
			}
		}
	else
		{
		*filenameHP = DmGetResource(strRsc, beamFilenameStrID);
		filenameP = MemHandleLock(*filenameHP);
		}
	
	
	*descriptionPP = descriptionP;
	*filenamePP = filenameP;
}


/***********************************************************************
 *
 * FUNCTION:    DateSendRecordTryCatch
 *
 * DESCRIPTION: Send a record.  
 *
 * PARAMETERS:	 dbP - pointer to the database to add the record to
 * 				 recordNum - the record number to send
 * 				 recordP - pointer to the record to send
 * 				 exgSocketP - the exchange socket used to send
 *
 * RETURNED:    0 if there's no error
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger  12/11/97  Initial Revision
 *
 ***********************************************************************/
SEC003 static Err DateSendRecordTryCatch (DmOpenRef dbP, Int16 recordNum, 
	ApptDBRecordPtr recordP, ExgSocketPtr exgSocketP)
{
   volatile Err error = 0;
	
	
		// An error can happen anywhere during the send process.  It's easier just to 
		// catch the error.  If an error happens, we must pass it into ExgDisconnect.
		// It will then cancel the send and display appropriate ui.
		ErrTry
			{
			PutString(exgSocketP, "BEGIN:VCALENDAR" imcLineSeparatorString);
			PutString(exgSocketP, "VERSION:1.0" imcLineSeparatorString);
			DateExportVCal(dbP, recordNum, recordP, exgSocketP, PutString, true);
			PutString(exgSocketP, "END:VCALENDAR" imcLineSeparatorString);
			}
		
		ErrCatch(inErr)
			{
			error = inErr;
			} ErrEndCatch

	
	return error;
}


/***********************************************************************
 *
 * FUNCTION:    DateSendRecord
 *
 * DESCRIPTION: Send a record.  
 *
 * PARAMETERS:	 dbP - pointer to the database to add the record to
 * 				 recordNum - the record to send
 *
 * RETURNED:    true if the record is found and sent
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   5/9/97   Initial Revision
 *
 ***********************************************************************/
SEC003 extern void DateSendRecord (DmOpenRef dbP, UInt16 recordNum)
{
	ApptDBRecordType record;
	MemHandle recordH;
	MemHandle descriptionH = NULL;
	Err error;
	ExgSocketType exgSocket;
	MemHandle nameH = NULL;
	Boolean empty;
	
	
	// Form a description of what's being sent.  This will be displayed
	// by the system send dialog on the sending and receiving devices.
	error = ApptGetRecord (dbP, recordNum, &record, &recordH);
	
	// If the description field is empty and the note field is empty, then
	// consider the record empty and don't beam it.  (Logic from ClearEditState)
	empty = true;
	if (record.description && *record.description)
		empty = false;
	else if (record.note && *record.note)
		empty = false;

	if (!empty)
	{
		// important to init structure to zeros...
		MemSet(&exgSocket, sizeof(exgSocket), 0);
		
		// Set the exg description to the record's description.
		SetDescriptionAndFilename(record.description, &exgSocket.description, 
			&descriptionH, &exgSocket.name, &nameH);
		
		exgSocket.length = MemHandleSize(recordH) + 100;		// rough guess
		exgSocket.target = datebookCreator;
		
		error = ExgPut(&exgSocket);   // put data to destination
		if (!error)
			{
			error = DateSendRecordTryCatch(dbP, recordNum, &record, &exgSocket);

			
			// Release the record before the database is sorted in loopback mode.
			if (recordH)
				{
				MemHandleUnlock(recordH);
				recordH = NULL;
				}
			
			ExgDisconnect(&exgSocket, error);
			}
	}
	else
		FrmAlert(NoDataToBeamAlert);
	
	
	// Clean winUp
	//
	// The description may be an allocated memeory block or it may be a 
	// locked resource.
	if (descriptionH)
		{
		MemHandleUnlock (descriptionH);
		if (MemHandleDataStorage (descriptionH))
			DmReleaseResource(descriptionH);
		else
			MemHandleFree(descriptionH);
		}

	// The name may be an allocated memeory block or it may be a 
	// locked resource.
	if (nameH)
		{
		MemHandleUnlock (nameH);
		if (MemHandleDataStorage (nameH))
			DmReleaseResource(nameH);
		else
			MemHandleFree(nameH);
		}

	if (recordH)
		MemHandleUnlock(recordH);
	
	
	return;
}


/************************************************************
 *
 * FUNCTION: DateImportVToDo
 *
 * DESCRIPTION: Import a VCal record of type vToDo by calling
 * the ToDo List app.
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *
 * RETURNS: true if the input was read
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	9/12/97		Created
 *
 *************************************************************/

SEC_RECV static Boolean DateImportVToDo(DmOpenRef dbP, void * inputStream, GetCharF inputFunc,
	Boolean obeyUniqueIDs, Boolean unusedFlag)	/* Db35: named formerly-anonymous param (gcc 2.95 C rejects unnamed params) */
{
#pragma unused (unusedFlag)
#if EMULATION_LEVEL == EMULATION_NONE
#pragma unused (dbP, inputFunc, obeyUniqueIDs)
#else
#pragma unused (dbP, inputStream, inputFunc, obeyUniqueIDs)
#endif

#if EMULATION_LEVEL == EMULATION_NONE
	AppCallWithCommand(sysFileCToDo, sysAppLaunchCmdExgReceiveData, inputStream);
#endif

	return false;
}


/***********************************************************************
 *
 * FUNCTION:		DateReceiveData
 *
 * DESCRIPTION:		Receives data into the output field using the Exg API
 *
 * PARAMETERS:		exgSocketP, socket from the app code
 *						 sysAppLaunchCmdExgReceiveData
 *
 * RETURNED:		error code or zero for no error.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			???	???		Created
 *			art	10/17/97	Added "go to" record logic
 *
 ***********************************************************************/
SEC_RECV extern Err DateReceiveData(DmOpenRef dbP, ExgSocketPtr exgSocketP)
{
	volatile Err err;

	// accept will open a progress dialog and wait for your receive commands
	err = ExgAccept(exgSocketP);
	
	if (!err)
		{
		// Catch errors receiving records.  The import routine will clean winUp the
		// incomplete record.  This routine passes the error to ExgDisconnect
		// which displays appropriate ui.
		ErrTry
			{
			// Keep importing records until it can't
			while (DateImportVCal(dbP, exgSocketP, GetChar, false, false, DateImportVToDo))
				{};
			}
		
		ErrCatch(inErr)
			{
			err = inErr;
			} ErrEndCatch
		
		
		ExgDisconnect(exgSocketP, err); // closes transfer dialog
		}
	
	return err;
}

