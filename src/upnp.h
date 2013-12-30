#ifndef UPNP_H_INCLUDED
#define UPNP_H_INCLUDED







void upnp_setcommand    ( request_t *, char * );
void upnp_parseinput    ( request_t *, char * );
void upnp_reply         ( request_t * );

#endif
