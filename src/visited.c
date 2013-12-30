#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#if HAVE_LIBGEN_H
#include <libgen.h>
#endif


#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"

#define DEBUG_FLAG 1024
#include "debug.h"
#include "conf.h"
#include "parser.h"
#include "request.h"
#include "visited.h"

#include "gdbm.h"



static char *visited_dbfile     = NULL;
static int   visited_mintime    = 30;
static float visited_percent    = 75.0;
static int   visited_use_ipfile = 0;
static char *visited_watched    = NULL;

char *visited_upnp_precat   = NULL;
char *visited_upnp_postpend = NULL;



// We will cache multiple files when we open files based on
// the IP, so we do not need to open/close too often.
#define VISITED_CACHE 5


struct visited_struct {
	char *fname;
	bytes_t bytes;
	time_t ctime;
	time_t mtime;
	int seen;
};

typedef struct visited_struct visited_t;


struct visited_dbfile_struct {
    GDBM_FILE dbf;
    char *ip;
    time_t time;

#define VISITED_MAX 1000
    visited_t visited_list[VISITED_MAX];

    // Circular position index
    int visited_insert_pos;
    int visited_search_pos;
    // Strictly not required, but speeds-up searches when less than MAX
    int visited_num;

};

typedef struct visited_dbfile_struct visited_dbfile_t;

// Define our cache for db files now.
THREAD_SAFE static visited_dbfile_t visited_dbfiles[VISITED_CACHE];


// Because the way that Syabas gets videos, (open, lseek, read 16k, close)
// we never see "one long transfer" of media. This means we need to remember
// the last few transfers, and slowly tally up the byte count.



visited_t *visited_db_find(visited_dbfile_t *, char *fname);
int visited_db_update(visited_dbfile_t *, visited_t *vnode);



visited_dbfile_t *visited_open_dbfile(request_t *node, int readonlyorcreate)
{
    char fname[512], *r, *ipstr;
    visited_dbfile_t *dbfile = NULL;
    int index, i, len;
    GDBM_FILE dbf = NULL;
    time_t lowest = 0;


    if (!visited_use_ipfile && visited_dbfile) {
        // Use slot 0.
        dbfile = &visited_dbfiles[0];

        // Not open? Open it ...
        // The test for 'time' is so we only try/fail once.
        if (!dbfile->dbf && !dbfile->time) {

            debugf("[visited] opening DB file '%s'\n", visited_dbfile);
            dbfile->dbf = gdbm_open(visited_dbfile,
                                    0,
                                    GDBM_WRCREAT,
                                    0600,
                                    NULL);
            if (!dbf)
                debugf("[visited] failed to open DB file '%s': errno %d, gdbm_errno %d: %s\n",
                       visited_dbfile,
                       errno,
                       gdbm_errno,
                       gdbm_strerror(gdbm_errno));

        } // if open

        dbfile->time = lion_global_time;

        // We are done in this simple mode.
        return dbfile;
    }




    // visited_use_ipfile == TRUE


    // Check first if we already have a slot for this IP.
    ipstr = lion_ntoa(node->ip);

    for (i = 0; i < VISITED_CACHE; i++) {
        if (visited_dbfiles[ i ].dbf &&
            visited_dbfiles[ i ].ip &&
            !strcmp(visited_dbfiles[ i ].ip, ipstr)) {
            // We have a slot, AND it is already opened
            dbfile = &visited_dbfiles[ i ];
            dbfile->time = lion_global_time;
            return dbfile;
        } // if open
    } // for all slots



    // Well bugger, now the question is: Is there a file there
    // to open for read. If not, are we supposed to create one.
    // First, work out what the filename should be ...

    r = strrchr(visited_dbfile, '.');
    if (r)
        len = strlen(visited_dbfile) - strlen(r);
    else
        len = strlen(visited_dbfile);

    // "llink.bdb" -> "llink-127.0.0.1.bdb"
    // "viewdb" -> "viewdb-127.0.0.1"
    snprintf(fname, sizeof(fname),
             "%*.*s-%s%s",
             len,len, visited_dbfile,
             lion_ntoa(node->ip),
             r ?  r  : "");
    debugf("[visited] DB name to be '%s'\n", fname);

    // If it is for 'readonly', try to open it. If that fails, just return
    // If 'readonly' open works, then we need to find a slot for it.
    // If we are to 'create', then open/create and find a slot.

    if (!readonlyorcreate) {
        dbf = gdbm_open(fname,
                        0,
                        GDBM_WRITER,
                        0600,
                        NULL);
    } else {
        dbf = gdbm_open(fname,
                        0,
                        GDBM_WRCREAT,
                        0600,
                        NULL);
        if (!dbf)
            debugf("[visited] failed to open DB file '%s': errno %d, gdbm_errno %d: %s\n",
                   fname,
                   errno,
                   gdbm_errno,
                   gdbm_strerror(gdbm_errno));
    }

    // No file, nothing to do.
    if (!dbf) return NULL;


    debugf("[visited] opening DB file '%s'\n", fname);


    // We have opened a file, for better or worse. Find a slot for it.
    // Either an empty one, or, the least-recently changed.
    for (dbfile = NULL, lowest = 0, index = 0, i = 0;
         i < VISITED_CACHE;
         i++) {

        // If we found a free one, stop
        if (!visited_dbfiles[ i ].dbf) {
            dbfile = &visited_dbfiles[ i ];
            break;
        }

        if (visited_dbfiles[ i ].time <= lowest) {
            lowest = visited_dbfiles[ i ].time;
            index = i;
        }
    } // for all slots

    // None free? Then pick oldest entry.
    if (!dbfile)
        dbfile = &visited_dbfiles[ index ];

    // Old entry open?
    if (dbfile->dbf) {
        gdbm_close(dbfile->dbf);
        SAFE_FREE(dbfile->ip);
    } // old open?

    // Assign slot
    dbfile->dbf  = dbf;
    dbfile->ip   = strdup(ipstr);
    dbfile->time = lion_global_time;

    return dbfile;
}



