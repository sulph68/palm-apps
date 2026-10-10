#ifdef _MULTISEGMENT

#define BEAM_SECTION	__attribute__ ((section ("sec001")))
#define AES_SECTION		__attribute__ ((section ("sec001")))
/* Sec35 shared-key crypto core (apps/lib/enccore.c) -- same segment as the
 * rest of this app's crypto, per the CryptoNote encryption plan. */
#define ENCCORE_SECTION	__attribute__ ((section ("sec001")))

#else // !_MULTISEGMENT

#define BEAM_SECTION
#define AES_SECTION
#define ENCCORE_SECTION

#endif
