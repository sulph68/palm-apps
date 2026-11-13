#ifndef _BUFVEC_H_
#define _BUFVEC_H_


#include <stdint.h>


struct bv {
	struct bv *next;
	void *ptr;
	uint16_t len;		//nonzero
};

#define BV_INIT(_ptr, _len, _next)	{.ptr = (_ptr), .len = (_len), .next = (_next), }


typedef struct bv* bv_iter;

#define BV_START(_bv)			(_bv)
#define BV_HAVEMORE(_bv)		(!!(_bv))
#define BV_NEXT(_bv)			((_bv)->next)

#define BV_ITER_PTR(_iter)		((_iter)->ptr)
#define BV_ITER_LEN(_iter)		((_iter)->len)

#define BV_SET_LEN(_bv, _len)	((_bv)->len = (_len))





#endif
