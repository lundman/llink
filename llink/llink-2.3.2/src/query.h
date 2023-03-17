#ifndef QUERY_H_INCLUDED
#define QUERY_H_INCLUDED


/* Defines */

#define QUERY_DEFAULT_INDEX "index.html"
#define QUERY_DEFAULT_SKIN "./skin/"



/* Variables */

struct query_struct {
	unsigned long ip;
	int port;
	char *host;
	char *cache_control;
	char *location;
	char *nt;
	char *nts;
	char *server;
	char *usn;
	char *man;
	char *mx;
	char *st;

	char *path;
	char *location_host;
	int location_port;
	lion_t *handle;
	time_t time;
	int m_search;
};

typedef struct query_struct query_t;


struct query_match_struct {
	char *match;
	char *pattern;
};

typedef struct query_match_struct query_match_t;

struct query_manual_struct {
	char *url;
	char *usn;
	int frequency;
	timers_t *timer;
	struct query_manual_struct *next;
};

typedef struct query_manual_struct query_manual_t;




/* Functions */

int         query_init            ( void );
void        query_free            ( void );
query_t    *query_newnode         ( void );
void        query_freenode        ( query_t * );
void        query_process         ( query_t * );
void        query_msearch         ( query_t *, lion_t * );

void        query_cmd_usn         ( char **, char **, int ,void * );
void        query_cmd_announce    ( char **, char **, int ,void * );

query_match_t *query_match_ip(char *ip);
#endif
