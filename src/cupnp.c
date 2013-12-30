#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lion.h"
#include "misc.h"
#include "mime.h"

#define DEBUG_FLAG 4096
#include "debug.h"
#include "httpd.h"
#include "parser.h"
#include "request.h"
#include "root.h"
#include "skin.h"
#include "cupnp.h"
#include "ssdp.h"
#include "cdms_filesys.h"
#include "conf.h"
#include "visited.h"

#if HAVE_CLINKC
#include <cybergarage/util/cmutex.h>
#include <cybergarage/upnp/cupnp.h>
#include <cybergarage/upnp/std/av/cmediaserver.h>
#include <cybergarage/upnp/std/av/ccontent.h>
#include <cybergarage/net/cinterface.h>
#include <cybergarage/util/cthread.h>
#include <cybergarage/upnp/std/av/cmediarenderer.h>
#include <cybergarage/util/clog.h>
#endif

#define  CG_UPNP_MEDIA_FILESYS_RESURL_MAXLEN (CG_NET_IPV6_ADDRSTRING_MAXSIZE + CG_MD5_STRING_BUF_SIZE + 64)

// Overall enabled
// ssdp_upnp

// Overall initialised or not.
static int cupnp = 0;

static unsigned int upnp_http_port = 0;

#if HAVE_CLINKC
//#define CLINKC_WITH_RENDERER 1


#define CUPNP_UPDATE_FREQ  30  // How often do we stat() all directories?
#define CUPNP_UPDATE_NEWER 30  // How much newer does it have to be, to be new?
#define CUPNP_UPDATE_SLEEP 60000 // Sleep between stating all dirs
#define CUPNP_UPDATE_IDLE  300 // When llink is this idle, dont stat dirs anymore.
void cupnp_ServerUpdateContainers(CgThread *thread);

static CgUpnpControlPoint *ctrlPoint        = NULL;
static CgUpnpAvServer     *mediaServer      = NULL;

#ifdef CLINKC_WITH_RENDERER
static CgUpnpAvRenderer   *mediaRenderer    = NULL;
#endif // RENDERER

static lion64u_t cupnp_update_version = 0;

CgUpnpAvContent *cg_upnpav_content_findbyuserfunc(CgUpnpAvContent *con, CG_UPNPAV_CONTENT_COMPARE_FUNC userFunc, void *userData);

// List of defined roots
static upnp_root_t *upnp_root_list = NULL;

#define USERAGENT_TIMEOUT 30
static CgMutex *useragent_mutex = NULL;
struct useragent_struct {
    void *id;
    char *useragent;
    char *host;
    time_t time;
    struct useragent_struct *next;
};
typedef struct useragent_struct useragent_t;
static useragent_t *useragent_head = NULL;
#endif



int request_writefile_handler(lion_t *handle, void *user_data,
                              int status, int size, char *line);


void cupnp_resume(request_t *node)
{

#if HAVE_CLINKC
    if (!cupnp) return;

    debugf("[cupnp] resume called, releasing mutex\n");
#ifdef WIN32
	node->cupnp_mutex = 0;
	return;
#endif

    if (node && node->cupnp_mutex)
      cg_mutex_unlock((CgMutex *)node->cupnp_mutex);

#endif
}


#if HAVE_CLINKC


//
// Slightly special version of hide path for cupnp.
//
// Normally, it would return "file.avi" from "dirname/file.avi". but we also
// need to handle "dirname/file.rar?f=12345,d=/name.avi".
// In this case, look for "?", then look for first "/".

char *cupnp_hide_path(char *s)
{
	char *r = NULL;
	char *quest = NULL;

	quest = strrchr(s, '?');
    if (quest) *quest = 0;
	r = strrchr(s, '/');
    if (quest) *quest = '?';

	return ((r && r[1]) ? &r[1] : s);

}


upnp_root_t *cupnp_findbypath(char *path)
{
    upnp_root_t *root;

    if (!path) return NULL;

    for (root = upnp_root_list;
         root;
         root = root->next) {

        if (!strcmp(root->path, path)) {
            return root;
        }
    }
    return NULL;
}

upnp_root_t *cupnp_findbyhash(unsigned long hash)
{
    upnp_root_t *root;

    for (root = upnp_root_list;
         root;
         root = root->next) {

        if (root->hash == hash)
            return root;
    }
    return NULL;
}


char *umime_class(char *mime)
{
    if (!mime || !*mime)
        return CG_UPNPAV_UPNPCLASS_ITEM;
    if (!strncmp("audio/", mime, 6))
        return CG_UPNPAV_UPNPCLASS_MUSIC;
    if (!strncmp("video/", mime, 6))
        return "object.item.videoItem";  // XBOX 360
    //return CG_UPNPAV_UPNPCLASS_MOVIE;
    if (!strncmp("image/", mime, 6))
        return CG_UPNPAV_UPNPCLASS_PHOTO;

    return CG_UPNPAV_UPNPCLASS_ITEM;
}




char *umisc_strjoin(char *a, char *b)
{
  char *result;
  int len_a, extra_slash;

  // Check if a ends with / or, b starts with /, if so dont
  // add a '/'.
  len_a = strlen(a);

  // If neither a ends with / AND b don't start with slash, add slash.
  if ((a[ len_a - 1 ] != '/') && b[0] != '/')
    extra_slash = 1;
  else
    extra_slash = 0;

  // If both have slashes, skip all leading 'b' slashes, and only use a.
  if ((a[ len_a - 1 ] == '/'))
      while(*b=='/') b++;


  // misc_strjoin('720/' + '/Music' -> '720//Music'

  result = (char *) malloc(len_a + strlen(b) + 1 + extra_slash);

  if (!result) {
    perror("malloc");
    exit(2);
  }

  if (extra_slash)
    sprintf(result, "%s/%s", a, b);
  else
    sprintf(result, "%s%s", a, b);

  return result;
}







//
// Take a string, and allocate a new string which is URL encoded,
// even if no encoding was needed, it is allocated.
// Is it worth running through once to know how much to allocate, or
// just allocate * 3 ? But to allocate *3 I need to call strlen anyway.
// (Although, some OS can do that based on longs)
//
// Only alphanumerics [0-9a-zA-Z], the special characters "$-_.+!*'(),"
char *umisc_url_encode(char *s)
{
	char *result = NULL;
	int len = 0;
	char *r, *r2=0;
	static char hex[] = "0123456789ABCDEF";

	// How much space to allocate?
	while(1) {

		r = s;

		while (*r) {

			switch(tolower(*r)) {
			case 'a':
			case 'b':
			case 'c':
			case 'd':
			case 'e':
			case 'f':
			case 'g':
			case 'h':
			case 'i':
			case 'j':
			case 'k':
			case 'l':
			case 'm':
			case 'n':
			case 'o':
			case 'p':
			case 'q':
			case 'r':
			case 's':
			case 't':
			case 'u':
			case 'v':
			case 'w':
			case 'x':
			case 'y':
			case 'z':
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '$':
			case '-':
			case '_':
			case '.':
			case '+':
			case '!':
			case '*':
			case '(':
			case ')':
			case ',':
			case ':': // looks nicer if colon iskept
			case '/': // and slash man!
				len++;
				if (result) *r2++ = *r;
				break;
			default:
				len+=3;
				if (result) {
					*r2++ = '%';
					*r2++ = hex[((*r>>4)&0x0f)];
					*r2++ = hex[((*r)&0x0f)];
				}
			} // switch
			r++;
		} // while r

		if (result) {
			*r2 = 0;
			break;
		}

		if (!len) break;

		result = malloc(len + 1);

		if (!result) break;

		r2 = result;

	} // while 1

	return result;

}




