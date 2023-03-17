#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"
#include "dirlist.h"

#define DEBUG_FLAG 8
#include "debug.h"
#include "llrar.h"
#include "conf.h"
#include "parser.h"
#include "request.h"
#include "skin.h"
#include "visited.h"


static int llrar_handler(lion_t *handle, void *user_data,
						 int status, int size, char *line);
static int llrar_extract_handler(lion_t *handle, void *user_data,
								 int status, int size, char *line);


static int llrar_spawned_list = 0;
static int llrar_spawned_get  = 0;
static int llrar_resumed_get  = 0;

// How many bytes should the seek be before we switch to re-spawning
// using the seek option.
static lion64u_t LLRAR_SEEK_TRIGGER = 1 * 1024 * 1024;



// Cache of RAR files we have already listed.
static llrar_cache_t llrar_cache[LLRAR_CACHE_SIZE];
static int llrar_cache_index = 0;




int llrar_init(void)
{
    int i;

    for (i = 0; i < LLRAR_CACHE_SIZE; i++) {
        llrar_cache[i].path  = NULL;
        llrar_cache[i].files = NULL;
    }
    llrar_cache_index = 0;

	return 0;
}



void llrar_free(void)
{
    int i;
    llrar_cache_entry_t *runner, *next;

	debugf("[llrar] spawned %3d unrar list processes.\n"
		   "[llrar] spawned %3d unrar  get processes.\n"
		   "[llrar] resumed %3d unrar  get processes.\n",
		   llrar_spawned_list,
		   llrar_spawned_get,
		   llrar_resumed_get);

    for (i = 0; i < LLRAR_CACHE_SIZE; i++) {
        SAFE_FREE(llrar_cache[i].path);
        for (runner = llrar_cache[i].files; runner; runner = next) {
            next = runner->next;
            file_free(&runner->file);
            SAFE_FREE(runner);
        }
        llrar_cache[i].files = NULL;
    }
    llrar_cache_index = 0;
}


//
// This function is called a lot.
//
void llrar_compute_blocksize(request_t *node)
{
	lion64_t remaining, buffer, before;

	// If we are asking for just a part of the file, we need to work out
	// how much more they want, and if that is smaller than the next
	// buffer read, we set the buffer size smaller.
	if (!node->bytes_to && !node->bytes_from)
		return;

	buffer = (lion64_t) lion_get_buffersize(node->althandle);
	before = buffer;

	// lseek, are we before from, and would move past it?
	if (node->bytes_from) {

		if ((node->bytes_sent < node->bytes_from) &&
			((node->bytes_sent + buffer) > node->bytes_from)) {
			buffer = node->bytes_from - node->bytes_sent;

			debugf("[llrar] adjusting buffersize from %"PRIu64".\n",
				   buffer);
		}

	}


	if (node->bytes_to) {

		if (node->bytes_sent > node->bytes_to) {
			debugf("[llrar] bug, already sent > to\n");
			return;
		}

		remaining = node->bytes_to - node->bytes_sent + 1;

		if (buffer > remaining) {
			buffer = remaining;

			debugf("[llrar] adjusting buffersize to %"PRIu64".\n",
				   buffer);
		}
	}



	if (before != buffer)
		lion_set_buffersize(node->althandle, (int)buffer);

}







void llrar_stop_process(request_t *node)
{

	// No process? Just return
	if (!node->althandle)
		return;

	debugf("[llrar] stopping old process at %"PRIu64"\n", node->bytes_sent);

    visited_fileclosed(node);

	// We need to kill the process without fiddling with anything else really.
	// userdata to NULL, then all handlers do nothing.
	lion_set_userdata(node->althandle, NULL);

	// Allow it to drain
	lion_enable_read(node->althandle);

	// Disconnect it
	lion_disconnect(node->althandle);

	// Set it to NULL
	node->althandle = NULL;

	SAFE_FREE(node->rar_name);
	node->bytes_sent = 0;


}

void llrar_fail(request_t *node)
{

	lion_printf(node->handle, "HTTP/1.1 500 unrar process failed\r\n\r\n");
	lion_close(node->handle);

}

