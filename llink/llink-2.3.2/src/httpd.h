#ifndef HTTPD_H_INCLUDED
#define HTTPD_H_INCLUDED


/* Defines */

#define HTTPD_DEFAULT_PORT 1900
#define HTTPD_DEFAULT_MULTICAST "239.255.255.250"



/* Variables */

extern int                httpd_port;



/* Functions */

int    httpd_init          ( void );
void   httpd_free          ( void );
unsigned long httpd_myip(void);


void   httpd_cmd           ( char **, char **, int ,void * );




#endif
