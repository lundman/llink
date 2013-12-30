#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"
#include "dirlist.h"

#include "dvd_types.h"
//#include "dvd_reader.h"
#include "dvdread/nav_types.h"
#include "dvdread/ifo_types.h" /* For vm_cmd_t */
#include "dvdnav.h"
#include "dvdnav_events.h"
#include "dvdread/dvd_reader.h"
#undef stat // it defines it to stat64, and we do as well.

#define DEBUG_FLAG 512
#include "debug.h"
#include "conf.h"
#include "parser.h"
#include "request.h"
#include "skin.h"
#include "visited.h"
#include "advd.h"

#define MAX_TITLES 100

#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf



//
// advd_list /path/filename.iso
// return lion_t, that they can set_handler, etc.
// Send dirlisting results to handler.
//
// advd_get /path/filename.iso VIDEO_TS/VTS_01_0.VOB 123456
// return lion_t
// send data to handler.
//
// advd_stop /path/filename.iso
//
// Attempt to keep dvdread_t open, so it needs not re-authenticate drives.
// forced close, or timeout?
//
//
struct advd_node_struct {
	char *path;       // /path/file.ISO
	char *archive;    // /path/file.RAR
	char *filename;   // VTS_01_1.VOB
    char *subdir;     // title1/
	lion64u_t offset; // lseek start position.
	lion64u_t end_offset; // lseek start position.
	int idle;
	lion_t *child_handle;
	request_t *request_node;
};

typedef struct advd_node_struct advd_t;

struct advd_child_node {
	dvd_reader_t *dvdread;
	dvdnav_t *dvdnav;
	lion_t *handle;
	dvd_file_t *dvdfile;
	int dvdvobs;
	lion64u_t bytes_from;
	lion64u_t bytes_to;
	lion64u_t bytes_send;
	lion64u_t bytes_sent;
	int paused;
    int audio_stream;
    int audio_select;
    uint64_t seek_block;
};

typedef struct advd_child_node advd_child_t;





int advd_list_handler(lion_t *handle, void *user_data,
					  int status, int size, char *line);
int advd_child_handler(lion_t *handle, void *user_data,
					   int status, int size, char *line);
int advd_child_main(lion_t *parent_handle, void *user_data, void *arg);
int advd_get_handler(lion_t *handle, void *user_data,
					 int status, int size, char *line);
void advd_doget(advd_child_t *advd_cnode, char *line);
int advd_send_data(advd_child_t *node);


int advd_init(void)
{
	return 0;
}

void advd_free(void)
{

}


void advd_buffer_used(request_t *node)
{
	advd_t *advd_node;

	if (node->advd_node) {
		advd_node = (advd_t *)node->advd_node;
		if (advd_node->child_handle)
			lion_disable_read(advd_node->child_handle);
	}
}


void advd_buffer_empty(request_t *node)
{
	advd_t *advd_node;

	if (node->advd_node) {
		advd_node = (advd_t *)node->advd_node;
		if (advd_node->child_handle)
			lion_enable_read(advd_node->child_handle);
	}
}




void advd_freenode(advd_t *dvdnode)
{

	if (!dvdnode) return;

	debugf("[advd] freenode(%p:%s)\n",
		   dvdnode, dvdnode && dvdnode->path ? dvdnode->path : "(null)");

	if (dvdnode->request_node) {
		request_t *node = dvdnode->request_node;

		dvdnode->request_node->advd_node = NULL;

		// We need to tell request we closed, so that it can close its socket
		//visited_fileclosed(node);
        // Actually no, if we are in advd_list, closing the handle now, possibly
        // while expanding a directory listing would be rather short-sighted.
		// However, if its a GET request, we do need to close it.
#if 1
		if (node->handle) {

			debugf("[advd] socket read restored\n");
			lion_enable_read(node->handle);

			if ((node->type & REQUEST_TYPE_CLOSED)) {
				debugf("[advd] closing socket due to TYPE_CLOSED\n");
				lion_close(node->handle);
			}

		}
#endif

	}

	if (dvdnode->child_handle) {
		debugf("[advd] closing child handle\n");
		lion_set_userdata(dvdnode->child_handle, NULL);

		debugf("[advd] parent->child socket %d\n", lion_fileno(dvdnode->child_handle));
#ifdef WIN32
		// Windows does not close the pipe if there is data in it, and fd_set for read
		// does not trigger (and read returns EWOULDBLOCK). So we have to force it.
		shutdown(lion_fileno(dvdnode->child_handle), 2);
#endif
		lion_close(dvdnode->child_handle);
	}
	dvdnode->child_handle = NULL;

	dvdnode->request_node = NULL;
	SAFE_FREE(dvdnode->subdir);
	SAFE_FREE(dvdnode->path);
	SAFE_FREE(dvdnode->filename);
	SAFE_FREE(dvdnode->archive);
	SAFE_FREE(dvdnode);

}


void advd_freenode_req(request_t *node)
{
	advd_t *anode;

	anode = (advd_t *)node->advd_node;
	node->advd_node = NULL;

	if (anode)
		advd_freenode(anode);

	node->advd_node = NULL;

}


//
// Open a ISO file.
//
int advd_list(char *isoname, char *archive, request_t *request_node)
{
	advd_t *dvdnode;

	dvdnode = malloc(sizeof(*dvdnode));
	if (!dvdnode) return -1;
	memset(dvdnode, 0, sizeof(*dvdnode));

	dvdnode->request_node = request_node;
	dvdnode->path = strdup(isoname);
	if (archive)
		dvdnode->archive = strdup(archive);

    SAFE_COPY(dvdnode->subdir, request_node->dvdread_directory);

	debugf("[advd] starting worker child for '%s' (%s). Subdir '%s'\n",
           isoname,
		   archive?archive:"",
           dvdnode->subdir?dvdnode->subdir:"");


	// fork off a child.
	dvdnode->child_handle = lion_fork(advd_child_main, LION_FLAG_NONE,
									  (void *)dvdnode, NULL);
	if (!dvdnode->child_handle) {
		debugf("[advd] failed to work eh..\n");
		//		if (dvdnode->handler)
		//	dvdnode->handler();
		// FIXME
		return 1;
	}

	lion_set_handler(dvdnode->child_handle, advd_list_handler);

	request_node->advd_node = (void *) dvdnode;

	// Opened, return new node
	return 0;
}





