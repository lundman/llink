#ifndef LLRAR_H_INCLUDED
#define LLRAR_H_INCLUDED

#include "request.h"

/* Defines */

#define LLRAR_CACHE_SIZE 5  // How many rar caches to remember?

struct llrar_cache_entry_struct {
    lfile_t file;
    struct llrar_cache_entry_struct *next;
};

typedef struct llrar_cache_entry_struct llrar_cache_entry_t;

struct llrar_cache_struct {
    char *path; // "/directory/file.rar" to uniquely identify
    llrar_cache_entry_t *files;
};

typedef struct llrar_cache_struct llrar_cache_t;

/* Variables */




/* Functions */

int         llrar_init            ( void );
void        llrar_free            ( void );

void        llrar_list            ( request_t *, char * );
int         llrar_get             ( request_t *, char * );
#ifdef WITH_UNRAR_BUFFERS
int         llrar_incaches        ( request_t * );
#endif
lion64_t    llrar_getsize         ( request_t * );

#endif
