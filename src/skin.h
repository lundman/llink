#ifndef SKIN_H_INCLUDED
#define SKIN_H_INCLUDED

#include "file.h"
#include "request.h"
#include "process.h"


/* Defines */

#define SKIN_DEFAULT_HEAD "head.html"
#define SKIN_DEFAULT_TAIL "tail.html"
#define SKIN_DEFAULT_MENU "menu.html"
#define SKIN_DEFAULT_DELAY 1


/* Variables */

enum macro_enum {
	LLINK_SERVER_NAME = 0,
	LLINK_FILE_NAME,
	LLINK_FILE_URL,
	LLINK_FILE_SHORT_URL,
	LLINK_FILE_SIZE,
	LLINK_FILE_DATE,
	LLINK_FILE_TVID,
	LLINK_FILE_EXT,
	LLINK_FILE_MIME,
    LLINK_FILE_UPNP_TYPE,
    LLINK_UPNP_METANAME,
    LLINK_UPNP_UUID,
	LLINK_PARENT_DIRECTORY,
	LLINK_CURRENT_DIRECTORY,
	LLINK_DETAIL_SIZE,
	LLINK_PAGE_PREV_URL,
	LLINK_PAGE_NEXT_URL,
	LLINK_PARENT_DIRECTORY_URL,
	LLINK_CURRENT_DIRECTORY_URL,
	LLINK_ONKEYUP_PREV,
	LLINK_ONKEYDOWN_NEXT,
	LLINK_PAGE_FOCUS,
	LLINK_ZODIAC,

	LLINK_EXTNFO_ICON,
	LLINK_EXTNFO_TITLE,
	LLINK_EXTNFO_LOCAL_TITLE,
	LLINK_EXTNFO_COUNTRY,
	LLINK_EXTNFO_LENGTH,
	LLINK_EXTNFO_TAGLINE,
	LLINK_EXTNFO_DESCRIPTION,
	LLINK_EXTNFO_GENRES,
	LLINK_EXTNFO_DATE,
	LLINK_EXTNFO_DIRECTORS,
	LLINK_EXTNFO_CAST,
	LLINK_EXTNFO_RATING,
	LLINK_EXTNFO_IMDBURL,

	LLINK_CURRENT_PLAYALL,
	LLINK_FILE_PLAYALL,
	LLINK_TOP_BOTTOM,
	LLINK_FILE_PAGEID,

	LLINK_RAND,

	LLINK_FULL_URL,

	LLINK_CURRENT_VIEWALL,
	LLINK_VIEW_SONGLIST,

	LLINK_PAGE_HELPERS,
	LLINK_TEXT_VISITED,

	LLINK_IF_CLIENT_IS_SYABAS,
	LLINK_IF_CLIENT_IS_SYABAS_E,
	LLINK_IF_CLIENT_IS_NOT_SYABAS,
	LLINK_IF_CLIENT_IS_NOT_SYABAS_E,
	LLINK_IF_CLIENT_ELSE,

	LLINK_STATUS_MESSAGE,

	LLINK_SORT_NEXT,
	LLINK_SORT_PREV,

    LLINK_DIRECTORY_TVID_START,
    LLINK_DIRECTORY_TVID_END,

	LLINK_DIRECTORY_PAGE_START,
	LLINK_DIRECTORY_PAGE_END,

	LLINK_DIRECTORY_PAGE_CURRENT,
    LLINK_DIRECTORY_TVID_CURRENT_START,
    LLINK_DIRECTORY_TVID_CURRENT_END,
    LLINK_DIRECTORY_TVID_CURRENT_COUNT,

};

typedef enum macro_enum macro_type_t;



struct macro_struct {
	macro_type_t type;
	unsigned int offset;
};

typedef struct macro_struct macro_t;


struct skin_struct {
	char *ext;               // *.mpg
	char *ignore;            // ignore filematch
	char *name;              // Movies
	char *html;              // The html data
	char *filename;          // Diskname
	char *args;              // cmd=external, hold args for execution
    char *upnp_type;         // The container type to send with UPNP
	unsigned int size;       // Size of said data
	unsigned int index;      // While reading, how many bytes are in.
	macro_t *macros;         // Each macro in order, sorted.
	unsigned int num_macros; // The number of macros.

	request_process_t process_cmd;

	int delay;
	int pass;                // As in num_passes this loop.
	int count;               // Separator entry, at this count.
    int is_sd;               // Set if considered SD version of the skin

	struct skin_struct *next;
};

typedef struct skin_struct skin_t;


struct cache_struct {
	lfile_t file;
	skin_t *skin;
	extnfo_t extnfo;
	request_process_t process_function;
	struct cache_struct *next;
};

typedef struct cache_struct cache_t;



enum size_type_enum {
	SIZE_TYPE_BYTES = 0,
	SIZE_TYPE_HUMAN,
	SIZE_TYPE_NONE
};

typedef enum size_type_enum size_type_t;


/* Functions */

int    skin_init          ( void );
void   skin_free          ( void );


void   skin_cmd           ( char **, char **, int ,void * );
void   skin_upnpcmd       ( char **, char **, int ,void * );
void   skin_cmd_type      ( char **, char **, int ,void * );
void   skin_cmd_rand      ( char **, char **, int ,void * );
void   skin_cmd_sort      ( char **, char **, int ,void * );
void   skin_cmd_transcode ( char **, char **, int ,void * );


int    skin_handler       ( lion_t *, void *, int, int, char * );


void   skin_write_head    ( request_t * );
int    skin_write_tail    ( request_t * );
void   skin_write_type    ( request_t *, char * );
skin_t *skin_process_cmd  ( request_t * );

char   *skin_get_rar      ( void );
char   *skin_redirect     ( skin_types_ptr, char * );
skin_t *skin_findbyname   ( skin_types_ptr, char *, int );
skin_t *skin_findbyname_all(char *, int );
void    skin_write_menu   ( request_t *, char *);
skin_t *skin_findbymatch  ( skin_types_ptr, char *, int );
void    skin_setsort      ( request_t *, int);
char   *skin_get_sort_str ( request_t * );
int     skin_get_sort_flags(request_t * );
void    skin_cache_clear  ( request_t * );
void    skin_set_skin     ( request_t *, char * );
char   *skin_get_root     ( skin_types_ptr );
char   *skin_get_ffmpeg   ( void );
char   *skin_transcode_id2args ( unsigned int );
char   *skin_transcode_getext ( unsigned int );

#endif

