#include "Database.h"

#define kRecordColumn		0



Boolean MainFormHandleEvent(EventPtr eventP);
void MainInvalidateCardDir(void);	/* force a re-scan next time the card browser opens */
void DrawStringWithEllipsis (Char *, Int16, Int16, Int16, JustificationEnum);

void DecryptRecord(UInt16, Char *);