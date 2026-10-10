/* sects.h - segment attributes for the bundled crypto (aes.c, enccore.c).
 *
 * Default (SecTest / Sec35, single-segment): empty. A multi-segment host (e.g.
 * Address35) force-includes its own seg header FIRST and can #define AES_SECTION /
 * ENCCORE_SECTION to a real __attribute__((section(...))) to move the crypto off
 * the main code resource. */
#ifndef SECTS_H
#define SECTS_H
#ifndef AES_SECTION
#define AES_SECTION
#endif
#ifndef ENCCORE_SECTION
#define ENCCORE_SECTION
#endif
#endif
