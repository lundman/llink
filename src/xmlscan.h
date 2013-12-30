#ifndef XMLSCAN_H_INCLUDED
#define XMLSCAN_H_INCLUDED


/* Defines */


/* Variables */



/* Functions */

void xmlscan_run        ( void );
void xmlscan_finishdir  ( request_t * );
void xmscan_addfile     ( request_t *, skin_t *, lfile_t * );
void xmlscan_imdb       ( char **, char **, int ,void * );


#endif
