#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lion.h"
#include "misc.h"

#include "conf.h"
#include "parser.h"
#include "ssdp.h"
#include "query.h"
#include "httpd.h"
#include "root.h"
#include "file.h"
#include "skin.h"
#include "debug.h"
#include "xmlscan.h"
#include "visited.h"
#include "cgicmd.h"


char   *conf_file            = NULL;
//int     conf_buffersize      = 1400;
int     conf_buffersize      = 16384;
char   *conf_announcename    = NULL;
int     conf_listenport      = 8001;
int     conf_xmlscan         = 0;
int     conf_fixup_syabas_subtitles = 0;
int     conf_fixup_nokeepalive = 1;
int     conf_fixup_childCount = 0;

int     conf_dvdread_merge   = 0;
int     conf_dvdread_nots    = 0;
int     conf_dvdread_rariso  = 0;

int     conf_expand_rar      = 0;
int     conf_expand_iso      = 0;

char   *conf_send_index      = NULL;

char   *conf_pin             = NULL;

del_perm_t	conf_del_file      = CONF_DEL_NO;
del_perm_t	conf_del_dir       = CONF_DEL_NO;
del_perm_t	conf_del_recursive = CONF_DEL_NO;
del_perm_t	conf_unrar         = CONF_DEL_NO;

static lion_t *conf_handle = NULL;
static int conf_read_done = 0;

static int conf_handler(lion_t *, void *, int, int, char *);



static parser_command_list_t conf_command_list[] = {
	{ COMMAND( "SSDP"     ), ssdp_cmd           },
	{ COMMAND( "ANNOUNCE" ), query_cmd_announce },
	{ COMMAND( "USN"      ), query_cmd_usn      },
	{ COMMAND( "HTTP"     ), httpd_cmd          },
	{ COMMAND( "ROOT"     ), root_cmd           },
	{ COMMAND( "SKIN"     ), skin_cmd           },
	{ COMMAND( "SKINUPNP" ), skin_upnpcmd       },
	{ COMMAND( "TYPE"     ), skin_cmd_type      },
	{ COMMAND( "RAND"     ), skin_cmd_rand      },
	{ COMMAND( "SORT"     ), skin_cmd_sort      },
	{ COMMAND( "TRANSCODE"), skin_cmd_transcode },
	{ COMMAND( "IMDB"     ), xmlscan_imdb       },
	{ COMMAND( "OPT"      ), conf_opt           },
	{ COMMAND( "VISITED"  ), visited_cmd        },
	{ COMMAND( "SCRIPT"   ), cgicmd_addscript   },
	{ COMMAND( "AUTH"     ), root_auth          },
	{ NULL,  0             , NULL               }
};



int conf_init(void)
{

	// Setup defaults if they are not set.
	if (!conf_file)
		conf_file = strdup(CONF_DEFAULT_FILE);

	if (!conf_listenport)
		conf_listenport = 8000;

    debugf("[conf] opening '%s' ...\n", conf_file);

	// Using lion to read a config file, well, well...
	conf_handle = lion_open(conf_file, O_RDONLY, 0600,
						  LION_FLAG_NONE, NULL);


	conf_read_done = 0;

	if (conf_handle)
		lion_set_handler(conf_handle, conf_handler);
	else {
		debugf("[conf] failed to read '%s'\n", conf_file);
		return -1;
	}


	while(!conf_read_done) {
		lion_poll(10, 0);
	}


	if (!conf_announcename)
		conf_announcename = strdup(CONF_DEFAULT_ANNOUNCE);

	return 0;
}




void conf_free(void)
{

	SAFE_FREE(conf_announcename);

	if (conf_handle) {
		lion_close(conf_handle);
		conf_handle = NULL;
	}

	SAFE_FREE(conf_pin);
    SAFE_FREE(conf_send_index);
}












//
// userinput for reading the config.
//

static int conf_handler(lion_t *handle, void *user_data,
						int status, int size, char *line)
{

	switch(status) {

	case LION_FILE_OPEN:
		debugf("[conf]: reading configuration file...\n");
		break;


	case LION_FILE_FAILED:
	case LION_FILE_CLOSED:
		debugf("[conf]: finished reading settings.\n");
		conf_read_done = 1;
		conf_handle = NULL;
		break;

	case LION_INPUT:
		//debugf("settings: read '%s'\n", line);
		parser_command(conf_command_list, line, NULL);

		break;

	}

	return 0;

}



