#if HAVE_CONFIG_H
#include <config.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_PWD_H
#include <pwd.h>
#endif

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"
#include "dirlist.h"

#define DEBUG_FLAG 32
#include "debug.h"
#include "root.h"
#include "conf.h"
#include "parser.h"
#include "request.h"
#include "skin.h"

#if WIN32
#include "win32.h"
#endif



static root_t           *root_array       = NULL;
static int               root_count       = 0;

static unsigned int      root_secret      = 0;

struct root_register_struct {
	unsigned long ip;
	time_t time;
	struct root_register_struct *next;
};

typedef struct root_register_struct root_register_t;

static root_register_t *root_register_head = NULL;
static unsigned int root_registered_num = 0;
#define ROOT_REGISTERED_MAX 10
static char *root_registered_patterns[ROOT_REGISTERED_MAX] = { NULL };



static int root_handler(lion_t *handle, void *user_data,
						int status, int size, char *line);
static int root_zfs_handler(lion_t *handle, void *user_data,
                            int status, int size, char *line);



int root_init(void)
{

	if (dirlist_init( 1 )) {
		debugf("[root] failed to initialise dirlist\n");
		return -1;
	}

	return 0;
}



void root_free(void)
{
	int i;

	// Free the match list, if any.
	for (i = 0; i < root_count; i++) {
		SAFE_FREE(root_array[i].path);
		SAFE_FREE(root_array[i].http_host);
		SAFE_FREE(root_array[i].http_file);
		SAFE_FREE(root_array[i].url);
	}

	SAFE_FREE(root_array);
	root_count = 0;

	dirlist_free();

}



char *root_getroot(int index)
{

	if ((index < 0) || (index >= root_count)) return NULL;

	return root_array[index].path;

}


int root_is_dvdread(int index)
{

	if ((index < 0) || (index >= root_count)) return 0;

	return root_array[index].dvdread;

}






