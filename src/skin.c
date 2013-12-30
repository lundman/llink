#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"
#include "dirlist.h"

#define DEBUG_FLAG 16
#include "debug.h"
#include "httpd.h"
#include "parser.h"
#include "skin.h"
#include "query.h"
#include "conf.h"
#include "file.h"
#include "version.h"
#include "root.h"
#include "process.h"
#include "extra.h"
#include "xmlscan.h"
#include "llrar.h"
#include "visited.h"
#include "mime.h"
#include "ssdp.h"

#if WIN32
#include "win32.h"
#endif

extern int do_exit;

struct skin_types_struct {
    char       *skin_root;  // disk path
    char       *useragent;  // pattern match to enable this skin, NULL for default

    skin_t     *skin_types;

    int         skin_max_namelength;
    size_type_t skin_size_type;  // HUMAN
    int         skin_page_size;  // 10
    int         skin_passes;     // 1
    unsigned long skin_rand_last;
    int         skin_rand_freq;  // 3600
    int         skin_rand_max;   // 1
    int         skin_rand_value;

    int         upnp;

    struct skin_types_struct *next;
};

typedef struct skin_types_struct skin_types_t;


static skin_types_t *skin_types_head = NULL;


struct transcode_struct {
    unsigned int id;
    char *useragent;
    char *server;
    char *ext;
    char *newext;
    char *args;
    struct transcode_struct *next;
};

typedef struct transcode_struct transcode_t;

static transcode_t *skin_transcodes = NULL;


// One day, move UPNP skin into above style.
static    lion_t     *handle              =    NULL;
static    int         skin_done_reading   =    0;
//static    int         skin_tvid           =    0;
//static    int         skin_last_tvid      =    0;

static    skin_t     *skin_newnode      ( void );
static    void        skin_freenode     ( skin_t * );
static    void        skin_load         ( skin_t * );
static    char       *request_get_zodiac( void );
static    void        skin_write        ( request_t *, skin_t *, lfile_t *);

static    int         skin_sort_num       = 0;
static    char      **skin_sort_str       = NULL;

static    char       *skin_ffmpeg_cmd     = NULL;


// This array needs to be in the same order as
// enum macro_enum {
static char *macro_strings[] = {
	"LLINK_SERVER_NAME",
	"LLINK_FILE_NAME",
	"LLINK_FILE_URL",
	"LLINK_FILE_SHORT_URL",
	"LLINK_FILE_SIZE",
	"LLINK_FILE_DATE",
	"LLINK_FILE_TVID",
	"LLINK_FILE_EXT",
	"LLINK_FILE_MIME",
    "LLINK_FILE_UPNP_TYPE",
    "LLINK_UPNP_METANAME",
    "LLINK_UPNP_UUID",
	"LLINK_PARENT_DIRECTORY",
	"LLINK_CURRENT_DIRECTORY",
	"LLINK_DETAIL_SIZE",
	"LLINK_PAGE_PREV_URL",
	"LLINK_PAGE_NEXT_URL",
	"LLINK_PARENT_DIRECTORY_URL",
	"LLINK_CURRENT_DIRECTORY_URL",
	"LLINK_ONKEYUP_PREV",
	"LLINK_ONKEYDOWN_NEXT",
	"LLINK_PAGE_FOCUS",
	"LLINK_ZODIAC",

	"LLINK_EXTNFO_ICON",
	"LLINK_EXTNFO_TITLE",
	"LLINK_EXTNFO_LOCAL_TITLE",
	"LLINK_EXTNFO_COUNTRY",
	"LLINK_EXTNFO_LENGTH",
	"LLINK_EXTNFO_TAGLINE",
	"LLINK_EXTNFO_DESCRIPTION",
	"LLINK_EXTNFO_GENRES",
	"LLINK_EXTNFO_DATE",
	"LLINK_EXTNFO_DIRECTORS",
	"LLINK_EXTNFO_CAST",
	"LLINK_EXTNFO_RATING",
	"LLINK_EXTNFO_IMDBURL",

	"LLINK_CURRENT_PLAYALL",
	"LLINK_FILE_PLAYALL",

	"LLINK_TOP_BOTTOM",
	"LLINK_FILE_PAGEID",

	"LLINK_RAND",

	"LLINK_FULL_URL",

	"LLINK_CURRENT_VIEWALL",
	"LLINK_VIEW_SONGLIST",

	"LLINK_PAGE_HELPERS",

	"LLINK_TEXT_VISITED",

	"LLINK_IF_CLIENT_IS_SYABAS",
	"/LLINK_IF_CLIENT_IS_SYABAS",
	"LLINK_IF_CLIENT_IS_NOT_SYABAS",
	"/LLINK_IF_CLIENT_IS_NOT_SYABAS",
	"LLINK_IF_CLIENT_ELSE",

	"LLINK_STATUS_MESSAGE",

	"LLINK_SORT_NEXT",
	"LLINK_SORT_PREV",

    "LLINK_DIRECTORY_TVID_START",
    "LLINK_DIRECTORY_TVID_END",

	"LLINK_DIRECTORY_PAGE_START",
	"LLINK_DIRECTORY_PAGE_END",

	"LLINK_DIRECTORY_PAGE_CURRENT",
    "LLINK_DIRECTORY_TVID_CURRENT_START",
    "LLINK_DIRECTORY_TVID_CURRENT_END",
    "LLINK_DIRECTORY_TVID_CURRENT_COUNT",
	NULL   // terminating null
};


//
// return a filename, from skin_root and with pass inside.
// this string needs to be freed when done.
//
char *skin_filename(char *root, char *name, int pass)
{
	int len = 0;
	char *result;

	if (!root || !name) return NULL;
	if (pass > 9) pass = 9; // Maximum 9 passes.

	// first pass, then just the filename
	if (pass == 1) return misc_strjoin(root, name);

	// higher passnumber
	len += strlen(root);
	len++; // pass number
	len += strlen(name);
	len++; // null byte

	result = (char *) malloc(len);
	if (!result) return NULL;

	snprintf(result, len, "%s%c%s", root, pass + 0x30, name);

	return result;
}



int skin_init(void)
{
	skin_t *runner, *next, *tmp;
	skin_t *skin_head;
	skin_t *skin_tail;
    skin_types_t *type;
	int pass;
    int seen_default = 0;

	// Load in head.html, tail.html and pre-parse it.
    for (type = skin_types_head; type; type = type->next) {

        if (type->upnp) {
            skin_t *upnp_head, *upnp_tail, *upnp_file, *upnp_dirs, *upnp_root;

            upnp_head = skin_newnode();
            upnp_tail = skin_newnode();
            upnp_file = skin_newnode();
            upnp_dirs = skin_newnode();
            upnp_root = skin_newnode();

            if (upnp_head && upnp_tail && upnp_file && upnp_dirs && upnp_root) {
                debugf("[skin] loading UPnP skin '%s' ... \n", type->skin_root);

                upnp_head->next = type->skin_types;
                type->skin_types = upnp_head;
                upnp_head->filename = skin_filename(type->skin_root,
                                                    "head.xml", 1);
                upnp_head->name = strdup("UPNPHEAD");
                upnp_head->pass = 1;

                upnp_tail->next = type->skin_types;
                type->skin_types = upnp_tail;
                upnp_tail->filename = skin_filename(type->skin_root,
                                                    "tail.xml", 1);
                upnp_tail->name = strdup("UPNPTAIL");
                upnp_tail->pass = 1;

                upnp_file->next = type->skin_types;
                type->skin_types = upnp_file;
                upnp_file->filename = skin_filename(type->skin_root,
                                                    "file.xml", 1);
                upnp_file->name = strdup("UPNPFILE");
                upnp_file->pass = 1;
                //upnp_file->ext = strdup("*");

                upnp_dirs->next = type->skin_types;
                type->skin_types = upnp_dirs;
                upnp_dirs->filename = skin_filename(type->skin_root,
                                                    "dirs.xml", 1);
                upnp_dirs->name = strdup("UPNPDIRS");
                upnp_dirs->pass = 1;

                upnp_root->next = type->skin_types;
                type->skin_types = upnp_root;
                upnp_root->filename = skin_filename(type->skin_root,
                                                    "llink.xml", 1);
                upnp_root->name = strdup("UPNPROOT");
                upnp_root->pass = 1;

            } // alloc OK




        } else {  // if NOT upnp


            if (!type->useragent)
                seen_default++;

            // if NOT upnp
            for (pass = 1; pass <= type->skin_passes; pass++) {

                skin_head = skin_newnode();
                skin_tail = skin_newnode();

                if (!skin_head || !skin_tail)
                    return -1; // resource leak

                // Link into list
                skin_head->next = type->skin_types;
                type->skin_types = skin_head;

                skin_tail->next = type->skin_types;
                type->skin_types = skin_tail;

                skin_head->filename = skin_filename(type->skin_root,
                                                    SKIN_DEFAULT_HEAD, pass);
                skin_head->name = strdup("HEAD");
                skin_head->pass = pass;

                skin_tail->filename = skin_filename(type->skin_root,
                                                    SKIN_DEFAULT_TAIL, pass);
                skin_tail->name = strdup("TAIL");
                skin_tail->pass = pass;

            } // passes

        } // UPNP or not


        for (runner = type->skin_types; runner; runner = runner->next)
            skin_load(runner);



        // Because we want TYPES to be listed "top-down" order in the conf
        // file, and linked-lists end up in reverse (insert-at-front) we
        // will re-order the list now.
        for(tmp = NULL, runner = type->skin_types; runner; runner = next) {
            next = runner->next;
            runner->next = tmp;
            tmp = runner;
        }
        type->skin_types = tmp;



    } // types



    debugf("[skin] Zodiac is '%s'\n", request_get_zodiac());

	// If no SORT styles are defined, set some default here.
	if (!skin_sort_num || !skin_sort_str) {
		skin_sort_str = (char **) malloc(sizeof(char *));
		if (skin_sort_str) {
			skin_sort_str[0] = strdup("-l");
			skin_sort_num = 1;
		}
	}

    if (seen_default == 1)
        return 0;

    if (!seen_default) {
        printf("[skin] did not define a default SKIN (without USERAGENT set).\nPlease fix your conf file\n");
        do_exit = 1;
        return 1;
    }

    if (seen_default) {
        printf("[skin] multi (%d) default SKINs have been defined (SKIN without USERAGENT set)\n",
               seen_default);
        return 0;
    }

    // not reached
	return 0;
}


void skin_set_skin(request_t *node, char *useragent)
{
    skin_types_t *type, *def_type = NULL;

    for (type = skin_types_head; type; type = type->next) {

        if (!type->useragent) {
            // Just find default?
            if (!useragent) break;
            def_type = type; // remember default in case we have no match

        } else {

            if (useragent &&
                !lfnmatch(type->useragent,
                          useragent,
                          LFNM_CASEFOLD))
                break;

        }

    } // type


    if (type)
        node->skin_type = type;
    else
        node->skin_type = def_type;

    debugf("[skin] skin type selected '%s'\n",
           node->skin_type->skin_root);
}

char *skin_get_root(skin_types_t *type)
{
    if (!type) return NULL;
    return type->skin_root;

}



skin_types_t *skin_type_new(void)
{
    skin_types_t *result;

    result = malloc(sizeof(*result));
    if (!result) return NULL;

    memset(result, 0, sizeof(*result));

    result->skin_passes    = 1;
    result->skin_size_type = SIZE_TYPE_HUMAN;
    result->skin_rand_max  = 1;
    result->skin_rand_freq = 3600;

    // Link it
    result->next = skin_types_head;
    skin_types_head = result;

    return result;
}

void skin_type_free(skin_types_t *type)
{
	skin_t *runner, *next;

    if (!type) return;

	// We need to iterate all skin nodes here, and free them
	for (runner = type->skin_types; runner; runner = next) {
		next = runner->next;
		skin_freenode(runner);
	}

    SAFE_FREE(type->skin_root);
    SAFE_FREE(type->useragent);
    SAFE_FREE(type);
}




