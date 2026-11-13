#ifndef _SWAP_H_
#define _SWAP_H_


#if !defined(SASD_LITTLE_ENDIAN) && !defined(SASD_BIG_ENDIAN)
#define SASD_BIG_ENDIAN
#endif



#if defined(SASD_BIG_ENDIAN)

	#define TO_U16(_v)			(_v)
	#define TO_U32(_v)			(_v)
	
#elif defined(SASD_LITTLE_ENDIAN)

	#define TO_U16(_v)			__builtin_bswap16(_v)
	#define TO_U32(_v)			__builtin_bswap32(_v)

#else

	#error "endian error"
	
#endif



#ifdef __GNUC__
	#define PACKED_PRE
	#define PACKED_POST	__attribute__((packed))
#else
	#define PACKED_PRE
	#define PACKED_POST
	#define fprintf(...)
#endif



#endif
