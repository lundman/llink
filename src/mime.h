#ifndef _MIME_H_INCLUDED
#define _MIME_H_INCLUDED

#include "request.h"

#define MIME_DEFAULT_FILE "mime.types"






int     mime_init( void );
void    mime_free( void );
char   *mime_type( char * );



#endif