int advd_list_handler(lion_t *handle, void *user_data,
					  int status, int size, char *line)
{
	advd_t *dvdnode;

	dvdnode = (advd_t *) user_data;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[advd] child is alive, sending listing request.\n");
		lion_printf(dvdnode->child_handle, "LIST %s\r\n",
                    dvdnode->subdir ? dvdnode->subdir : "");
		break;

	case LION_PIPE_FAILED:
		debugf("[advd] child failed.\n");
		// FIXME, send event.
		advd_freenode(dvdnode);
		break;

	case LION_PIPE_EXIT:
		debugf("[advd] child exit.\n");
		// FIXME, send event.
		advd_freenode(dvdnode);
		break;

	case LION_INPUT:
		debugf("[advd] list '%s'\n", line);

		if ((size == 4) &&
			!mystrccmp(":END", line)) {
			debugf("[advd] finished dirlist.\n");
			if (dvdnode->request_node)
				request_finishdir(dvdnode->request_node);
			break;
		}

		// "fv=36864,/VIDEO_TS.IFO"
		if (dvdnode->request_node && (size > 4)) {
			char buffer[1024];
			lion64u_t size = 0;

            //drhorrible.iso?dv=228696000,/title_01_(00%3A42%3A21)&h=5,1,2&s=0

			if (line[2] == '=')
				size = (lion64u_t) strtoull(&line[3], NULL, 10);

			// If it is RAR listing, add in the archive name.
			if (dvdnode->archive)
				snprintf(buffer, sizeof(buffer),
  "%cr--r--r-- 1 dvdread dvdread %"PRIu64" Aug 16 14:52 %s?f=%"PRIu64",%s&%s",
                         line[0] == 'd' ? 'd' : '-',
						 size,
						 dvdnode->request_node->path,
						 dvdnode->request_node->bytes_size,
						 dvdnode->path,
						 line);

			else
				snprintf(buffer, sizeof(buffer),
				"%cr--r--r-- 1 dvdread dvdread %"PRIu64" Aug 16 14:52 %s?%s",
                         line[0] == 'd' ? 'd' : '-',
						 size,
						 dvdnode->request_node->path,
						 line);

			skin_write_type(dvdnode->request_node, buffer);
		}
		break;

	}

	return 0;
}



// filename.RAR archive
// filename.ISO path
// filename.VOB filename

int advd_get(char *vobname, char *isoname, char *archive, request_t *request_node)
{
	advd_t *dvdnode;

	// Fix subtitles later.
	if (!lfnmatch("*.srt", vobname, LFNM_CASEFOLD))
		return -1;
	if (!lfnmatch("*.smi", vobname, LFNM_CASEFOLD))
		return -1;
	if (!lfnmatch("*.sub", vobname, LFNM_CASEFOLD))
		return -1;
	if (!lfnmatch("*.ssa", vobname, LFNM_CASEFOLD))
		return -1;

	if (request_node->head_method)
		return 0;

	dvdnode = malloc(sizeof(*dvdnode));
	if (!dvdnode) return -1;
	memset(dvdnode, 0, sizeof(*dvdnode));

	dvdnode->request_node = request_node;
	dvdnode->path = strdup(isoname);
	if (archive)
		dvdnode->archive = strdup(archive);
	dvdnode->filename = strdup(vobname);
	dvdnode->offset = request_node->bytes_from;
	dvdnode->end_offset = request_node->bytes_to ? request_node->bytes_to : request_node->bytes_size;

	debugf("[advd] starting worker child for '%s:%s:%s'.\n",
		   dvdnode->filename, dvdnode->path, archive?archive:"");

	// fork off a child.
	dvdnode->child_handle = lion_fork(advd_child_main, LION_FLAG_NONE,
									  (void *)dvdnode, NULL);
	if (!dvdnode->child_handle) {
		debugf("[advd] failed to work eh..\n");
		//		if (dvdnode->handler)
		//	dvdnode->handler();
		// FIXME
		return 1;
	}

	lion_set_handler(dvdnode->child_handle, advd_get_handler);
	request_node->advd_node = (void *) dvdnode;

	// Opened, return new node
	return 0;
}



int advd_get_handler(lion_t *handle, void *user_data,
					 int status, int size, char *line)
{
	advd_t *dvdnode;

	dvdnode = (advd_t *) user_data;
	if (!dvdnode) return -1;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[advd] child is alive, sending GET request.\n");
		lion_printf(dvdnode->child_handle, "GET %"PRIu64",%"PRIu64",%s\r\n",
					dvdnode->offset, dvdnode->end_offset, dvdnode->filename);
		lion_enable_binary(dvdnode->child_handle);
		break;

	case LION_PIPE_FAILED:
		debugf("[advd] child failed.\n");
		// FIXME, send event.
		advd_freenode(dvdnode);
		break;

	case LION_PIPE_EXIT:
		debugf("[advd] child exit.\n");
		// FIXME, send event.
		advd_freenode(dvdnode);
		break;

	case LION_INPUT:
		break;
	case LION_BINARY:
		if (dvdnode->request_node &&
			dvdnode->request_node->handle) {
			if (lion_output(dvdnode->request_node->handle,
							line,
							size) < 0)
				advd_freenode(dvdnode); // ??
		}
		break;

	}

	return 0;
}








/* *********************************************************************** *
 * *********************************************************************** *
 *                                                                         *
 * The following functions are all run as Child Process.                   *
 *                                                                         *
 * *********************************************************************** *
 * *********************************************************************** */




// ************************************************************************* //

int advd_dolist_sub(advd_child_t *advd_cnode, int title,
					dvd_read_domain_t domain, int part)
{
	dvd_stat_t dvdstat;
	char *ext="";
	lion64_t fsize;

	if (!advd_cnode || !advd_cnode->dvdread) return -1;
	if (!advd_cnode->handle) return -1;

	memset(&dvdstat, 0, sizeof(dvdstat));

	if (DVDFileStat(advd_cnode->dvdread, title, domain, &dvdstat))
		return -1;

	if (!part)
		debugf("[advd_child] title %d claims %d parts\n",
			   title, dvdstat.nr_parts);

	if (dvdstat.nr_parts < part)
		return -1;

	switch(domain) {
    case DVD_READ_INFO_FILE:
		ext = ".IFO";
		break;
    case DVD_READ_MENU_VOBS:
    case DVD_READ_TITLE_VOBS:
		ext = ".VOB";
		break;
    case DVD_READ_INFO_BACKUP_FILE:
		ext = ".BUP";
		break;
	}

	// "-rwxr-xr-x  1 unrar unrar %s Jun 7 %s %s&fv=%s,%s/%s",
	//fsize,
	//						 node->path,
	//						 fsize,
	//						 node->dvdread_directory ? node->dvdread_directory : "",
	//						 name);
	if (conf_dvdread_merge) {

		fsize = (lion64_t) dvdstat.size;

	} else {

		fsize = ((domain == DVD_READ_TITLE_VOBS) && dvdstat.parts_size[ part - 1 ])
			? (lion64_t) dvdstat.parts_size[ part - 1]
			: (lion64_t) dvdstat.size;
	}

	if (!title) {
		if (lion_printf(advd_cnode->handle,
						"fv=%"PRIu64",/VIDEO_TS%s\r\n",
						(lion64_t) dvdstat.size,
						ext) < 0)
			advd_cnode->handle = NULL;
	} else {
		if (lion_printf(advd_cnode->handle,
						"fv=%"PRIu64",/VTS_%02d_%d%s\r\n",
						fsize,
						title,
						part,
						ext) < 0)
			advd_cnode->handle = NULL;
	}

	return 0;
}


