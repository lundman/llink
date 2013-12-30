#ifndef CGICMD_H_INCLUDED
#define CGICMD_H_INCLUDED

#include "request.h"
#include "conf.h"

/* Defines */

struct cgicmd_struct {
	char *name;
	char *path;
	del_perm_t pin_required;
	struct cgicmd_struct *next;
};
typedef struct cgicmd_struct cgicmd_script_t;




/* Variables */




/* Functions */

int     cgicmd_init       ( void );
void    cgicmd_free       ( void );

void    cgicmd_delete     ( request_t *, char * );
void    cgicmd_unrar      ( request_t *, char * );
void    cgicmd_userexec   ( request_t *, char *, char *);
void    cgicmd_addscript  ( char **, char **, int ,void * );


#endif
