#ifndef CUPNP_H_DEFINED
#define CUPNP_H_DEFINED

#define UPNPAVDUMP_DMS_DEVICETYPE "urn:schemas-upnp-org:device:MediaServer:1"
#define UPNPAVDUMP_DMS_CONTENTDIR_SERVICETYPE "urn:schemas-upnp-org:service:ContentDirectory:1"
#define UPNPAVDUMP_DMS_BROWSE_ACTIONNAME "Browse"
#define  CG_UPNP_MEDIA_FILESYS_RESURL_PATH "content"


typedef enum  {
    UPNP_CONTENT_CONTAINER = 1 /*CG_UPNP_MEDIA_CONTENT_CONTAINER*/,
    UPNP_CONTENT_AUDIO,
    UPNP_CONTENT_VIDEO,
    UPNP_CONTENT_IMAGE,
    UPNP_CONTENT_SUBTITLES,
    UPNP_CONTENT_OTHER,
    UPNP_CONTENT_ALL
} upnp_content_type_t;


struct upnp_content_struct {
    upnp_content_type_t type;
    char *id;
    char *parent_id;
    char *title;       // was "name", now is title.
    char *url;
    char *mime;

    char *owner;
    char *creator;
    char *artist;
    char *album_art;
    char *album;
    char *genre;
    char *description;
    char *producer;
    char *rating;
    char *resolution;

    unsigned long duration;
    unsigned long long size;
    char *date;

    // Internal
    struct upnp_content_struct *_next;
};

typedef struct upnp_content_struct upnp_content_t;


typedef int (*upnp_virtual_callback)(char *, char *,
                                     upnp_content_t *,
                                     unsigned int *,
                                     void **arg, char *, char *);


struct upnp_root_struct {
    char *path;
    char *name;
    unsigned int hash;  // Used as a unique identifier.
    char hash_str[9];   // Hashin str form.
    upnp_virtual_callback callback;
    int (*open) (void **fh, char *path, char *id);
    int (*seek) (void **fh, unsigned long long pos);
    int           (*read) (void **fh, char *buf, unsigned int size);
    int (*close)(void **fh);
    struct upnp_root_struct *next;
};

typedef struct upnp_root_struct upnp_root_t;



char *umisc_url_encode(char *s);
char *umisc_url_decode(char *s);

upnp_root_t *cupnp_findbyhash(unsigned long hash);


int   cupnp_init         ( void );
void  cupnp_free         ( void );
void  cupnp_resume       ( request_t * );

void  cupnp_add_useragent( void *id, char *useragent, char *host );
char *cupnp_get_useragent( void *id );
char *cupnp_get_host     ( void *id );


#endif