static char *time2str(uint64_t time)
{
    static char workstr[100];
    unsigned short int days,hours,mins,secs;;

    time /= 90000;
    days=(time/86400),hours,mins,secs;
    hours=((time-(days*86400))/3600);
    mins=((time-(days*86400)-(hours*3600))/60);
    secs=(time-(days*86400)-(hours*3600)-(mins*60));

    workstr[0]=(char)0;
    //if(days)
    //  snprintf(workstr,sizeof(workstr), "%dd",days);
    snprintf(workstr,sizeof(workstr),"%s%s%02d",workstr,(workstr[0])?":":"",hours);
    snprintf(workstr,sizeof(workstr),"%s%s%02d",workstr,(workstr[0])?":":"",mins);
    snprintf(workstr,sizeof(workstr),"%s%s%02d",workstr,(workstr[0])?":":"",secs);

    if (!days && !hours && !mins && !secs)
    snprintf(workstr,sizeof(workstr),"00:00:00");

    return(workstr);
}
char *lang2str(int32_t num, uint32_t lang)
{
    static char result[3];

    result[2] = 0;

    if (lang == 0xffff) return "??";

	if (!lang) {
		snprintf(result, sizeof(result), "%02u", num);
		return result;
	}

    result[0] = (lang >> 8) & 0xff;
    result[1] = lang & 0xff;

    return result;
}


char *format2str(uint16_t format)
{
    switch(format) {
    case DVDNAV_FORMAT_AC3:
        return "AC3";
    case DVDNAV_FORMAT_MPEGAUDIO:
        return "MPEGAudio";
    case DVDNAV_FORMAT_LPCM:
        return "LPCM";
    case DVDNAV_FORMAT_DTS:
        return "DTS";
    case DVDNAV_FORMAT_SDDS:
        return "SDDS";
    default:
        return "Unknown";
    }

}


lion64_t advd_title_getnumbytes(advd_child_t *node)
{
    uint32_t pos, end;
	dvdnav_status_t     result;
	int                 event, len;
    unsigned char buffer[DVD_VIDEO_LB_LEN];
    unsigned char *block;
    int i = 0;


    while (1) {

        block = buffer;
		result = dvdnav_get_next_cache_block(node->dvdnav,
                                             &block, &event, &len);
		if(result == DVDNAV_STATUS_ERR) return 0;

        if (block != buffer)
            dvdnav_free_cache_block(node->dvdnav, block);


		switch(event) {
		case DVDNAV_CELL_CHANGE:
			debugf("Block %d CELL_CHANGE\n", i);
            pos = 0;
            len = 0;
            dvdnav_get_position(node->dvdnav, &pos, &end);
            debugf("pos %u and len %u\n", pos, end);
            if (end) return (end * DVD_VIDEO_LB_LEN);
			break;
        case DVDNAV_STILL_FRAME:
            dvdnav_still_skip(node->dvdnav);
            break;
		case DVDNAV_WAIT: //                     13
            dvdnav_wait_skip(node->dvdnav);
			break;
		case DVDNAV_BLOCK_OK:
		case DVDNAV_STOP:
            debugf("[advd] failed to get numbytes (%d)\n", event);
            return 0;
        default:
            break;
        }
        i++;
    }

    // not reached
    return 0;

}