//
// Decode the url, allocate new area
//
char *umisc_url_decode(char *s)
{
	char *result, *r, chr;

	result = strdup(s);

	if (!result) return NULL;

	r = result;

	while(*s) {

		if (*s != '%')
			*r++ = *s++;
		else {
			chr = 0;
			s++;

			if (!*s) break;

			if ((toupper(*s) >= 'A') &&
				(toupper(*s) <= 'F'))
				chr = toupper(*s) - 'A' + 0x0A;
			else if ((*s >= '0') &&
					 (*s <= '9'))
				chr = *s - '0';
			s++;
			chr <<= 4;
			if ((toupper(*s) >= 'A') &&
				(toupper(*s) <= 'F'))
				chr |= (toupper(*s) - 'A' + 0x0A) & 0x0f;
			else if ((*s >= '0') &&
					 (*s <= '9'))
				chr |= (*s - '0') & 0x0f;
			s++;
			*r++ = chr;

		} // %

	}

	*r = 0;

	return result;

}



void cupnp_release_content_node(upnp_content_t *content)
{
    if (!content) return;

    SAFE_FREE(content->id);
    SAFE_FREE(content->parent_id);
    SAFE_FREE(content->title);
    SAFE_FREE(content->url);
    SAFE_FREE(content->mime);

    SAFE_FREE(content->date);
    SAFE_FREE(content->creator);
    SAFE_FREE(content->artist);
    SAFE_FREE(content->owner);
    SAFE_FREE(content->album_art);
    SAFE_FREE(content->album);
    SAFE_FREE(content->description);
    SAFE_FREE(content->genre);
    SAFE_FREE(content->producer);
    SAFE_FREE(content->rating);
    SAFE_FREE(content->resolution);

    content->_next = NULL;

}

void cupnp_release_content(upnp_content_t *content)
{
    if (!content) return;

    cupnp_release_content_node(content);
    SAFE_FREE(content);
}


#define DATESTR_LEN 20
char *misc_datestr(time_t ftime)
{
    // <dc:date>2010-11-24T15:03:28</dc:date>
    static char result[DATESTR_LEN];
    struct tm tmt;

    //result = malloc(DATESTR_LEN);
    //if (!result) return NULL;

    memset(&tmt, 0, sizeof(tmt));
    localtime_r(&ftime, &tmt);

    snprintf(result, DATESTR_LEN,
             "%04u-%02u-%02uT%02u:%02u:%02u",
             tmt.tm_year + 1900,
             tmt.tm_mon + 1,
             tmt.tm_mday,
             tmt.tm_hour,
             tmt.tm_min,
             tmt.tm_sec);
    //debugf("** %lu => %s\n", ftime, result);
    return result;
}

time_t misc_str2date(char *d)
{
  struct tm tmw;
  time_t result;

  memset(&tmw, 0, sizeof(tmw));

  if (sscanf(d, "%04u-%02u-%02uT%02u:%02u:%02u",
             &tmw.tm_year,
             &tmw.tm_mon,
             &tmw.tm_mday,
             &tmw.tm_hour,
             &tmw.tm_min,
             &tmw.tm_sec) == 6) {
      tmw.tm_year -= 1900;
      tmw.tm_mon  -= 1;

      result = mktime(&tmw);
      //debugf("** %s => %lu\n", d, result);
      return result;
  }

  return 0;
}


int cupnp_callback(char *path, char *id,
                   upnp_content_t *buffers,
                   unsigned int *num_buffers,
                   void **arg,
                   char *useragent, char *host)
{
    request_t *node;
    cache_t *cache, *cache_start = NULL;
    int counter;
    upnp_content_t *content;
    CgNetworkInterfaceList *netIfList = NULL;
    CgNetworkInterfaceList *netIf;

    if (!cupnp) return 0;

    // arg is set to request node
    node = (request_t *)*arg;

    debugf("[cupnp] callback(%p)\n", node);

    // First call on a new listing?
    if (!node) {

        //node = calloc(1, sizeof(*node));
        // if (!node) goto failed_or_finished;

        node = request_newnode();
        if (!node) goto failed_or_finished;

        // We need to set the User-Agent here!
        if (useragent)
            node->useragent = strdup(useragent);

        if (host)
            node->ip = lion_addr(host);

        skin_set_skin(node, NULL);

        // Setup path we are to list
        node->path = misc_strjoin(path, id);
        node->upnp_type = UPNP_CUPNP;

        debugf("[cupnp] calling setpath on '%s'\n", node->path);

        // Call setpath
        if (root_setpath(node)) {
            debugf("[cupnp] setpath failed.\n");
            goto failed_or_finished;
        }

        // Assign the request node for next call.
        *arg = (void *)node;

#ifdef WIN32
		node->cupnp_mutex = 1;
#else
        // Create a mutex to release us when ready
        node->cupnp_mutex = (void *) cg_mutex_new();

        // Initially lock it
		cg_mutex_lock((CgMutex *)node->cupnp_mutex);
#endif

        // When we open a tmpfile, it is actually the main thread that calls
        // lion, and handle the requests, not this one.
		node->tmpname = strdup(TMP_TEMPLATE);
		if (!mktemp(node->tmpname)) {
            goto failed_or_finished;
		}

        debugf("[cupnp] Calling list (tmpfile '%s')...\n",
               node->tmpname);

		node->tmphandle = lion_open(node->tmpname,
									// We use EXCL to make sure it doesn't
									// already exist, and avoid a race thing
									// Hmm Win32 wont let you open it again
									// and with the threads trailing, it would
									// occasionally fail.
									O_WRONLY | O_CREAT /*| O_EXCL*/,
									0600,
									LION_FLAG_NONE,
									(void *) node);

		if (!node->tmphandle) {
            goto failed_or_finished;
		}


		// Set handler
		lion_set_handler(node->tmphandle, request_writefile_handler);

		// Disable read since we only write to this file.
		lion_disable_read(node->tmphandle);

		// We CAN NOT write to the tmphandle YET, as lion does
		// not guarantee this until FILE_OPEN event.
        // So the node has status set to call root_list when ready.

        // This sort of crap should be shot at dawn
        //while(node->current_event != REQUEST_EVENT_WFILE_OPENED)
        //sleep(1);

        //root_list(node, node->disk_path);

        // Wait for listing to finish.
#ifdef WIN32
		// The Windows code here is horrendous. Oh dear lord..
		debugf("[cupnp] thread: trying to lock mutex, waiting to be released\n");
		while(node->cupnp_mutex) {
			lion_poll(0,1);
			request_events();
		}
        debugf("[cupnp] thread: released.\n");
#else
		debugf("[cupnp] thread: trying to lock mutex, waiting to be released\n");
        cg_mutex_lock((CgMutex *)node->cupnp_mutex);
        debugf("[cupnp] thread: released.\n");
        cg_mutex_unlock((CgMutex *)node->cupnp_mutex);
#endif
        //while(node->upnp_type == UPNP_CUPNP)
        //   sleep(1);

        node->cupnp_lister = NULL;
        cache_start = (cache_t *)node->cache;  // Start listing from the.. start
    } else {
        // Not first time, but continuation
        cache_start = (cache_t *) node->cupnp_lister;
    }

    debugf("[cupnp] iterating cache...Node is %p\n", cache_start);
    netIfList = cg_net_interfacelist_new();
    cg_net_gethostinterfaces(netIfList);
    netIf = cg_net_interfacelist_gets(netIfList);
    if (!netIf) {
        goto failed_or_finished;
    }


    // If we are NOT starting (first loop), AND there is no continue
    // node, then we have finished.
    if (!cache_start)
        goto failed_or_finished;

    // For loop either starts from beginning, or where we stopped last time
	for (cache = cache_start,
             counter = 0;
         cache;
         cache=cache->next, counter++) {
        debugf(" ++ '%s'\n", cache->file.name);

        if (counter >= *num_buffers) break;

        // Make content, and give upstream
        content = &buffers[ counter ];

        memset(content, 0, sizeof(*content));
        content->title= strdup(hide_path(cache->file.name));
        content->size = cache->file.size;
        content->date = strdup(misc_datestr(cache->file.date));

        if (cache->file.directory == YNA_YES) {

            content->type = UPNP_CONTENT_CONTAINER;

        } else {

			int myport;
			unsigned long myip;
            char url[64]; // http + IP + port + some slashes
            char *encoded, *full;
			lion_getsockname(node->handle, &myip, &myport);
			snprintf(url, sizeof(url), "http://%s:%d/",
                     //lion_ntoa(myip),
                     cg_net_interface_getaddress(netIf),
                     httpd_port);

            full = misc_strjoin(id, cupnp_hide_path(cache->file.name));
            debugf("[cupnp] full path is then: '%s'\n", full);
            encoded = request_url_encode(full);
            content->type = UPNP_CONTENT_OTHER;
            content->url  = misc_strjoin(url, encoded);

            // Visited check mark?
            if (visited_upnp_precat) {
                if (visited_filename(node, full)) {
                    char *tmp;
                    tmp = content->title;
                    content->title = misc_strjoin_plain(visited_upnp_precat, tmp);
                    SAFE_FREE(tmp);
                }
            }
            if (visited_upnp_postpend) {
                if (visited_filename(node, full)) {
                    char *tmp;
                    tmp = content->title;
                    content->title = misc_strjoin_plain(tmp, visited_upnp_postpend);
                    SAFE_FREE(tmp);
                }
            }

            SAFE_FREE(full);
            SAFE_FREE(encoded);

        } // File


    } // for all in cache..

    *num_buffers = counter;

    node->cupnp_lister = (void *) cache;

    return 1; // 1 means keep iterating

 failed_or_finished:

    if (netIfList)
        cg_net_interfacelist_delete(netIfList);

#ifndef WIN32
    if (node->cupnp_mutex)
        cg_mutex_delete((CgMutex *)node->cupnp_mutex);
#endif

    request_freenode(node); // Also clears cache
    *arg = NULL;
    *num_buffers = 0; // Tell them nothing return
    return 0; // means ok, but stop.

}