//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : ROOT
// Required : path
// Optional : flags, http, proxy, subdir, dvdread
//
void root_cmd(char **keys, char **values,
			  int items,void *optarg)
{
	char *path, *flags, *http, *proxy, *subdir, *dvdread, *zfs, *url;
	root_t *tmp;

	// Requireds
	path     = parser_findkey(keys, values, items, "PATH");

	if (!path || !*path) {
		printf("      : [root] missing required field: PATH\n");
		//do_exit = 1;
		return;
	}

	// Optionals
	flags     = parser_findkey(keys, values, items, "FLAGS");
	http      = parser_findkey(keys, values, items, "HTTP");
	proxy     = parser_findkey(keys, values, items, "PROXY");
	subdir    = parser_findkey(keys, values, items, "SUBDIR");
	dvdread   = parser_findkey(keys, values, items, "DVDREAD");
	zfs       = parser_findkey(keys, values, items, "ZFS");
	url       = parser_findkey(keys, values, items, "URL");

	if (zfs && *zfs) {
        char buffer[1024];
        lion_t *spawn;
        // Spawn zfs list to find any file-systems with set attribute.
        // In future, it would perhaps be nicer to use libzfs.
        debugf("      : looking for ZFS filesystems\n");
        snprintf(buffer, sizeof(buffer), "%s list -H -o mountpoint,%s",
                 path, zfs);
        spawn = lion_system(buffer, 0, LION_FLAG_FULFILL, zfs);
        if (spawn) lion_set_handler(spawn, root_zfs_handler);
        return; // Don't allocate new buffers.
    }

	tmp = (root_t *) realloc(root_array,
								   sizeof(root_t) *
								   (root_count + 1));

	if (!tmp) {
		debugf("      : out of memory allocating ROOT\n");
		return;
	}


	// Increase our counter
	root_count++;

	// Assign us in.
	root_array = tmp;

	// The new area
	tmp = &root_array[ root_count - 1 ]; // Nobody likes -1

	// Make it be cleared
	memset(tmp, 0, sizeof(*tmp));

#ifdef WIN32
	// Change \ to / cos its prettier and works in browsers.
	// If its c:/ make it "c:\"
	// remove / if it ends with a slash
	{
		char *r;
		while ((r = strchr(path, '\\')))
			*r = '/';
		// Just c:/ ?
		if (path[0] && (path[1] == ':') && (path[2] == '/') && !path[3])
			path[2] = '\\';
		misc_stripslash(path);
	}
#endif



	tmp->path = strdup(path);  // Do we need to strdup?


#if HAVE_PWD_H
    debugf("[root] '%s'\n", path);

    // Attempt to expand ~username, we only let the first character be ~, since
    // it does not make any sense to specify a path before it.
    if (path[0] == '~') {
        // Fetch out the username,if any. (~username/) or (~/) are valid.
        char *ar, *username;
        struct passwd *pw;

        ar = path;
        username = misc_digtoken(&ar, "/\r\n");

        // Skip all leading "~"s
        while(username && *username == '~') username++;

        if (!username || !*username) {
            // Look up current user then.
            pw = getpwuid(getuid());
        } else {
            // Look up by username
            pw = getpwnam(username);
        }

        // Lookup successful?
        if (pw) {
            SAFE_FREE(tmp->path);
            tmp->path = misc_strjoin(pw->pw_dir, ar);
            debugf("[root] expanded to '%s'\n",
                   tmp->path);
        } // pw
    }
#endif



	if (flags && *flags)
		tmp->flags = dirlist_a2f(flags);
	else
		tmp->flags = 0;

	// We no longer use the FLAGS part of ROOT. See skin_sort_*
	// Use 0 sorting for speed.
	tmp->flags = 0;
	// Things that are not allowed to be set.
	tmp->flags &= ~( DIRLIST_SHORT | DIRLIST_XML | DIRLIST_FILE
					 | DIRLIST_SHOW_DOT); // Used to hide parential dirs

	// Essential flags.
	tmp->flags |= DIRLIST_LONG | DIRLIST_PIPE;

	if (http && *http) {

		// root.http
		if (!strncasecmp("http://", http, 7)) {
			char *ar, *host, *port = "80";

			ar = &http[7];
			host = misc_digtoken(&ar, "/:\r\n");
			if (host) {
				// Optional port?
				if (misc_digtoken_optchar == ':')
					port = misc_digtoken(&ar, "/\r\n");
				if (!port || !*port)
					port = "80";

				tmp->http_host = strdup(host);
				tmp->http_port = atoi(port);
				tmp->http_file = strdup(ar);

			}

		}

	}

	if (url && *url) {
		if (!strncasecmp("http://", url, 7)) {
            tmp->url = strdup(url);
        }
    }


	if (proxy)
		tmp->proxy = 1;

	if (subdir)
		tmp->subdir = strdup(subdir);

	if (dvdread)
		tmp->dvdread = 1;



	debugf("      : [root] recognising '%s'\n",
		   tmp->path);

}