//
// Take a http request node, which should have "cwd" filled in appropriately.
// We then call dirlist to issue listing(s) for this path.
//
void advd_dolist(advd_child_t *advd_cnode, char *subdir)
{
	int title, part;
	dvd_dir_t *dvd_dir;
	dvd_dirent_t *dvd_dirent;

	debugf("[advd_child] list requested: '%s'\n", subdir);

#if 0
	debugf("[advd_child] dvdread_dir '%s', dvdread_file '%s', dvdread_name '%s'\n",
		   node->dvdread_directory ? node->dvdread_directory : "(null)",
		   node->dvdread_file ? node->dvdread_file : "(null)",
		   node->dvdread_name ? node->dvdread_name : "(null)");

	// If it is root, just fake a video_ts folder first.
	// "drwxr-xr-x  1 unrar unrar %s Jun 7 %s %s&dv=%s/%s",
	// node->path,
	// node->dvdread_directory ? node->dvdread_directory : "",
	// name);
	if (!conf_dvdread_nots) {
		if (!node->dvdread_directory || !mystrccmp(node->dvdread_directory, "/")) {

			lion_printf(*handle,
						"?dv=/VIDEO_TS\n");
			return;
		}
	}
#endif

	// Test for BD-ISO?
    if (advd_cnode->dvdread) {

        dvd_dir = DVDOpenDir(advd_cnode->dvdread, "/BDMV/STREAM");
        if (!dvd_dir) dvd_dir = DVDOpenDir(advd_cnode->dvdread, "/HVDVD_TS");
        if (dvd_dir) {
            while((dvd_dirent = DVDReadDir(advd_cnode->dvdread, dvd_dir))) {
                if (dvd_dirent->d_type == DVD_DT_REG) {

                    if (!lfnmatch("?????.m2ts", (char *)dvd_dirent->d_name,
                                  LFNM_CASEFOLD)) {

                        if (lion_printf(advd_cnode->handle,
                                        "fv=%"PRIu64",/BDMV/STREAM/%s\r\n",
                                        dvd_dirent->d_filesize,
                                        dvd_dirent->d_name) < 0)
                            advd_cnode->handle = NULL;
                    } else if (!lfnmatch("FEATURE_*.EVO",(char *)dvd_dirent->d_name,
                                         LFNM_CASEFOLD)) {

                        if (lion_printf(advd_cnode->handle,
                                        "fv=%"PRIu64",/HVDVD_TS/%s\r\n",
                                        dvd_dirent->d_filesize,
                                        dvd_dirent->d_name) < 0)
                            advd_cnode->handle = NULL;
                    } // if m2ts or .evo
                } // DT_REG file
                if (!advd_cnode->handle) break;
            } // while readdir
            DVDCloseDir(advd_cnode->dvdread, dvd_dir);
            return;
        }
        return;
    } // dvdread/BD-ISO

#if 0
	// "List" the contents of the ISO file.
	advd_dolist_sub(advd_cnode, 0, DVD_READ_INFO_FILE, 0);
	advd_dolist_sub(advd_cnode, 0, DVD_READ_INFO_BACKUP_FILE, 0);
	advd_dolist_sub(advd_cnode, 0, DVD_READ_MENU_VOBS, 0);

	for (title = 1; title < MAX_TITLES; title++) {
		if (advd_dolist_sub(advd_cnode, title, DVD_READ_INFO_FILE, 0))
			break; // end of titles?
		advd_dolist_sub(advd_cnode, title, DVD_READ_INFO_BACKUP_FILE, 0);
		advd_dolist_sub(advd_cnode, title, DVD_READ_MENU_VOBS, 0);

		if (conf_dvdread_merge) {

			advd_dolist_sub(advd_cnode, title, DVD_READ_TITLE_VOBS, 1);

		} else {

			for (part = 1; part < 10; part++)
				if (advd_dolist_sub(advd_cnode, title, DVD_READ_TITLE_VOBS, part))
					break; // end of parts?

		} // merge?

	} // titles
#endif

    // dvdnav is loaded
    if (advd_cnode->dvdnav) {
        int32_t numTitles, numStreams;
        uint64_t *times = NULL, duration;
        lion64u_t bytes = 0;


        // List a specific title, or all titles?
        if (subdir && !strncmp("/title_", subdir, 7)) {

            // Find title
            title = atoi(&subdir[7]);
            debugf("[advd] requested title %02u.\n", title);

            // iterate all the audio streams
            dvdnav_title_play(advd_cnode->dvdnav, title);

            // Find the actual length of this title in bytes. This
            // appears to not work with libdvdnav until after we
            // get a CELL_CHANGE.
            bytes = advd_title_getnumbytes(advd_cnode);
            if (!bytes) {
                debugf("[advd] failed to get numbytes of title. Using 8GB as emergency value\n");
                bytes = (lion64u_t)8 * 1024 * 1024 * 1024;
            }

            debugf("     Title is %"PRIu64" bytes.\n", bytes);

            debugf("     Checking audio streams from title %d\n", title);

            // Look up the size for filesize, not really used.
            duration = 0;
            if (dvdnav_describe_title_chapters(advd_cnode->dvdnav,
                                               title, &times,
                                               &duration) > 0) {
                free(times);
            }

            for (numStreams = 0; numStreams <= 8; numStreams++) {
                audio_attr_t audio_attr;
                int32_t logical;

                if ((logical = dvdnav_get_audio_logical_stream(advd_cnode->dvdnav, numStreams)) < 0) break;

                dvdnav_get_audio_attr(advd_cnode->dvdnav, logical,
                                      &audio_attr);

                debugf("     title %d audio %d stream: %08X '%s' format %s channels %u\n",
                       title, numStreams,
                       audio_attr.lang_code,
                       lang2str(numStreams, audio_attr.lang_code),
                       format2str(dvdnav_audio_stream_format(advd_cnode->dvdnav,
                                                             logical)),
                       dvdnav_audio_stream_channels(advd_cnode->dvdnav,
                                                    logical));


                if (lion_printf(advd_cnode->handle,
            "fv=%"PRIu64",/title_%02u_audio_%02u_(%s:%s:%uch).mpg\r\n",
                                bytes,
                                title, numStreams,
                                lang2str(numStreams, audio_attr.lang_code),
                                format2str(
                                dvdnav_audio_stream_format(advd_cnode->dvdnav,
                                                         logical)
                                           ),
                                dvdnav_audio_stream_channels(advd_cnode->dvdnav,
                                                             logical)) < 0) {

                    advd_cnode->handle = NULL;
                    return ;
                } // if lion_printf

            } // for numStreams

            return;
        } // list specific title





        // We can enumerate the titles?
        if (dvdnav_get_number_of_titles(advd_cnode->dvdnav,
                                        &numTitles) == DVDNAV_STATUS_OK) {
            int32_t numParts;

            debugf("[advd] dvdnav reports %d titles.\n", numTitles);

            // iterate all titles
            for (title = 0; title < numTitles; title++) {

                dvdnav_get_number_of_parts(advd_cnode->dvdnav,
                                           title, &numParts);
                debugf("    title %d has %d parts\n", title, numParts);

                duration = 0;
                if (dvdnav_describe_title_chapters(advd_cnode->dvdnav,
                                                   title, &times,
                                                   &duration) > 0) {
                free(times);
                }

                // Print the entry
                //drhorrible.iso?dv=228696000,/title_01_(00%3A42%3A21)&h=5,1,2&s=0
                if (lion_printf(advd_cnode->handle,
                                "dv=%"PRIu64",/title_%02u_(%s)\r\n",
                                duration,
                                title,
                                time2str(duration)
                                ) < 0 ) {

                    advd_cnode->handle = NULL;
                    return ;
                }

            } // for all titles

        } // num titles ok

    } // dvdnav

}




int advd_child_handler(lion_t *handle, void *user_data,
					   int status, int size, char *line)
{
	advd_child_t *advd_cnode = (advd_child_t *)user_data;


	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[advd_child] parent is alive\n");
		break;
	case LION_PIPE_FAILED:
		debugf("[advd_child] parent failed, and so shall I\n");
		if (advd_cnode) {
			if (advd_cnode->dvdread)
				DVDClose(advd_cnode->dvdread);
			if (advd_cnode->dvdnav)
				dvdnav_close(advd_cnode->dvdnav);
			advd_cnode->dvdread = NULL;
			advd_cnode->dvdnav = NULL;
		}
		break;
	case LION_PIPE_EXIT:
		debugf("[advd_child] parent exited, and so shall I\n");
		if (advd_cnode) {
			if (advd_cnode->dvdread)
				DVDClose(advd_cnode->dvdread);
			if (advd_cnode->dvdnav)
				dvdnav_close(advd_cnode->dvdnav);
			advd_cnode->dvdread = NULL;
			advd_cnode->dvdnav = NULL;
		}
		break;
	case LION_INPUT:
		debugf("[advd_child] input '%s'\n", line);

		if (!advd_cnode) return -1;

		if ((size > 4) && !strncmp("LIST ", line, 5)) {
			// Move handle to myhandle, so we can pass by reference, this is
			// then set to NULL if there was output errors. (socket was closed)
			debugf("[advd_child] got LIST request...\n");
			advd_dolist(advd_cnode, &line[5]);

			if (advd_cnode->handle)
				lion_printf(advd_cnode->handle, ":END\r\n");

			if (advd_cnode->dvdread)
				DVDClose(advd_cnode->dvdread);
			if (advd_cnode->dvdnav)
				dvdnav_close(advd_cnode->dvdnav);
			advd_cnode->dvdread = NULL;
			advd_cnode->dvdnav  = NULL;
			break;
		}
		if ((size > 4) && !strncmp("GET ", line, 4)) {
			// [advd_child] input 'GET 0,0,/VTS_01_1.VOB'
			advd_doget(advd_cnode, &line[4]);
		}
		break;

	case LION_BUFFER_USED:
		//debugf("[paused]\n");
		advd_cnode->paused = 1;
		break;

	case LION_BUFFER_EMPTY:
		//debugf("[play]\n");
		advd_cnode->paused = 0;
		break;

	}
	return 0;
}