void cupnp_hash(char *path, int len, unsigned int *hash)
{
    static unsigned int counter = 0x01234567;

    *hash = counter++;

}


int cupnp_addroot(
                  char *path, char *name,
                  upnp_virtual_callback callback,
                  int (*open) (void **fh, char *path, char *id),
                  int (*seek) (void **fh, unsigned long long pos),
                  int           (*read) (void **fh, char *buf, unsigned int size),
                  int (*close)(void **fh)
                  )
{
    upnp_root_t *noot;  // new root
    unsigned int hash;

    //    if (!ctrlPoint)
    //   return UPNP_ERR_NOT_INITIALISED;

    // As strange as it sounds, llink uses Virtual with no file IO operations
    // set, as it uses own HTTP listener port. It does this by setting content->url
    // in callback() listing.
    if (!path || !callback /*|| !open || !seek || !read || !close*/)
        return -3;

    cupnp_hash(path, strlen(path), &hash);

    debugf("[cupnp] Registering '%s' as root ('%s'): %08X\n",
           path, name ? name : "", hash);

    // Check it is not already registered
    if (cupnp_findbypath(path) ||
        cupnp_findbyhash(hash))
        return -1; // Already exists

    noot = calloc(1, sizeof(*noot));

    if (!noot) return -2;


    noot->next = upnp_root_list;
    upnp_root_list = noot;

    noot->path = strdup(path);
    noot->hash = hash;
    snprintf(noot->hash_str, sizeof(noot->hash_str),
             "%08x", hash);
    noot->callback = callback;

    noot->open  = open;
    noot->seek  = seek;
    noot->read  = read;
    noot->close = close;

    if (name)
        noot->name = strdup(name);

    debugf("[cupnp] '%s' hashes to '%08x'\n", path, noot->hash);


    return 0;

}

void cupnp_release_root(upnp_root_t *root)
{
    if (!root) return;

    debugf("[cupnp] releasing '%s'\n", root->path);

    SAFE_FREE(root->path);
    SAFE_FREE(root->name);
    root->callback = NULL;
    root->next = NULL;

    SAFE_FREE(root);
}

int cupnp_delroot(char *path)
{
    upnp_root_t *root, *prev;

    if (!path) return -3;

    for (root = upnp_root_list, prev = NULL;
         root;
         prev = root, root = root->next) {

        if (!strncmp(root->path, path, strlen(root->path))) {

            if (!prev)
                upnp_root_list = root->next;
            else
                prev->next = root->next;

            cupnp_release_root(root);
            return 0;
        }
    }

    return 99; // not found
}




void cupnp_add_metadata(CgUpnpAvContent *con, upnp_content_t *content)
{

    // dc:date
    if (content->date)
        cg_xml_node_setchildnode(con, CG_UPNPAV_OBJECT_DATE, content->date);

    // dc:owner
    if (content->owner)
        cg_xml_node_setchildnode(con, "dc:owner", content->owner);

    // dc:creator
    if (content->creator)
        cg_xml_node_setchildnode(con, "dc:creator", content->creator);

    // dc:artist
    if (content->artist)
        cg_xml_node_setchildnode(con, "dc:artist", content->artist);

    // dc:album
    if (content->album)
        cg_xml_node_setchildnode(con, "dc:album", content->album);

    // dc:genre
    if (content->genre)
        cg_xml_node_setchildnode(con, "dc:genre", content->genre);

    // dc:description
    if (content->description)
        cg_xml_node_setchildnode(con, "dc:description", content->description);

    // dc:producer
    if (content->producer)
        cg_xml_node_setchildnode(con, "dc:producer", content->producer);

    // dc:rating
    if (content->rating)
        cg_xml_node_setchildnode(con, "dc:rating", content->rating);

    // dc:album_art
    if (content->album_art)
        cg_xml_node_setchildnode(con, CG_UPNPAV_CONTENT_UPNPALBUMARTURI, content->album_art);


}




