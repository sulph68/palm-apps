/*
 * RecField.h - the record view's "full field" popup and Edit > Copy (RecField.c, sec002).
 * Tapping a line of the record view (Local or CardDAV) shows that field's complete text in a
 * read-only scrolling box with a Copy button; the Edit menu copies the last tapped field, or
 * the whole contact, to the clipboard.
 */
#ifndef RECFIELD_H
#define RECFIELD_H

#include <PalmOS.h>

/* the record view's display lines (RecordForm.c builds them, RecField.c reads the tapped one) */
#define recHeaderLine	1	/* bold, full-width (the name)            */
#define recSubLine		2	/* normal, full-width (company / title)   */
#define recFieldLine	3	/* "label : value" (aligned colon column) */
#define recContLine		4	/* value only, at the value column        */
#define recValLen		130		/* per-rendered-LINE text (an address sub-line etc.; the name line is 129) */

typedef struct {
	UInt8	kind;
	Int16	field;		/* source record field index (-1 = header/none) */
	char	label[24];
	char	value[recValLen];
} RecLine;

/* The tapped display line (lines[line]) was tapped: show its whole field (record field index; -1 =
 * the name block, shown as "Name") of the contact in the record view. The label is the field's
 * first line's label ("Email (Other)"). */
void RecFieldShow(const RecLine *lines, UInt16 line) SEC002;
/* Edit > Copy Field: the last tapped field to the clipboard (the name block if none was tapped). */
void RecFieldCopy(void) SEC002;
/* Edit > Copy Contact: the contact as the view shows it ("Label: value" lines) to the clipboard. */
void RecContactCopy(const RecLine *lines, UInt16 count) SEC002;
/* Record > Set as me...: Local's Me card, or - in a CardDAV book's record view - that book's own Me. */
void RecSetMe(void) SEC002;
/* A new contact is shown: forget the last tapped field. */
void RecFieldReset(void) SEC002;

#endif
