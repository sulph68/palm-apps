///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Program Name:  endianUtils.h                                              //
//       Author:  Mike McCollister                                           //
//     Language:  PRC-Tools 2.3                                              //
//                Palm OS 5 SDK R3                                           //
//                PilRC 3.2                                                  //
//                HandEra Header Files from SDK 1.04                         //
//                palmOne Header Files from SDK version 5.1                  //
//                Sony Header Files from SDK 5.0 version 1.2                 //
//  Description:  Endian Utilities.                                          //
//===========================================================================//
//  Version: 3.20                                                            //
//     Date: 11-18-2003                                                      //
//   Author: Michael J. McCollister                                          //
// Comments: Initial Release.                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef ENDIAN_H
#define	ENDIAN_H 1

/////////////
// Defines //
/////////////

#define ByteSwap16(n) ( ((((unsigned int) n) << 8) & 0xFF00) | \
                        ((((unsigned int) n) >> 8) & 0x00FF) )

#define ByteSwap32(n) ( ((((unsigned long) n) << 24) & 0xFF000000) | \
                        ((((unsigned long) n) <<  8) & 0x00FF0000) | \
                        ((((unsigned long) n) >>  8) & 0x0000FF00) | \
                        ((((unsigned long) n) >> 24) & 0x000000FF) )

#define Read68KUnaligned32(addr) \
  ( ((((unsigned char *)(addr))[0]) << 24) | \
    ((((unsigned char *)(addr))[1]) << 16) | \
    ((((unsigned char *)(addr))[2]) <<  8) | \
    ((((unsigned char *)(addr))[3])) )

#define Write68KUnaligned32(addr, value) \
  ( ((unsigned char *)(addr))[0]=(unsigned char)((unsigned long)(value)>>24), \
    ((unsigned char *)(addr))[1]=(unsigned char)((unsigned long)(value)>>16), \
    ((unsigned char *)(addr))[2]=(unsigned char)((unsigned long)(value)>> 8), \
    ((unsigned char *)(addr))[3]=(unsigned char)((unsigned long)(value)) )

#endif
