// $Id: file.h,v 1.5 2009/09/08 13:41:52 lundman Exp $
//
// Jorgen Lundman 18th March 2004.

#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED


#include "lion_types.h"
#include <sys/types.h>


// Defines








// Variables

enum yesnoauto_enum {
	YNA_NO = 0,
	YNA_YES,
	YNA_AUTO
};

typedef enum yesnoauto_enum yesnoauto_t;



struct file_struct {

	char *name;
	time_t date;
	lion64u_t size;
	char *user;
	char *group;
	char perm[14];

	yesnoauto_t directory;
	yesnoauto_t soft_link;

	unsigned int tvid;
};

// Solaris already defines "file_t".
typedef struct file_struct lfile_t;







// Functions

int          file_parse      ( lfile_t *, char * );
void         file_free       ( lfile_t * );
void         file_dupe       ( lfile_t *, lfile_t * );
char         *hide_path      ( char * );



#endif
