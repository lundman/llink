#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lion.h"
#include "misc.h"

#include "request.h"
#include "mime.h"

#include "debug.h"

#define EXT( cmd ) (cmd) , sizeof (cmd) -1

struct mime_type_struct {
	char *ext;
	int   strlen;  // Does it make sense to have strlen when all ext is 2 or 3
	char *type;
	struct mime_type_struct *next;
};

typedef struct mime_type_struct mime_type_t;


static    mime_type_t      *mime_types_head     =    NULL;
static    lion_t           *mime_handle         =    NULL;
static    int               mime_read_done      =    0;
static    char             *mime_file           =    MIME_DEFAULT_FILE;
static    int               mime_num_types      =    0;
static    int               mime_num_ext        =    0;


static int mime_handler(lion_t *, void *, int, int, char *);



static mime_type_t *mime_newnode(void)
{
	mime_type_t *result;

	result = malloc(sizeof(*result));
	if (!result) return NULL;

	memset(result, 0, sizeof(*result));

	return result;

}


static void mime_freenode(mime_type_t *node)
{

	SAFE_FREE(node->ext);
	SAFE_FREE(node->type);

	SAFE_FREE(node);

}



int mime_init( void )
{

	// Using lion to read a config file, well, well...
	mime_handle = lion_open(mime_file, O_RDONLY, 0600,
							LION_FLAG_NONE, NULL);

	mime_read_done = 0;

	if (mime_handle)
		lion_set_handler(mime_handle, mime_handler);
	else {
		debugf("[mime] failed to read '%s'\n", mime_file);
		return -1;
	}


	while(!mime_read_done) {
		lion_poll(10, 0);
	}

	return 0;

}




void mime_free( void )
{
	mime_type_t *runner, *next;

	if (mime_handle) {
		lion_close(mime_handle);
		mime_handle = NULL;
	}

	for (runner = mime_types_head; runner; runner = next) {
		next = runner->next;
		mime_freenode(runner);
	}

	mime_types_head = NULL;

}



char *mime_type(char *search)
{
	char *ext;
	int len;
	mime_type_t *runner;

	debugf("[mime] type for '%s'\n", search);

	// Extension based mime type..
	ext = strrchr(search, '.');

	if (ext) {

		ext++;
		len = strlen(ext);

		for (runner = mime_types_head; runner; runner = runner->next) {

			if ((runner->strlen == len) &&
				!mystrccmp(runner->ext, ext)) {
				return (runner->type);

			}

		}

	}

	// No extention, or no match... what do send.
	// In v2, would could read a few bytes, and guess.

	// What is the best default
	return "application/octet-stream";
}


//
// userinput for reading the mime.types
//

static int mime_handler(lion_t *handle, void *user_data,
						int status, int size, char *line)
{
	char *type, *ext, *ar;
	mime_type_t *node;

	switch(status) {

	case LION_FILE_OPEN:
		debugf("[mime]: reading types...\n");
		break;


	case LION_FILE_FAILED:
	case LION_FILE_CLOSED:
		debugf("[mime]: finished reading %d types and %d extentions.\n",
			   		mime_num_types, mime_num_ext);
		mime_read_done = 1;
		mime_handle = NULL;
		break;

	case LION_INPUT:

		// Allow, and ignore, comments.
		if (!line || !*line || *line == '#') break;

		//video/mpeg			mpeg mpg mpe
		ar = line;

		type = misc_digtoken(&ar, " \t");

		if (!type || !*type || *type == '#')
			break;

		mime_num_types++;

		// For each extension
		while (( ext = misc_digtoken(&ar, " \t"))) {

			node = mime_newnode();
			if (!node) continue;

			node->ext = strdup(ext);
			node->strlen = strlen(ext);
			node->type = strdup(type);

			node->next = mime_types_head;
			mime_types_head = node;

			mime_num_ext++;

		}

		break;

	}

	return 0;

}