static void advd_signal_handler(void)
{

	debugf("[advd_child] sigpipe ignored.\n");

}
#include <signal.h>
int advd_child_main(lion_t *parent_handle, void *user_data, void *arg)
{
	char *filename, *isoname;
	advd_t *dvdnode;
	advd_child_t advd_cnode;

#ifndef WIN32
	// Under Unix we rely on SIGPIPE to kill the underlying UNRAR process,
	// the way to do that is to set it here, then popen() will make it default.
	signal(SIGPIPE, advd_signal_handler);
#endif

	memset(&advd_cnode, 0, sizeof(advd_cnode));

	lion_set_handler(parent_handle, advd_child_handler);
	lion_set_userdata(parent_handle, (void *) &advd_cnode);

	dvdnode = (advd_t *)user_data;
	if (!dvdnode) lion_exitchild(1);

	filename = strdup(dvdnode->path);

	debugf("[advd_child] child->parent socket %d\n", lion_fileno(parent_handle));

	// Attempt to open the said file/device.
	if (dvdnode->archive) {
		debugf("[advd_child] Calling dvdnav in RAR mode (%s,%s)\n",
			   dvdnode->archive, filename);

		isoname = dvdnode->path;
		// No leading "/" in rar
		while (*isoname == '/') isoname++;

        // Change to use RAR input methods
        dvdinput_setupRAR(skin_get_rar(), isoname);

		//advd_cnode.dvdread = DVDOpen(dvdnode->archive);
		dvdnav_open(&advd_cnode.dvdnav, dvdnode->archive);

        // Opening the DVD might fail, if it is a UDF2.50 image.
        if (!advd_cnode.dvdnav) {
            // Attempt to open dvdread directly.
            advd_cnode.dvdread = DVDOpen(dvdnode->archive);
        }

		isoname = NULL;

	} else {
        // Change to use default input methods
        dvdinput_setupRAR(NULL, NULL);
		//advd_cnode.dvdread = DVDOpen(filename);
		dvdnav_open(&advd_cnode.dvdnav, filename);

        // Opening the DVD might fail, if it is a UDF2.50 image.
        if (!advd_cnode.dvdnav) {
            // Attempt to open dvdread directly.
            advd_cnode.dvdread = DVDOpen(filename);
        }
	} // rar or not


	dvdnode = NULL; /* Child should not use this */
	advd_cnode.handle = parent_handle;

	if (!advd_cnode.dvdnav && !advd_cnode.dvdread) {
		debugf("[advd_child] failed to dvdnav_open/DVDOpen device '%s':%s\n",
			   filename,
			   strerror(errno));
		lion_printf(parent_handle, ":END\r\n");
	}

    // Set some dvdnav specific defaults
    if (advd_cnode.dvdnav) {
        if (dvdnav_set_readahead_flag(advd_cnode.dvdnav,  1) !=
            DVDNAV_STATUS_OK) {
            debugf("Error on dvdnav_set_readahead_flag: %s\n",
                   dvdnav_err_to_string(advd_cnode.dvdnav));
        }
        /* set the language */
        if (dvdnav_menu_language_select(advd_cnode.dvdnav, "en") !=
            DVDNAV_STATUS_OK ||
            dvdnav_audio_language_select(advd_cnode.dvdnav, "en") !=
            DVDNAV_STATUS_OK ||
            dvdnav_spu_language_select(advd_cnode.dvdnav, "en") !=
            DVDNAV_STATUS_OK) {
            debugf("Error on setting languages: %s\n",
                   dvdnav_err_to_string(advd_cnode.dvdnav));
        }
        if (dvdnav_set_PGC_positioning_flag(advd_cnode.dvdnav, 1) !=
            DVDNAV_STATUS_OK) {
            debugf("Error on dvdnav_set_PGC_positioning_flag: %s\n",
                   dvdnav_err_to_string(advd_cnode.dvdnav));
        }
    } // if dvdnav

	while(advd_cnode.dvdnav || advd_cnode.dvdread) {
		if (lion_poll(0, 1) < 0) break;
		if (advd_cnode.dvdread || advd_cnode.dvdnav)
			advd_send_data(&advd_cnode);
	}

	if (advd_cnode.dvdnav) {
		dvdnav_close(advd_cnode.dvdnav);
		advd_cnode.dvdnav = NULL;
	}
	if (advd_cnode.dvdread) {
		DVDClose(advd_cnode.dvdread);
		advd_cnode.dvdread = NULL;
	}

	lion_close(parent_handle);
	parent_handle = NULL;

	debugf("[advd_child] done, quitting gracefully.\n");
	lion_exitchild(0);
	return(0); // ignore the warning
}


