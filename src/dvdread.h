#ifndef DVDREAD_H_INCLUDED
#define DVDREAD_H_INCLUDED

#include "request.h"

/* Defines */




/* Variables */




/* Functions */

int         dvdread_init            ( void );
void        dvdread_free            ( void );
void        dvdread_release         ( request_t * );
void        dvdread_resume          ( void );

void        dvdread_list            ( request_t * );
void        dvdread_get             ( request_t * );

void        dvdread_buffer_used     ( request_t * );
void        dvdread_buffer_empty    ( request_t * );
void        dvdread_rarlist         ( request_t * );
void        dvdread_rarget          ( request_t * );


#endif