//
// Take a http request node, which should have "cwd" filled in appropriately.
// We then call dirlist to issue listing(s) for this path.
//
void llrar_list(request_t *node, char *executable)
{
	char buffer[1024];

	debugf("[llrar] file listing requested: %s - %s\n",
		   node->disk_path, node->path);

    debugf("        process %d, dvdread_file '%s'\n", node->process_function,
           node->dvdread_file ? node->dvdread_file : "");


	// If we already have a process running, kill it
	llrar_stop_process(node);


	SAFE_FREE(node->rar_name);

	// Spawn unrar
	//-ap$DIR -av- -c- -p- -y
	// ok, -ap$DIR does not work in listings.
	// but -ndirectory1/ -ndirectory1/directory2/ does appear to work. Needs
	// trailing slash, or it shows itself.

	// Old style was to use "-n/path" to attempt to list only one directory
	// within a .rar, but unrar has a bug where it will just not show the
	// directory names, etc. So, we change to using "v" to list.
	// Now we get one line with full path, followed by a line of details.
	// That way, we can skip the first space, then look for last "/", this is
	// the full path, and filenames. Spaces included.
	//
    //  directory1/directory2/file2.avi
    //          7       17 242% 04-12-07 14:11 -rw-r--r-- 16B28489 m3b 2.9
	//
	// We then filter out any entries for the path we care about, drop the
	// rest. No slash in filename means it is in root.

    // If we are listing ISO inside RAR, we need to also pass the filename
    // along with the rar name.  "-R filename"
    if (/*(node->process_function == REQUEST_PROCESS_RARISO) &&*/
        (node->dvdread_file)) {

        debugf("[llrar] RARISO for '%s' -X \"%s\" \n", node->dvdread_file,
               skin_get_rar());
        snprintf(buffer, sizeof(buffer), "%s v -X \"%s\" -R \"%s\" -v -c- -p- -y -cfg- -- \"%s\"",
                 executable,
                 skin_get_rar(),
                 node->dvdread_file,
                 node->disk_path);

    } else {

        snprintf(buffer, sizeof(buffer), "%s v -v -c- -p- -y -cfg- -- \"%s\"",
                 executable,
                 node->disk_path);

    }



	node->rar_name = strdup("rar listing");



	debugf("[llrar] spawning new '%s'\n", buffer);

	node->unrar_parse = 0;

	llrar_spawned_list++;

	node->althandle = lion_system(buffer, 1, LION_FLAG_NONE, node);

	if (!node->althandle) {
		debugf("[llrar] failed to launch unrar.\n");
//		request_reply(node, 513, "failed to launch unrar");
		// llrar_fail(node);
        request_finishdir(node);
		return;
	}

	// Set list handler
	lion_set_handler(node->althandle, llrar_handler);

}



