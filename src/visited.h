#ifndef VISITED_H_INCLUDED
#define VISITED_H_INCLUDED

#include "request.h"

/* Defines */




/* Variables */

extern char *visited_upnp_precat;
extern char *visited_upnp_postpend;



/* Functions */

int         visited_init            ( void );
void        visited_free            ( void );

void        visited_fileclosed      ( request_t * );
int         visited_filename        ( request_t *, char * );
int         visited_file            ( request_t *, lfile_t * );
void        visited_cmd             ( char **, char **, int ,void * );

#endif