//
// '0,0,/VTS_01_1.VOB'
//
void advd_doget(advd_child_t *advd_cnode, char *line)
{
	int domain=0, title=0, part = 0;
	char *ext, *name;
	char *dvdread_file, *next = NULL;

	advd_cnode->bytes_from = (lion64u_t) strtoull(line, &next, 10);
	if (!next || (*next!=',')) {
		// Parse error.
		debugf("[advd_child] parse error on '%s'\n", line);
		return; // FIXME
	}
	line=&next[1];
	advd_cnode->bytes_to = (lion64u_t) strtoull(line, &next, 10);
	if (!next || (*next!=',')) {
		// Parse error.
		debugf("[advd_child] parse 2 error on '%s'\n", line);
		return; // FIXME
	}

	dvdread_file = &next[1];

	debugf("[advd_child] dvdread_file '%s' offset %"PRIu64" - %"PRIu64"\n",
		   dvdread_file, advd_cnode->bytes_from, advd_cnode->bytes_to);


    // This is the old dvdread method.
#if 0

	// First we have to take the filename:
	// dvdread_file '/VIDEO_TS/VTS_02_0.IFO
	// and translate the .IFO to the dvd_domain
	// then the _02_ into title
	// and finally _0 into part, if any.

	name = strrchr(dvdread_file, '/');
	ext = strrchr(dvdread_file, '.');

	if (!name || !ext) {
		//request_reply(node, 500, "Unable to parse name or ext from request");
		return ; // FIXME
	}

	while(*name == '/') name++; // skip the "/"
	debugf("[advd_child] '%s'\n", name);
	advd_cnode->dvdvobs = 1;

	//[dvdread] 'VTS_02_0.IFO'
	// Is the filename video_ts.?
	if (!lfnmatch("VIDEO_TS.*", name, LFNM_CASEFOLD)) {
		domain = DVD_READ_MENU_VOBS;
		title = 0;
	}


	// Is the filename "VTS_XX_X.?" This should be most cases for titles
	if (!lfnmatch("VTS_??_?.*", name, LFNM_CASEFOLD)) {

		domain = DVD_READ_TITLE_VOBS;
		title = atoi(&name[4]);
		part  = atoi(&name[7]);

		// If it is _0.* it is also MENU
		if (!part) domain = DVD_READ_MENU_VOBS;

	}

	// Check the extention for the easy ones, VOB gets type from above.
	if (!mystrccmp(".IFO", ext)) {
		domain = DVD_READ_INFO_FILE;
		advd_cnode->dvdvobs = 0;
	} else if (!mystrccmp(".BUP", ext)) {
		domain = DVD_READ_INFO_BACKUP_FILE;
		advd_cnode->dvdvobs = 0;
	}

	debugf("[advd_child] '%s' translates to domain %d, title %d, part %d\n",
		   dvdread_file, domain, title, part);


	// Open said thingy!
	advd_cnode->dvdfile = DVDOpenFile(advd_cnode->dvdread,
									 title, domain);
	if (!advd_cnode->dvdfile) {
		debugf("[advd_child] DVDOpenFile(%p, %d, %d) failed. Trying direct open '%s'\n",
			   advd_cnode->dvdread, title, domain, dvdread_file);

		advd_cnode->dvdfile = DVDOpenFilename(advd_cnode->dvdread, dvdread_file);
		if (!advd_cnode->dvdfile) {
			debugf("[advd_child] failed to open, giving up.\n");

			//request_reply(node, 500, "DVDOpenFile failed to open");
			return; // FIXME
		}
		advd_cnode->dvdvobs = 1; // Simulate block mode.
	}

	// lseek() if needed, non-vobs
	if (!advd_cnode->dvdvobs && advd_cnode->bytes_from) {
		// int? so can't handle files over 4.4GB, but perhaps nor can
		// UDF. Files, not VOBs
		DVDFileSeek(advd_cnode->dvdfile, advd_cnode->bytes_from);
	}

	if (advd_cnode->dvdvobs) {
		// If they asked for a higher part, we need to skip forward
		// the size of the previous parts.
		dvd_stat_t dvdstat;
		int i;
		memset(&dvdstat, 0, sizeof(dvdstat));

		if (!DVDFileStat(advd_cnode->dvdread, title, domain, &dvdstat)) {

			// If bytes_to is not specified, we make it the end of this
			// part.
			if (!advd_cnode->bytes_to) {
				if (conf_dvdread_merge)
					advd_cnode->bytes_to = (lion64_t) dvdstat.size;
				else
					advd_cnode->bytes_to = (lion64_t) dvdstat.parts_size[ part-1 ];
			}

			// Add on the offset
			if (conf_dvdread_merge)
				advd_cnode->bytes_send = 0;
			else
				for (i = 0; i < (part-1); i++) {
					advd_cnode->bytes_send += (lion64_t) dvdstat.parts_size[ i ];
				}
		} // stat ok

	} // VOBs and "part" requested.

#endif

    // If it is dvdread (BD-ISO) attempt to open it directly.
    if (advd_cnode->dvdread) {
		advd_cnode->dvdfile = DVDOpenFilename(advd_cnode->dvdread, dvdread_file);
		if (!advd_cnode->dvdfile) {
			debugf("[advd_child] failed to open, giving up.\n");

			//request_reply(node, 500, "DVDOpenFile failed to open");
			return; // FIXME
		}
		advd_cnode->dvdvobs = 1; // Simulate block mode.
    }


    // Is it a dvdnav (DVD)?
    if (advd_cnode->dvdnav) {
        uint32_t pos, len;

        if (!dvdread_file || strncmp("/title_", dvdread_file, 7)) {
            debugf("[advd_child] requested file '%s' does not start with /title_\n",
                   dvdread_file);
            return;
        }

        title = atoi(&dvdread_file[7]);

        // "/title_01_audio_02_"
        if (strncmp("_audio_", &dvdread_file[9], 7)) {
            debugf("[advd_child] requested file '%s' does not contain _audio_\n",
                   dvdread_file);
            return;
        }

        advd_cnode->audio_stream = atoi(&dvdread_file[16]);
        advd_cnode->audio_select = 1;

        if (dvdnav_title_play(advd_cnode->dvdnav, title) != DVDNAV_STATUS_OK) {
            debugf("[advd_child] there is no title %02u\n", title);
            return;
        }

        debugf("[advd_child] requested title %02u with audio stream %02u\n",
               title, advd_cnode->audio_stream);

        // With dvdnav we can seek to the block we want, but only after the
        // first CELL_CHANGE. Set it up to do so.
        advd_cnode->seek_block = advd_cnode->bytes_from / DVD_VIDEO_LB_LEN;


    } // dvdnav


	debugf("[advd_child] Start: from %"PRIu64" -> %"PRIu64". Size %"PRIu64" bytes.\n",
		   advd_cnode->bytes_from + advd_cnode->bytes_send,
		   advd_cnode->bytes_to + advd_cnode->bytes_send,
		   advd_cnode->bytes_to - advd_cnode->bytes_from);


	// Tell main we want to send data.
	//node->dvdread_parse = 1;
	//node->current_event = REQUEST_EVENT_DVDREAD_SEND;

}


// Apparently MSVC++ don't like these defines
#define Xgetu32(b, l) ({													\
			uint32_t x = (b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]);	\
			b+=4;														\
			l-=4;														\
			x;															\
		})
#define getu32(b, l) (uint32_t)(b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]); b+=4; l-=4;

#define Xgetu16(b, l) ({													\
			uint16_t x = (b[0] << 8 | b[1]);							\
			b+=2;														\
			l-=2;														\
			x;															\
		})
#define getu16(b, l) (uint16_t)(b[0] << 8 | b[1]); b+=2;l-=2;


#define Xgetu8(b, l) ({													\
			uint8_t x = b[0];											\
			b+=1;														\
			l-=1;														\
			x;															\
		})
#define getu8(b, l) b[0]; b++; l--;													\