int llrar_get(request_t *node, char *executable)
{
	char buffer[1024];
	int keep_process;


	debugf("[llrar] get file requested: '%s' ('%s')\n",
		   node->path,
		   node->dvdread_file ? node->dvdread_file : "");

	// Alas, subtitle lookup will have wrong size:(
    if (node->rar_file && !lfnmatch("*.srt",
									node->rar_file,
									LFNM_CASEFOLD)) {
		node->bytes_size = 500 * 1024;
    }


	// HEAD MODE, DO NOTHING
	if (node->head_method)
		return 0;

	// Safety, we aren't called if this is not set in request.c
	if (!node->rar_file)
		return 0;

	// Now, we both need to url_decode this part, and re-assign.
	//	name = request_url_decode(ar);

	// Keep process?
	keep_process = 0;


	debugf("[llrar] checking for resume: rar_name (%s), althandle (%p), name (%s), from (%"PRIu64") >= sent (%"PRIu64")\n",
		   node->rar_name ? node->rar_name : "",
		   node->althandle,
		   node->rar_file ? node->rar_file : "",
		   node->bytes_from,
		   node->bytes_sent);


	if (node->rar_name &&
		node->althandle &&
		!mystrccmp(node->rar_file, node->rar_name) &&
		node->bytes_from >= node->bytes_sent) {
		keep_process = 1;
		debugf("[llrar] resuming old unrar\n");
		llrar_resumed_get++;
	}


	// Now, if we have special unrar with seek support, we should use it if
	// we are seeking far.
	if (keep_process) {
		// We thought we were going to keep the process, but lets see if the
		// seek is too big, we might as well abort, and use the seek feature.

		if ((node->bytes_from - node->bytes_sent) >=
			LLRAR_SEEK_TRIGGER) {
			keep_process = 0;

			debugf("[llrar] seek too large, respawning with seek: %"PRIu64" <= %"PRIu64"\n",
				   node->bytes_from,
				   node->bytes_sent);
		}
	}

	if (!keep_process) {


#ifdef WITH_UNRAR_BUFFERS
        // We have decided we need to restart the unrar process here,
        // but as a special case, if we can satisfy the ENTIRE request
        // from cached-buffers, do so instead and update no variables.
        if (llrar_incaches(node)) return 0;

#endif


		llrar_stop_process(node);

		// Copy the rar_file name to rar_name, so we can compare for resumes
		node->rar_name = strdup(node->rar_file);

#ifdef WIN32
        // Skip leading slash, since we skip that in the print. Then
        // convert all "/" to "\" since unrar.exe expects it.
        {
            char *r = node->rar_name;
            while (*r == '/') r++;
            while ((r = strchr(r, '/'))) *r = '\\';
        }
#endif

        debugf("[[llrar] using unrar with seek %"PRIu64".\n",
               node->bytes_from);


        if (node->dvdread_file) {

                debugf("[llrar] RARISO for '%s'\n", node->dvdread_file);

                snprintf(buffer, sizeof(buffer),
                         "%s p -X \"%s\" -R \"%s\" -inul -c- -p- -y -cfg- -sk%"PRIu64" -- \"%s\" \"%s\"",
                         executable,
                         skin_get_rar(),
                         node->dvdread_file,
                         node->bytes_from,
                         node->disk_path,
                         (node->rar_name[0] == '/') ? &node->rar_name[1] :
                         node->rar_name);

        } else { // Not ISO in RAR, straight...

            snprintf(buffer, sizeof(buffer),
                     "%s p -inul -c- -p- -y -cfg- -sk%"PRIu64" -- \"%s\" \"%s\"",
                     executable,
                     node->bytes_from,
					 node->disk_path,
                     (node->rar_name[0] == '/') ? &node->rar_name[1] :
                     node->rar_name);

        } // RARISO


        // Make it think we've already seen "seek" bytes.
        node->bytes_sent = node->bytes_from;


        debugf("[llrar] spawning new '%s'\n", buffer);

		node->unrar_parse = 0;

		llrar_spawned_get++;

		node->althandle = lion_system(buffer, 1, LION_FLAG_NONE, node);

		if (!node->althandle) {
			debugf("[llrar] failed to launch unrar\n");
			llrar_fail(node);
	//		request_reply(node, 513, "failed to launch unrar");
			return -2;
		}

		// Set extract handler
		lion_set_handler(node->althandle, llrar_extract_handler);
		// Set binary mode
		lion_enable_binary(node->althandle);
		// Disable read until we are ready
		lion_disable_read(node->althandle);


	}  // !keep_process


	// Set right chunk mode.
	llrar_compute_blocksize(node);

	debugf("[llrar] processing: sent (%"PRIu64") bytes_from (%"PRIu64") bytes_to (%"PRIu64")\n",
		   node->bytes_sent, node->bytes_from, node->bytes_to);

	return 0;
}










static int llrar_handler(lion_t *handle, void *user_data,
						 int status, int size, char *line)
{
	request_t *node = (request_t *) user_data;
	int directory;
	char list[1024];
	char *name, *fsize, *packed, *ratio, *date, *thyme, *attr;
	char *ar, *path, *slash, *root;
    llrar_cache_entry_t *runner, *next;
    llrar_cache_t *cache;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[llrar] connected to external process, issuing root for /%s\n",
			   node->path);
		break;


	case LION_PIPE_RUNNING:
		debugf("[llrar] unrar is running\n");
		//lion_printf(handle, "HELLO THERE\r\n");

        debugf("[llrar] clearing cache slot %d.\n", llrar_cache_index);

        // Set up RAR cache here.
        cache = &llrar_cache[ llrar_cache_index ];
        node->llrar_cacher = (void *) cache;
        // If this entry is in use, free it.
        SAFE_FREE(cache->path);
        cache->path = strdup(node->disk_path);
        for (runner = cache->files; runner; runner = next) {
            next = runner->next;
            file_free(&runner->file);
            SAFE_FREE(runner);
        }
        cache->files = NULL;

        // Increase cache pointer
        llrar_cache_index++;
        if (llrar_cache_index >= LLRAR_CACHE_SIZE)
            llrar_cache_index = 0;

		break;
	case LION_PIPE_FAILED:
		debugf("[llrar] unrar failed to launch: %d:%s\n", size, line);
        node->llrar_cacher = NULL;
		node->althandle = NULL;
		SAFE_FREE(node->rar_name);
		request_finishdir(node);
		break;
	case LION_PIPE_EXIT:
		debugf("[llrar] unrar finished: %d\n", size);
		if (size == -1) {
			lion_want_returncode(handle);
			break;
		}
        node->llrar_cacher = NULL;
		node->althandle = NULL;
		SAFE_FREE(node->rar_name);
		request_finishdir(node);

        //llrar_dumpcache();
		break;

	case LION_INPUT:
