#ifdef _MULTISEGMENT

/* Beam moved out of sec001 (2026-11-19) to make room there for the record
 * metadata changes; sec003 also holds the secondary-database code. */
#define BEAM_SECTION	__attribute__ ((section ("sec003")))
#define AES_SECTION		__attribute__ ((section ("sec001")))
/* Sec35 shared-key crypto core (apps/lib/enccore.c) -- same segment as the
 * rest of this app's crypto, per the CryptoNote encryption plan. */
#define ENCCORE_SECTION	__attribute__ ((section ("sec001")))
/* Password entry (PwMask.c), the global-password dialogs and the category
 * encrypt/decrypt loops. No libgcc here (no 32-bit multiply/divide). */
#define UI2_SECTION		__attribute__ ((section ("sec002")))
/* Secondary databases, record metadata, moves (SecDb.c). */
#define DB3_SECTION		__attribute__ ((section ("sec003")))
/* The combined list (ViewList.c) and the new forms (DbForms.c). */
#define UI4_SECTION		__attribute__ ((section ("sec004")))

#else // !_MULTISEGMENT

#define BEAM_SECTION
#define AES_SECTION
#define ENCCORE_SECTION
#define UI2_SECTION
#define DB3_SECTION
#define UI4_SECTION

#endif
