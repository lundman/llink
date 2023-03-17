#ifndef CONF_H_INCLUDED
#define CONF_H_INCLUDED


/* Defines */

#define CONF_DEFAULT_FILE "llink.conf"
#define CONF_DEFAULT_ANNOUNCE "llink-daemon"
#ifdef WIN32
#define VERSION "2.3.2"
#endif


/* Variables */

enum del_perm_enum {
	CONF_DEL_NO = 0,
	CONF_DEL_YES,
	CONF_DEL_PIN,
};

typedef enum del_perm_enum del_perm_t;


extern char *conf_file;
extern int   conf_buffersize;
extern char *conf_announcename;
extern int   conf_listenport;
extern int   conf_xmlscan;
extern int   conf_fixup_syabas_subtitles;
extern int   conf_fixup_nokeepalive;
extern int   conf_fixup_childCount;
extern int   conf_dvdread_merge;
extern int   conf_dvdread_nots;
extern int   conf_dvdread_rariso;
extern int   conf_expand_rar;
extern int   conf_expand_iso;
extern char *conf_send_index;


extern char *conf_pin;
extern del_perm_t	conf_del_file;
extern del_perm_t	conf_del_dir;
extern del_perm_t	conf_del_recursive;
extern del_perm_t	conf_unrar;

/* Functions */

int    conf_init          ( void );
void   conf_free          ( void );
void   conf_opt           ( char **, char **, int, void * );


#endif