// Name             Size   Packed Ratio  Date   Time     Attr      CRC   Meth Ver
//-------------------------------------------------------------------------------
// aaf-rc.s03e13.avi 105119744 14999906  --> 11-11-07 23:52  .....A.   78AF2535 m0g 2.0
//-------------------------------------------------------------------------------
		debugf("[llrar] >> '%s'\n", line);

		switch(node->unrar_parse) {
		case 0:  // start of unrar list, waiting for line with -----
			if (!strncmp("--------------------", line, 20))
				node->unrar_parse++;
			break;

		case 1:  // woohooo, actually parsing entries
			// This is the filename part
			// " directory1/directory2/file2.avi"

			if (!strncmp("--------------------", line, 20)) {
				node->unrar_parse = 0;
				break;
			}

			if (*line != ' ') {
				debugf("[llrar] unable to parse: '%s'\n", line);
				break;
			}

			// Skip that leading space.
			if (*line == ' ')
				line++;

			// Remember this line until next parsing line.
			node->rar_name = strdup(line);

			node->unrar_parse++;
			break;

		case 2: // parse out filesize, type etc.
			// "   7       17 242% 04-12-07 14:11 -rw-r--r-- 16B28489 m3b 2.9"

			// Alternate filename/data lines, change back to filename
			node->unrar_parse = 1;

			ar = line;

			// node->rar_name hold the FULL path name.
			fsize  = misc_digtoken(&ar, " \r\n");
			packed = misc_digtoken(&ar, " \r\n");
			ratio  = misc_digtoken(&ar, " \r\n");
			date   = misc_digtoken(&ar, " \r\n");
			thyme  = misc_digtoken(&ar, " \r\n");
			attr   = misc_digtoken(&ar, " \r\n");

			if (!node->rar_name ||
				!fsize|| !*fsize||
				!attr || !*attr) {
				debugf("[llrar] unable to parse -- skipping\n");
				break;
			}

			// Files spanning Volumes will be repeated, but we can tell by
			// lookip at "ratio" column.
			if (!mystrccmp("<->", ratio) ||
				!mystrccmp("<--", ratio)) {

				SAFE_FREE(node->rar_name);
				break;
			}


			debugf("[llrar] parsed name '%s'\n", node->rar_name);

			// Now we need to look at the rar_file fullname, and
			// split it into directory/filename
			// Find last slash. Will windows use '\' ?
#ifdef WIN32
			for (name = node->rar_name; *name; name++)
				if (*name == '\\') *name = '/';
#endif
			slash = strrchr(node->rar_name, '/');
			if (!slash) { // We are in root.
				path = "/";
				name = node->rar_name;
			} else { // We are in a subdir
				*slash = 0;
				name = &slash[1];
				path = node->rar_name;
			}
			// Now "path" should hold full path, and "name" just the
			// entry name.
			if (node->rar_directory) {
				root = node->rar_directory;
				// Skip leading "/" as unrar wont start with /
				while (*root == '/') root++;
			} else
				root = "/";

			debugf("[llrar] Checking '%s' == '%s'\n",
				   path, root);


			//  Now check if it is for a path we care about
            // and not in cachefill-only mode
			if (!node->llrar_cache_fill && !mystrccmp(path, root)) {
				// It is

				//rar attributes. If DOS format, it will be
				// .D.....    : 7 chars.
				// Unix format:
				// d--------- :  10 chars
				directory = 0;
				if ((tolower(*attr) == 'd') ||
					(tolower(attr[1]) == 'd'))
					directory = 1;

                if (node->dvdread_file) {

                    if (directory)
                        snprintf(list, sizeof(list),
              "drwxr-xr-x  1 unrar unrar %s Jun 7 %s %s?fv=%"PRIu64",%s&d=%s/%s",
                                 fsize,
                                 thyme,
                                 node->path,

                                 node->bytes_size,
                                 node->dvdread_file,

                                 node->rar_directory ? node->rar_directory : "",
                                 name);
                    else
                        snprintf(list, sizeof(list),
             "-rwxr-xr-x  1 unrar unrar %s Jun 7 %s %s?fv=%"PRIu64",%s&f=%s,%s/%s",
                                 fsize,
                                 thyme,
                                 node->path,

                                 node->bytes_size,
                                 node->dvdread_file,

                                 fsize,
                                 node->rar_directory ? node->rar_directory : "",
                                 name);

                } else { // No dvdread_file (RARISO)

                    if (directory)
                        snprintf(list, sizeof(list),
                                 "drwxr-xr-x  1 unrar unrar %s Jun 7 %s %s?d=%s/%s",
                                 fsize,
                                 thyme,
                                 node->path,
                                 node->rar_directory ? node->rar_directory : "",
                                 name);
                    else
                        snprintf(list, sizeof(list),
                                 "-rwxr-xr-x  1 unrar unrar %s Jun 7 %s %s?f=%s,%s/%s",
                                 fsize,
                                 thyme,
                                 node->path,
                                 fsize,
                                 node->rar_directory ? node->rar_directory : "",
                                 name);
                } // dvdread_file

				skin_write_type(node, list);

			} // directory we care about (in our path)


            // RAR cache.
            if (node->llrar_cacher) {
                llrar_cache_entry_t *nfile;
                nfile = (llrar_cache_entry_t *)malloc(sizeof(llrar_cache_entry_t));
                if (nfile) {
                    memset(nfile, 0, sizeof(*nfile));
                    nfile->file.name = misc_strjoin(path,
                                                    name);
                    nfile->file.size = strtoull(fsize, NULL, 10);
                    nfile->file.directory = YNA_NO;
                    // Link in
                    nfile->next = ((llrar_cache_t *)(node->llrar_cacher))->files;
                    ((llrar_cache_t *)(node->llrar_cacher))->files = nfile;
                } // malloc
            } // cacher


			// Release the filename from previous line.
			SAFE_FREE(node->rar_name);

			break;

		case 3:  // seen ----- the rest is fluff
			break;
		}




		break;

	}

	return 0;
}








