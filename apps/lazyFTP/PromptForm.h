/*
 * PromptForm.h - reusable single-field text-entry modal dialog (New
 * Folder's name, Rename's new name). Title/label/initial value are all
 * set at runtime so one resource form serves both callers.
 */
#ifndef PROMPTFORM_H
#define PROMPTFORM_H

#include "LazyFTP.h"

/* Shows the dialog with the given title/label/starting value. On OK,
 * copies the entered text into outValue (outValueLen incl. NUL) and
 * returns true; on Cancel, leaves outValue untouched and returns false. */
Boolean PromptFormRun(const char *title, const char *label,
	const char *initialValue, char *outValue, UInt16 outValueLen) SEC001;

#endif
