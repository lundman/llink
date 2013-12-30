#ifndef EXTERNAL_H_INCLUDED
#define EXTERNAL_H_INCLUDED

#include "request.h"
#include "skin.h"



/* Defines */





/* Variables */




/* Functions */

int    external_init          ( void );
void   external_free          ( void );


int    external_launch        ( request_t *, skin_t * );




#endif