//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : OPT
// Optional : DVDREAD_MERGE
//
void conf_opt(char **keys, char **values,
			  int items,void *optarg)
{
	char *merge, *nots, *rariso, *txt;
    char *expand_rar, *expand_iso, *sendindex;

	merge = parser_findkey(keys, values, items, "DVDREAD_MERGE");

	if (merge && *merge && (atoi(merge) != 0)) {
		conf_dvdread_merge = 1;
	}

	nots = parser_findkey(keys, values, items, "DVDREAD_NOTSDIR");

	if (nots && *nots && (atoi(nots) != 0)) {
		conf_dvdread_nots = 1;
	}

	rariso = parser_findkey(keys, values, items, "DVDREAD_RARISO");

	if (rariso && *rariso && (atoi(rariso) != 0)) {
		conf_dvdread_rariso = 1;
	}


	// OPT|DEL_FILE=YES       #YES, NO, PIN
	// OPT|DEL_DIR=YES        #YES, NO, PIN
	// OPT|DEL_RECURSIVE=PIN  #YES, NO, PIN
	txt = parser_findkey(keys, values, items, "DEL_FILE");
	if (txt && *txt) {
		if (
			!mystrccmp(txt, "YES") ||
			!mystrccmp(txt, "ON") ||
			!mystrccmp(txt, "TRUE") ||
			(atoi(txt) != 0))
			conf_del_file = CONF_DEL_YES;
		else if (
				 !mystrccmp(txt, "PIN") ||
				 !mystrccmp(txt, "SECURE") ||
				 !mystrccmp(txt, "SAFE"))
			conf_del_file = CONF_DEL_PIN;
	}
	txt = parser_findkey(keys, values, items, "DEL_DIR");
	if (txt && *txt) {
		if (
			!mystrccmp(txt, "YES") ||
			!mystrccmp(txt, "ON") ||
			!mystrccmp(txt, "TRUE") ||
			(atoi(txt) != 0))
			conf_del_dir = CONF_DEL_YES;
		else if (
				 !mystrccmp(txt, "PIN") ||
				 !mystrccmp(txt, "SECURE") ||
				 !mystrccmp(txt, "SAFE"))
			conf_del_dir = CONF_DEL_PIN;
	}
	txt = parser_findkey(keys, values, items, "DEL_RECURSIVE");
	if (txt && *txt) {
		if (
			!mystrccmp(txt, "YES") ||
			!mystrccmp(txt, "ON") ||
			!mystrccmp(txt, "TRUE") ||
			(atoi(txt) != 0))
			conf_del_recursive = CONF_DEL_YES;
		else if (
				 !mystrccmp(txt, "PIN") ||
				 !mystrccmp(txt, "SECURE") ||
				 !mystrccmp(txt, "SAFE"))
			conf_del_recursive = CONF_DEL_PIN;
	}

	if (conf_del_recursive) {
		debugf("[conf] WARNING: del_recursive set to YES\n");
	}


	// Unrar
	txt = parser_findkey(keys, values, items, "UNRAR");
	if (txt && *txt) {
		if (
			!mystrccmp(txt, "YES") ||
			!mystrccmp(txt, "ON") ||
			!mystrccmp(txt, "TRUE") ||
			(atoi(txt) != 0))
			conf_unrar = CONF_DEL_YES;
		else if (
				 !mystrccmp(txt, "PIN") ||
				 !mystrccmp(txt, "SECURE") ||
				 !mystrccmp(txt, "SAFE"))
			conf_unrar = CONF_DEL_PIN;
	}


	expand_rar = parser_findkey(keys, values, items, "EXPAND_RAR");

	if (expand_rar && *expand_rar && (atoi(expand_rar) != 0)) {
		conf_expand_rar = 1;
	}

	expand_iso = parser_findkey(keys, values, items, "EXPAND_ISO");

	if (expand_iso && *expand_iso && (atoi(expand_iso) != 0)) {
		conf_expand_iso = 1;
	}

	sendindex = parser_findkey(keys, values, items, "SEND_INDEX");

	if (sendindex && *sendindex) {
        SAFE_FREE(conf_send_index);
        conf_send_index = strdup(sendindex);
	}


}