static int llrar_extract_handler(lion_t *handle, void *user_data,
								 int status, int size, char *line)
{
	request_t *node = (request_t *) user_data;
	int toomuch;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[llrar] connected to unrar process\n");
		break;

	case LION_PIPE_FAILED:
		debugf("[llrar] unrar failed to launch: %d:%s\n",
			   size, line ? line : "");

		node->althandle = NULL;
		SAFE_FREE(node->rar_name);
		request_reply(node, 500, line ? line : "Error with unrar");
		break;

	case LION_PIPE_EXIT:
		debugf("[llrar] unrar finished: %d\n", size);
		if (size == -1) {
			lion_want_returncode(handle);
			break;
		}

		visited_fileclosed(node);

		node->althandle = NULL;
		SAFE_FREE(node->rar_name);

        if (!node->handle) break;

		debugf("[request] socket read restored\n");
		lion_enable_read(node->handle);

		if ((node->type & REQUEST_TYPE_CLOSED)) {
			debugf("[request] closing socket due to TYPE_CLOSED\n");
			lion_close(node->handle);
		}

		break;

	case LION_BINARY:



#ifdef WITH_UNRAR_BUFFERS
        // Add to the buffer cache.
        if (size > 0) {
            unsigned int i, found;
            char *buffer;

            // Make sure we do not already have this buffer
            for (found = 0, i = 0;
                 i < WITH_UNRAR_BUFFERS;
                 i++) {
                if (node->unrar_buffers[ i ] &&
                    node->unrar_offsets[ i ] == node->bytes_sent) {
                    found = 1;
                    break;
                }
            }

            if (!found) {

                // Allocate new buffer area
                buffer = malloc(size);

                if (buffer) {

                    // Clear the node we are about to replace first.
                    i = node->unrar_radix;
                    SAFE_FREE(node->unrar_buffers[ i ]);

                    // Assign new buffer.
                    node->unrar_offsets[ i ] = node->bytes_sent;
                    node->unrar_sizes  [ i ] = size;
                    node->unrar_buffers[ i ] = buffer;

                    // Move to next buffer.
                    i++;
                    if (i >= WITH_UNRAR_BUFFERS)
                        i = 0;
                    node->unrar_radix = i;

                } // buffer
            } // !found
        } // size
#endif




		// Check if we are lseeking
		if (node->bytes_from) {

			if ((node->bytes_sent + (lion64u_t)size) <= node->bytes_from) {
				if (!node->bytes_sent) {
					debugf("[llrar] seeking to start position\n");
				}
				// Just discard, do nothing.
				node->bytes_sent += (lion64u_t)size;
				// Compute new blocksize
				llrar_compute_blocksize(node);
				break;
			}

			// Complicated case, sent < from but sent + size > from
			if ((node->bytes_sent < node->bytes_from) &&
				(node->bytes_sent + (lion64u_t)size) > node->bytes_from) {
				debugf("[llrar] special case sent < from, but sent+size > from!\n");
				// decrease size, increase line
			}
		}


		// Check if we would send too much
		// Check if we have sent exactly enough
		toomuch = 0;


		if (node->bytes_to) {

			if ((node->bytes_sent + (lion64u_t) size) >
				node->bytes_to)
				toomuch = 1;
		}

		node->bytes_sent += size;

		if (toomuch) {
			// bytes_sent is our bytes index into the unrar read file
			// but we need to make size smaller so that we don't send more
			// than promised.
			int before;

			before = size;
			size -= (int) node->bytes_sent - node->bytes_to - 1 ;

			debugf("[llrar] final block %d\n", size);
			if (before > size)
				debugf("[llrar] Warning, overshot our place, can't resume. read %d wanted %d\n",
					   before, size);

			// Sleep unrar process incase we want to read more soon
			// Warning, because we enable read of handle below
			// its handler enables althandle, so we must set it there
			lion_disable_read(node->althandle);

			// Set default buffersize again
			lion_set_buffersize(node->althandle, 0);

		}

		if (lion_output(node->handle, line, size) == -1) {
			debugf("[request] socket write failed.\n");
			break;
		}

		// Check if we should limit the next read
		if (!toomuch) {
			llrar_compute_blocksize(node);
		} else {

			if (node->handle) {

				// Also close socket?
				// Warning, if this DOES close the socket, "node"
				// is entirely clear, and any references after is INVALID.
				if ((node->type & REQUEST_TYPE_CLOSED)) {
					debugf("[llrar] closing socket due to TYPE_CLOSED\n");
					// Close rar too
					lion_disconnect(node->althandle);
					// Close http socket
					lion_close(node->handle);
					break;
				}

				debugf("[llrar] socket read restored %p\n",
					   node->handle);
				lion_enable_read(node->handle);

			}
		}

		break;

	} // switch status

	return 0;
}