static void root_next_list(request_t *node, lion_t *handle)
{
	char *str;
    int flags;

	// More dirlistings?
	while(node->next_list < root_count) {

		if (root_array[node->next_list].http_host) {
			str = misc_strjoin(
							   "drwxr-xr-x 1 http http 0 Dec 15  2007 ",
							   root_array[node->next_list].path);
			if (str) {
                // strjoin adds a "/" which we don't like. blank it.
                if (str[38] == '/')
                    str[38] = ' ';
				skin_write_type(node, str);
            }
			SAFE_FREE(str);

			node->next_list++;
			continue;

		} // http type

		if (root_array[node->next_list].url) {
			str = misc_strjoin(
							   "drwxr-xr-x 1 http http 0 Dec 15  2007 ",
							   root_array[node->next_list].url);
			if (str) {
                // strjoin adds a "/" which we don't like. blank it.
                //if (str[38] == '/')
                //    str[38] = ' ';
                debugf("[root] url sending '%s'\n", str);
				skin_write_type(node, str);
            }
			SAFE_FREE(str);

			node->next_list++;
			continue;

		} // url type


		debugf("[root] listing '%s' with flags %x\n",
			   root_array[node->next_list].path,
			   root_array[node->next_list].flags);

		if (root_array[node->next_list].subdir) {

			str = misc_strjoin(
							   "drwxr-xr-x 1 sub dir 0 Dec 15  2007 ",
							   root_array[node->next_list].subdir);
			if (str)
				skin_write_type(node, str);
			SAFE_FREE(str);

			node->next_list++;
			continue;
		}

        // Some dirlist flags need to be passed down to libdirlist...
        flags = skin_get_sort_flags(node);
        flags &= (DIRLIST_SHOW_DIRSIZE|DIRLIST_SHOW_GENRE);

		dirlist_list(handle,
					 root_array[node->next_list].path,
					 NULL,
					 root_isregistered(node)
					 ? (root_array[node->next_list].flags|DIRLIST_SHOW_DOT|flags)
					 : (root_array[node->next_list].flags|flags),
					 (void *)node);

		// requested dirlist, we go sleep now, wait for input back
		node->next_list++;
		return;

	} // while roots

	request_finishdir(node);


}




static int root_request_handler(lion_t *handle, void *user_data,
								int status, int size, char *line)
{
	request_t *node = (request_t *) user_data;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[root] connected to remote http, issuing GET\n");

		lion_printf(handle,
					"GET /%s HTTP/1.1\r\n"
					"Host: %s\r\n",
					root_array[node->root_index].http_file,
					root_array[node->root_index].http_host);

		lion_printf(handle, "Connection: close\r\n");

		// End of header.
		lion_printf(handle, "\r\n");

		break;

	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:
		node->roothandle = NULL;
		request_finishdir(node);
		break;

	case LION_INPUT:
		// FIXME, we just look for urls here.. Don't even care if we are in header or
		// body.
		//debugf("[root] request '%s'\n", line);

		if (line && *line) {
			char str[] = "-rwxr-xr-x 1 u g 0 Dec 15 21:44 ";
			char *part, *ar, *url, *entry;
			char *next;

			next = line;

			while((part = strstr(next, "\"http://"))) {

				ar = part;
				if ((url = misc_digtoken(&ar, "\"\r\n"))) {

					entry = misc_strjoin(str, url);
					if (entry) {
						debugf("[root] adding url '%s'\n", url);
						skin_write_type(node, entry);
					}
					SAFE_FREE(entry);

				} // if good url

				// Ok, skip this http://, look for more.
				next = ar;

			} // while strstr

		} // line

		break;

	}

	return 0;
}

static int root_zfs_handler(lion_t *handle, void *user_data,
                            int status, int size, char *line)
{
    char *zfs_attribute = (char *) user_data;

	// If node isn't set, it's b0rken.
	if (!zfs_attribute) return 0;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("      : zfs command running\n");
		break;

	case LION_PIPE_FAILED:
		debugf("      : zfs command failed: %d\n", size);
        break;

	case LION_PIPE_EXIT:
        debugf("      : zfs command finished.\n");
		break;

	case LION_INPUT:
		if (line && *line) {
            // "/export/media ON". zfs list -H should be separated by TAB.
            char *path, *value;

            path  = misc_digtoken(&line, "\t\r\n");
            value = misc_digtoken(&line, "\t\r\n");

            if (path && value && !mystrccmp("on", value)) {
                char *keys[2] = {"PATH", NULL};
                char *values[2] = {path, NULL};

                debugf("      : zfs adding '%s'\n", path);

                // Don't add "ZFS" here, or we will fork-bomb.
                root_cmd(keys, values, 1, NULL);

            }

		} // line

		break;

	}

	return 0;
}