int visited_init(void)
{
	int i,j;

    // Clear the cache.
    for (i = 0; i < VISITED_CACHE; i++) {
        visited_dbfiles[i].dbf  = NULL;
        visited_dbfiles[i].ip   = NULL;
        visited_dbfiles[i].time = 0;

        for (j = 0; j < VISITED_MAX; j++) {
            memset(&visited_dbfiles[i].visited_list[j], 0, sizeof(visited_t));
        }

        visited_dbfiles[i].visited_insert_pos = 0;
        visited_dbfiles[i].visited_search_pos = 0;
        visited_dbfiles[i].visited_num = 0;
    }

	return 0;
}



void visited_free(void)
{
	int i,j ;

    for (i = 0; i < VISITED_CACHE; i++) {

        if (visited_dbfiles[i].dbf)
            gdbm_close(visited_dbfiles[i].dbf);

        visited_dbfiles[i].dbf = NULL;
        SAFE_FREE(visited_dbfiles[i].ip);
        visited_dbfiles[i].time = 0;

        visited_dbfiles[i].visited_insert_pos = 0;
        visited_dbfiles[i].visited_search_pos = 0;
        visited_dbfiles[i].visited_num = 0;

        for (j = 0; j < VISITED_MAX; j++) {
            SAFE_FREE(visited_dbfiles[i].visited_list[ i ].fname);
        }
    }

    SAFE_FREE(visited_upnp_postpend);
    SAFE_FREE(visited_upnp_precat);
}





//
//
// CMD      : VISITED
// Required : FILE, MINTIME, PERCENT
// Optional : IPNAME, WATCHED=path
//
void visited_cmd(char **keys, char **values,
			  int items,void *optarg)
{
	char *file, *mintime, *percent, *ipname, *watched, *precat, *postpend;

	// Required
	file    = parser_findkey(keys, values, items, "FILE");
	mintime = parser_findkey(keys, values, items, "MINTIME");
	percent = parser_findkey(keys, values, items, "PERCENT");

	if (!file || !*file || !mintime || !*mintime || !percent || !*percent) {
		printf("      : [visited] missing required field: FILE, MINTIME, PERCENT\n");
		//do_exit = 1;
		return;
	}

	SAFE_COPY(visited_dbfile, file);
	visited_mintime = atoi(mintime);
	visited_percent = atof(percent);

	// Optional
	ipname  = parser_findkey(keys, values, items, "IPNAME");
    if (ipname && atoi(ipname)) {
        visited_use_ipfile = 1;
    }

	watched = parser_findkey(keys, values, items, "WATCHED");
    if (watched && *watched) {
        debugf("[visited] adding watched directory '%s'.\n", watched);
        visited_watched = strdup(watched);
    }

	precat  = parser_findkey(keys, values, items, "UPNP_PRECAT");
    if (precat && *precat)
        visited_upnp_precat = strdup(precat);

	postpend  = parser_findkey(keys, values, items, "UPNP_POSTPEND");
    if (postpend && *postpend)
        visited_upnp_postpend = strdup(postpend);

}



void visited_freenode(visited_t *vnode)
{
	SAFE_FREE(vnode->fname);
	memset(vnode, 0, sizeof(*vnode));
}