void cupnp_add_container(CgUpnpAvContentList *parentCon,
                         upnp_root_t *root,
                         upnp_content_t *content,
                         char *path)
{
	CgUpnpAvContent *con;
	CgString *fullPathStr;
    char *encname = NULL;

    fullPathStr = cg_string_new();
    cg_string_addvalue(fullPathStr, root->hash_str);
    cg_string_addvalue(fullPathStr, ":");

    if (path) {
        cg_string_addvalue(fullPathStr, path);
        if (strcmp("/", path))
            cg_string_addvalue(fullPathStr, "/");
        cg_string_addvalue(fullPathStr, content->title);
    } else {
        cg_string_addvalue(fullPathStr, "/");
    }

	encname = umisc_url_encode((char *)cg_string_getvalue(fullPathStr));

    debugf("[cupnp] adding '%s' ('%s' -> enc '%s') with parentID '%s'\n",
           content->title,
           path ? path : "",
           //(char *)cg_string_getvalue(fullPathStr),
		   encname,
           cg_upnpav_content_getid(parentCon));

    con = cg_upnpav_content_new();

    //cg_upnpav_content_setid(con, (char *)cg_string_getvalue(fullPathStr));
    cg_upnpav_content_setid(con, encname);
    //printf("content_setid('%s')\n", (char *)cg_string_getvalue(fullPathStr));
    debugf("[cupnp] content_setid('%s')\n", encname);

    // VLC requires childCount, even though it is optional.
    // Alas, Softmedia player on Android, then ASSUMES sub-directories only
    // have 1 entry. So we either have to dive into it, or, only send it
    // for VLC.
    if (conf_fixup_childCount)
        cg_xml_node_setattribute(con, CG_UPNPAV_CONTENT_CHILDCOUNT, "1");


    /* title */
    cg_upnpav_content_settitle(con, cg_strdup(content->title));
    debugf("[cupnp] content_settitle('%s')\n", content->title);

    /* parent id */
    if (path && !strcmp("/", path)) {
        cg_upnpav_content_setparentid(con,
                                          CG_UPNPAV_ROOT_CONTENT_ID);
        debugf("[cupnp] content_setparentid('%s')\n",
               CG_UPNPAV_ROOT_CONTENT_ID);

    } else {

        cg_upnpav_content_setparentid(con,
                                          cg_upnpav_content_getid(parentCon));
        debugf("[cupnp] content_setparentid('%s')\n",
               cg_upnpav_content_getid(parentCon));
    }

    // object.container.storageFolder
    /* type */
    cg_upnpav_content_settype(con, 	CG_UPNPAV_CONTENT_CONTAINER );

    /* upnp:class */

    cg_upnpav_content_setupnpclass(con, CG_UPNPAV_UPNPCLASS_CONTAINER);
    //cg_upnpav_content_setupnpclass(con, "object.container.storageFolder");

    //cg_upnpav_dms_filesys_content_setpublicdirectory(con,
	//cg_string_getvalue(fullPathStr));

    cupnp_add_metadata(con, content);

    //*** cg_upnpav_dms_filesys_content_setpublicdirectory(con,
    //                                                encname);

    // Set restricted
    cg_xml_node_setattribute(con, CG_UPNPAV_CONTENT_RESTRICTED, "1");
    //cg_xml_node_setchildnode(con, "dc:creator", "UNKNOWN");

    //printf("content_setpublicdirectory('%s')\n",
	//     cg_string_getvalue(fullPathStr));
    //debugf("[cupnp] content_setpublicdirectory('%s')\n",
    //      encname);


    // This adds it to the dir we are listing, say mediaServer->0->$new
    // and we can also add it to mediaServer->$new
    cg_upnpav_content_addchildcontent(parentCon, con);

#if 0
    cg_upnpav_content_setparentid(con,
                                  cg_upnpav_content_getid(parentCon));
    printf("re-set content_setparentid('%s')\n",
           cg_upnpav_content_getid(parentCon));
#endif

    cg_string_delete(fullPathStr);
    SAFE_FREE(encname);
}



void cupnp_add_content(CgUpnpAvContentList *parentCon,
                       upnp_root_t *root,
                       upnp_content_t *content,
                       char *path)
{
	CgUpnpAvContent *con;
	CgString *fullPathStr;
    CgNetworkInterfaceList *netIfList;
    CgNetworkInterfaceList *netIf;
#if 0
    char dlnaAttr[CG_UPNPAV_DLNAATTR_MAXLEN];
#endif
    CgUpnpAvResource *res;
    char resURL[CG_UPNP_MEDIA_FILESYS_RESURL_MAXLEN];
	char *encname;

    netIfList = cg_net_interfacelist_new();
    cg_net_gethostinterfaces(netIfList);
    netIf = cg_net_interfacelist_gets(netIfList);
    if (!netIf) {
        cg_net_interfacelist_delete(netIfList);
        return;
    }

    fullPathStr = cg_string_new();
    cg_string_addvalue(fullPathStr, root->hash_str);
    cg_string_addvalue(fullPathStr, ":");

    if (path) {
        cg_string_addvalue(fullPathStr, path);
        if (strcmp("/", path))
            cg_string_addvalue(fullPathStr, "/");
        cg_string_addvalue(fullPathStr, content->title);
    } else {
        cg_string_addvalue(fullPathStr, "/");
    }

	encname = umisc_url_encode((char *)cg_string_getvalue(fullPathStr));

    debugf("[cupnp] adding '%s' ('%s' -> '%s') with parentID '%s'\n",
           content->title,
           path ? path : "",
           encname,
           cg_upnpav_content_getid(parentCon));

    con = cg_upnpav_content_new();

    cg_upnpav_content_setid(con, encname);
    debugf("[cupnp] content_setid('%s')\n", encname);

    /* title */
    cg_upnpav_content_settitle(con, cg_strdup(content->title));
    debugf("[cupnp] content_settitle('%s')\n", content->title);

    /* parent id */
    if (path && !strcmp("/", path)) {
        cg_upnpav_content_setparentid(con,
                                          CG_UPNPAV_ROOT_CONTENT_ID);
        debugf("[cupnp] content_setparentid('%s')\n",
               CG_UPNPAV_ROOT_CONTENT_ID);

    } else {

        cg_upnpav_content_setparentid(con,
                                          cg_upnpav_content_getid(parentCon));
        debugf("[cupnp] content_setparentid('%s')\n",
               cg_upnpav_content_getid(parentCon));
    }

    /* type */
    cg_upnpav_content_settype(con, CG_UPNPAV_CONTENT_ITEM);

    /* upnp:class */
    //cg_upnpav_content_setupnpclass(con, CG_UPNPAV_UPNPCLASS_MOVIE);
    cg_upnpav_content_setupnpclass(con, umime_class(mime_type(content->title)));

    res = cg_upnpav_resource_new();

    // If the URL is already set by the lower layer, we trust it implicitly
    // if not set, we handle it internally.
    if (content->url) {

        cg_upnpav_resource_seturl(res, content->url);

    } else {
        char *url;

        snprintf(resURL, sizeof(resURL),
                 "http://%s:%d/%s",
                 cg_net_interface_getaddress(netIf),
                 upnp_http_port,
                 CG_UPNP_MEDIA_FILESYS_RESURL_PATH
                 );
        // Join the URL part, and %-encoded path here, so there
        // is no max-path limits
        url = umisc_strjoin(resURL, encname);
        cg_upnpav_resource_seturl(res, url);
		debugf("[cupnp] seturl('%s')\n",
			   url);

        SAFE_FREE(url);
        //cg_upnpav_resource_setmimetype(res, CG_UPNPAV_MIMETYPE_MPEG);
    }

	debugf("[cupnp] class '%s' mime '%s'\n",
		   umime_class(mime_type(content->title)),
		   mime_type(content->title));

    // fill in mime type
    cg_upnpav_resource_setmimetype(res, mime_type(content->title));
#if 0
    cg_upnpav_resource_setdlnaattribute(res,
                                        cg_upnpav_resource_getdlnaattributes(
                                                                             res, dlnaAttr, sizeof(dlnaAttr)));
#endif

    // Alas, setsize() is 32bit only.
    //cg_upnpav_resource_setsize(res, (long)fileSize);
    if (content->size) {
        snprintf(resURL, sizeof(resURL),
                 "%llu",
                 content->size);
        cg_xml_node_setattribute(res,
                                 CG_UPNPAV_RESOURCE_PROTOCOLINFO_SIZE,
                                 resURL);
    }

    if (content->duration) {
        // 00:00:00.000
        unsigned long hours, mins, secs, msecs;
        unsigned long duration;

        duration = content->duration;

        hours = duration / ( 1000 * 60 * 60);
        duration -= (hours * 1000 * 60 * 60);
        mins = duration / ( 1000 * 60 );
        duration -= (mins * 1000 * 60 );
        secs = duration / ( 1000 );
        duration -= (secs * 1000 );
        msecs = duration;

        snprintf(resURL, sizeof(resURL),
                 "%02lu:%02lu:%02lu.%03lu",
                 hours, mins, secs, msecs);
        cg_xml_node_setattribute(res,
                                 CG_UPNPAV_RESOURCE_PROTOCOLINFO_DURATION,
                                 resURL);
    }

    if (content->resolution) {
        cg_xml_node_setattribute(res,
                                 CG_UPNPAV_RESOURCE_PROTOCOLINFO_RESOLUTION,
                                 content->resolution);
    }


    cg_upnpav_content_addresource(con, res);


    // *** cg_upnpav_dms_filesys_content_setpublicdirectory(con,
    //											 encname);

    debugf("[cupnp] content_encname('%s')\n",
           encname);


    cupnp_add_metadata(con, content);

    cg_upnpav_content_addchildcontent(parentCon, con);

#if 0
    cg_upnpav_content_setparentid(con,
                                  cg_upnpav_content_getid(parentCon));
    printf("re-set content_setparentid('%s')\n",
           cg_upnpav_content_getid(parentCon));
#endif


    cg_string_delete(fullPathStr);
	SAFE_FREE(encname);
    cg_net_interfacelist_delete(netIfList);
}