static void root_http_list(request_t *node, lion_t *handle)
{

	if (!root_secret) root_secret = rand();

	// Connect and "get" the list of urls, attempting to match out those that we like.
	// Skin will filter out the types we handle, so we just need to find good urls.
	if (node->roothandle) {
		lion_disconnect(node->roothandle);
	}

	// Sanity check
	if ((node->root_index < 0 ) || (node->root_index > root_count)) return;
	// This root needs to be a HTTP type.
	if (!root_array[node->root_index].http_host) return;

	// Connect
	debugf("[root] http request connecting to %s:%d\n",
		   root_array[node->root_index].http_host,
		   root_array[node->root_index].http_port);


	node->roothandle = lion_connect(root_array[node->root_index].http_host,
								   root_array[node->root_index].http_port,
								   0, 0, LION_FLAG_FULFILL,
								   (void *)node);
	lion_set_handler(node->roothandle, root_request_handler);

	return;

}



//
// Take a http request node, which should have "cwd" filled in appropriately.
// We then call dirlist to issue listing(s) for this path.
//
void root_list(request_t *node, char *newpath)
{
    int flags;

	// Fetch the current directory. Note if they request "/"
	// we need to iterate all "root"s.

	debugf("[root] listing of '%s' requested\n", newpath);

	// FIXME, no paths.
	if (!root_count) return;


	// If next_list is 0 we only list one directory. If it is not 0,
	// we list that and increment.

	node->next_list = 0;

	if (!mystrccmp("/", newpath)) {

		// List more, if any.
		node->next_list = 1;

	}

	// Set handler to be here, temporarily
	lion_set_handler(node->tmphandle, root_handler);


	if (node->next_list) {

		debugf("[root] root listing, iterating roots\n");

		node->next_list = 0;

		// Issue listing request
		root_next_list(node, node->tmphandle);

		return;
	}


	// HTTP method
	if ((node->root_index < 0) ||
		(node->root_index >= root_count))
		return;

	if (root_array[node->root_index].http_host) {
		root_http_list(node, node->tmphandle);
		return;
	}

	// Stop dirlisting at this one.
	node->next_list = root_count;

    // Some dirlist flags need to be passed down to libdirlist...
    flags = skin_get_sort_flags(node);
    flags &= (DIRLIST_SHOW_DIRSIZE|DIRLIST_SHOW_GENRE);


    debugf("[root] asking for dirlist of '%s' with precat of '%s'\n",
           newpath,
           node->path);

	// Just list
	dirlist_list(node->tmphandle,
				 newpath,
				 node->path,
				 root_isregistered(node)
				 ? (root_array[node->root_index].flags|DIRLIST_SHOW_DOT|flags)
				 : (root_array[node->root_index].flags|flags),
				 node);

}





static int root_handler(lion_t *handle, void *user_data,
						int status, int size, char *line)
{
	request_t *node = (request_t *) user_data;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[root] connected to remote device, issuing root for /%s\n",
			   node->path);
		break;

	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:
		break;

	case LION_INPUT:
		debugf("[root] >> '%s'\n", line);

		// First line from dirlist is ":x reason". If 0, it is good.
		// Not 0 is failure. Followed by dirlist, then
		// :END end lists.
		if (line && *line == ':') {

			// If we failed, or, finished a dirlist, we need to either
			// issue the next list, or, stop and signal that we are done.
			if ((atoi(&line[1]) > 0) ||
				((line[1] == 'E') &&
				 (line[2] == 'N') &&
				 (line[3] == 'D'))) {

				debugf("[root] listing finished\n");


				root_next_list(node, handle);

				return 0;

			} // issue more lists?

			break;

		} // Not a colon

		// Directory entry here.
		skin_write_type(node, line);


	}

	return 0;
}


static int cmptail(char *string, char *sub)
{
  int l1 = strlen(string), l2 = strlen(sub);
  if (l1 < l2)
    return 0;
  string = string + l1 - l2;
  if (strcasecmp(string, sub))
	  return 0;
  return 1;
}




