#ifndef EXTRA_H_INCLUDED
#define EXTRA_H_INCLUDED

#include "file.h"

/* Defines */

#define EXTRA_NUM_DIRECTOR  5
#define EXTRA_NUM_CAST     10
#define EXTRA_NUM_GENRE     5
#define EXTRA_QUOTECHARS "&"

/* Variables */

struct extnfo_struct {
	char *filename;
	char *title;
	char *icon_path;
	char *title_local;
	char *country;
	char *length;
	char *tagline;
	char *description;
	char *genre[EXTRA_NUM_GENRE];
	char *date;
	char *director[EXTRA_NUM_DIRECTOR];
	char *cast[EXTRA_NUM_CAST];
	char *rating;
	char *imdburl;
	int person_type; // 0 notseen, 1 actor, 2 director
};

typedef struct extnfo_struct extnfo_t;



/* Functions */

int       extra_init          ( void );
void      extra_free          ( void );

int       extra_lookup        ( extnfo_t *, char *, char *, lfile_t *, void * );
void      extra_release       ( extnfo_t * );

void      extra_dupe          ( extnfo_t *, extnfo_t * );

#endif
