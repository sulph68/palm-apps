#ifndef _STDBOOL_SHIM_H_
#define _STDBOOL_SHIM_H_
/* gcc 2.95 has no usable stdbool.h next to PalmTypes.h; files that do not include PalmOS.h still need these */
#ifndef bool
#define bool unsigned char
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif
#define __bool_true_false_are_defined 1
#endif