void root_undot(char *s)
{
	/*
	 * remove /./
	 *        /../
	 *        /..
	 *     and now also remove // because it looks nicer.
	 *     and finally, things ending with /.
	 */

	char *r, *e;

	//	consolef("file_undot() from '%s'\n", s);

    /* In Win32 we say //c/dir/file.ext */
#ifdef WIN32
	while((r = (char *) strstr(&s[1], "//")))
#else
	while((r = (char *) strstr(s, "//")))
#endif
		{

			strcpy(&r[1], &r[2]);

		}

	while((r = (char *) strstr(s, "/./"))) {

		strcpy(&r[1], &r[3]);

	}

	while((r = (char *) strstr(s, "/../"))) {
		*r = (char) 0;
		if (!(e = (char *) strrchr(s, '/'))) {
			/* Tried to CD too far */
			strcpy(&r[1], &r[4]);
			continue;
		}

		strcpy(e, &r[3]);

	}

	if (cmptail(s, "/..")) {
		s[strlen(s)-3] = (char) 0;
		if ((e = (char *) strrchr(s, '/')))
			e[1] = (char) 0;
	}

	if (cmptail(s, "/.")) {
		s[strlen(s)-1] = (char) 0;
	}

	if (!*s) { /* Make sure it's always "/" and not "" */
		s[0] = '/';
		s[1] = (char) 0;
	}

	//	consolef("file_undot() to '%s'\n", s);

}