void skin_free(void)
{
	int i;
    skin_types_t *runner, *next;

	if (handle) {
		lion_disconnect(handle);
		handle = NULL;
	}

    // Release skin_types
    for (runner = skin_types_head; runner; runner = next) {
        next = runner->next;
        skin_type_free(runner);
    }

    skin_types_head = NULL;

	for (i = 0; i < skin_sort_num; i++)
		SAFE_FREE(skin_sort_str[i]);
	SAFE_FREE(skin_sort_str);
	skin_sort_num = 0;

}






static skin_t *skin_newnode(void)
{
	skin_t *result;

	result = malloc(sizeof(*result));
	if (!result) return NULL;

	memset(result, 0, sizeof(*result));

	return result;

}


static void skin_freenode(skin_t *node)
{


	SAFE_FREE(node->ext);
	SAFE_FREE(node->ignore);
	SAFE_FREE(node->name);
	SAFE_FREE(node->html);
	SAFE_FREE(node->filename);
	SAFE_FREE(node->macros);
	SAFE_FREE(node->args);
    SAFE_FREE(node->upnp_type);
	SAFE_FREE(node);

}





//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : SKIN
// Required : path
// Optional : MaxNameLength
//
void skin_cmd(char **keys, char **values,
			  int items,void *optarg)
{
	char *path;
	char *maxname, *useragent;
	char *size_type, *page_size, *passes;
	char *freq, *max;
    skin_types_t *skin_type;

	// Required
	path      = parser_findkey(keys, values, items, "PATH");

	if (!path || !*path) {
		printf("      : [skin] missing required field: PATH\n");
		//do_exit = 1;
		return;
	}

	// Optional
	maxname   = parser_findkey(keys, values, items, "MAXNAMELENGTH");
	size_type = parser_findkey(keys, values, items, "SIZE_TYPE");
	page_size = parser_findkey(keys, values, items, "PAGE_SIZE");
	passes    = parser_findkey(keys, values, items, "PASSES");
	freq      = parser_findkey(keys, values, items, "FREQ");
	max       = parser_findkey(keys, values, items, "MAX");
	useragent = parser_findkey(keys, values, items, "USERAGENT");


    skin_type = skin_type_new();
    if (!skin_type) return;


    skin_type->skin_root = strdup(path);

	if (useragent && *useragent) {
		skin_type->useragent = strdup(useragent);
        if (!mystrccmp("UPNP", useragent))
            skin_type->upnp = 1;
    }

	if (maxname && *maxname)
		skin_type->skin_max_namelength = atoi(maxname);

	if (size_type && *size_type) {
		if (!mystrccmp("bytes", size_type))
			skin_type->skin_size_type = SIZE_TYPE_BYTES;
		else if (!mystrccmp("human", size_type))
			skin_type->skin_size_type = SIZE_TYPE_HUMAN;
		else if (!mystrccmp("none", size_type))
			skin_type->skin_size_type = SIZE_TYPE_NONE;

	} // size_type

	if (page_size && *page_size)
		skin_type->skin_page_size = atoi(page_size);

    if (freq && *freq)
        skin_type->skin_rand_freq = atoi(freq);
    if (max && *max)
        skin_type->skin_rand_max = atoi(max);

	if (passes && *passes)
		skin_type->skin_passes = atoi(passes);

	debugf("[skin] '%s' skin is to use %d pass%s\n",
           path,
           skin_type->skin_passes,
		   skin_type->skin_passes == 1 ? "" : "es");
}



//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : SKIN
// Required : path
// Optional : MaxNameLength
//
void skin_upnpcmd(char **keys, char **values,
                  int items,void *optarg)
{

    debugf("[skin] SKINUPNP command is no longer used, use SKIN with USERAGENT=UPNP\n");
}



//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : TYPE
// Required : ext(s), filename
// Optional : name, cmd, args, upnp_type
//
void skin_cmd_type(char **keys, char **values,
				   int items,void *optarg)
{
	char *ext, *filename;
	char *name, *cmd, *ignore, *args, *delay, *count, *upnp_type;
	skin_t *node;
	int pass;
    skin_types_t *type;

	// Required
	ext       = parser_findkey(keys, values, items, "EXT");
	filename  = parser_findkey(keys, values, items, "FILENAME");

	if (!ext || !*ext ||
		!filename || !*filename) {
		printf("      : [type] missing required field: EXT, FILENAME\n");
		//do_exit = 1;
		return;
	}

	// Optional
	name      = parser_findkey(keys, values, items, "NAME");
	cmd       = parser_findkey(keys, values, items, "CMD");
	ignore    = parser_findkey(keys, values, items, "IGNORE");
	args      = parser_findkey(keys, values, items, "ARGS");
	delay     = parser_findkey(keys, values, items, "DELAY");
	count     = parser_findkey(keys, values, items, "COUNT");
	upnp_type = parser_findkey(keys, values, items, "UPNP_TYPE");


    for (type = skin_types_head; type; type = type->next) {

        // upnp has static types "file" and "dirs"
        if (type->upnp) continue;

        for (pass = 1; pass <= type->skin_passes; pass++) {

            node = skin_newnode();
            if (!node) return;

            node->pass = pass;

            SAFE_COPY(node->ext, ext);
            SAFE_COPY(node->ignore, ignore);

            if (args && *args)
                node->args = request_url_decode(args);

            node->filename = skin_filename(type->skin_root, filename, pass);

            if (name && *name)
                SAFE_COPY(node->name, name);

            if (count && *count)
                node->count = atoi(count);

            if (upnp_type && *upnp_type)
                node->upnp_type = strdup(upnp_type);

            // Link it in.
            node->next = type->skin_types;
            type->skin_types = node;

            if (cmd && *cmd && !mystrccmp("bin2mpg", cmd))
                node->process_cmd = REQUEST_PROCESS_BIN2MPG;

            if (cmd && *cmd && !mystrccmp("unrar", cmd))
                node->process_cmd = REQUEST_PROCESS_UNRAR;

            if (cmd && *cmd && !mystrccmp("dvdread", cmd))
                node->process_cmd = REQUEST_PROCESS_DVDREAD;

            if (cmd && *cmd && !mystrccmp("redirect", cmd))
                node->process_cmd = REQUEST_PROCESS_REDIRECT;

            if (cmd && *cmd && !mystrccmp("external", cmd)) {
                node->process_cmd = REQUEST_PROCESS_EXTERNAL;

                if (delay && *delay)
                    node->delay = atoi(delay);
                else
                    node->delay = SKIN_DEFAULT_DELAY;

            }

        } // for passes

    } // for types

}


void skin_cmd_rand(char **keys, char **values,
				   int items,void *optarg)
{

    debugf("Command RAND is no longer used, it has been combined into SKIN\n");

}

//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : SORT
// Required : set
// Optional :
//
void skin_cmd_sort(char **keys, char **values,
				   int items,void *optarg)
{
	char *type;
	char **tmp;
	int i;

	while ((type = parser_findkey_once(keys, values, items, "SET"))) {

		// Allocate space for the string.
		tmp = (char **)realloc(skin_sort_str, sizeof(char *) * (skin_sort_num + 1));
		if (!tmp) continue;

		skin_sort_str = tmp;
		skin_sort_str[ skin_sort_num ] = strdup(type);
		skin_sort_num++;
	}

	debugf("[skin] Defined %d SORT: ", skin_sort_num);
	for (i = 0; i < skin_sort_num; i++) {
		debugf("%s, ", skin_sort_str[i]);
	}
	debugf("\n");

}



//
// Transorcery functions!
//
// CMD      : TRANSCODE
// Required :
// Optional : ffmpeg (set path to executable)
// Optional : useragent, ext, newext, args
//
void skin_cmd_transcode(char **keys, char **values,
                        int items,void *optarg)
{
	char *ffmpeg;
    char *useragent, *ext, *newext, *args, *server;
    static unsigned int id = 1;
    transcode_t *node;

	// Required

	// Optional
	ffmpeg     = parser_findkey(keys, values, items, "FFMPEG");
	useragent  = parser_findkey(keys, values, items, "USERAGENT");
	server     = parser_findkey(keys, values, items, "SERVER");
	ext        = parser_findkey(keys, values, items, "EXT");
	newext     = parser_findkey(keys, values, items, "NEWEXT");
	args       = parser_findkey(keys, values, items, "ARGS");

    // Set binary path.
    if (ffmpeg && *ffmpeg) {
        SAFE_FREE(skin_ffmpeg_cmd);
        skin_ffmpeg_cmd = strdup(ffmpeg);
    }

    // Define a new transcode option
    if (ext && newext && args) {

        node = (transcode_t *)calloc(1, sizeof(*node));

        if (!node) return;

        if (useragent)
            node->useragent = strdup(useragent);
        if (server)
            node->server = strdup(server);
        node->ext       = strdup(ext);
        node->newext    = strdup(newext);
        node->args      = strdup(args);
        node->id        = id++;

        node->next = skin_transcodes;
        skin_transcodes = node;

        debugf("[skin] transcoding '%s' -> '%s' for any '%s'/'%s' useragents/server.\n",
               ext, newext,
               useragent ? useragent : "",
               server ? server : "");
    }

}


void skin_setsort(request_t *node, int sort)
{

	sort = (sort % skin_sort_num);

	debugf("[skin] sort type set to %d '%s'\n",
		   sort, skin_sort_str[sort]);

	node->sort = sort;
}





static void skin_load(skin_t *node)
{
	struct stat stsb;

	// Do nothing for redirects, nothing to load.
	if (node->process_cmd == REQUEST_PROCESS_REDIRECT) return;

	debugf("[skin] loading '%s' ... \n", node->filename);

	// Loop to read the file
	skin_done_reading = 0;

	// Get filesize
	//if (fstat(lion_fileno(handle), &stsb)) {
	if (stat(node->filename, &stsb)) {
		debugf("[skin] I can't stat file to open?!\n");
		goto failed;
	}


	// Allocate space for the file
	node->html = (char *) malloc(stsb.st_size);

	if (!node->html) {
		debugf("[skin] failed to allocate memory for file\n");
		goto failed;
	}

	handle = lion_open(node->filename,
					   O_RDONLY,
					   0400,
					   LION_FLAG_NONE,
					   (void *) node);

	if (!handle) {
		printf("[skin] failed to open '%s', results will be interesting...\n",
			   node->filename);
		goto failed;
	}

	lion_set_handler(handle, skin_handler);

	while(!skin_done_reading) {
		// Win32 build needs a way to set io_force_loop
	    // and EOF, but at the moment it instead is notice
		// in the next loop. So time out is low here.
		lion_poll(10, 0);
	}

	handle = NULL;

	return;


 failed:
	// We release the filename so we ignore it in future.
	SAFE_FREE(node->filename)

	if (handle) {
		lion_disconnect(handle);
		handle = NULL;
	}
	return;
}




void skin_macro_add(skin_t *node, macro_type_t type, unsigned int offset)
{
	macro_t *tmp;

	tmp = (macro_t *) realloc(node->macros,
							  sizeof(macro_t) * (node->num_macros + 1));

	if (!tmp) return;

	node->macros = tmp;

	// Assign tmp to the new area, for easy access
	tmp = &node->macros[ node->num_macros ];

	tmp->type = type;
	tmp->offset = offset;

	node->num_macros++;

	//	debugf("[skin] inserting macro %d type %d at offset %u\n",
	//   node->num_macros, type, offset);

}