//
// Search for existing node matching this name, starting from
// "search_pos". (Looping at the end). Update search_pos to found location.
//
visited_t *visited_find(visited_dbfile_t *dbfile, char *fname)
{
	int i, place;

    if (!dbfile) return NULL;

	for (i = 0, place = dbfile->visited_search_pos;
		 i < dbfile->visited_num;
		 i++, place++) {

		//if (place >= VISITED_MAX) place = 0;
		if (place >= dbfile->visited_num) place = 0;

		if (dbfile->visited_list[ place ].fname &&
            !mystrccmp(fname, dbfile->visited_list[ place ].fname)) {
			dbfile->visited_search_pos = place;
			return &dbfile->visited_list[ place ];
		}
	}

    // Attempt to locate the node in the disk storage:
    return visited_db_find(dbfile, fname);

}



// Use the next "insert_pos" node, clear it, add in new name, and ctime.
// return it.
visited_t *visited_add(visited_dbfile_t *dbfile, char *fname)
{
	visited_t *vnode;

    if (!fname || !*fname) return NULL;
    if (!dbfile) return NULL;

	// Already added? We do no longer check this here, we check it before
    // calling visited_add(). Otherwise we end up in a loop from db_add
	//if ((vnode = visited_find(fname))) return vnode;

	// insert_pos points to where we insert, free it first.
	vnode = &dbfile->visited_list[ dbfile->visited_insert_pos ];
	visited_freenode(vnode);

	// loop the list if we stepped over.
	if (dbfile->visited_insert_pos++ >= VISITED_MAX)
        dbfile->visited_insert_pos = 0;

	vnode->fname = strdup(fname);
	vnode->ctime = lion_global_time;

	if (dbfile->visited_num < VISITED_MAX)
		dbfile->visited_num++;

	debugf("[visited] added '%s'\n", fname);

	return vnode;
}



//
// This function is closed when a file has been closed, and we should
// decide if it was "watched" enough to be listed as seen.
//
// We are given normal path, plus rar_file if unrar code is active.
//
void visited_fileclosed(request_t *node)
{
	time_t duration;
	bytes_t in = 0, out;
	float percent = 0.0;
	visited_t *vnode;
    char *dir = NULL, *path = NULL;
    visited_dbfile_t *dbfile = NULL;

	// Quick reasons as to why we should ignore this file
	if (!node) return;

	debugf("[visited] closed file '%s' ('%s'): alt %p size %"PRIu64"\n", 
		   node->path,
           node->rar_file ? node->rar_file : "",
		   node->althandle,
		   node->bytes_size);

	//if (!node->althandle) return;
	if (!node->bytes_size) return;

	if (node->althandle) {	
		lion_get_bytes(node->althandle, &in, &out);
		duration = lion_get_duration(node->althandle);
	} else if (node->handle) {
		return;
		lion_get_bytes(node->handle, &out, &in);
		duration = lion_get_duration(node->handle);
	} 

	// No bytes sent
	if (!in) return;


	// Watched it for minimum time?
   	if (duration <= visited_mintime) return;

    // Set the path to use.
    if (node->rar_file) {
        path = misc_strjoin(node->path, node->rar_file);
    } else {
        path = strdup(node->path);
    }

	// Calculate percentage bytes sent
	// look up the node, or create, so we can keep stats.
    dbfile = visited_open_dbfile(node, 1);
    debugf("[visited] open dbfile %p\n", dbfile);

	vnode  = visited_find(dbfile, path); // find in RAM or DISK

	if (!vnode)
		vnode = visited_add(dbfile, path); // ADD to RAM

	if (!vnode) goto finished;


	// Update node.
	vnode->bytes += in;

	// Let's only update it once a second.
	if (vnode->mtime == lion_global_time) goto finished;

	vnode->mtime = lion_global_time;


	//percent = node->bytes_size * 100 / vnode->bytes;
	percent = vnode->bytes * 100 / node->bytes_size;
	duration = vnode->mtime - vnode->ctime;

	debugf("[visited] file played for %ld seconds\n", duration);
	debugf("[visited] and we have sent %"PRIu64" bytes out of %"PRIu64". Bytes percent %2.2f%%\n",
		   vnode->bytes, node->bytes_size, percent );

	// Not watched enough
	if (percent < visited_percent) goto finished;

	if (vnode->seen) goto finished;

	// Update the DB here.
	debugf("[visited] adding to db '%s'\n", vnode->fname);

	vnode->seen = 1;

	// Write us to disk.
    dbfile = visited_open_dbfile(node, 1); // Create if not found.
	visited_db_update(dbfile, vnode); // ADD to DISK


    // Also add the containing directory?
    dir = dirname(node->path);

    debugf("[visited] calling dirname(%s) -> '%s'\n", node->path, dir);

    // If we had a dir, and its not root.
    if (dir && *dir && strcmp(dir, "/")) {
        vnode = visited_find(dbfile, dir);

        if (!vnode)
            vnode = visited_add(dbfile, dir);

        if (!vnode) goto finished;

        if (!vnode->seen) {
            debugf("[visited] also adding directory: '%s'\n",
                   dir);
            vnode->mtime = lion_global_time;
            vnode->seen = 1;
            visited_db_update(dbfile, vnode); // ADD to DISK
            //vnode = visited_find(dir);

        } else {

            // if NOT vnode->seen
            // Check if the directory timestamp is newer than saved, then
            // perhaps directory has newer items, so we mark it unseen.

            // TODO

        } // !vnode->seen

    }

    // Additionally, the code above would, when given:
    // "dir/file.rar/file.avi" would mark
    // "dir/file.rar" as visited, but we also want
    // "dir" to be marked.
    if (node->rar_file) {

        dir = dirname(path);

        debugf("[visited] calling dirname(%s) -> '%s'\n", path, dir);

        // If we had a dir, and its not root.
        if (dir && *dir && strcmp(dir, "/")) {
            vnode = visited_find(dbfile, dir);

            if (!vnode)
                vnode = visited_add(dbfile, dir);

            if (!vnode) goto finished;

            if (!vnode->seen) {
                debugf("[visited] also adding directory: '%s'\n",
                       dir);
                vnode->mtime = lion_global_time;
                vnode->seen = 1;
                visited_db_update(dbfile, vnode); // ADD to DISK
                // vnode = visited_find(dir);
            }
        }
    } // rar_file



 finished:
    SAFE_FREE(path);

}