//
// This takes the "virtual path" ->path sent over by the HTTP request, and
// attempts to match it to one of the roots we have setup from conf file.
// If there is a valid match, ->disk_path is assigned.
//
// Note that if "disk_path" is already set, we don't do anything, and assume
// it is good. Only place that sets it apart from this function, is when we
// copy over the temporary filename, which is strictly controlled in
// request_action()
//
int root_setpath(request_t *node)
{
	char *tmpname, *subdir, *skin_root = NULL;
	char *str;
	int i;

	// If disk_path is set, we need not do anything.
	if (node->disk_path && node->disk_path) {
		debugf("[root] trusting that '%s' is valid.\n",
			   node->disk_path);
		// However, if the path is "/", or stat to a directory we
		// call dirlist.
		stat(node->disk_path, &node->stat);
		return 0;
	}


	// Reduce away any naughty "../../../" parts.
	root_undot(node->path);


	// If its "/" we just leave it as that.
	if (!mystrccmp("/", node->path)) {
		// Strictly not needed. We dont expand "/" to the real root as
		// we may have multiple real roots. So it is left as a secret
		// match strict which starts listing of all roots.
		SAFE_COPY(node->disk_path, node->path);
		return 0;
	}

	// Anything else, we need to find the "right" root, and
	// set full path. Any multiple hits are ignored as we just
	// settle for first come.

	// First we check if it is in the skin directory.
    if ((skin_root = skin_get_root(node->skin_type))) {
        tmpname = misc_strjoin(skin_root, node->path);

        debugf("[root] testing path '%s'\n", tmpname);

        // Windows does not like "directory/" in stat.
        misc_stripslash(tmpname);

        if (!stat(tmpname, &node->stat)) {

            // misc_strjoin allocates string, so we can just assign it.
            node->disk_path = tmpname;

            // Caveat, if the target is a directory, we have to ensure
            // it has a trailing "/" for dirlist's precat in ->path
            if (S_ISDIR(node->stat.st_mode) &&
                node->path[ strlen(node->path) ] != '/') {
                tmpname = misc_strjoin(node->path, "");
                SAFE_FREE(node->path);
                node->path = tmpname;
            }

            return 0;

        }

        SAFE_FREE(tmpname);
    } // skin


#if 0
    // The above (identical) code uses skin_get_root() which should return
    // the root of the current skin, which should be set to the upnp skin
    // in skin_set_skin().

	// First we check if it is in the upnpskin directory.
    if (skin_upnp_root) {

        tmpname = misc_strjoin(skin_upnp_root, node->path);

        debugf("[root] testing path '%s'\n", tmpname);

        // Windows does not like "directory/" in stat.
        misc_stripslash(tmpname);

        if (!stat(tmpname, &node->stat)) {

            // misc_strjoin allocates string, so we can just assign it.
            node->disk_path = tmpname;

            // Caveat, if the target is a directory, we have to ensure
            // it has a trailing "/" for dirlist's precat in ->path
            if (S_ISDIR(node->stat.st_mode) &&
                node->path[ strlen(node->path) ] != '/') {
                tmpname = misc_strjoin(node->path, "");
                SAFE_FREE(node->path);
                node->path = tmpname;
            }

            return 0;

        }

        SAFE_FREE(tmpname);
    } //upnp skin
#endif



	// Now try all the roots defined.

	for (i = 0; i < root_count; i++) {

		// Check for exact http ROOT matches
		if (root_array[i].http_host) {

			// If its a proxy request (file mode)

			if (node->cgi_host && node->cgi_file) {
				node->root_index = i;
				node->stat.st_mode = S_IFREG;
				node->disk_path = strdup("http-proxy");
				return 0;
			}

			// it will start with a slash, so skip it
			str = node->path;
			while (*str == '/') str++;

			// Compare it
			if (!mystrccmp(root_array[i].path, str)) {
				debugf("[root] ROOT|http match '%s'\n", str);
				node->root_index = i;
				// Due to stat()s, we need to send a real dir here.
				// Don't worry, "/" means dirlist, not that it lists from root
				node->disk_path = strdup("http-listing");
				//Fake a stat of a dirlist
				node->stat.st_mode = S_IFDIR;

				return 0;
			}
		}


		tmpname = misc_strjoin(root_array[i].path, node->path);
		misc_stripslash(tmpname);

		debugf("[root] testing path '%s'\n", tmpname);

		if (!stat(tmpname, &node->stat)) {


			// misc_strjoin allocates string, so we can just assign it.
			node->disk_path = tmpname;

			// Caveat, if the target is a directory, we have to ensure
			// it has a trailing "/" for dirlist's precat in ->path
			if (S_ISDIR(node->stat.st_mode) &&
				node->path[ strlen(node->path) ] != '/') {
				tmpname = misc_strjoin(node->path, "");
				SAFE_FREE(node->path);
				node->path = tmpname;
			}

			node->root_index = i;

			return 0;

		}

		SAFE_FREE(tmpname);

	}





	// This test destroys node->path, so it needs to be last

	debugf("[root] testing for SUBDIR type: '%s'\n", node->path);

	// it might be using a SUBDIR style syntax.
	str = node->path;
	while (*str == '/') str++;

	// Might be the root of SUBDIR, or deeper request.
	if ((subdir = strchr(str, '/'))) {

		*subdir = 0;
		subdir++;
	} else {
		subdir = "";
	}



	debugf("[root] looking for '%s' rest '%s'\n", str, subdir);

	for (i = 0; i < root_count; i++) {
		if (root_array[i].subdir &&
			!mystrccmp(root_array[i].subdir, str)) {

			node->disk_path = misc_strjoin(root_array[i].path, subdir);

			debugf("[root] testing subdir path '%s'\n", node->disk_path);

			misc_stripslash(node->disk_path);

			if (!stat(node->disk_path, &node->stat)) {

				// Caveat, if the target is a directory, we have to ensure
				// it has a trailing "/" for dirlist's precat in ->path
				// path = path + subdir
				tmpname = misc_strjoin(node->path, subdir);
				SAFE_FREE(node->path);
				node->path = tmpname;

				if (S_ISDIR(node->stat.st_mode) &&
					node->path[ strlen(node->path) ] != '/') {
					tmpname = misc_strjoin(node->path, "");
					SAFE_FREE(node->path);
					node->path = tmpname;
				}

				node->root_index = i;
				debugf("[root] returning subdir: '%s':%d\n", node->disk_path,
					   node->root_index);
				return 0;


			} // stat
		} // mystrccmp
	} // for roots


	debugf("[root] giving up, path not valid.\n");
	// we've trashed the path anyway?
	SAFE_FREE(node->path);

	// Invalid path. Do we ensure it will fail later here? Or just assume
	// that the stat will fail later on.
	return -1;

}