#ifdef WITH_UNRAR_BUFFERS
//
// If we get a small request for data already in the cache,
// and we can satisfy the ENTIRE request, we do so here
// immediately, without updating any values, or changing any
// running unrar (so it can resume should there be a continuing
// stream request in future).
//
// This is entirely to deal with Syabas' MKV code, that likes to seek -16383
// back, and return.
//
int llrar_incaches(request_t *node)
{
    lion64_t bytes_wanted = 0, len = 0, off = 0;
    int i, radix;
    int success_redo_and_send = 0;

    debugf("[llrar] CACHE: incaches check: from %"PRIu64" - to %"PRIu64"\n",
           node->bytes_from, node->bytes_to);

    // Has to be a "from-to" range.
    if (!node->bytes_to ||
        (node->bytes_to <= node->bytes_from)) return 0;

    // 2 passes, first pass check only, 2nd pass, send as well.
    while (1) {


        bytes_wanted = node->bytes_to - node->bytes_from;

        // Loop through the cache, and subtract any buffers within said range.
        // This code ASSUMES that the cache is ORDERED, and CONTIGUOUS.

        for (i = 0, radix = node->unrar_radix;
             i < WITH_UNRAR_BUFFERS;
             i++, radix++) {

            if (radix >= WITH_UNRAR_BUFFERS)
                radix = 0;

            if (node->unrar_buffers[ radix ] &&
                node->unrar_sizes[ radix ]) {

                // look for when buffers actually cross the start position
                if ((node->unrar_offsets[ radix ] + node->unrar_sizes[ radix ])
                    >= node->bytes_from) continue;

                if ((node->bytes_from >= node->unrar_offsets[ radix ]) &&
                    (node->bytes_from <
                     (node->unrar_offsets[ radix ] + node->unrar_sizes[ radix ]))) {

                    break;

                }// inside
            } // valid buffer

        } // looking for start

        // Not at all in our buffers
        if (i >= WITH_UNRAR_BUFFERS) return 0;


        for ( i=i;  // leave i alone! from previous for.
              i <= WITH_UNRAR_BUFFERS;
              i++,radix++) {

            if (radix >= WITH_UNRAR_BUFFERS)
                radix = 0;

            // Now, how much of "this" cache-buffer do we use.
            off = node->bytes_from - node->unrar_offsets[ radix ];
            len = node->unrar_sizes[ radix ] - off;

            // if diff >= bytes_wanted, we are finished.
            if (len >= bytes_wanted) {
                // Send 'bytes_wanted' here.
                if (success_redo_and_send &&
                    lion_output(node->handle,
                                &node->unrar_buffers[radix][off],
                                (unsigned int)bytes_wanted) == -1)
                    goto failed;

                debugf("[llrar] CACHE: pass one good!\n");

                bytes_wanted = 0;
                success_redo_and_send++;
                break;
            } else {
                // send diff bytes here.
                if (success_redo_and_send &&
                    lion_output(node->handle,
                                &node->unrar_buffers[radix][off],
                                (unsigned int)len) == -1)
                    goto failed;
                bytes_wanted -= len;
            }

        } // for i

        // If we get here, and "success_redo_and_send" is set, AND
        // bytes_wanted is 0, we can in fact completely satisfy this request.
        // Do it all again, but this time, send stuff.
        if (bytes_wanted) {
            debugf("[llrar] CACHE: Had some buffers, but missing %"PRIu64"\n",
                   bytes_wanted);
            return 0;
        }

        if (!success_redo_and_send) return 0;


        if (success_redo_and_send > 1) {
            debugf("[llrar] CACHE: back-read found in cache!\n");
            if ((node->type & REQUEST_TYPE_CLOSED)) {
                debugf("[llrar] closing socket due to TYPE_CLOSED\n");
                // Close rar too
                lion_disconnect(node->althandle);
                // Close http socket
                lion_close(node->handle);
                return 1;
            }

            debugf("[llrar] socket read restored %p\n",
                   node->handle);
            lion_enable_read(node->handle);
            return 1; // Two passes means we are done.
        }

    } // while 1.

    /* not reached */
    return 0;

 failed:
    debugf("[llrar] failure while cache-buffer send.\n");
    lion_close(node->handle);
    return 1; // it does less code above to return success

}