//
// This is called for every file item when listing directories (so make it light)
// to test if a file has been visited already.
// Note the path is CGI encoded, which is rather annoying.
//
// file->name = file.rar?f=12334,/name.avi
//
int visited_filename(request_t *node, char *filename)
{
	visited_t *vnode;
    char *fname = NULL;
    char *r;
    visited_dbfile_t *dbfile = NULL;

    fname = strdup(filename);
    if ((r = strchr(fname, '?'))) *r = 0; // Terminate at cgi start
    if ((r = strchr(fname, '&'))) *r = 0; // Terminate at cgi start

    debugf("[visited] file: '%s'\n", fname);

    dbfile = visited_open_dbfile(node, 0);
	vnode  = visited_find(dbfile, fname);

	debugf("[visited] testing '%s'. %s found. (%d)\n", fname,
		   vnode ? "" : "not",
           vnode ? vnode->seen : -1);

    SAFE_FREE(fname);

	return vnode ? vnode->seen : 0;
}

int visited_file(request_t *node, lfile_t *file)
{
    return visited_filename(node, file->name);
}



int visited_db_update(visited_dbfile_t *dbfile, visited_t *vnode)
{
    datum lore, data;
    int ret;

    if (!dbfile || !dbfile->dbf) return 1; // No DB file open.

    // The key is the filename path
    lore.dptr  = vnode->fname;
    lore.dsize = strlen(vnode->fname) + 1; // Include the NULL in key.

    // Data is really just the "existance" but, we could just store it all
    data.dptr  = (void *)vnode;
    data.dsize = sizeof(*vnode);

    ret = gdbm_store(dbfile->dbf, lore, data, GDBM_REPLACE);

    if (ret) {
        debugf("[visited] error inserting '%s' into DB: %d : %s\n",
               vnode->fname,
               gdbm_errno,
               gdbm_strerror(gdbm_errno));
        return 2;
    }

    debugf("[visited] Added '%s' to DB.\n", vnode->fname);

    if (visited_watched) {
        char *path, *path2;
        FILE *fd = NULL;

        path = misc_strjoin(visited_watched, hide_path(vnode->fname));
        if (path) {

            path2 = misc_strjoin(path, "/watched");
            if (path2) {

                path2[ strlen(path2) - 8 ] = '.'; //Change / to .

                fd = fopen(path2, "w");
                if (fd)
                    fclose(fd);

                debugf("[visited] Also added '%s'.\n", path2);

                SAFE_FREE(path2);

            } // path2

            SAFE_FREE(path);

        } // path

    }

    return 0;
}

visited_t *visited_db_find(visited_dbfile_t *dbfile, char *fname)
{
    datum lore, data;
    visited_t *result;
    visited_t tmp;

    if (!dbfile || !dbfile->dbf) return NULL;

    lore.dptr  = fname;
    lore.dsize = strlen(fname) + 1;

    data = gdbm_fetch(dbfile->dbf, lore);

    // Did we get something?
    if (!data.dptr || (sizeof(tmp) != data.dsize))
        return NULL;

    // We need to turn this into a real node.
    result = visited_add(dbfile,fname);

    // Copy over the information.
    memcpy(&tmp, data.dptr, sizeof(tmp));

    result->seen  = tmp.seen;
    result->mtime = tmp.mtime;
    result->ctime = tmp.ctime;
    result->bytes = tmp.bytes;

    return result;
}


