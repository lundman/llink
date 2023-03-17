#ifndef SSDP_H_INCLUDED
#define SSDP_H_INCLUDED


/* Defines */

#define SSDP_DEFAULT_PORT      1900
#define SSDP_DEFAULT_MULTICAST "239.255.255.250"
#define SSDP_DEFAULT_UUID      "uuid:E25641A0-8228-11DE-AFB8-0002A5D5C51B"


/* Variables */

extern  unsigned long      ssdp_interface;
extern  int                ssdp_upnp;
extern  char              *ssdp_upnp_uuid;
extern  int                ssdp_upnp_port;



/* Functions */

int    ssdp_init          ( void );
void   ssdp_free          ( void );


void   ssdp_cmd           ( char **, char **, int ,void * );




#endif