void cg_upnpav_content_updateid(CgUpnpAvServer *dms, char *conID)
{
    char buffer[100];
    char *var, *id, *count;
    CgStringTokenizer *tok;
    CgString *new_var;
    CgUpnpStateVariable *stateVar;
    int first = 1, found = 0;
    CgUpnpDevice *dev;
    CgUpnpAvContent *content;

    if (!conID) return;
    content = cg_upnpav_dms_findcontentbyid(dms,
                                            conID);

    if (!content) return;

    debugf("[cupnp] Invalidating cache for '%s'\n",
           conID);

    dev = cg_upnpav_dms_getdevice(dms);

#if 0
    attr = cg_xml_node_getchildnodevalue(content, "ver");
    if (!attr) {
        vers = 0;
    } else {
        vers = strtoul(attr, NULL, 10);
        vers++;
    }
    // Change var to string
    snprintf(buffer, sizeof(buffer), "%"PRIu64, vers);
#else
    cupnp_update_version++;
    // Change var to string
    snprintf(buffer, sizeof(buffer), "%"PRIu64, cupnp_update_version);
#endif


#if 0
    cg_xml_node_setchildnode(content,
                             "ver",
                             buffer);
#endif

    // Get the current value of SystemUpdateIDs
    stateVar = cg_upnp_device_getstatevariablebyname(dev,
                                                     "ContainerUpdateIDs");

    if (!stateVar) return;

    var = cg_upnp_statevariable_getvalue(stateVar);

    debugf("[cupnp] before value '%s'\n", var);


    // Allocate string to hold new value
    new_var = cg_string_new();

    // If we are already in the list, we replace instead of add.
    if (var) {
        tok = cg_string_tokenizer_new(var, ",");
        while (cg_string_tokenizer_hasmoretoken(tok) == TRUE) {
            id = cg_string_tokenizer_nexttoken(tok);
            if (cg_string_tokenizer_hasmoretoken(tok) == FALSE) break;
            count = cg_string_tokenizer_nexttoken(tok);

            if (!cg_streq(id, conID)) {
                // If not our ID, just add it back in
                if (!first)
                    cg_string_addvalue(new_var, ",");
                cg_string_addvalue(new_var, id);
                cg_string_addvalue(new_var, ",");
                cg_string_addvalue(new_var, count);
            } else {
                // If our ID, add in new count
                if (!first)
                    cg_string_addvalue(new_var, ",");
                cg_string_addvalue(new_var, id);
                cg_string_addvalue(new_var, ",");
                cg_string_addvalue(new_var, buffer);
                found=1;
            }
            first = 0;
        } // while has tokens
        cg_string_tokenizer_delete(tok);

        // If our ID was not in there, we add it.
        if (!found) {
            if (!first)
                cg_string_addvalue(new_var, ",");
            cg_string_addvalue(new_var, conID);
            cg_string_addvalue(new_var, ",");
            cg_string_addvalue(new_var, buffer);
        }

    } else {
        // var is not set, just add our new id
        cg_string_addvalue(new_var, conID);
        cg_string_addvalue(new_var, ",");
        cg_string_addvalue(new_var, buffer);
    }

    debugf("[cupnp] after value '%s'\n", (char *)cg_string_getvalue(new_var));

    cg_upnp_statevariable_setvalue(stateVar,  (char *)cg_string_getvalue(new_var));

    cg_string_delete(new_var);

    // We have updated a directory, also increment the SystemID
    cg_upnpav_dms_condir_updatesystemupdateid(dms);

}


#define NUM_CONTENTS 100


#if 0
CgUpnpAvContent *rewt = NULL;


BOOL printme(CgUpnpAvContent *con, void *userData)
{
    printf("   '%s' (%s): %p\n",
           cg_upnpav_content_getid(con),
           cg_upnpav_content_gettitle(con),
           con);
    return FALSE;
}

void list_tree(CgUpnpAvContent *con, int indent)
{

    if (!rewt && con) rewt = con;
    if (!con) con = rewt;

    printf("Starting from '%s' ('%s') which has %u children\n",
           cg_upnpav_content_getid(con),
           cg_upnpav_content_gettitle(con),
           cg_upnpav_content_getnchildcontents(con));

    cg_upnpav_content_findbyuserfunc(con, printme, NULL);

    printf("Finished\n");
}
#endif

void cg_upnpav_content_copy2(CgUpnpAvContent *destContent, CgUpnpAvContent *srcContent)
{
	CgXmlAttribute *attr;
	CgUpnpAvContent *destNode;
	CgUpnpAvContent *srcNode;
	CgUpnpAvContent *srcNode2;

	cg_xml_node_setname(destContent, cg_xml_node_getname(srcContent));
	cg_xml_node_setvalue(destContent, cg_xml_node_getvalue(srcContent));
	for (attr=cg_xml_node_getattributes(srcContent); attr; attr=cg_xml_attribute_next(attr))
		cg_xml_node_setattribute(destContent, cg_xml_attribute_getname(attr), cg_xml_attribute_getvalue(attr));

	for (srcNode=cg_xml_node_getchildnodes(srcContent); srcNode; srcNode=cg_xml_node_next(srcNode)) {

        for (srcNode2=cg_xml_node_getchildnodes(srcNode); srcNode2; srcNode2=cg_xml_node_next(srcNode2)) {
            debugf("  copy2 sub: srcNode2 name '%s'\n",
                   cg_xml_node_getname(srcNode2));

        }

#if 0
        if (cg_upnpav_content_haschildcontents(srcNode)) {
            debugf("  copy2: has children '%s'\n", cg_xml_node_getname(srcNode));
        } else {

        }
#endif


		if (cg_upnpav_content_gettype(srcNode) == CG_UPNPAV_CONTENT_NONE)
			continue;
		if (cg_streq(cg_xml_node_getname(srcNode), CG_UPNPAV_RESOURCE_NAME)) {
			destNode = cg_upnpav_resource_new();
			cg_upnpav_resource_copy(destNode, srcNode);
		}
		else {
			destNode = cg_upnpav_content_new();
			cg_xml_node_setname(destNode, cg_xml_node_getname(srcNode));
			cg_xml_node_setvalue(destNode, cg_xml_node_getvalue(srcNode));
			for (attr=cg_xml_node_getattributes(srcNode); attr; attr=cg_xml_attribute_next(attr))
				cg_xml_node_setattribute(destNode, cg_xml_attribute_getname(attr), cg_xml_attribute_getvalue(attr));
            cg_upnpav_content_copy(destNode, srcNode);
		}
		cg_xml_node_addchildnode(destContent, destNode);
	}
}