//
// As we read in the skin html files, we will look for the HTML comments tag
// "<!--". The find its closing tag "-->". If neither is found, we leave it
// untouched. If we find both, we match the string inbetween against our
// macro_strings to attempt a match. We then fill in the "expand" array
// for this html file with the offset this was found at, and its type.
//
void skin_macro_encode(skin_t *node, char *line, int size)
{
	int index = 0, len, i, new_index;
	char *start, *end;

	//	debugf("[skin] encoding '%s'\n", line);

	while( ( start = strstr(&line[index], "<!--"))) {

		// We have found comment start.

		end = strstr(&line[index+4], "-->");

		// If we can not find comment end, at all, just send the
		// remainder of the string out, and be done.
		if (!end) break;

		// We have start, and we have end, find a match.
		// +4 = "<!--"
		len = end - (start + 4); // Ugh, pointer arithmatic.

		// Look for match.
		for (i = 0; macro_strings[i]; i++) {

			if (!strncmp(macro_strings[i], &start[4], len)) {

				// We have a match, first just write out all up until
				// the "start" position. Work out how many bytes there
				// are between index, and start.
				new_index = (start - line) - index;

				// Anything to copy over?
				if (new_index > 0) {

					memcpy(&node->html[node->index],
						   &line[index],
						   new_index);
					node->index += new_index;
				}

				skin_macro_add(node, (macro_type_t) i,
							   node->index);

				// Skip everything until we are passed "end"
				// +3 = "-->"
				index = (end + 3) - line;

				// Break the for loop looking for matches
				break;

			} // string match

		} // for macros

		// If for loop finished with a match, continue the while
		// loop
		if (!macro_strings[i]) {
			// We had a normal comment, we could actually just strip
			// these, since they will just waste bandwidth. But if the
			// designer really wanted a comment, I guess we send it.

			new_index = ((end + 3) - line) - index;

			memcpy(&node->html[node->index],
				   &line[index],
				   new_index);
			node->index += new_index;

			index += new_index;

		} // No match, save comment

	} // while strstr / comment start hit.

	// No more comment starts found, just save whatever remains in string.
	new_index = size - index;

	memcpy(&node->html[node->index],
		   &line[index],
		   new_index);
	node->index += new_index;

}






int skin_handler(lion_t *handle, void *user_data,
					int status, int size, char *line)
{
	skin_t *node;
	//	char *ar, *token;

	node = (skin_t *) user_data;

	if (!node) return 0;


	switch(status) {

	case LION_FILE_OPEN:
		debugf("[skin] file open\n");
		break;

	case LION_FILE_FAILED:
		debugf("[skin] file failed %d:%s\n",
			   size, line ? line : "");
		handle = NULL;
		skin_done_reading = 1;
		break;

	case LION_FILE_CLOSED:
		debugf("[skin] closed. size %d num_macros %d\n", node->index,node->num_macros);
		handle = NULL;
		skin_done_reading = 1;
		return 0;


	case LION_INPUT:
		//		debugf("[skin] >> '%s'\n",line);

	  //	  strcat(line, "\r\n");

        // Stripping \r\n does not exactly save us much, and it turns out
        // other languages (xmc for example) care very much about \r\n.
		//if (debug_on && line && *line) {
		if (line && *line) {

			// in debug, lets give them \r\n back for readability
			char *linee = malloc(size + 2);
			if (linee) {
				strncpy(linee, line, size);
				linee[size] = '\n';
				linee[size + 1] = 0;
				skin_macro_encode(node, linee, size+1);
				SAFE_FREE(linee);
				break;
			} // malloc

			// Just send the line without "nl" if malloc fails

		} // debug

		skin_macro_encode(node, line, size);

		break;

	}

	return 0;
}




// "325.2 MB"
// "12.5 GB"
// Since we return a static string, you can only call this function ONE
// time per printf
//
char *misc_bytestr(lion64u_t bytes)
{
  static char work[100];

  if (bytes >= 1099511627776LL)
	  snprintf(work, sizeof(work), "%.1fTB",
			   ((float)bytes) / 1099511627776.0);
  else if (bytes >= 1073741824)
	  snprintf(work, sizeof(work), "%.1fGB",
				   ((float)bytes) / 1073741824.0);
  else if (bytes >= 1048576)
	  snprintf(work, sizeof(work), "%.1fMB",
				   ((float)bytes) / 1048576.0);
  else if (bytes >= 1024)
	  snprintf(work, sizeof(work), "%.1fKB",
				   ((float)bytes) / 1024.0);
  else
	  snprintf(work, sizeof(work), "%.1f",
				   ((float)bytes));

  return work;
}



static char *request_get_zodiac(void)
{
	// This is straight from http://en.wikipedia.org/wiki/Zodiac
	// Aries 	    March 21 – April 19[9]
	// Taurus 	    April 20 – May 20
	// Gemini 		May 21 – June 21
	// Cancer 	    June 22 – July 22
	// Leo 	        July 23 – August 22
	// Virgo  	    August 23 – September 22
	// Libra 	    September 23 – October 22
	// Scorpio      October 23 – November 21
	// Sagittarius  November 22 – December 21
	// Capricorn	December 22 – January 19
	// Aquarius 	January 20 – February 18
	// Pisces       February 19 – March 20
	static char *names[] = {"aries", "taurus", "gemini", "cancer", "leo", "virgo", "libra",
							"scorpio", "sagittarius", "capricorn", "aquarius", "pisces" };

	time_t now;
	struct tm *tnow;

	time(&now);
	tnow = localtime(&now);

	if (tnow) {
		switch (tnow->tm_mon) {
		case 2: // March
			if (tnow->tm_mday < 21) return names[11]; // Pisces
			return names[0];                          // Aries
		case 3: // April
			if (tnow->tm_mday < 20) return names[0];  // Aries
			return names[1];                          // Taurus
		case 4: // May
			if (tnow->tm_mday < 21) return names[1];  // Taurus
			return names[2];                          // Gemini
		case 5: // June
			if (tnow->tm_mday < 22) return names[2];  // Gemini
			return names[3];                          // Cancer
		case 6: // July
			if (tnow->tm_mday < 23) return names[3];  // Cancer
			return names[4];                          // Leo
		case 7: // August
			if (tnow->tm_mday < 23) return names[4];  // Leo
			return names[5];                          // Virgo
		case 8: // September
			if (tnow->tm_mday < 23) return names[5];  // Virgo
			return names[6];                          // Libra
		case 9: // October
			if (tnow->tm_mday < 23) return names[6];  // Libra
			return names[7];                          // Scorpio
		case 10:// November
			if (tnow->tm_mday < 22) return names[7];  // Scoprio
			return names[8];                          // Sagittarius
		case 11: // December
			if (tnow->tm_mday < 22) return names[8];  // Sagittarius
			return names[9];                          // Capricorn
		case 0:  // January
			if (tnow->tm_mday < 20) return names[9];  // Capricorn
			return names[10];                         // Aquarius
		case 1:  // February
			if (tnow->tm_mday < 19) return names[10]; // Aquarius
			return names[11];                         // Pisces
		default:
				break;

		} // switch

	} // if tnow

	// Just return something valid;
	return names[0];
}