// This function is shamelessly lifted from lonelycoder's showtime.
// (Andreas Ãman)
int
dvd_pes_block(advd_child_t *node, uint32_t sc, uint8_t *buf, int len,
              uint8_t *sc_ptr)
{
    uint8_t flags, hlen, x;

    x     = getu8(buf, len);
    flags = getu8(buf, len);
    hlen  = getu8(buf, len);

    if(len < hlen)
        return 1;

    if((x & 0xc0) != 0x80) {
        /* no MPEG 2 PES */
        // usually padding packets
        if (sc != 0x1be)
            debugf("Not PES packet %x, remaining %d\n", x, len);
        return 2;
    }

    if((flags & 0xc0) == 0xc0) {
        if(hlen < 10)
            return 3;

        //pts = getpts(buf, len);
        //dts = getpts(buf, len);
        buf += 10;
        len -= 10;

        hlen -= 10;

    } else if((flags & 0xc0) == 0x80) {
        if(hlen < 5)
            return 4;

        //dts = pts = getpts(buf, len);
        buf += 5;
        len -= 5;

        hlen -= 5;
    }

    buf += hlen;
    len -= hlen;

    if(sc == PRIVATE_STREAM_1) {
        if(len < 1)
            return 6;

        sc_ptr = buf;
        sc = getu8(buf, len);

        //printf("Private stream 1 substream %x\n", sc);

    }

    if(sc > 0x1ff)
        return 8;

    if(sc >= 0x1e0 && sc <= 0x1ef) {
        //codec_id  = CODEC_ID_MPEG2VIDEO;
        //debugf("Video stream %x\n", sc - 0x1e0);

    } else if((sc >= 0x80 && sc <= 0x9f) ||
              (sc >= 0x1c0 && sc <= 0x1df)) {

        // audio
        //if((sc & 7) != track)
        //  return NULL;

        switch(sc) {
        //case 0x80 ... 0x87:  // MSVC++ can't do "..."
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
            //codec_id = CODEC_ID_AC3;
            //printf("Audio AC3 stream %x\n", sc-0x80);
            if (node->audio_select) {
                *sc_ptr = 0x80+node->audio_stream;
                node->audio_select = 0;
            }
            break;

	//        case 0x88 ... 0x9f:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x8f:
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0x98:
        case 0x99:
        case 0x9a:
        case 0x9b:
        case 0x9c:
        case 0x9d:
        case 0x9e:
        case 0x9f:
            //codec_id = CODEC_ID_DTS;
            //printf("Audio DTS stream %x\n", sc-0x88);
            if (node->audio_select) {
                *sc_ptr = 0x88+node->audio_stream;
                node->audio_select = 0;
            }
            break;

	//        case 0x1c0 ... 0x1df:
		case 0x1c0:
		case 0x1c1:
		case 0x1c2:
		case 0x1c3:
		case 0x1c4:
		case 0x1c5:
		case 0x1c6:
		case 0x1c7:
		case 0x1c8:
		case 0x1c9:
		case 0x1ca:
		case 0x1cb:
		case 0x1cc:
		case 0x1cd:
		case 0x1ce:
		case 0x1cf:
		case 0x1d0:
		case 0x1d1:
		case 0x1d2:
		case 0x1d3:
		case 0x1d4:
		case 0x1d5:
		case 0x1d6:
		case 0x1d7:
		case 0x1d8:
		case 0x1d9:
		case 0x1da:
		case 0x1db:
		case 0x1dc:
		case 0x1dd:
		case 0x1de:
		case 0x1df:

            //codec_id = CODEC_ID_MP2;
            //printf("Audio MP2 stream %x\n", sc-0x1c0);
            if (node->audio_select) {
                sc_ptr[0] = 0x00;
                sc_ptr[1] = 0x00;
                sc_ptr[2] = 0x01;
                sc_ptr[3] = 0xc0+node->audio_stream;
                node->audio_select = 0;
            }
            break;
        default:
            return 9;
        }

    } else if (sc >= 0x20 && sc <= 0x3f) {

        // subtitle
        //printf("Subtitle stream %x\n", sc-0x20);

    } else {
        debugf("Unknown startcode %x\n", sc);
        return 10;
    }


  return 0;
}






static void
dvd_ps_block(advd_child_t *node, uint8_t *buf, int len)
{
	uint32_t startcode;
	int pes_len;
    int result;
    int8_t stuff = buf[13] & 7;
    uint8_t *sc_ptr;

	buf += 14;
	len -= 14;

	if(stuff != 0)
		return;

	while(len > 0) {

		if(len < 4)
			break;

        sc_ptr = buf;
		startcode = getu32(buf, len);
		pes_len = getu16(buf, len);

		if(pes_len < 3)
			break;
        //
        // Private stream 1 (Audio and subpictures)
        //
        // * sub-stream 0x20 to 0x3f are subpictures
        // * sub-stream 0x80 to 0x87 are audio (AC3, DTS, SDDS)
        // * sub-stream 0xA0 to 0xA7 are LPCM audio
        // case 0x80 ... 0x87:
        //   codec_id = CODEC_ID_AC3;
        // case 0x88 ... 0x9f:
        //   codec_id = CODEC_ID_DTS;
        // case 0x1c0 ... 0x1df:
        //   codec_id = CODEC_ID_MP2;
        //
		switch(startcode) {
		case PADDING_STREAM:
		case PRIVATE_STREAM_1:
		case PRIVATE_STREAM_2:
		//		case 0x1c0 ... 0x1df: // DVDs only allow 8
		case 0x1c0:
		case 0x1c1:
		case 0x1c2:
		case 0x1c3:
		case 0x1c4:
		case 0x1c5:
		case 0x1c6:
		case 0x1c7:
		case 0x1c8:
		case 0x1c9:
		case 0x1ca:
		case 0x1cb:
		case 0x1cc:
		case 0x1cd:
		case 0x1ce:
		case 0x1cf:
		case 0x1d0:
		case 0x1d1:
		case 0x1d2:
		case 0x1d3:
		case 0x1d4:
		case 0x1d5:
		case 0x1d6:
		case 0x1d7:
		case 0x1d8:
		case 0x1d9:
		case 0x1da:
		case 0x1db:
		case 0x1dc:
		case 0x1dd:
		case 0x1de:
		case 0x1df:
		//		case 0x1e0 ... 0x1ef: // DVDs only allow 1
		case 0x1e0:
		case 0x1e1:
		case 0x1e2:
		case 0x1e3:
		case 0x1e4:
		case 0x1e5:
		case 0x1e6:
		case 0x1e7:
		case 0x1e8:
		case 0x1e9:
		case 0x1ea:
		case 0x1eb:
		case 0x1ec:
		case 0x1ed:
		case 0x1ee:
		case 0x1ef:
			result = dvd_pes_block(node, startcode, buf, pes_len, sc_ptr);
			break;

		default:
            debugf("*** <klaxon> No idea %x </klaxon>\n", startcode);
			return;
		}

        len -= pes_len;
        buf += pes_len;

	}
}