void cupnp_get_contents(CgUpnpAvServer *dms,
                        CgUpnpAvContentList *parentCon,
                        char *contentID)
{
    upnp_root_t *root;
    char *subID, *ID = NULL;
    char *decID = NULL;
	CgUpnpAvContent *con;
	//CgUpnpMediaFileSystemContentData *conData;
    upnp_content_t *content = NULL;
    unsigned int num_bufs;
    void *user_arg;
    int i, dirty;
    CgUpnpAvContent *childCon;
    CgUpnpAvContent *childCon2;
    CgUpnpAvContent *next;
#if 0
    CgUpnpAvContent *last = NULL;
#endif
    CgUpnpAvContent *newCon = NULL;
    char *useragent = NULL;
    char *host = NULL;
    int numBefore;

    if (!cupnp) return;

    if (!cg_upnpav_content_iscontainernode(parentCon)) {
        debugf("[cupnp] Peculiar, listing of FILEITEM requested?\n");
    }

    newCon = cg_upnpav_content_new();
    if (!newCon) return;

    debugf("[cupnp] Populating new contents of '%s' %p...\n",
           cg_upnpav_content_getid(parentCon), parentCon);

    // We need to invalidate the cache at the bottom of this function too.
#if 0
    {

        copyCon = cg_upnpav_content_new();
        cg_upnpav_content_copy(copyCon, parentCon);
        for (con=cg_upnpav_content_getchildcontents(parentCon); con; con=cg_upnpav_content_next(con)) {
#if 0
            void *conData;

            conData =cg_upnpav_content_getuserdata(con);
            if (!conData)
                continue;
            //cg_upnpav_dms_filesys_content_data_delete(conData);
            free(conData);
            cg_upnpav_content_setuserdata(con, NULL);
#endif
        }
        cg_upnpav_content_clearchildcontents(parentCon);

        // Copy everything back, but without the children
        cg_upnpav_content_copy(parentCon, copyCon);
        cg_upnpav_content_delete(copyCon);

    }
#endif

    if (!contentID) return;

    // Allocate content buffer
    content = (upnp_content_t *) calloc(NUM_CONTENTS, sizeof(upnp_content_t));
    if (!content) return;

    // Set UserAgent if we can
    host = cupnp_get_host(cg_thread_self());
    useragent = cupnp_get_useragent(cg_thread_self());

    debugf("[cupnp] filling contents for '%s' (User-Agent: '%s': '%s')\n",
           contentID,
           useragent ? useragent : "",
           host ? host : "");

    //debugf("[cupnp] self %p\n", cg_thread_self());

    // Root is special case!
    if (!strcmp("0", contentID)) {
        //CG_UPNP_MEDIA_ROOT_CONTENT_ID
        for (root = upnp_root_list;
             root;
             root = root->next) {

            // Virtual sub-directory, or listed in root?
            if (root->name) {
                upnp_content_t con;

                memset(&con, 0, sizeof(con));
                con.title = root->name;
                con.date = 0;
                cupnp_add_container(newCon, root, &con, NULL);


            } else {

                debugf("[cupnp] listing root '%s'\n",
                       root->path);

                num_bufs = NUM_CONTENTS;
                user_arg = NULL;

                while( root->callback(root->path,
                                      "/",
                                      content,
                                      &num_bufs,
                                      &user_arg,
                                      useragent, host) == 1) { // 1 = iterate

                    for (i = 0; i < num_bufs; i++) {

                        debugf("[cupnp] callback returned: '%s'\n", content[i].title );
                        if (content[i].type == UPNP_CONTENT_CONTAINER)
                            cupnp_add_container(newCon, root, &content[i],
                                                 "/");
                        else
                            cupnp_add_content(newCon, root, &content[i],
                                               "/");

                    } // for all bufs

                    // Free contents
                    for (i = 0; i < num_bufs; i++) {
                        cupnp_release_content_node(&content[i]);
                    }

                    num_bufs = NUM_CONTENTS;
                } // while iterate

            } // name == NULL

        } // for all

        //cg_upnpav_dms_condir_updatesystemupdateid(dms);
        goto success;

    } // if 0


    if ((strlen(contentID) < 9) ||
        (contentID[8] != ':')) {

        goto failure;

    }

    //Looking for content '6f49829c:'
    // 6f49829c:
    // Find the root
    ID = strdup(contentID);
    ID[8] = 0;
    subID = &ID[9];

    root = cupnp_findbyhash(strtoul(ID, NULL, 16));

    if (!root) {
        debugf("[cupnp] Could not find root for hash '%s' (ID '%s')\n",
               contentID, ID);
        goto failure;
    }

    decID = umisc_url_decode(subID);

    debugf("[cupnp] listing root '%s' for subID '%s' ('%s')\n",
           root->name ? root->name : root->path, subID, decID);

    num_bufs = NUM_CONTENTS;
    user_arg = NULL;

    while( root->callback(root->path,
                          decID ? decID : "/",
                          content,
                          &num_bufs,
                          &user_arg,
                          useragent, host) == 1) { // 1 is keep iterating

        for (i = 0; i < num_bufs; i++) {

            debugf("[cupnp] callback returned: '%s'\n", content[i].title );
            if (content[i].type == UPNP_CONTENT_CONTAINER)
                cupnp_add_container(newCon, root, &content[i], decID);
            else
                cupnp_add_content(newCon, root, &content[i], decID);

        }
        // Free contents.
        for (i = 0; i < num_bufs; i++) {
            cupnp_release_content_node(&content[i]);
        }

        num_bufs = NUM_CONTENTS;
    }

    //cg_upnpav_dms_condir_updatesystemupdateid(dms);


    /* Fall-through */
 success:

    dirty = 0;

    // For all directories in "newCon", find them in "parentCon",
    // and if they have children, copy over...
    debugf("   ** looking to remove entries...\n");
    numBefore = cg_upnpav_content_getnchildcontents(parentCon);

    for (childCon=cg_upnpav_content_getchildcontents(parentCon);
         childCon;
         childCon=next) {

        next = cg_upnpav_content_next(childCon);
        childCon2 = cg_upnpav_content_getbyid(newCon,
                                              cg_upnpav_content_getid(childCon));
        if (!childCon2) {
            dirty = 1;
            debugf("  removing '%s'\n",
                   cg_upnpav_content_getid(childCon));
            cg_upnpav_content_remove(childCon);
        }
    }

    debugf("   ** looking to add entries...\n");
    for (childCon=cg_upnpav_content_getchildcontents(newCon);
         childCon;
         childCon=cg_upnpav_content_next(childCon)) {


        childCon2 = cg_upnpav_content_getbyid(parentCon,
                                              cg_upnpav_content_getid(childCon));
        if (!childCon2) {
            dirty = 1;
            debugf("  adding '%s'\n",
                   cg_upnpav_content_getid(childCon));
            con = cg_upnpav_content_new();
            cg_upnpav_content_copy(con, childCon);
            cg_upnpav_content_addchildcontent(parentCon, con);
        }

    }

    // Delete the newly listed node.
    cg_upnpav_content_delete(newCon);

    // If this was first time we listed this directory, we do not consider it
    // changed.
    if (!numBefore)
        dirty = 0;

    // If changed, make events fly!
    if (dirty)
        cg_upnpav_content_updateid(dms, contentID);


    //list_tree(cg_upnpav_dms_getrootcontent(dms), 0);


    /* Fall-through */
 failure:
    SAFE_FREE(content);
    SAFE_FREE(decID);
    SAFE_FREE(ID);
    SAFE_FREE(useragent);
    SAFE_FREE(host);

    debugf("[cupnp] List done.\n");

}