void skin_write_macro(request_t *node, macro_t *macro, lfile_t *file, skin_t *skin)
{
	char *str;
	int i;

	switch(macro->type) {

	case LLINK_SERVER_NAME:
		lion_output(node->tmphandle,
					conf_announcename,
					strlen(conf_announcename));
		break;

	case LLINK_FILE_NAME:
		if (file) {
			str = hide_path(file->name);
			if (node->skin_type->skin_max_namelength &&
				(strlen(str) > node->skin_type->skin_max_namelength)) {
				lion_printf(node->tmphandle,
							"%.*s~",
							node->skin_type->skin_max_namelength,
							str);
                break;
            }

            // We could be fancy here, and show the containers?
            if (conf_expand_rar && strstr(file->name, "?f=")) {
                if (conf_expand_iso && strstr(file->name, "&fv=")) {
                    lion_printf(node->tmphandle,
                                "%s (isorar)", str);
                    break;
                }


                // transcode directory?
                if (skin_transcodes && strstr(file->name, "transcode=")) {
                    lion_printf(node->tmphandle,
                                "%s (rar, transcode)", str);
                    break;
                }

                // just rared
                lion_printf(node->tmphandle,
                            "%s (rar)", str);
                break;
            }
            // Just ISO?
            if (conf_expand_iso && strstr(file->name, "?fv=")) {
                lion_printf(node->tmphandle,
                            "%s (iso)", str);
                break;
            }
            // rar directory?
            if (conf_expand_iso && strstr(file->name, "?d=")) {
                lion_printf(node->tmphandle,
                            "%s (rar)", str);
                break;
            }

            // transcode file?
            if (skin_transcodes && strstr(file->name, "transcode=")) {
                lion_printf(node->tmphandle,
                            "%s (transcode)", str);
                break;
            }

            // Just send the filename
            lion_output(node->tmphandle,
                        str,
                        strlen(str));

		} else {
            debugf("[skin] FILE_NAME: no name, sending dir\n");

            if (!strcmp("/", node->path) && !node->rar_directory) {
                lion_printf(node->tmphandle,
                            "0");
            } else {
                char *path, *name;

                path = misc_strjoin(node->path,
                                    node->rar_directory ? node->rar_directory : "");
                misc_stripslash(path);
                debugf("[skin] work path '%s'\n", path);
                name = strrchr(path, '/');
                if (!name) name = path;
                else name++; // skip slash

                misc_stripslash(name);

                debugf("[skin] meta sending '%s'\n", name);

                lion_printf(node->tmphandle,
                            "%s",
                            name);

                SAFE_FREE(path);
            }

        }


		break;

	case LLINK_FILE_URL:
		// If it already starts with http:// we send it as is. See ROOT|HTTP
		if (file) {

			if (!strncasecmp("/http://", file->name, 8)) {
				lion_output(node->tmphandle,
							&file->name[1],
							strlen(file->name)-1);
				break;
			}

			str = request_url_encode(file->name);

			// Directory, and files that act like directories (rar, dvdread)
			// also send history. Files do not, so that the media device
			// can use filename.ext to guess media type.
			if ((file->directory == YNA_YES) ||
				(node->process_function == REQUEST_PROCESS_UNRAR) ||
				(node->process_function == REQUEST_PROCESS_DVDREAD)) {

				if (node->history)
					lion_printf(node->tmphandle, "%s&h=%s,%d&s=%d",
								str,
								node->history,
								file->tvid,
								node->sort);
				else
					lion_printf(node->tmphandle, "%s&h=%d&s=%d",
								str,
								file->tvid,
								node->sort);
			} else {
				lion_printf(node->tmphandle, "%s",
							str);
			}
			SAFE_FREE(str);
		}
		break;

	case LLINK_FILE_SHORT_URL:
		// If it already starts with http:// we send it as is. See ROOT|HTTP
		if (file) {

			if (!strncasecmp("/http://", file->name, 8)) {
				lion_output(node->tmphandle,
							&file->name[1],
							strlen(file->name)-1);
				break;
			}

			str = request_url_encode(file->name);

            debugf("[skin] shorturl using '%s'\n", str);
			lion_printf(node->tmphandle, "%s",str);

			SAFE_FREE(str);

		}
		break;

	case LLINK_FILE_SIZE:
		if (file) {

			switch(node->skin_type->skin_size_type) {

			case SIZE_TYPE_NONE:
				break;

			case SIZE_TYPE_BYTES:
				lion_printf(node->tmphandle,
							"%"PRIu64
							,file->size);
				break;

			case SIZE_TYPE_HUMAN:
			default:
				lion_printf(node->tmphandle,
							"%s",
							misc_bytestr(file->size));
				break;
			} // switch size_type

		} // if file

		break;


	case LLINK_DETAIL_SIZE:
		if (file)
			lion_printf(node->tmphandle,
						"%"PRIu64
						,file->size);
		break;


	case LLINK_FILE_DATE:
		// ctime guarantees a string of 26 bytes, so we can eat the \n that
		// way. In html, "\n" should not matter though.
		if (file)
			lion_printf(node->tmphandle,
						"%24.24s",  // Disregard the \n if provided.
						ctime(&file->date));

		break;

	case LLINK_FILE_TVID:
		if (file)
			lion_printf(node->tmphandle,
						"%u",
						file->tvid);
		break;

	case LLINK_FILE_EXT:
		if (file && (str = strrchr(file->name, '.')))
			lion_printf(node->tmphandle,
						"%s", &str[1]);
		break;

	case LLINK_FILE_MIME:
		if (file)
			lion_printf(node->tmphandle,
						"%s", mime_type(file->name));
		break;

	case LLINK_FILE_UPNP_TYPE:
		if (skin && skin->upnp_type)
			lion_printf(node->tmphandle,
                        "%s", skin->upnp_type);
		break;

	case LLINK_PARENT_DIRECTORY:
		// if rar_directory is set, reduce it first, if that becomes
		// "/", then reduce normal path by one.
		if (node->rar_directory &&
			(strlen(node->rar_directory) > 1)) {

			// Reduce rar path by one.
			str = misc_strjoin(node->rar_directory, "../");
			root_undot(str);
			// rar fails with trailing slash
			misc_stripslash(str);

			lion_printf(node->tmphandle,
						"%s%s",
						node->path,
						str);
			SAFE_FREE(str);
			break;
		}

        debugf("******** parent url of '%s' file '%s'\n", node->path,
               file && file->name ? file->name : "");

        if (node->rar_file) {
            debugf("[skin] rar_file '%s' set, reducing away .rar\n",
                   node->rar_file);
            str = misc_strjoin(node->path, "../");

            root_undot(str);
            misc_stripslash(str);
            SAFE_FREE(node->path);
            node->path = str;
            str = NULL;
        }

        // IN upnp, parent of "/" is -1
        if ((node->upnp_type == UPNP_BROWSEMETADATA) &&
            !strcmp("/", node->path)) {
            debugf("[skin] ******* BrowseMetadata for / so sending -1\n");
            lion_printf(node->tmphandle,
                        "-1");
            break;
        }

        if (file && file->name)
            str = strdup(node->path);
        else
            str = misc_strjoin(node->path, "../");

		root_undot(str);
		misc_stripslash(str);

        debugf("******** reduced url of '%s'\n", str);

        // IN upnp, "/" is 0
        if ((node->upnp_type != UPNP_NONE) &&
            !strcmp("/", str)) {
            SAFE_FREE(str);
            str = strdup("0");
        }


        debugf("******** sending url of '%s'\n", str);

		lion_output(node->tmphandle,
					str,
					strlen(str));
		SAFE_FREE(str);
		break;


	case LLINK_CURRENT_DIRECTORY:
        lion_printf(node->tmphandle,
                    "%s%s",
                    node->path,
                    node->rar_directory ? node->rar_directory : "" );
        break;

	case LLINK_UPNP_METANAME:
        // UPNP
        debugf("[skin] metaname '%s'\n", node->path);

        if (!strcmp("/", node->path) && !node->rar_directory) {
            lion_printf(node->tmphandle,
                        "0");
        } else {
            char *path, *name;

            path = misc_strjoin(node->path,
                                node->rar_directory ? node->rar_directory : "");
            misc_stripslash(path);
            debugf("[skin] work path '%s'\n", path);
            name = strrchr(path, '/');
            if (!name) name = path;
            else name++; // skip slash

            misc_stripslash(name);

            debugf("[skin] meta sending '%s'\n", name);

            lion_printf(node->tmphandle,
                        "%s",
                        name);

            SAFE_FREE(path);
        }

		break;

	case LLINK_UPNP_UUID:
        lion_printf(node->tmphandle,
                    "%s",
                    ssdp_upnp_uuid);
        break;


	case LLINK_PARENT_DIRECTORY_URL:
        debugf("[skin] Computing PARENT URL from '%s'\n     rar_dir '%s'\n",
               node->path, node->rar_directory ? node->rar_directory : "");

		{
			char *r = NULL, save_char = 0;
			int page = 0, focus = 1;
            char *new_rar = NULL;

			str = NULL;

			// if rar_directory is set, reduce it first, if that becomes
			// "/", then reduce normal path by one.
			if (node->rar_directory &&
				(strlen(node->rar_directory) > 1)) {

				// Reduce rar path by one.
				new_rar = misc_strjoin(node->rar_directory, "../");
				root_undot(new_rar);
				// rar fails with trailing slash
				misc_stripslash(new_rar);

                debugf("    reduced rar_dir to '%s'\n", new_rar);
			}

			// If we need to reduce the path, do so now
			if (!new_rar) {

                // FIXME, if path is "/dir/file.iso" we need to go back two.
				str = misc_strjoin(node->path, "../");
				root_undot(str);
				misc_stripslash(str);

			} else if (!strcmp(new_rar, "/")) {
                // If we reduced new_rar down to just "/", then it's better not to
                // send path - for implied root
                SAFE_FREE(new_rar);
            }

			// If we want history, pop one off the stack
			if (node->history) {

				// If we have a "," pop the last number off
				if ((r = strrchr(node->history, ','))) {

					*r = 0;      // Truncate string here, to not pass it up

					// Read the wanted number off, and convert to from&focus pair
					page = atoi(&r[1]);  // fex: 75

				} else {
					// history is set, but no ",", probably at top. No need to
					// send history, just convert to from&focus
					r = node->history;
					page = atoi(node->history);

				}

				// "r" points to place to terminate string. and "page" has been
				// set to the value popped-off the history stack.
				// truncate the old history string now.
				save_char = *r;
				*r = 0;

				// page = 75;
                if (node->skin_type->skin_page_size) {
                    focus = page % node->skin_type->skin_page_size; // 5
                    page -= focus; // 70
                }
				// % = 0 means really last item.
				// if last item, page is one before.
				if (!focus) {
					focus = node->skin_type->skin_page_size;
					if (page)
						page-= node->skin_type->skin_page_size;
				}

			} // history


			// "/shorter/path&h=75,12,45&from=20&focus=4",
			//<a href="/&from=0&focus=1/&h=(null)" tvid="red" >parent</a>
            // We may have a new_dvd, or a new_rar, or a new str (node->path)
            if (new_rar) {

                debugf("[skin] reduced path '%s', history '%s' from=%d focus=%d\n",
                       new_rar , node->history ? node->history : "", page, focus);

                lion_printf(node->tmphandle,
                            //  "%s&h=%s&from=%d&focus=%d",
                            "%s?d=%s%s%s&from=%d&focus=%d&s=%d",
                            node->path,
                            new_rar,
                            node->history && *node->history ? "&h=" : "",
                            node->history && *node->history ? node->history : "",
                            page,
                            focus,
                            node->sort);
            } else {

                debugf("[skin] reduced path '%s', history '%s' from=%d focus=%d\n",
                       str ? str : node->path , node->history ? node->history : "", page, focus);

                lion_printf(node->tmphandle,
                            //  "%s&h=%s&from=%d&focus=%d",
                            "%s%s%s&from=%d&focus=%d&s=%d",
                            str ? str : node->path,
                            node->history && *node->history ? "&h=" : "",
                            node->history && *node->history ? node->history : "",
                            page,
                            focus,
                            node->sort);
            }

			debugf("[skin] parent url=%s%s%s&from=%d&focus=%d&s=%d\n",
				   str ? str : node->path,
				   node->history && *node->history ? "&h=" : "",
				   node->history && *node->history ? node->history : "",
				   page,
				   focus,
				   node->sort);


			if (r)
				*r = save_char; // restore string (macro can be used many times)

			// Free the reduced string.
			SAFE_FREE(str);

		} // case variable begin

		break;


	case LLINK_CURRENT_DIRECTORY_URL:

		// print URL path, add in &from=, &d=, &dv=, &h=
		lion_printf(node->tmphandle,
					"%s&from=%d&s=%d"
					"%s%s"         // &h=%s
					"%s%s",        // &d=%s
					node->path,
					node->page_from,
					node->sort,
					node->history && *node->history ? "&h="         : "",
					node->history && *node->history ? node->history : "",
					node->rar_directory    ?"&d="                   : "",
					node->rar_directory    ?node->rar_directory     : ""
					);

		break;


	case LLINK_PAGE_PREV_URL:
		if (node->skin_type->skin_page_size) {
			// when this macro is used in tail.html,
			// node->page_current is set to the number
			// of entries. We take advantage of this by
			// setting the previous page of the first
			// page to the last possible page.

			int from = (node->page_from - node->skin_type->skin_page_size);
			int focus = (from % node->skin_type->skin_page_size) + node->skin_type->skin_page_size;

			if (node->page_from > 0 &&
			    node->page_from < node->skin_type->skin_page_size) {
				from = 0;
				focus = 1;
			}

			if (from < 0) {
				if (node->page_current > 0) {
					from = ((node->page_current - 1)
						/ node->skin_type->skin_page_size)
						* node->skin_type->skin_page_size;
					focus = ((node->page_current - 1)
						% node->skin_type->skin_page_size) + 1;
				} else {
					from = 0;
					focus = 1;
				}
			}

			if ((from + focus) > (node->page_current))
				focus = node->page_current - node->page_from;

			debugf("[skin] PREV_URL page_from %d focus %d\n", from, focus);

 			//lion_printf(node->tmphandle,
			//"%s&from=%d&focus=%d&h=%s",
			//node->path,
			//from,
			//focus,
			//node->history ? node->history : "");

 			lion_printf(node->tmphandle,
 						"%s&from=%d&focus=%d&h=%s&s=%d",
 						node->path,
						from,
						focus,
 						node->history ? node->history : "",
						node->sort);
		}
		break;

	case LLINK_PAGE_NEXT_URL:
		if (node->skin_type->skin_page_size) {
			int next = node->page_from + node->skin_type->skin_page_size;
			// See above, when used in tail.html we know
			// the # of entries so when we go past it
			// wrap to the first page.
			if (next >= node->page_current)
				next = 0;
			debugf("next url is %d based on %d\n", next, node->page_current);
 			lion_printf(node->tmphandle,
 						"%s&from=%d&h=%s&s=%d",
 						node->path,
						next,
 						node->history ? node->history : "",
						node->sort);
		}

		break;


	case LLINK_ONKEYUP_PREV:
		if (node->skin_type->skin_page_size &&
			(file->tvid == (node->page_from + 1)))
			lion_printf(node->tmphandle,
						"onkeyupset=\"prev\"");
		break;

	case LLINK_ONKEYDOWN_NEXT:
		// If we are the last line of a page
		// OR, we are the last line entirely
		if (node->skin_type->skin_page_size &&
			((file->tvid == node->page_to) ||
			 (file->tvid == node->page_current)))
			lion_printf(node->tmphandle,
						"onkeydownset=\"next\"");
		break;

	case LLINK_PAGE_FOCUS:
		// Send "1" to focus on top, unless &focus= was sent.
		debugf("this page's focus set to %d\n",
					node->page_focus && (node->page_focus <= node->skin_type->skin_page_size) ?
					node->page_focus + node->page_from : node->page_from );

		lion_printf(node->tmphandle,
					"%d",
					node->page_focus && (node->page_focus <= node->skin_type->skin_page_size) ?
					node->page_focus + node->page_from : node->page_from );
		break;


	case LLINK_ZODIAC:
		lion_printf(node->tmphandle,
					"%s",
					request_get_zodiac());
		break;

	case LLINK_EXTNFO_ICON:
		if (node->extnfo.icon_path)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.icon_path);
		break;
	case LLINK_EXTNFO_TITLE:
		if (node->extnfo.title)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.title);
		break;
	case LLINK_EXTNFO_LOCAL_TITLE:
		if (node->extnfo.title_local)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.title_local);
		break;
	case LLINK_EXTNFO_COUNTRY:
		if (node->extnfo.country)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.country);
		break;
	case LLINK_EXTNFO_LENGTH:
		if (node->extnfo.length)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.length);
		break;
	case LLINK_EXTNFO_TAGLINE:
		if (node->extnfo.tagline)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.tagline);
		break;
	case LLINK_EXTNFO_DESCRIPTION:
		if (node->extnfo.description)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.description);
		break;
	case LLINK_EXTNFO_GENRES:
		for (i = 0; (i < EXTRA_NUM_GENRE) && node->extnfo.genre[i]; i++) {
			lion_printf(node->tmphandle,
						"%s ",
						node->extnfo.genre[i]);
		}
		break;
	case LLINK_EXTNFO_DATE:
		if (node->extnfo.date)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.date);
		break;
	case LLINK_EXTNFO_DIRECTORS:
		for (i = 0; (i < EXTRA_NUM_DIRECTOR) && node->extnfo.director[i]; i++) {
			lion_printf(node->tmphandle,
						"%s, ",
						node->extnfo.director[i]);
		}
		break;
	case LLINK_EXTNFO_CAST:
		for (i = 0; (i < EXTRA_NUM_CAST) && node->extnfo.cast[i]; i++) {
			lion_printf(node->tmphandle,
						"%s, ",
						node->extnfo.cast[i]);
		}
		break;
	case LLINK_EXTNFO_RATING:
		if (node->extnfo.rating)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.rating);
		break;
	case LLINK_EXTNFO_IMDBURL:
		if (node->extnfo.imdburl)
			lion_printf(node->tmphandle,
						"%s",
						node->extnfo.imdburl);
		break;


	case LLINK_CURRENT_PLAYALL:
		lion_printf(node->tmphandle,
					"%s&playall=1",
					node->path);
		break;


	case LLINK_FILE_PLAYALL:
		if (file) {

			if (!strncasecmp("/http://", file->name, 8)) {
				lion_printf(node->tmphandle,
							"%s&playall=1", &file->name[1]);
				break;
			}

			str = request_url_encode(file->name);
			lion_printf(node->tmphandle,
						"%s&playall=1", str);
			SAFE_FREE(str);
		}
		break;


	case LLINK_TOP_BOTTOM:
		if (node->skin_type->skin_page_size &&
			(node->page_current > (node->page_from + (node->skin_type->skin_page_size / 2))))
			lion_printf(node->tmphandle,
						"bottom");
		else
			lion_printf(node->tmphandle,
						"top");
		break;

	case LLINK_FILE_PAGEID:
		if (file)
			lion_printf(node->tmphandle,
						"%u",
						(file->tvid%node->skin_type->skin_page_size)?(file->tvid%node->skin_type->skin_page_size):node->skin_type->skin_page_size);
		break;

		// Conf sets how often, and how many values.
	case LLINK_RAND:
		if (!node->skin_type->skin_rand_last ||
			lion_global_time - node->skin_type->skin_rand_last >= node->skin_type->skin_rand_freq) {
			node->skin_type->skin_rand_last = lion_global_time;
			node->skin_type->skin_rand_value = (rand() % node->skin_type->skin_rand_max) + 1;
		}
		lion_printf(node->tmphandle,
					"%u", node->skin_type->skin_rand_value);
		break;

	case LLINK_FULL_URL:
		{
			int myport;
			unsigned long myip;
			lion_getsockname(node->handle, &myip, &myport);
			lion_printf(node->tmphandle, "http://%s:%d/",
						lion_ntoa(myip),
						httpd_port);
		}
		break;

	case LLINK_CURRENT_VIEWALL:
		lion_printf(node->tmphandle,
					"%s&viewall=1",
					node->path);
		break;

	case LLINK_VIEW_SONGLIST:
		// Lookup "Photos", see if it has an arg. Send MUTE if not.
		{
			skin_t *skin;
			int myport;
			unsigned long myip;
			lion_getsockname(node->handle, &myip, &myport);
			skin = skin_findbyname(node->skin_type, "Photo", 1);
			if (skin && skin->args)
				lion_printf(node->tmphandle, "http://%s:%d/%s&playall=1",
							lion_ntoa(myip),
							httpd_port,
							skin->args);
			else
				lion_printf(node->tmphandle,
							"MUTE");

			break;
		}

	case LLINK_PAGE_HELPERS:
		// <a href="/&from=0" tvid="1"></a>
		// If we are at from=20 and total 42 files, we will send tvid=1, 10,
		// 30, 40
		for (i = 0; i < node->page_from; i += node->skin_type->skin_page_size) {
			lion_printf(node->tmphandle,
						"<a href =\"%s%s%s&from=%d&s=%d\" tvid=\"%d\"></a>\n",
						node->path,
						node->history&&*node->history?"&h=":"",
						node->history&&*node->history?node->history:"",
						i,
						node->sort,
						i ? i : 1); // tvid=0 dont work, first is "1".
		}
		// from+size -> current
		for (i = node->page_from + node->skin_type->skin_page_size + node->skin_type->skin_page_size;
			 i <= node->page_current;
			 i += node->skin_type->skin_page_size) {
			lion_printf(node->tmphandle,
						"<a href =\"%s%s%s&from=%d&s=%d\" tvid=\"%d\"></a>\n",
						node->path,
						node->history&&*node->history?"&h=":"",
						node->history&&*node->history?node->history:"",
						i,
						node->sort,
						i);
		}
		break;


	case LLINK_TEXT_VISITED:
		lion_printf(node->tmphandle, "%s",
					visited_file(node, file) ? "visited" : "text");
		break;

	case LLINK_STATUS_MESSAGE:
		lion_printf(node->tmphandle, "%s",
					request_get_status());
		break;

	case LLINK_SORT_NEXT:
		lion_printf(node->tmphandle,
					"%d",
					(node->sort + 1) >= skin_sort_num ? 0 : node->sort + 1);
		break;

	case LLINK_SORT_PREV:
		lion_printf(node->tmphandle,
					"%d",
					node->sort ? node->sort - 1 : skin_sort_num-1);
		break;

	case LLINK_DIRECTORY_TVID_START:
	case LLINK_DIRECTORY_PAGE_START:
		lion_printf(node->tmphandle,
					"1");
		break;
	case LLINK_DIRECTORY_TVID_END:
		lion_printf(node->tmphandle,
					"%d",
                    // BrowseMetaData always return 1 item.
                    node->upnp_type == UPNP_BROWSEMETADATA ?
                    1 :
					node->skin_last_tvid);
		break;
	case LLINK_DIRECTORY_PAGE_END:
		if (node->skin_type->skin_page_size)
			lion_printf(node->tmphandle,
						"%d",
						node->skin_last_tvid / node->skin_type->skin_page_size + 1);
		break;

	case LLINK_DIRECTORY_PAGE_CURRENT:
		lion_printf(node->tmphandle,
					"%d",
					(node->page_from / node->skin_type->skin_page_size) + 1);
		break;
    case LLINK_DIRECTORY_TVID_CURRENT_START:
		lion_printf(node->tmphandle,
					"%d",
					node->page_from + 1);
        break;
    case LLINK_DIRECTORY_TVID_CURRENT_END:
		lion_printf(node->tmphandle,
					"%d",
					(node->page_from + node->skin_type->skin_page_size) <= node->page_current ?
                    node->page_from + node->skin_type->skin_page_size : node->page_current);
        break;
    case LLINK_DIRECTORY_TVID_CURRENT_COUNT:
        // If page_from is 0, then it is just page_current.
        // If "from" is set, then its current - from.
        debugf("************ current %d from %d to %d\n",
               node->page_current, node->page_from, node->page_to);
		lion_printf(node->tmphandle,
					"%d",
                    node->page_to ? node->page_to - node->page_from :
                    node->page_current - node->page_from);

        break;


	default:
		lion_output(node->tmphandle,
					"BAD MACRO",
					9);
	}

}




