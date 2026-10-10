#include "Database.h"

#define kRecordColumn		0

/**************** progress structure *************/
typedef struct
{
	UInt16	  soFar;
	UInt16	  total;
} ProgrAmountType;

typedef struct
{
	ProgrAmountType	major;
	ProgrAmountType	minor;
} ProgrType;

typedef Boolean ProgressStatusFuncType (MemPtr progrInfoP,
	 ProgrType* progressP, Char* messageP);

typedef struct
{
	FormPtr			progressForm;		  // Dialog that displays the progress
	UInt16			gaugeWidth;			  // Width of the gauge in pixels
	UInt16			gaugeLevel;			  // Filled-in width of the progress gauge
	ProgressStatusFuncType*	statusFunc;		// function that updates the status
	RectangleType	messageR;			  // Rectangle to display message in
	RectangleType	gaugeR;				  // Rectangle to draw bar in
} ProgressInfoType, * ProgressInfoPtr;

typedef struct
{
	ProgressInfoPtr	  progrInfoP;
	ProgrType*		  progrP;
} EraseProgrUpdateType;
/**************** progress structure *************/


Boolean MainFormHandleEvent(EventPtr eventP);
void MainInvalidateCardDir(void);	/* force a re-scan next time the card browser opens */
void DrawStringWithEllipsis (Char *, Int16, Int16, Int16, JustificationEnum);

void DecryptRecord(UInt16, Char *);