lion64_t advd_dvdnav_getblock(advd_child_t *node, lion64_t bnum,
                              unsigned char **block)
{
	dvdnav_status_t     result;
	int                 event, len;
    int i;

    i = (int) bnum;

    while (1) {

		result = dvdnav_get_next_cache_block(node->dvdnav,
                                             block, &event, &len);
		if(result == DVDNAV_STATUS_ERR) return 0;

		switch(event) {
		case DVDNAV_BLOCK_OK:
			//printf("Got block %i \n", i);
			dvd_ps_block(node, *block, len);
            return len;
			break;

		case DVDNAV_NOP: //                       1
			debugf("Block %d NOP\n", i);
			break;
		case DVDNAV_STILL_FRAME: //               2
			debugf("Block %d STILL_FRAME\n", i);
            dvdnav_still_skip(node->dvdnav);
			break;
		case DVDNAV_SPU_STREAM_CHANGE: //         3
			debugf("Block %d SPU_STREAM_CHANGE\n", i);
			break;
		case DVDNAV_AUDIO_STREAM_CHANGE: //       4
			debugf("Block %d AUDIO_STREAM_CHANGE\n", i);
			break;
		case DVDNAV_VTS_CHANGE: //                5
			debugf("Block %d VTS_CHANGE\n", i);
			break;
		case DVDNAV_CELL_CHANGE: //               6
			debugf("Block %d CELL_CHANGE\n", i);
            if (node->seek_block) {
                uint32_t pos = 0, len = 0;
				debugf("[advd_child] finally seeking\n");
                dvdnav_sector_search(node->dvdnav, node->seek_block, SEEK_SET);
                node->seek_block = 0;
                dvdnav_get_position(node->dvdnav, &pos, &len);
                debugf("    pos %u and len %u\n", pos, len);
            }
			break;
		case DVDNAV_NAV_PACKET: //                7
			debugf("Block %d NAV_PACKET\n", i);
			break;
		case DVDNAV_STOP: //                      8
			debugf("Block %d STOP, hammer time\n", i);
            return 0;
			break;
		case DVDNAV_HIGHLIGHT: //                 9
			debugf("Block %d HIGHLIGHT\n", i);
			break;
		case DVDNAV_SPU_CLUT_CHANGE: //          10
			debugf("Block %d SPU_CLUT_CHANGE\n", i);
			break;
		case DVDNAV_HOP_CHANNEL: //              12
			debugf("Block %d HOP_CHANNEL\n", i);
			break;
		case DVDNAV_WAIT: //                     13
			debugf("Block %d WAIT\n", i);
            dvdnav_wait_skip(node->dvdnav);
			break;

		default:
			debugf("unknown event %d\n", event);
			break;
		} // switch

	} // while forever

}




int advd_send_data(advd_child_t *node)
{
	unsigned char dvdblock[DVD_VIDEO_LB_LEN];
    unsigned char *blockptr = NULL;
	int i;
	lion64_t rbytes = 0;
	lion64_t block, modulus = 0;

	//debugf("[advd_child] advd_send_data\n");

	if (node->paused)   return 1; // paused, do nothing
	if (!node->dvdfile && node->dvdread)
        return 1; // no dvdread file handle open, do nothing


    // Technically this iterations thing is no longer required.
#define DVDREAD_MAX_ITERATIONS 1000

#if 0  // This mode does not exist with dvdnav
	// Is this file, or VOB mode?
	if (!node->dvdvobs) {


		// For max iterations, and as long as "want data" is set:
		for (i = 0;
			 (i < DVDREAD_MAX_ITERATIONS) && !node->paused;
			 i++) {

			if (!node->dvdfile) goto finished;

			rbytes = (lion64_t) DVDReadBytes(node->dvdfile,
											 dvdblock,
											 sizeof(dvdblock));
			if (rbytes <= 0) {
				// File finished, or failed.
				if (!rbytes) {
					debugf("[dvdread] file read finished.\n");
				} else {
					debugf("[dvdread] file read failed. \n");
				}

				goto finished;
				return 0;

			}

			// We have data, send all, or some?
			if (node && node->handle) {
				node->bytes_sent += rbytes;
				if (lion_send(node->handle,
                              blockptr ? blockptr : dvdblock,
                              (int)rbytes) < 0)
					goto finished;
			}

		} // MAX_ITERATIONS

		return 1;
	} // !vobs
#endif



	// VOBs
	// In VOB mode we need to compute the block to read from. If bytes_from
	// is in the middle of a block, we send just that little part so that
	// next reads are full blocks.

	// Say bytes_from is 4123456
	// the block to read is: 2013   (4123456 / 2048) = 2013.40625
	// the modulus is: 832
	// so that (2013 * 2048) = 4122624 + 832 => 4123456

	block = (node->bytes_from + node->bytes_send) / (lion64_t) sizeof(dvdblock);
	if (node->bytes_from + node->bytes_send)
		modulus = (node->bytes_from + node->bytes_send) - (block * 2048);

	if (modulus)
		debugf("[advd_child] block %"PRIu64" modulus %"PRIu64"\n", block, modulus);

	// Lets start reading blocks!
	// For max iterations, and as long as "want data" is set:
	for (i = 0;
		 (i < DVDREAD_MAX_ITERATIONS) && !node->paused;
		 i++) {

		// If we have already read past the "end", we simulate EOF.
		if (node->bytes_to &&
            (node->bytes_from >= node->bytes_to)) {
			debugf("[dvdread] from %"PRIu64" >= to %"PRIu64", simulating EOF\n",
                   node->bytes_from,
                   node->bytes_to);
			rbytes = 0;
			goto finished;
		} else {

            if (node->dvdread) {
                rbytes = DVDReadBlocks(node->dvdfile,
                                       block,
                                       1,
                                       dvdblock);
            }
            if (node->dvdnav) {
                blockptr = dvdblock;
                rbytes = advd_dvdnav_getblock(node,
                                              block,
                                              &blockptr);
            }


        }

		if (!rbytes) {
			debugf("[dvdread] dvdblock finished/failed\n");

			goto finished;
			return 0;

		} // !readblock


		// We have data, send all, or some?
		if (node && node->handle && rbytes) {
			lion64_t tosend;

			tosend = (lion64_t)sizeof(dvdblock) - modulus;

			if (node->bytes_from + tosend >= node->bytes_to)
				tosend = node->bytes_to + 1  - node->bytes_from;
			// to + 1 as apache bytes is inclusive.

			//debugf("[dvdread] block %d modulus %d tosend %"PRIu64" bytes_from %"PRIu64"\n",
			//				   block, modulus, tosend, node->bytes_from);

			node->bytes_sent += tosend;
			node->bytes_from += tosend;

			// Work out how much to "actually" send.
			// We want to send block-modulus
			// but if that goes past bytes_to, trim it further.

			if (lion_send(node->handle,
                          blockptr ? (char *)&blockptr[modulus]
                          : (char *) &dvdblock[modulus],
                          (int)tosend) < 0) goto finished;

#if 0
			{
				FILE *fd;
				fd = fopen("stream.mpg", "ab");
				if (fd) {
					fwrite(
						   blockptr ? (char *)&blockptr[modulus]
						   : (char *) &dvdblock[modulus],
						   1,
						   tosend,
						   fd);
					fclose(fd);
				}
			}
#endif

            // If dvdnav, and we got cached block instead of our block, free.
            if (node->dvdnav && (blockptr != dvdblock))
                dvdnav_free_cache_block(node->dvdnav, blockptr);

		} else return 0;

		// Get ready for next block
		modulus = 0;
		block++;

	} // MAX_ITERATIONS

	return 1;

 finished:
	debugf("[advd_child] send_data finished reached.\n");
	if (node->dvdfile)
		DVDCloseFile(node->dvdfile);
	node->dvdfile = NULL;
	if (node->dvdread)
		DVDClose(node->dvdread);
    if (node->dvdnav)
        dvdnav_close(node->dvdnav);
    node->dvdnav = NULL;
	node->dvdread = NULL;
	return -1;

}