//
// Caching code, for multiple-passes
//



static cache_t *skin_cache_newnode(void)
{
	cache_t *result;

	result = (cache_t *) malloc(sizeof(*result));

	if (!result) return NULL;

	memset(result, 0, sizeof(*result));

	return result;
}


static void skin_cache_add(request_t *node, skin_t *skin, lfile_t *file)
{
	cache_t *cache, *runner;
	// Sorted linked list, so always add to end. We could have an end pointer
	// to speed things up, but I predict this cache will generally be small.
	// (ie, PAGE_SIZE)

	// We only cache file entries
	if (!file) return;

	cache = skin_cache_newnode();
	if (!cache) return;

	debugf("[skin] cache adding '%s' (tvid: %d)\n",
		   file->name, node->skin_tvid);

	node->skin_tvid++;

	file_dupe(&cache->file, file);
	extra_dupe(&cache->extnfo, &node->extnfo);
    //debugf("[skin] would copy over process_function %d\n",
    //      node->process_function);
    //if (node->process_function != REQUEST_PROCESS_DVDREAD)
    //       cache->process_function = node->process_function;

    debugf("[skin] process_function: node %d, cache %d, skin %d\n",
           node->process_function,
           cache->process_function,
           skin->process_cmd);

	cache->skin = skin;

    cache->process_function = skin->process_cmd;

    debugf("[skin] did : cache->process_function %d\n",
           cache->process_function);


	if (!node->cache) {
		node->cache = (void *)cache;
		return;
	}

	for (runner = (cache_t *)node->cache; runner; runner=runner->next) {
		if (!runner->next) {
			runner->next = cache;
			return;
        }
	}

}


static void skin_cache_freenode(cache_t *cache)
{

    file_free(&cache->file);
    extra_release(&cache->extnfo);

    cache->skin = NULL; // Safety
    SAFE_FREE(cache);

}


void skin_cache_clear(request_t *node)
{
	cache_t *cache, *next;


    debugf("[cache] clearing cache.\n");

	for (cache = (cache_t *)node->cache; cache; cache=next) {
		next = cache->next;
        skin_cache_freenode(cache);
	}


	node->skin_tvid = 0;
	node->cache = NULL;

}