static void root_proxy_send(request_t *node, lion_t *handle)
{

	lion_printf(handle,
				"%s /%s HTTP/1.1\r\n"
				"Host: %s\r\n",
				node->head_method ? "HEAD" : "GET",
				node->cgi_file,
				node->cgi_host);
	if (node->bytes_from && node->bytes_to)
		lion_printf(handle, "Range: bytes=%"PRIu64"-%"PRIu64"\r\n",
					node->bytes_from, node->bytes_to);
	else if (node->bytes_from)
		lion_printf(handle, "Range: bytes=%"PRIu64"-\r\n",
					node->bytes_from);
	else if (node->bytes_to)
		lion_printf(handle, "Range: bytes=0-%"PRIu64"\r\n",
					node->bytes_to);

	if ((node->type & REQUEST_TYPE_CLOSED))
		lion_printf(handle, "Connection: close\r\n");
	else
		lion_printf(handle, "Connection: keep-alive\r\n");

	if ((node->type & REQUEST_TYPE_CLOSED)) {
		debugf("[root] Connection: closed\n");
		SAFE_FREE(node->cgi_host);
	} else
		debugf("[root] Connection: keep-alive\n");

	// End of header.
	lion_printf(handle, "\r\n");

	node->bytes_sent = node->bytes_from;


	if (node->handle && (node->type & REQUEST_TYPE_KEEPALIVE)) {
		if (!node->bytes_to && !node->bytes_size) {
			debugf("[root] re-enabling socket as we have no size\n");
			lion_enable_read(node->handle);
		}
	}
}

//
// Proxy mode handler
//
static int root_proxy_handler(lion_t *handle, void *user_data,
								int status, int size, char *line)
{
	request_t *node;

	node = (request_t *) user_data;
	if (!node) return 0;

	switch(status) {


	case LION_CONNECTION_CONNECTED:
		debugf("[root] proxy connection, sending request\n");

		node->time_idle  = lion_global_time;
		node->time_start = lion_global_time;

		root_proxy_send(node, handle);
		break;


	case LION_CONNECTION_LOST:
		debugf("[root] proxy failed: %d:%s\n",
			   size, line ? line : "unknown");

		request_reply(node, 500, line);

		/* fall through */
	case LION_CONNECTION_CLOSED:
		debugf("[root] proxy reached EOF\n");

		SAFE_FREE(node->cgi_host);
		node->roothandle = NULL;

		// File is done, enable reading off this socket.
		if (node->handle) {

			lion_enable_read(node->handle);
			debugf("[request] socket read restored\n");

			// Also close socket?
			// Warning, if this DOES close the socket, "node"
			// is entirely clear, and any references after is INVALID.
			if ((node->type & REQUEST_TYPE_CLOSED)) {
				debugf("[request] closing socket due to TYPE_CLOSED\n");
				lion_close(node->handle);
			}

		}
		break;


		// Should be no line data from files
	case LION_INPUT:
		break;

		// Send data to socket.
	case LION_BINARY:
		node->bytes_sent += (lion64u_t) size;

		if (node->handle)
			if (lion_output(node->handle, line, size) == -1) {
				debugf("[request] socket write failed.\n");
				break;
			}

		// Accept new requests?
		if (node->handle && (node->type & REQUEST_TYPE_KEEPALIVE)) {

			if (node->bytes_to && (node->bytes_sent >= node->bytes_to)) {
				debugf("[root] re-enabling socket due to byte count\n");
				lion_enable_read(node->handle);
			}
		}

		break;

	case LION_BUFFER_EMPTY:
		debugf("[buff emtpy]\n");
		break;
	case LION_BUFFER_USED:
		debugf("[buff full ]\n");
		break;
	}

	return 0;
}