#endif


void llrar_dumpcache()
{
    int i;
    llrar_cache_entry_t *runner;

    for (i = 0; i < LLRAR_CACHE_SIZE; i++) {
        if (llrar_cache[i].path) {
            debugf("[llrar] in CACHE slot %d\n", i);
            debugf("'%s'\n", llrar_cache[i].path);
            for (runner = llrar_cache[i].files; runner; runner=runner->next) {
                debugf("    %10"PRIu64" %s\n",
                       runner->file.size,
                       runner->file.name);
            }
        }
    }
}


lion64_t llrar_getsize(request_t *node)
{
    int i;
    llrar_cache_entry_t *runner;
    char *path, *name = NULL;

    debugf("[llrar] CACHE Looking for RAR '%s' rar_file '%s' \n",
           node->disk_path,
           node->rar_file ? node->rar_file : "");

    if (!node->rar_file) return 0;

    if (!node->rar_directory)
        path = "/";
    else
        path = node->rar_directory;

    for (i = 0; i < LLRAR_CACHE_SIZE; i++) {

        if (llrar_cache[i].path &&
            !mystrccmp(llrar_cache[i].path, node->disk_path)) {

            name = misc_strjoin(path, node->rar_file);

            debugf("        found RAR: looking for '%s'\n",
                   name);

            for (runner = llrar_cache[i].files; runner; runner=runner->next) {

                if (!mystrccmp(name, runner->file.name)) {
                    SAFE_FREE(name);
                    return runner->file.size;

                }
            }

            SAFE_FREE(name);

        } // RAR name match

    } // for all cache nodes

    return 0; // Not found
}