//
// Look for entries we want to expand. If found, call dirlist on them
// and remove from the cache. Return non-zero if that is the case
// Return 0 when finished.
//
int skin_cache_expand(request_t *node)
{
    cache_t *runner, *prev;
    char *ar;


    if (conf_expand_rar) { // Look and expand RAR?
        char *args;

        for (runner = (cache_t *)node->cache,
                 prev=NULL;
             runner;
             prev=runner,
                 runner=runner->next) {

            if (runner->process_function == REQUEST_PROCESS_UNRAR) {

                // Temporarily, we can't expand rariso
                if (!strcmp(runner->file.user, "unrar")) continue;

                // Expand this. First remove it from cache.
                if (!prev)
                    node->cache = (void *)runner->next;
                else
                    prev->next = runner->next;


                // Issue listing.
                debugf("[skin] we should expand '%s' here\n", runner->file.name);

                // Setup to call list again, loading the rar variables. Note
                // this means we can not handle rar inside rar.

				// RARs in root are a bit of a special case.
				if (!mystrccmp("/", node->disk_path)) {
					node->disk_path =
						misc_strjoin(root_getroot(node->root_index),
									 hide_path(runner->file.name));
					node->path = misc_strjoin(node->path,
											  hide_path(runner->file.name));
				} else {
					node->disk_path = misc_strjoin(node->disk_path,
												   hide_path(runner->file.name));
					node->path = misc_strjoin(node->path,
											  hide_path(runner->file.name));
				} // root ?


                debugf("[skin] expand calling llrar_list on '%s' for '%s':%s\n",
                       node->disk_path, node->path, runner->file.name);


                // remember with unrar/undvd process
                args = runner->skin->args;

                // Remove cache node before listing.
                skin_cache_freenode(runner);
                node->skin_tvid--;

                llrar_list(node, args);

                return 1;
            } // rar

        } // for

    } // expand rar

    if (conf_expand_iso) { // Look and expand DVDREAD?
        char *new_path;

        for (runner = (cache_t *)node->cache,
                 prev=NULL;
             runner;
             prev=runner,
                 runner=runner->next) {

			debugf("[skin] expand, looking for ISO: '%s'->%d\n",
				   runner->file.name,
				   runner->process_function);

            if ((runner->process_function == REQUEST_PROCESS_RARISO)) {

                // Expand this. First remove it from cache.
                if (!prev)
                    node->cache = (void *)runner->next;
                else
                    prev->next = runner->next;

                // Issue listing.
                debugf("[skin] we should expand '%s' here: %d\n",
                       runner->file.name,
                       runner->process_function);

                //[skin] we should expand '/tmp/The.Sword.In.The.Stone.1963.45th.Anniversary.Edition.NTSC.DVDR-MADE/made-tsits.rar?f=4640139264,/made-tsits.iso' here: 5
                //[skin] expand calling advd_list on '/Users/lundman/ftp/data//tmp/The.Sword.In.The.Stone.1963.45th.Anniversary.Edition.NTSC.DVDR-MADE/made-tsits.iso' for /made-tsits.iso


                // Is this a plain .ISO file, or one inside a rar file?
                // We really should record the type of the "parent cache" node
                // somewhere.
                if (( ar = strstr(runner->file.name, "?f=")) ||
                    ( ar = strstr(runner->file.name, "&f="))) {
                    char *args;

                    // Get the ISO name.
                    new_path = misc_strjoin("", hide_path(runner->file.name));

                    // Get the RAR name. Terminate after .rar
                    *ar = 0;

                    node->disk_path = misc_strjoin(node->disk_path,
                                                   hide_path(runner->file.name));
                    node->path = misc_strjoin(node->path,
                                              hide_path(runner->file.name));


                    // Remove cache node before listing.
                    args = runner->skin->args;

                    skin_cache_freenode(runner);
                    node->skin_tvid--;

                    debugf("[skin] expand calling llrar_list on '%s' inside %s\n",
                           new_path, node->disk_path);

                    // List it
                    node->dvdread_file = new_path;
                    new_path = NULL;
                    node->process_function = REQUEST_PROCESS_NONE;

                    llrar_list(node, args);
                    //llrar_list(new_path, node->disk_path, node);

                    //SAFE_FREE(new_path);


                    return 1;

                }

            } // ISO & RARISO

        } // for

    } // expand rar

    if (skin_transcodes && node->useragent) { // Look and expand TRANSCODES
        transcode_t *transcode;

        debugf("[skin] checking for matching transcoders to user-agent '%s'\n",
               node->useragent);

        for (transcode = skin_transcodes;
             transcode;
             transcode = transcode->next) {

            if (!lfnmatch(transcode->useragent,
                          node->useragent,
                          LFNM_CASEFOLD)) {

                debugf("[skin] matches '%s' (ext '%s')\n",
                       transcode->useragent, transcode->ext);

                for (runner = (cache_t *)node->cache,
                         prev=NULL;
                     runner;
                     prev=runner,
                         runner=runner->next) {

                    debugf("[skin] expand, looking for TRANSCODES: '%s'->%d\n",
                           runner->file.name,
                           runner->process_function);

                    if (!lfnmatch(transcode->ext,
                                  runner->file.name,
                                  LFNM_CASEFOLD)) {
                        char buffer[1024];
                        // "/path/movie.m4v" ->
                        // "/path/movie.m4v&transcode=1,f=movie.m4v.mpg"
                        snprintf(buffer, sizeof(buffer),
                                 "%s%ctranscode=%u,/%s%s",
                                 runner->file.name,
                                 strstr(runner->file.name, "?f=") ?
                                 '&' : '?',
                                 transcode->id,
                                 hide_path(runner->file.name),
                                 transcode->newext);

                        SAFE_FREE(runner->file.name);
                        runner->file.name = strdup(buffer);

                        debugf("[skin] will transcode, id %d: '%s'\n",
                               transcode->id, buffer);
                    } // fnmatch filename

                } // all runners

            } // useragent match

        } // for all transcodes

    } // if set transcodes

    return 0;
}





//
// These functions are pretty much copied verbatim out of libdirlist_sort
//

static int sortby_name(const void *data1, const void *data2)
{
	return (strcmp(
				   hide_path((*(cache_t **)data1)->file.name),
                   hide_path((*(cache_t **)data2)->file.name))
			);
}

static int sortby_name_reverse(const void *data1, const void *data2)
{
	return -1*(strcmp(
					  hide_path((*(cache_t **)data1)->file.name),
                      hide_path((*(cache_t **)data2)->file.name))
			   );
}

static int sortby_name_case(const void *data1, const void *data2)
{
	return (strcasecmp(
					   hide_path((*(cache_t **)data1)->file.name),
					   hide_path((*(cache_t **)data2)->file.name))
			);
}

static int sortby_name_case_reverse(const void *data1, const void *data2)
{
	return -1*(strcasecmp(
						  hide_path((*(cache_t **)data1)->file.name),
                          hide_path((*(cache_t **)data2)->file.name))
			   );
}

static int sortby_size(const void *data1, const void *data2)
{
	if ( (*(cache_t **)data1)->file.size >=
		 (*(cache_t **)data2)->file.size)
		return -1;
	else
		return 1;
}

static int sortby_size_reverse(const void *data1, const void *data2)
{
	if ( (*(cache_t **)data1)->file.size >=
		 (*(cache_t **)data2)->file.size)
		return 1;
	else
		return -1;
}

static int sortby_date(const void *data1, const void *data2)
{
	if ( (*(cache_t **)data1)->file.date >=
		 (*(cache_t **)data2)->file.date)
		return -1;
	else
		return 1;
}

static int sortby_date_reverse(const void *data1, const void *data2)
{
	if ( (*(cache_t **)data1)->file.date >=
		 (*(cache_t **)data2)->file.date)
		return 1;
	else
		return -1;
}

static int sortby_dirfirst(const void *data1, const void *data2)
{
	// 1 is dir, 2 is dir  ->  0
	// 1 is dir, 2 is file -> -1
	// 1 is file, 2 is dir ->  1
	// 1 is file, 2 is file->  0
	if ( (*(cache_t **)data1)->file.directory ==
		 (*(cache_t **)data2)->file.directory)
		return 0;

	if ( (*(cache_t **)data1)->file.directory == YNA_YES)
		return -1;

	return 1;
}


char *skin_get_sort_str(request_t *node)
{
    return skin_sort_str[node->sort];
}

int skin_get_sort_flags(request_t *node)
{
    return dirlist_a2f(skin_get_sort_str(node));
}



static void skin_cache_sort_sub(char *sortstr, int total, cache_t **list)
{
	int flags, sort, i;
	int (*sorter)(const void *, const void *) = NULL;


	flags =  dirlist_a2f(sortstr);

	// Mask out any sort type we don't handle yet.
	sort = flags&(DIRLIST_SORT_NAME|DIRLIST_SORT_DATE|DIRLIST_SORT_SIZE|
				  DIRLIST_SORT_CASE|DIRLIST_SORT_REVERSE);

	// Work out how to sort.
#define N DIRLIST_SORT_NAME
#define D DIRLIST_SORT_DATE
#define S DIRLIST_SORT_SIZE
#define C DIRLIST_SORT_CASE
#define R DIRLIST_SORT_REVERSE

	// If NAME is set, unset DATE, SIZE
	if (sort & DIRLIST_SORT_NAME)
		sort &= ~(DIRLIST_SORT_DATE | DIRLIST_SORT_SIZE);

	// if DATE is set, unset SIZE and CASE (NAME we already know isnt set)
	if (sort & DIRLIST_SORT_DATE)
		sort &= ~(DIRLIST_SORT_SIZE | DIRLIST_SORT_CASE);

	// is SIZE is set, unset CASE
	if (sort & DIRLIST_SORT_SIZE)
		sort &= ~(DIRLIST_SORT_CASE);

	switch(sort) {

	case N:
		sorter = sortby_name;
		break;
	case N|C:
		sorter = sortby_name_case;
		break;
	case N|R:
		sorter = sortby_name_reverse;
		break;
	case N|C|R:
		sorter = sortby_name_case_reverse;
		break;

	case D:
		sorter = sortby_date;
		break;
	case D|R:
		sorter = sortby_date_reverse;
		break;
	case S:
		sorter = sortby_size;
		break;
	case S|R:
		sorter = sortby_size_reverse;
		break;
	}

	if (!sorter) // No sorter set, just return
		return;

	// If we want directories first, we need to sort it for that first.
	// then find the first non-directory entry, and call sort twice,
	// once on directory, and once on files.

	if (flags & DIRLIST_SORT_DIRFIRST) {

		qsort((void *)list, total, sizeof(cache_t *), sortby_dirfirst);

		// Loop and find first non-directory

		for ( i = 0; i < total; i++) {
			if (list[ i ]->file.directory != YNA_YES)
				break;
		}

		// If is the first element, or, the list, it is either
		// all files, or, all directories, and we can just sort it
		// once. Otherwise, sort twice.

		if ((i > 0) && (i < total)) {

#ifdef DEBUG
			printf("   [dirlist_child_sort] found split at %d\n", i);
#endif

			// Sort directories (they are first)
			qsort((void *)list, i, sizeof(cache_t *), sorter);

			// Sort files
			qsort((void *)&list[ i ], total - i, sizeof(cache_t *), sorter);

			return;

		} // two types


	} // DIRFIRST


	qsort((void *)list, total, sizeof(cache_t *), sorter);

	// Lets not pollute the name space
#undef N
#undef D
#undef S
#undef C
#undef R


}


//
// Sort the file cache. But linked-lists suck abit for sorting, so
// we convert it to an array of ptr. Then we can use OS sort().
//
static void skin_cache_sort(request_t *node)
{
	cache_t **cache_all, *cache;
	int i;

	debugf("[skin] sorting cache...\n");

	cache_all = (cache_t **) malloc(sizeof(cache_t *) * node->skin_last_tvid);
	if (!cache_all) return; // No sorting.

	// Load all ptrs
	for (cache = (cache_t *)node->cache, i=0;
		 cache;
		 cache=cache->next,
		 i++) {
		cache_all[i] = cache;
	}

    debugf("[skin] setup %d ptrs for sorting\n", i);

	// Sort it, based on type.
	skin_cache_sort_sub(skin_sort_str[node->sort], node->skin_last_tvid, cache_all);

	// And re-order the linked-list.
	for (i = 0; (i < node->skin_last_tvid); i++) {
		if (!i)
			node->cache = cache_all[i];
		else
			cache_all[i-1]->next = cache_all[i];

	}
    if (i)
        cache_all[i-1]->next = NULL;

	SAFE_FREE(cache_all);

	debugf("[skin] sorting complete...\n");

}





skin_t *skin_findbyname(skin_types_t *type, char *name, int pass)
{
	skin_t *runner;

    if (!type) return NULL;

    //debugf("[skin] findbyname '%s'\n", name);

	for (runner = type->skin_types; runner; runner = runner->next) {

		if (pass != runner->pass) continue;
		if (!runner->name) continue;

		if (!mystrccmp(name, runner->name)) {
            return runner;
        }

	}

    //debugf("[skin] failed skin_findbyname('%s', %d).\n", name, pass);

	return NULL;
}

skin_t *skin_findbyname_all(char *name, int pass)
{
    skin_types_t *type;
	skin_t *runner;

    //debugf("[skin] findbyname_all '%s'\n", name);

	// Find the type that matches the file, for all types...
    for (type = skin_types_head; type; type = type->next) {

        for (runner = type->skin_types; runner; runner = runner->next) {

            if (pass != runner->pass) continue;
            if (!runner->name) continue;

            if (!mystrccmp(name, runner->name)) {
                return runner;
            }

        }

    }

    //debugf("[skin] failed skin_findbyname_all('%s', %d).\n", name, pass);

	return NULL;
}



