#ifndef ADVD_H_INCLUDED
#define ADVD_H_INCLUDED


/* Defines */




/* Variables */




/* Functions */

int         advd_init            ( void );
void        advd_free            ( void );
void        advd_buffer_used     ( request_t * );
void        advd_buffer_empty    ( request_t * );
void        advd_freenode_req    ( request_t * );
int         advd_list            ( char *, char *, request_t * );
int         advd_get             ( char *, char *, char *, request_t * );



#endif
