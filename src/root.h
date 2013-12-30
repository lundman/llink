#ifndef ROOT_H_INCLUDED
#define ROOT_H_INCLUDED

#include "request.h"

/* Defines */

#define ROOT_PIN_TIMEOUT 1200  // 20 mins?



/* Variables */

struct root_struct {
	char         *path;
	unsigned int  flags;
	char         *http_host;
	unsigned int  http_port;
	char         *http_file;
	char         *subdir;
	int           proxy;
	int           dvdread;
	char         *url;
};

typedef struct root_struct root_t;



/* Functions */

int         root_init            ( void );
void        root_free            ( void );

char       *root_getroot         ( int );

void        root_cmd             ( char **, char **, int ,void * );
void        root_auth            ( char **, char **, int ,void * );


void        root_list            ( request_t *, char * );


int         root_setpath         ( request_t * );

void        root_undot           ( char * );

void        root_proxy           ( request_t * );

int         root_is_dvdread      ( int );

void        root_register_ip     ( request_t * );
void        root_deregister_ip   ( request_t * );

int         root_isregistered    ( request_t * );

#endif