skin_t *skin_findbymatch(skin_types_t *type, char *name, int pass)
{
	skin_t *runner;
	char *ar, *ext;
	int found = 0;

	for (runner = type->skin_types; runner; runner = runner->next) {

		if (pass != runner->pass) continue;
		if (!runner->name) continue;

		ar = runner->ext;
		while((ext = misc_digtoken(&ar, "/"))) {

			if (!lfnmatch(ext,
						  name,
						  LFNM_CASEFOLD)) {

				found = 1;

			}

			// If we matched something, put the "/" so we can
			// keep matching.
			if ((ar > runner->ext) && misc_digtoken_optchar)
				ar[-1] = misc_digtoken_optchar;

			if (found) return runner;

		} // while ext

	} // for skins



	return NULL;
}


//
// Dump the cached entries in dirlists etc.
// skin_tvid counts from 1 and up, even outside our page number.
// page_from and page_to has the tvid's we are to display
// page_current counts from 1 to page_size inside the displayed page.
//
static void skin_cache_write(request_t *node)
{
	cache_t *cache;
	skin_t *skin, *separator;

	separator = skin_findbyname(node->skin_type, "Separator", node->pass);

	node->page_current = node->page_from;
	node->skin_tvid = 0;
	node->page_current = node->skin_last_tvid;

    // We have no page_size in UPNP
    if ((node->upnp_type == UPNP_BROWSEDIRECTCHILDREN)) {

        // If they requested no upper-limit, just send all.
        if (!node->page_to)
            node->page_to = node->skin_last_tvid;

    } else {

        if (node->skin_type->skin_page_size) {
            // If page_to is not set, we might as well set it here, with the default
            if (!node->page_to)
                node->page_to = node->page_from + node->skin_type->skin_page_size;
        }
    }

    debugf("[skin] dumping cache (from %d to %d)\n",
           node->page_from, node->page_to);

	for (cache = (cache_t *)node->cache; cache; cache=cache->next) {

		node->skin_tvid++;
		cache->file.tvid = node->skin_tvid;

		// pagination?
		if (node->skin_type->skin_page_size) {
			if ((node->skin_tvid <= node->page_from) ||
				(node->skin_tvid > node->page_to))
				continue; // keep looping until we are inside the page.
		}

		// Add a separator if needed
		//if (separator && (separator->count == (node->page_current - node->page_from - 1)))
		if (separator && (separator->count == (node->skin_tvid - node->page_from)))
			skin_write(node, separator, NULL);

		// Since skin change by pass, look up correct skin
        skin = skin_findbyname(node->skin_type, cache->skin->name, node->pass);

        //debugf("[skin] write: %p and upnp %d\n", skin, node->upnp_type);

        // If we are in UPNP mode, we look up the UPNP skin instead, but
        // keeping the upnp type.
        if ((node->upnp_type == UPNP_BROWSEDIRECTCHILDREN)) {
            char *upnp_type = NULL;

            if (skin && skin->upnp_type)
                upnp_type = strdup(skin->upnp_type);

            skin = NULL;

			if (cache->file.directory == YNA_YES)
				skin = skin_findbyname_all("UPNPDIRS", 1);
			else
				skin = skin_findbyname_all("UPNPFILE", 1);

            if (skin) {
                SAFE_FREE(skin->upnp_type);
                skin->upnp_type = upnp_type;
            }
		} // UPNP BROWSE

		if (!skin) {
            debugf("[skin] cache_write: uhoh, no skin\n");
            continue;
        }

        debugf("%s using skin %s\n", cache->file.name, skin->name);


		// extnfo work, clear it, technically not required.
		memset(&node->extnfo, 0, sizeof(node->extnfo));

		// .. dupe from cache
		extra_dupe(&node->extnfo, &cache->extnfo);

		// Remember processing function
		node->process_function = cache->process_function;

		// Write this node
		skin_write(node, skin, cache->file.name ? &cache->file : NULL);

		// Clear duped extnfo
		extra_release(&node->extnfo);

        // page_to set?
		if (node->page_to &&
            (node->skin_tvid >= node->page_to)) {
            debugf("[skin] stopping as we reached page_to %d\n",
                   node->page_to);
            break;
        }
	}

    debugf("[skin] cache_write done. tvid %d and page_to %d\n",
           node->skin_tvid, node->page_to);
    // Here, if they asked for "to" to be 6, but directory only actually
    // contained "5" or less, we change "to" to how many we actually
    // sent, so that CURRENT_COUNT will be accurate.
    if (node->skin_tvid < node->page_to)
        node->page_to = node->skin_tvid;

}


// Technically the same function as skin_cache_write, but this one does
// not iterate the cache, and writes exactly ONE meta info node.
// Used with UPNP.
static void skin_meta_write(request_t *node)
{
	skin_t *skin;
    char *upnp_type = NULL;
    lfile_t *file = NULL;

	node->page_current = node->page_from;
	node->skin_tvid = 0;
	node->page_current = node->skin_last_tvid;

    // Go stat() the wanted entry
    root_setpath(node);

    // We look up real sin to get the objectID type
    if (S_ISDIR(node->stat.st_mode)) {
        file = NULL;
        skin = skin_findbyname(node->skin_type, "Directory", node->pass);
    } else {
        if (node->cache)
            file = &((cache_t *)node->cache)->file;
        // Look up node based on filename
        skin = skin_findbymatch(node->skin_type,
                                file ? file->name : node->path, node->pass);
    }

    debugf("[skin] skin_meta_write: path '%s': stat %d: skin '%s'. path '%s' rar_file '%s'  file_name '%s'\n",
           node->path, S_ISDIR(node->stat.st_mode),
           skin && skin->name ? skin->name : "",
           node->path, node->rar_file ? node->rar_file : "",
           file && file->name? file->name : "");

    // Remember the type
    if (skin && skin->upnp_type)
        upnp_type = strdup(skin->upnp_type);

    // Now look up the UPNP skin to send
    skin = NULL;
    if (S_ISDIR(node->stat.st_mode)) {
        skin = skin_findbyname_all("UPNPDIRS", 1);
    } else {
        skin = skin_findbyname_all("UPNPFILE", 1);
    }

    if (skin) {
        SAFE_FREE(skin->upnp_type);
        skin->upnp_type = upnp_type;
    }

    if (!skin) {
        debugf("[skin] meta_write: uhoh, no skin\n");
        return;
    }

    debugf("using skin %s\n", skin->name);

    // extnfo work, clear it, technically not required.
    memset(&node->extnfo, 0, sizeof(node->extnfo));

    // .. dupe from cache
    //extra_dupe(&node->extnfo, &cache->extnfo);

    // Remember processing function
    //node->process_function = cache->process_function;

    // Write this node
    skin_write(node, skin, file);

    // Clear duped extnfo
    // extra_release(&node->extnfo);

}



static void skin_playlist(request_t *node, skin_t *skin, lfile_t *file)
{
	unsigned long myip;
	int myport;
	char *name;

	// NULL FILE is not allowed
	if (!file) return; // Thats head, and tail.html.

	// We skip directories, unless playall recursive?
	if (file->directory == YNA_YES) return;

	// We skip directories, unless playall recursive?
	// Skin all non media files.
	if (skin) {
		switch (skin->process_cmd) {
		case REQUEST_PROCESS_NONE:
		case REQUEST_PROCESS_UNRAR:
		case REQUEST_PROCESS_DVDREAD:
			break;
		default:
			debugf("[skin] skipping due to non-media '%s'\n", file->name);
			return;
		}
	}


	// The rest should be media files.
	// Toy Story 2|0|0|http://iHome/toystory2.mpg|
	lion_getsockname(node->handle, &myip, &myport);

	name = file->name;
	while(*name == '/') name++;

	debugf("[skin] writing playlist with '%s'... \n", name);

    lion_printf(node->tmphandle, "%s|0|0|http://%s:%d/%s|\r\n",
                hide_path(file->name),
                lion_ntoa(myip),
                httpd_port,
                name);
}



static void skin_viewlist(request_t *node, skin_t *skin, lfile_t *file)
{
	char *name;
	unsigned long myip;
	int myport;

	// NULL FILE is not allowed
	if (!file) return; // Thats head, and tail.html.

	// We skip directories, unless playall recursive?
	if (file->directory == YNA_YES) return;

	// We skip directories, unless playall recursive?
	// Skin all non media files.
	if (skin) {
		switch (skin->process_cmd) {
		case REQUEST_PROCESS_NONE:
			break;
		default:
			debugf("[skin] skipping due to non-media '%s'\n", file->name);
			return;
		}
	}

	lion_getsockname(node->handle, &myip, &myport);

	name = file->name;
	while(*name == '/') name++;

	debugf("[skin] writing viewlist with '%s'... \n", name);

	lion_printf(node->tmphandle, "5|0|%s|http://%s:%d/%s|\r\n",
				hide_path(name),
				lion_ntoa(myip),
				httpd_port,
				name);
}




//
// Take a request, which has the tmpfile/althandle open to write to, and
// the skin html file to write and output to it. Expanding macros as we
// go along.
//
static void skin_write(request_t *node, skin_t *skin, lfile_t *file)
{
	int index, macro, len;
	int visible = 1;

	// scan mode
	if (conf_xmlscan) {
		if (file && file->name)
			xmscan_addfile(node, skin, file);
		return;
	}

    //debugf("skin_write: pass %d '%s'\n", node->pass, file ? file->name : "(null)");

	// pass=0, cache everything.
	if (!(node->pass) && !node->menu_mode) {
		// Look up the extnfo once, and cache it.
		memset(&node->extnfo, 0, sizeof(node->extnfo));
		extra_lookup(&node->extnfo, root_getroot(node->root_index), node->path,
					 file, skin);

		// Add to cache
		skin_cache_add(node, skin, file);

		// Clear out our version of extnfo.
		extra_release(&node->extnfo);
		return;
	}


	// In playlist mode, if &type=blah was set, only display
	// entries with TYPE|NAME=blah
	if ((node->playall || node->viewall) && node->typename && skin->name) {
		char *typename = strdup(node->typename);
		char *ar = typename;
		char *token;
		int match = 0;
		while((token = misc_digtoken(&ar, ",")) != NULL) {
			if (mystrccmp(token, skin->name) == 0)
				match++;
		}
		SAFE_FREE(typename);
		if (!match)
			return;
	}


	debugf("[skin] writing skin '%s'/'%s' to tmpfile: %d (pass %d)\n",
		   skin->name ? skin->name : "",
		   skin->filename, node->page_current, node->pass);

	// If we are in playlist mode, do that instead.
	if (node->playall) {
		skin_playlist(node, skin, file);
		return;
	}

	if (node->viewall) {
		skin_viewlist(node, skin, file);
		return;
	}



	// For each macros we have, write out from "index" to
	// just before macro, then the macro, repeat until the end.

	index = 0;

	for (macro = 0; macro < skin->num_macros; macro++) {

		len = skin->macros[ macro ].offset - index;

		// Write from start to macro
		if (visible)
			lion_output(node->tmphandle,
						&skin->html[index],
						len);

		// write the macro
		switch (skin->macros[macro].type) {
		case LLINK_IF_CLIENT_IS_SYABAS:
			visible = node->client_is_syabas;
			break;
		case LLINK_IF_CLIENT_IS_SYABAS_E:
			visible = 1;
			break;
		case LLINK_IF_CLIENT_IS_NOT_SYABAS:
			visible = !node->client_is_syabas;
			break;
		case LLINK_IF_CLIENT_IS_NOT_SYABAS_E:
			visible = 1;
			break;
		case LLINK_IF_CLIENT_ELSE:
			visible = !visible;
			break;
		default:
			// write the macro
			if (visible)
				skin_write_macro(node,
								 &skin->macros[ macro ], file, skin);
			break;
		}

		// Update index to be that of last macro
		index = skin->macros[ macro ].offset;

	}

	// Also write any remainder now.
	len = skin->index - index;

	if (len > 0)
		lion_output(node->tmphandle,
					&skin->html[index],
					len);


}