//
// Only thing we have to tie a user-agent to is the thread id
// which is not exactly guaranteed to be unique.
//
void cupnp_add_useragent(void *id, char *useragent, char *host)
{
    useragent_t *node;

    if (!id || !useragent || !host) return;

    node = calloc(1, sizeof(*node));
    if (!node) return;

    node->id = id;
    node->useragent = strdup(useragent);
    node->host = strdup(host);
    node->time = time(NULL);

    // List becomes ordered by time, which is nice.
    cg_mutex_lock(useragent_mutex);
    node->next = useragent_head;
    useragent_head = node;
    cg_mutex_unlock(useragent_mutex);

}


char *cupnp_get_useragent(void *id)
{
    useragent_t *node, *prev, *next;
    char *result = NULL;
    time_t now;

    now = time(NULL);

    cg_mutex_lock(useragent_mutex);
    for (node = useragent_head, prev = NULL;
         node;
         node = next) {

        next = node->next;

        // Perfect match, remember the result to return
        if (id == node->id) {
            result = node->useragent;
            node->useragent = NULL;  // we return it, and will be freed by caller.
        }

        if (!node->useragent || // If it was matched above
            (node->time < (now - USERAGENT_TIMEOUT))) {  // or it is too old
            // We could make it more efficient, since if we hit a too old
            // node, all the rest of the nodes can be deleted too (same or older)
            SAFE_FREE(node->useragent);
            SAFE_FREE(node->host);
            if (!prev)
                useragent_head = node->next;
            else
                prev->next = node->next;

            SAFE_FREE(node);
        } else { // deletenode, when we remove nodes, prev dont move.
            prev = node;
        }

    } // for all
    cg_mutex_unlock(useragent_mutex);
    return result;
}


char *cupnp_get_host(void *id)
{
    useragent_t *node;
    char *result;

    cg_mutex_lock(useragent_mutex);
    for (node = useragent_head;
         node;
         node = node->next) {

        // Perfect match, remember the result to return
        if (id == node->id) {
            cg_mutex_unlock(useragent_mutex);
            result = node->host;
            node->host = NULL;
            return result;
        }

    } // for all
    cg_mutex_unlock(useragent_mutex);
    return NULL;
}


void cupnp_register_protocolinfo(void)
{
    char *protocols[] = {
        "http-get:*:*:*",
        "http-get:*:video/*:*",
        "http-get:*:audio/*:*",
        "http-get:*:image/*:*",
#if 1
        "http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM;DLNA.ORG_FLAGS=8cf00000000000000000000000000000",
        "http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED;DLNA.ORG_FLAGS=8cf00000000000000000000000000000",
        "http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG;DLNA.ORG_FLAGS=8cf00000000000000000000000000000",
#endif
        "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:audio/L16:DLNA.ORG_PN=LPCM;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_HD_24_AC3_ISO;SONY.COM_PN=AVC_TS_HD_24_AC3_ISO;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_24_AC3;SONY.COM_PN=AVC_TS_HD_24_AC3;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_24_AC3_T;SONY.COM_PN=AVC_TS_HD_24_AC3_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_PS_PAL;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_PS_NTSC;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_50_L2_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_60_L2_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_EU_ISO;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_EU;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_EU_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_50_AC3_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_50_L2_ISO;SONY.COM_PN=HD2_50_ISO;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_60_AC3_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_60_L2_ISO;SONY.COM_PN=HD2_60_ISO;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_50_L2_T;SONY.COM_PN=HD2_50_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_60_L2_T;SONY.COM_PN=HD2_60_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_HD_50_AC3_ISO;SONY.COM_PN=AVC_TS_HD_50_AC3_ISO;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_50_AC3;SONY.COM_PN=AVC_TS_HD_50_AC3;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_HD_60_AC3_ISO;SONY.COM_PN=AVC_TS_HD_60_AC3_ISO;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_60_AC3;SONY.COM_PN=AVC_TS_HD_60_AC3;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_50_AC3_T;SONY.COM_PN=AVC_TS_HD_50_AC3_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_HD_60_AC3_T;SONY.COM_PN=AVC_TS_HD_60_AC3_T;DLNA.ORG_FLAGS=8d700000000000000000000000000000",
        "http-get:*:video/x-mp2t-mphl-188:*",
        //
        "http-get:*:video/mp4:DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000",
        NULL };
    CgUpnpAvProtocolInfo *info;
    char *str, *network, *mime, *extra;
    int i;
    for (i = 0; protocols[i]; i++) {

        str = strdup(protocols[i]);
        if (!str) continue;
        network = strchr(str, ':');
        if (network) {
            *network = 0;
            network++;
            mime = strchr(network, ':');
            if (mime) {
                *mime = 0;
                mime++;
                extra = strchr(mime, ':');
                if (extra) {
                    *extra = 0;
                    extra++;

                    if ((info = cg_upnpav_protocolinfo_new())) {
                        cg_upnpav_protocolinfo_setprotocol(info, str);
                        cg_upnpav_protocolinfo_setnetwork(info, network);
                        cg_upnpav_protocolinfo_setmimetype(info, mime);
                        cg_upnpav_protocolinfo_setadditionainfo(info, extra);
                        cg_upnpav_dms_addprotocolinfo(mediaServer, info);
                        debugf("[cupnp] register %s:%s:%s:%s\n",
                               str, network, mime, extra);
                    }
                }
            }
        }
        free(str);
    }
}


void cupnp_add_icon(char *url, char *mime, char *size)
{
    CgUpnpIcon *icon;
    CgXmlNode *iconNode;

    iconNode = cg_xml_node_new();
    icon = cg_upnp_icon_new();
    cg_xml_node_setname(iconNode, CG_UPNP_ICON_ELEM_NAME);
    cg_upnp_icon_seticonnode(icon, iconNode);
    cg_upnp_icon_setmimetype(icon, mime);
    cg_upnp_icon_setwidth(icon, size);
    cg_upnp_icon_setheight(icon, size);
    cg_upnp_icon_setdepth(icon, "24");
    cg_upnp_icon_seturl(icon, url);
    cg_upnp_device_addicon(cg_upnpav_dms_getdevice(mediaServer), icon);
    cg_xml_node_delete(iconNode);
    cg_upnp_icon_delete(icon);
}

void cupnp_init_icons(void)
{

    cupnp_add_icon("/icon/llink-48x48.jpg", "image/jpg", "48");
    cupnp_add_icon("/icon/llink-48x48.png", "image/png", "48");
    cupnp_add_icon("/icon/llink-120x120.jpg", "image/jpg", "120");
    cupnp_add_icon("/icon/llink-120x120.png", "image/png", "120");

}
#endif   // HAVE_CLINKC



