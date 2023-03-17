#ifndef PROCESS_H_INCLUDED
#define PROCESS_H_INCLUDED


#include "lion_types.h"
#include "request.h"


typedef int (*process_function_t)(request_t *, char *, int, char **, int *);




/* Defines */




/* Variables */



/* Functions */


int      process_bin2mpg      ( request_t *, char **, int *, 
								char **, int *, int * );
int      process_vcd_sector   ( request_t * );

void     process_bin2mpg_size ( lion64u_t * );

#endif
