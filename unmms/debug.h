// $Id: debug.h,v 1.1 2009/10/12 02:36:49 lundman Exp $
// 
// Jorgen Lundman 10th October 2003.

#ifndef DEBUG_H_INCLUDED
#define DEBUG_H_INCLUDED

// Defines.
#ifndef DEBUG_FLAG
#define DEBUG_FLAG 1
#endif

#define debugf if (debug_on&DEBUG_FLAG) printf




// Variables

extern int debug_on;


#endif