void skin_write_head(request_t *node)
{
	skin_t *head;

    debugf("[skin] write_head %d\n", node->pass);

	if (!node->pass) return; // Do nothing in caching pass
	if ((node->playall || node->viewall)) return; // no head in playlist mode

    if ((node->upnp_type == UPNP_BROWSEDIRECTCHILDREN) ||
        (node->upnp_type == UPNP_BROWSEMETADATA))
        head = skin_findbyname_all("UPNPHEAD", 1);
    else
        head = skin_findbyname(node->skin_type, "HEAD", node->pass);

	if (head)
		skin_write(node, head, NULL);

	// Reset the tvid now.
	node->skin_tvid = 0;
}




int skin_write_tail(request_t *node)
{
	skin_t *tail;
    int passes;

	// First time we get here, we are at pass = 0. This should be
	// a caching only pass.
	if (!node->pass) {
		debugf("[skin] Finished cache run: total %d\n", node->skin_tvid);

        // So we have finished listing directories, but we could now
        // chose to "expand" some types. For example, open RARs and show
        // contents, or dvdread, or even lower directories.

        // Problem here is, expand() needs to adjust "disk_path" and "path"
        // to simulate listings in RAR and ISO. So we need to keep those
        // original values between expands.

        if (!node->keep_path) {
            node->keep_disk_path = node->disk_path;
            node->keep_path = node->path;
        } else {
            if (node->keep_path != node->path) {
                SAFE_FREE(node->path);
                node->path = node->keep_path;
            }
            if (node->keep_disk_path != node->disk_path) {
                SAFE_FREE(node->disk_path);
                node->disk_path = node->keep_disk_path;
            }

        }

        if (skin_cache_expand(node)) return 1; // Keep calling us

        // No longer need to keep.
        node->keep_path = NULL;
        node->keep_disk_path = NULL;

		// We now sort the list the way the user wants.
		debugf("[skin] Finished expanding total %d\n", node->skin_tvid);
		node->skin_last_tvid = node->skin_tvid;

		skin_cache_sort(node);
	}

    // write the head first...
    skin_write_head(node);

    // Setup to iterate the passes.
    passes = node->skin_type->skin_passes;
    // But there is only ever one in UPNP
    if ((node->upnp_type == UPNP_BROWSEDIRECTCHILDREN) ||
        (node->upnp_type == UPNP_BROWSEMETADATA)) {
        passes = 1;
        debugf("[skin] UPNP: passes forced to 1\n");
    }


    if (node->upnp_type == UPNP_CUPNP) {
        debugf("[skin] CUPNP: returning with cache\n");
        return 0;
    }


	// Do we stop now, or increase passes and run again?
	// We only do one pass in "playall" mode.
	while (node->pass < passes) {

		node->pass++;

		debugf("[skin] processing pass %d ... \n", node->pass);

		// Write the next head
		skin_write_head(node);

		// write all the entries
        if (node->upnp_type == UPNP_BROWSEMETADATA)
            skin_meta_write(node);
        else
            skin_cache_write(node);

		// Only do one pass in playall (or we repeat lots), and no tail
		if (node->playall) break;
		if (node->viewall) break;

		// write tail
        if ((node->upnp_type == UPNP_BROWSEDIRECTCHILDREN) ||
            (node->upnp_type == UPNP_BROWSEMETADATA))
            tail = skin_findbyname_all("UPNPTAIL", 1);
        else
            tail = skin_findbyname(node->skin_type, "TAIL", node->pass);

		if (tail)
			skin_write(node, tail, NULL);

	} // while passes


	// Release cache
	skin_cache_clear(node);

	// All done, back to request_finishdir();
	node->skin_last_tvid = 0;
	node->pass = 0;

    return 0; // tail finished.
}


int skin_ignore(lfile_t *file, skin_t *skin)
{
	char *ar, *ext;
	int found = 0;

	if (!skin || !skin->ignore || !*skin->ignore)
		return 0;

	ar = skin->ignore;

	while((ext = misc_digtoken(&ar, "/"))) {

		if (!lfnmatch(ext,
					  hide_path(file->name),
					  LFNM_CASEFOLD)) {

			found = 1;

		}

		// If we matched something, put the "/" so we can
		// keep matching.
		if ((ar > skin->ignore) && misc_digtoken_optchar)
			ar[-1] = misc_digtoken_optchar;

		if (found) break;

	}

	return found;
}



void skin_write_type(request_t *node, char *line)
{
	lfile_t file;
	skin_t *runner;
	char *ar, *ext;
	int found = 0;


	if (conf_xmlscan && node->skin_type)
		node->skin_type->skin_page_size = 999999;

    //debugf("[skin] write_type: parsing %s\n", line);

	// "drwxr-xr-x 1 root wheel 512 Oct 28 15:18 screens"
	if (!file_parse(&file, line)) {

		// If this is a directory, we use type "dir" if it exists
		if (file.directory == YNA_YES) {

			// Check if it fits the ignore paths
			// we no longer check pass here, as that is done in cache_write
			runner = skin_findbyname(node->skin_type, "Directory", 1);

			if (runner && skin_ignore(&file, runner)) {
				file_free(&file);
				return;
			}

			if (runner) {
				skin_write(node, runner, &file);
			}


			file_free(&file);
			return ;
		} // directory


        if (!node->skin_type) {
            debugf("[skin] odd, type is NULL for %s\n", file.name);
            file_free(&file);
            return ;
        }

		// Find the type that matches the file, for all types...
		for (runner = node->skin_type->skin_types; runner; runner = runner->next) {

			// Only match against the current pass
			if (runner->pass != 1) continue;

			// If we don't have a ext match (head, tail etc) skip
			if (!runner->ext) continue;

			// For each extention listed for this type...
			ar = runner->ext;

			while((ext = misc_digtoken(&ar, "/"))) {

				if (!lfnmatch(ext,
							  file.name,
							  LFNM_CASEFOLD)) {

					// If we matched something, put the "/" so we can
					// keep matching.
					if ((ar > runner->ext) && misc_digtoken_optchar)
						ar[-1] = misc_digtoken_optchar;

					debugf("[skin] ext '%s' with '%s' is type '%s' %d\n",
						   ext, file.name, runner->name, runner->pass);

					// Check if it fits the ignore paths
					if (skin_ignore(&file, runner))
						break;

					// If this filetype had special processing function set
					// assign that now.
					node->process_function = runner->process_cmd;

                    debugf("upgrade place..\n");
#if 0
                    // Hack, if we are expanding from RAR, we may need
                    // to upgrade to RARISO
                    if ((node->process_function == REQUEST_PROCESS_UNRAR) /*&&
                                                                            (node->rar_file)*/) {
                        // We should ONLY upgrade if it matches ISO though?
                        runner = skin_findbyname(node->skin_type, "DVD_Directory", 1);
                        if (node->rar_file)
                            node->process_function = REQUEST_PROCESS_RARISO;
                        else
                            node->process_function = REQUEST_PROCESS_UNRAR;
                        debugf("[skin] Expand RAR, upgrading to RAR/ISO: %d (%s)\n",
                               node->process_function,
                               node->rar_file ? node->rar_file : "");
                    }
#endif

					skin_write(node, runner, &file);

                    node->process_function = REQUEST_PROCESS_NONE;

					found = 1;
					break;

				}

				if ((ar > runner->ext) && misc_digtoken_optchar)
					ar[-1] = misc_digtoken_optchar;

			} // while "/" seperators

			if (found) break;

		} // for runner/types

	} // if parsed OK

	file_free(&file);

}




skin_t *skin_process_cmd(request_t *node)
{
	skin_t *runner;
	char *ar, *ext, *name;
	int found = 0;

	name = hide_path(node->disk_path);

	// Find the type that matches the file, for all types...
	for (runner = node->skin_type->skin_types; runner; runner = runner->next) {

		if (runner->pass != 1) continue;

		if (!runner->ext) continue;


		// For each extention listed for this type...
		ar = runner->ext;

		while((ext = misc_digtoken(&ar, "/"))) {

			//				debugf("[skin] checking ext '%s' with '%s'\n",
			//	   ext, file.name);

			if (!lfnmatch(ext,
						  name,
						  LFNM_CASEFOLD)) {

				// If we matched something, put the "/" so we can
				// keep matching.
				if ((ar > runner->ext) && misc_digtoken_optchar)
					ar[-1] = misc_digtoken_optchar;

				//debugf("[skin] ext '%s' with '%s' is type '%s'\n",
				//	   ext, name, runner->name);

				// If this filetype had special processing function set
				// assign that now.
				node->process_function = runner->process_cmd;

				found = 1;
				break;

			}

			if ((ar > runner->ext) && misc_digtoken_optchar)
				ar[-1] = misc_digtoken_optchar;

		} // while "/" seperators

		if (found) break;

	} // for runner/types

	if (found) return runner;

	return NULL;

}



char *skin_get_rar(void)
{
    skin_types_t *type;
	skin_t *runner;

	// Find the type that matches the file, for all types...
    for (type = skin_types_head; type; type = type->next) {

        for (runner = type->skin_types; runner; runner = runner->next) {
            if ((runner->process_cmd == REQUEST_PROCESS_UNRAR) &&
                runner->args) {
                debugf("[skin] get_rar matching '%s' returns '%s'\n",
                       runner->name, runner->args);
                return runner->args;
            }
        }
    }

	return NULL;
}



char *skin_redirect(skin_types_t *type, char *name)
{
	char *ar, *ext;
	skin_t *runner;

    if (!type) return NULL;

	for (runner = type->skin_types; runner; runner = runner->next) {
		if ((runner->process_cmd == REQUEST_PROCESS_REDIRECT) &&
			runner->args) {

			if (!runner->ext) continue;


			// For each extention listed for this type...
			ar = runner->ext;

			while((ext = misc_digtoken(&ar, "/"))) {

				if (!lfnmatch(ext,
							  name,
							  LFNM_CASEFOLD)) {

					// If we matched something, put the "/" so we can
					// keep matching.
					if ((ar > runner->ext) && misc_digtoken_optchar)
						ar[-1] = misc_digtoken_optchar;

					debugf("[skin] redirect ext '%s' with '%s' is type '%s'\n",
						   ext, name, runner->name);

					return runner->args;

				}

				if ((ar > runner->ext) && misc_digtoken_optchar)
					ar[-1] = misc_digtoken_optchar;

			} // while "/" seperators

		} // process_cmd

	} //for

	return NULL;

}



void skin_write_menu(request_t *node, char *line)
{
	lfile_t file;
	skin_t *runner;

	debugf("[skin] skin_write_menu: '%s'\n", line);

    if (node->upnp_type == UPNP_ROOTXML) {
        runner = skin_findbyname_all("UPNPROOT", 1);
        debugf("[skin] llink.xml, expanding: '%s'\n",
               runner && runner->name ? runner->name : "");
        if (runner) {
            skin_write(node, runner, NULL);
        }
        return;
    }

	if (*line == 'd')
		runner = skin_findbyname(node->skin_type, "DirectoryMenu", 1);
	else
		runner = skin_findbyname(node->skin_type, "Menu", 1);

	// FIXME, use ext= to search for a better match of menu!

	if (!runner) {
		debugf("[skin] No skin for MENU. Nothing will happen...\n");
		return;
	}

	// "drwxr-xr-x 1 root wheel 512 Oct 28 15:18 screens"
	if (!file_parse(&file, line)) {
		skin_write(node, runner, &file);
		file_free(&file);
	}

	debugf("[skin] skin_write_menu done\n");
}

char *skin_get_ffmpeg(void)
{
	return skin_ffmpeg_cmd;
}


char *skin_transcode_id2args(unsigned int id)
{
    transcode_t *runner;

    for (runner = skin_transcodes;
         runner;
         runner = runner->next) {

        if (runner->id == id)
            return runner->args;

    }

    return NULL;
}

char *skin_transcode_getext(unsigned int id)
{
    transcode_t *runner;

    for (runner = skin_transcodes;
         runner;
         runner = runner->next) {

        if (runner->id == id)
            return runner->newext;

    }

    return NULL;
}
