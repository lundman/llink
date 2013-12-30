#ifndef REQUEST_H_INCLUDED
#define REQUEST_H_INCLUDED


#include "lion_types.h"
#include <sys/stat.h>

#ifdef WIN32
#define _WINDOWS_MEAN_AND_LEAN
#include <windows.h>
#include "win32.h"   // For the stat->_stat64 define.
#endif

#include "extra.h"


/* Defines */

#define REQUEST_TYPE_KEEPALIVE 1
#define REQUEST_TYPE_CLOSED    2


// Buffer unrar data for negative-lseeks?
#define WITH_UNRAR_BUFFERS 100



enum request_process_enum {
	REQUEST_PROCESS_NONE = 0,
	REQUEST_PROCESS_BIN2MPG,
	REQUEST_PROCESS_EXTERNAL,
	REQUEST_PROCESS_UNRAR,
	REQUEST_PROCESS_REDIRECT,
	REQUEST_PROCESS_DVDREAD,
	REQUEST_PROCESS_MENU,
	REQUEST_PROCESS_RARISO,
};

typedef enum request_process_enum request_process_t;

enum request_event_enum {
	REQUEST_EVENT_NEW = 0,
	REQUEST_EVENT_ACTION,
	REQUEST_EVENT_WFILE_OPENED,
	REQUEST_EVENT_WFILE_CLOSED,
	REQUEST_EVENT_RFILE_OPENED,
	REQUEST_EVENT_DVDREAD_SEND,
	REQUEST_EVENT_POST_READDATA,
	REQUEST_EVENT_RAR_NEEDSIZE,
	REQUEST_EVENT_CLOSED
};

typedef enum request_event_enum request_event_t;

enum upnp_enum {
    UPNP_NONE = 0,
    UPNP_GETSORTCAPABILITIES,
    UPNP_GETSEARCHCAPABILITIES,
	UPNP_BROWSEDIRECTCHILDREN,
    UPNP_GETSYSTEMUPDATEID,
    UPNP_BROWSEMETADATA,
    UPNP_ROOTXML,
    UPNP_CUPNP,
    UPNP_CUPNP_READY,
};

typedef enum upnp_enum upnp_t;


#ifdef WIN32
#define TMP_TEMPLATE ".llink.XXXXXX"
#else
#define TMP_TEMPLATE "/tmp/.llink.XXXXXX"
#endif




/* Variables */
// opaque for outsiders
typedef struct skin_types_struct *skin_types_ptr;


struct request_struct {
	request_event_t current_event;
	struct request_struct *next;

	lion_t *handle;
	lion_t *althandle; // the file
	lion_t *tmphandle; // the tmpfile writing..
	lion_t *external_handle; // when launched external command
	lion_t *roothandle; // when connecting to remote http.

	unsigned long ip;
	int port;

	int read_disabled;
	int head_method;
	int post_method;
	int subscribe_method;
    int unknown_request;
	int client_is_syabas;

	int inheader;
	char *path;          // Path from virtual "/"
	char *cgi;           // cgi part of url after virtual path
	char *disk_path;     // Full path from real "/"
	unsigned int type;
	char *rar_name;      // spawned filename for resuming
	char *rar_directory; // path inside rar for directory listings
	char *rar_file;      // file path to GET

    char *keep_path;     // temporary holders of original path/disk_path for
    char *keep_disk_path;// expanding dirlisting cache passes.

    char *useragent;
    unsigned int transcode_id;

	unsigned int cgi_secret;
	char *cgi_host;
	unsigned int cgi_port;
	char *cgi_file;

	unsigned int cgi_stream;

	char *dvdread_file;  // ISO filename inside RAR
	int dvdread_parse;

	char *history;
	char *typename;

	int width;
	int height;

	lion64_t bytes_from;
	lion64_t bytes_to;
	lion64_t bytes_send; // How many bytes to send, if range is specified.
	lion64_t bytes_sent;
	lion64_t bytes_size;

	time_t time_idle;
	time_t time_start;

	char *tmpname;

	struct stat stat;

	int next_list;
	int root_index;

	request_process_t process_function;

	int unrar_parse;

	int page_from;
	int page_to;
	int page_current;
	int page_focus;
	int playall;
	int viewall;
	int menu_mode;
	int sort;
    int send_range;

    skin_types_ptr skin_type;

	int pass; // as in number of passes to process.

	// cache
	void *cache;

	extnfo_t extnfo;
	lfile_t *scanfile; // used only in xmlscan

	void *advd_node;
#ifdef WITH_DVDREAD
	void *dvdread;
	void *dvdfile;
	int dvdvobs;
#endif

#ifdef WIN32
	HANDLE *pipe_handle;
#endif

    upnp_t upnp_type;

#ifdef WITH_UNRAR_BUFFERS
    char        *unrar_buffers[ WITH_UNRAR_BUFFERS ];
    lion64_t     unrar_offsets[ WITH_UNRAR_BUFFERS ];
    unsigned int unrar_sizes  [ WITH_UNRAR_BUFFERS ];
    unsigned int unrar_radix; // Which buffer to replace next.
#endif

    void *cupnp_mutex;
    void *cupnp_lister;

    int skin_tvid;
    int skin_last_tvid;

    void *llrar_cacher; // Only used during RAR listings.
    int llrar_cache_fill; // Just fill the cache, no processing.
  int dlna_streaming;
};

typedef struct request_struct request_t;



/* Functions */

int    request_init          ( void );
void   request_free          ( void );


void   request_cmd           ( char **, char **, int ,void * );


int    request_handler       ( lion_t *, void *, int, int, char * );

void   request_action        ( request_t * );

void   request_finishdir     ( request_t * );
char  *request_url_encode    ( char * );
char  *request_url_encode_strict( char * );
char  *request_url_decode    ( char * );
void   request_reply         ( request_t *, int, char * );

request_t *request_newnode   ( void );
void       request_freenode  ( request_t * );
int    request_redirect      ( request_t * );

void   request_events        ( void );

void  request_set_status     ( char * );
char *request_get_status     ( void );
void  request_cgi_parse      ( request_t * );
void   request_update_idle   ( void );
time_t request_get_idle      ( void );

#endif