//
// Connect to a url for them
//
void root_proxy(request_t *node)
{

	debugf("[root] proxy '%s' %p %p\n",
		   node->cgi_host,
		   node,
		   node->roothandle);

	if (node->cgi_secret != root_secret) {
		request_reply(node, 500, "Incorrect cgi code");
		SAFE_FREE(node->cgi_host);
		return;
	}

	// Connect to host, option port. Issue the request, then
	// whatever is sent back, we send to Media Device.

	// Lets connect! Already connected due to keep-alive?
	if (node->cgi_host && node->roothandle) {
		debugf("[root] re-using connection\n");
		root_proxy_send(node, node->roothandle);
		return;
	}

	debugf("[root] proxy connecting to http://%s:%u/%s ....\n",
		   node->cgi_host, node->cgi_port, node->cgi_file);

	node->roothandle = lion_connect(node->cgi_host, node->cgi_port, 0, 0,
								   LION_FLAG_FULFILL, (void *) node);
	lion_set_handler(node->roothandle, root_proxy_handler);
	lion_enable_binary(node->roothandle);

	return;

}




//
// User has entered a valid PIN, we then register this IP, and a time-stamp
//
void root_register_ip(request_t *req)
{
	root_register_t *node;

	node = (root_register_t *) malloc(sizeof(*node));

	if (!node) return;

	memset(node, 0, sizeof(*node));

	node->ip = req->ip;
	node->time = lion_global_time;

	node->next = root_register_head;
	root_register_head = node;

	debugf("[root] register IP %s\n", lion_ntoa(req->ip));
}


void root_deregister_ip(request_t *req)
{
	root_register_t *node, *prev = NULL, *next;

	// If PINs are NOT set, we will say ok. If they want to hide dot entries
	// but don't care about PINs, just set it to anything
	if (!conf_pin) {
		debugf("[root] no PIN, always elevated\n");
		return;
	}

	for (node = root_register_head; node; node=next) {

		next = node->next;

		// Check if it there, if so, update it, and return it.
		if (node->ip == req->ip) {
			debugf("[root] clearing PIN for IP\n");
			if (!prev) root_register_head = node->next;
			else prev->next = node->next;
			SAFE_FREE(node);
			continue;
		}

		prev = node;

	} // For

	return;

}


int root_isregistered(request_t *req)
{
	root_register_t *node, *prev = NULL, *next;
    int i;
    char *ipstr;

	// If PINs are NOT set, we will say ok. If they want to hide dot entries
	// but don't care about PINs, just set it to anything
	if (!conf_pin) {
        //		debugf("[root] no PIN, always elevated\n");
		return 1;
	}

	for (node = root_register_head; node; node=next) {

		next = node->next;

		// Check if it too old first, then remove.
		if ((lion_global_time - node->time) >= ROOT_PIN_TIMEOUT) {
			// remove it.
			if (!prev) root_register_head = node->next;
			else prev->next = node->next;
			SAFE_FREE(node);
			continue;
		}

		// Check if it there, if so, update it, and return it.
		if (node->ip == req->ip) {
			debugf("[root] elevating level\n");
			node->time = lion_global_time;
			return 1;
		}

		prev = node;

	} // For


    // Finally, if it is an IP listed in the static list, just allow it.
    ipstr = lion_ntoa(req->ip);

    for (i = 0; i < root_registered_num; i++) {
        if (!lfnmatch(root_registered_patterns[i],
                      ipstr,
                      LFNM_CASEFOLD)) return 1;
    }

	return 0;

}

//
// Add static IP pattern as registered/authenticated (PIN)
//
// CMD      : AUTH
// Required : pattern
// Optional :
//
void root_auth(char **keys, char **values,
               int items,void *optarg)
{
	char *pattern;

	// Requireds
	pattern  = parser_findkey(keys, values, items, "PATTERN");

	if (!pattern || !*pattern) {
		printf("      : [root] missing required field: PATTERN\n");
		//do_exit = 1;
		return;
	}

    if (root_registered_num >= ROOT_REGISTERED_MAX) {
        printf("Sorry, maximum AUTH lines have already been defined. \n");
        printf("This program was compiled with a maximum of %d AUTH defines.\n",
               ROOT_REGISTERED_MAX);
        return;
    }

    root_registered_patterns[ root_registered_num++ ] = strdup(pattern);

}