int cupnp_init(void)
{
    char name[256];

    cupnp = 0;

    if (ssdp_upnp != 1) return 1;

#if HAVE_CLINKC

    //    cg_log_add_target( "stdout", SEV_INFO | SEV_DEBUG_L1 | SEV_DEBUG_L2 | SEV_DEBUG_L3 | SEV_DEBUG_L4);

	ctrlPoint = cg_upnp_controlpoint_new();

    if (!ctrlPoint) {
        debugf("[cupnp] failed to start UPNP\n");
        return 0;
    }

    // Move this to upnpInit(argument) ? It doesn't need changing, but more
    // options the merrier.
    cg_upnp_controlpoint_seteventport(ctrlPoint, 39600);

    // Do we need this?
    cg_upnp_controlpoint_start(ctrlPoint);

    cg_net_allow_interface("en0");

    mediaServer = cg_upnpav_dms_new();
    if (!mediaServer)
        return 0;

    cg_upnp_device_sethttpport(cg_upnpav_dms_getdevice(mediaServer),
                               ssdp_upnp_port);

    cg_upnpav_dms_setactionlistener(mediaServer,
                                    cg_upnpav_dms_filesys_actionlistner);

    cg_upnpav_dms_sethttplistener(mediaServer,
                                  cg_upnpav_dms_filesys_http_listener);

    //cg_upnpav_dms_filesys_setpublicationdirectory(mediaServer, "NotUsed");

    snprintf(name, sizeof(name),
             "%s UPNP Media Server (MediaServer) : 1",
             conf_announcename);
    cg_upnpav_dms_setfriendlyname(mediaServer,
                                  name);
    cg_upnpav_dms_setudn(mediaServer,
                         ssdp_upnp_uuid);

    useragent_mutex = cg_mutex_new();

    cupnp_init_icons();

    cupnp_register_protocolinfo();

    // We will use upnp by setting the URL of content, and ignoring the
    // built in HTTP handler.
    cupnp_addroot("/",
                  NULL,
                  cupnp_callback,
                  NULL, //open
                  NULL, //seek
                  NULL, //read
                  NULL);//close

	// Set initialised, before we start the threads, since they call listing.
    cupnp = 1;

    cg_upnpav_dms_start(mediaServer);

    debugf("[cupnp] started and registered UPNP MediaServer\n");

    upnp_http_port = cg_upnp_device_gethttpport(cg_upnpav_dms_getdevice(mediaServer));
    debugf("[cupnp] UPNP HTTP port %u\n", upnp_http_port);


    {
        CgThread *contThread;

        // Spawn a thread to look for changes in directories already cached
        contThread = cg_thread_new();
        cg_thread_setaction(contThread, cupnp_ServerUpdateContainers);
        cg_thread_setuserdata(contThread, mediaServer);

        if (cg_thread_start(contThread) == FALSE) {
            cg_thread_delete(contThread);
            contThread = NULL;
            // To be fair, only the events for container changes would not
            // work, but the rest ok. But then, we dont expect it to fail to
            // start a thread either.
            return -10;
        }
    }
#ifdef CLINKC_WITH_RENDERER
    mediaRenderer = cg_upnpav_dmr_new();
    if (!mediaRenderer)
        return -9;

    snprintf(name, sizeof(name),
             "%s UPNP Media Renderer (MediaRenderer) : 1",
             conf_announcename);
    cg_upnpav_dms_setfriendlyname(mediaRenderer,
                                  name);
    cg_upnpav_dms_setudn(mediaRenderer,
                         "E25641A0-8228-11DE-AFB8-1232A5D5C51B");

	sleep(1);
    cg_upnpav_dmr_start(mediaRenderer);

    debugf("[cupnp] started and registered UPNP MediaRenderer\n");

#endif // RENDERER

    cg_upnp_controlpoint_search(ctrlPoint, UPNPAVDUMP_DMS_DEVICETYPE);

#endif
    return 1;
}



void cupnp_free(void)
{

#if HAVE_CLINKC
    if (!cupnp) return;

    // turn it off real early, so that any requests coming in will be ignored.
    cupnp = 0;

    debugf("[cupnp] sending bye packets, this takes a while...\n");

#ifdef CLINKC_WITH_RENDERER
    if (mediaRenderer) {
        // Stop is slow, lets ignore it.
        cg_upnpav_dmr_stop(mediaRenderer);
        cg_upnpav_dmr_delete(mediaRenderer);
        mediaRenderer = NULL;
    }
#endif // RENDERER

    cg_mutex_delete(useragent_mutex);
    useragent_mutex = NULL;

    cupnp_delroot("/");

    // Stop is slow, lets ignore it.
    cg_upnpav_dms_stop(mediaServer);
    cg_upnpav_dms_delete(mediaServer);
    mediaServer = NULL;

    if (ctrlPoint) {
        cg_upnp_controlpoint_stop(ctrlPoint);
    }

    ctrlPoint = NULL;




#endif

}


#if HAVE_CLINKC

struct event_thread_struct {
    time_t now;
    CgUpnpAvServer *dms;
};

BOOL cupnp_events_recurse(CgUpnpAvContent *con, void *userData)
{
    time_t old;
    struct event_thread_struct *event_data = (struct event_thread_struct *) userData;

    if (cg_upnpav_content_iscontainernode(con)) {
        char *attr;

        attr = cg_xml_node_getchildnodevalue(con, CG_UPNPAV_OBJECT_DATE);

        if (attr) {
            char *r;

#if 0
            debugf("   '%s' (%s): %s\n",
                   cg_upnpav_content_getid(con),
                   cg_upnpav_content_gettitle(con),
                   attr);
#endif

            r = strchr(cg_upnpav_content_getid(con), ':');
            if (r) {
                request_t *node = NULL;
                node = request_newnode();
                if (node) {
                    node->path = strdup(&r[1]);
                    if (!root_setpath(node)) {

                        old = misc_str2date(attr);

                        if ((old > 0) &&
                            (node->stat.st_mtime >= event_data->now) &&
                            (node->stat.st_mtime > old) &&
                            ((node->stat.st_mtime - old) > CUPNP_UPDATE_NEWER)) {

#if 0
                            debugf("   '%s' (%s): %s\n",
                                   cg_upnpav_content_getid(con),
                                   cg_upnpav_content_gettitle(con),
                                   attr);
                            debugf("  stat says %s\n",
                                   misc_datestr(node->stat.st_mtime));
#endif

                            cg_xml_node_setchildnode(con,
                                                     CG_UPNPAV_OBJECT_DATE,
                                                     misc_datestr(node->stat.st_mtime));
                            debugf("'%s' is NEWER! (%ld : %ld)\n",
                                   cg_upnpav_content_getid(con),
                                   old,
                                   (node->stat.st_mtime - old));


                            cg_upnpav_content_updateid(event_data->dms,
                                                       cg_upnpav_content_getid(con));

                        } // newer

                    }  // setpath

                    request_freenode(node);

                } // node

            } // r


        } //attr


    } // is container

    return FALSE;
}


//
// Thread that periodically checks all Containers in the known cache
// and stat the directories to check for updates. This is so we can
// send Events with ContainerUpdateIDs. (For example, XBMC needs this)
//
void cupnp_ServerUpdateContainers(CgThread *thread)
{
    struct event_thread_struct event_data;
    CgUpnpStateVariable *stateVar;
    char *var;

    event_data.dms = (CgUpnpAvServer *)cg_thread_getuserdata(thread);

    if (!event_data.dms) return;

    stateVar = cg_upnp_device_getstatevariablebyname(cg_upnpav_dms_getdevice(event_data.dms),
                                                     "ContainerUpdateIDs");
    if (!stateVar) return;

    while (cg_thread_isrunnable(thread) == TRUE) {
        //debugf("[thread] sleeping...\n");

        cg_wait(CUPNP_UPDATE_SLEEP);

        time(&event_data.now);

        if ((event_data.now - request_get_idle()) > CUPNP_UPDATE_IDLE) continue;

        // We don't care about directories that are "old", since clearly
        // they did not update. (We assume updating a container makes it
        // the current time.)
        event_data.now -= 60 * 60 * 24;

        // Clear statevariable (assuming we sent it during the sleeping time)
        cg_upnp_statevariable_setvaluewithoutnotify(stateVar,  "");

        // If the mediaServer has been idle for long time, stop stating
        // everything all the time. A listing, or GET, will wake it up.

        // Iterate all cache objects
        cg_upnpav_content_findbyuserfunc(cg_upnpav_dms_getrootcontent(event_data.dms),
                                         cupnp_events_recurse,
                                         (void *)&event_data);
        var = cg_upnp_statevariable_getvalue(stateVar);

    }
}


#endif // CLINKC
