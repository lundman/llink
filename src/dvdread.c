#if HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Interface API between llink and libdvdread.
 * 
 * Unfortunately libdvdread has no support of non-blocking-IO, so we attempt
 * to simulate that as best as possible. We will read DVD blocks, and send
 * down the lion_t until we receive a buffering event. Only at this point to
 * we return and IO loop continues. Naturally we will have a maximum-blocks
 * to iterate as not to cause starvation of lion.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"
#include "dirlist.h"

#if WITH_DVDREAD
#include "dvdread/dvd_reader.h"
#undef stat // it defines it to stat64, and we do as well.
#endif

#define DEBUG_FLAG 512
#include "debug.h"
#include "dvdread.h"
#include "conf.h"
#include "parser.h"
#include "request.h"
#include "skin.h"


#if WITH_DVDREAD
static int dvdread_abort_read = 0;
#endif


#define MAX_TITLES 100

#define DVDREAD_MAX_ITERATIONS 100 // 200k


int dvdread_init(void)
{
#if WITH_DVDREAD
	DVDInit();
#endif
	return 0;
}



void dvdread_free(void)
{
#if WITH_DVDREAD
	DVDFinish();
#endif
}

void dvdread_release(request_t *node)
{
	debugf("[dvdread] release %p\n", node);

#if WITH_DVDREAD
	if (node->dvdfile) {
		//this is probably no longer needed, as we call this differently.
		//dvdread_abort_read = 1;
		DVDCloseFile((dvd_file_t *)node->dvdfile);
		debugf("[dvdread] released dvdread\n");
	}
	if (node->dvdread)
		DVDClose((dvd_reader_t *)node->dvdread);
#endif

	node->dvdfile = NULL;
	node->dvdread = NULL;
	node->dvdvobs = 0;
	node->dvdread_parse = 0;
	if (node->current_event == REQUEST_EVENT_DVDREAD_SEND)
		node->current_event = REQUEST_EVENT_NEW;

}



// ************************************************************************* //

#if WITH_DVDREAD
int dvdread_list_sub(request_t *node, int title, dvd_read_domain_t domain, int part)
{
	dvd_stat_t dvdstat;
	char *ext="", buffer[1024];
	lion64_t fsize;

	if (!node->dvdread) return -1;

	memset(&dvdstat, 0, sizeof(dvdstat));

	if (DVDFileStat(node->dvdread, title, domain, &dvdstat))
		return -1;

	if (!part) 
		debugf("[dvdread] title %d claims %d parts\n", title, dvdstat.nr_parts);

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

	misc_stripslash(node->path);

	if (!title)
		snprintf(buffer, sizeof(buffer), 
 "-r--r--r-- 1 dvdread dvdread %"PRIu64" May 30 10:42 %s?fv=%"PRIu64",%s/VIDEO_TS%s",
				 (lion64_t) dvdstat.size,
				 node->path, 
				 (lion64_t) dvdstat.size,
				 node->dvdread_directory ? node->dvdread_directory : "",
				 ext);
	else
		snprintf(buffer, sizeof(buffer), 
 "-r--r--r-- 1 dvdread dvdread %"PRIu64" May 30 10:42 %s?fv=%"PRIu64",%s/VTS_%02d_%d%s",
				 fsize,
				 node->path, 
				 fsize,
				 node->dvdread_directory ? node->dvdread_directory : "",
				 title,
				 part,
				 ext);

	debugf("[dvdread] %s\n", buffer);
	skin_write_type(node, buffer);

	return 0;
}
#endif






//
// Take a http request node, which should have "cwd" filled in appropriately.
// We then call dirlist to issue listing(s) for this path.
//
void dvdread_list(request_t *node)
{
	debugf("[dvdread] list requested for '%s'\n", node->disk_path);
#if WITH_DVDREAD
{	
	char buffer[1024];
	int title, part;

	debugf("[dvdread] dvdread_dir '%s', dvdread_file '%s', dvdread_name '%s'\n", 
		   node->dvdread_directory ? node->dvdread_directory : "(null)",
		   node->dvdread_file ? node->dvdread_file : "(null)",
		   node->dvdread_name ? node->dvdread_name : "(null)");

	// Release all, not really needed, clearnode() does it.
	dvdread_release(node);


	// Warning, this appears to use chdir().
	node->dvdread = (dvd_reader_t *)DVDOpen(node->disk_path);

	if (!node->dvdread) {
		debugf("[dvdread] failed to DVDOpen device '%s':%s\n",
			   node->disk_path,
			   strerror(errno));
		request_finishdir(node);
		return;
	}

	// If it is root, just fake a video_ts folder first.
	// "drwxr-xr-x  1 unrar unrar %s Jun 7 %s %s&dv=%s/%s",
	// node->path,
	// node->dvdread_directory ? node->dvdread_directory : "",
	// name);
	if (!conf_dvdread_nots) {
		if (!node->dvdread_directory || !mystrccmp(node->dvdread_directory, "/")) {
			
			snprintf(buffer, sizeof(buffer), 
					 "dr-xr-xr-x 1 dvdread dvdread %"PRIu64" May 30 10:42 %s?dv=%s/VIDEO_TS",
					 node->bytes_size,
					 node->path, 
					 node->dvdread_directory ? node->dvdread_directory : "");
			
			skin_write_type(node, buffer);
			request_finishdir(node);
			
			dvdread_release(node);
			return;
		}
	}

	// "List" the contents of the ISO file.
	dvdread_list_sub(node, 0, DVD_READ_INFO_FILE, 0);
	dvdread_list_sub(node, 0, DVD_READ_INFO_BACKUP_FILE, 0);
	dvdread_list_sub(node, 0, DVD_READ_MENU_VOBS, 0);

	for (title = 1; title < MAX_TITLES; title++) {
		if (dvdread_list_sub(node, title, DVD_READ_INFO_FILE, 0)) 
			break; // end of titles?
		dvdread_list_sub(node, title, DVD_READ_INFO_BACKUP_FILE, 0);
		dvdread_list_sub(node, title, DVD_READ_MENU_VOBS, 0);

		if (conf_dvdread_merge) {

			dvdread_list_sub(node, title, DVD_READ_TITLE_VOBS, 1);

		} else {

			for (part = 1; part < 10; part++) 
				if (dvdread_list_sub(node, title, DVD_READ_TITLE_VOBS, part))
					break; // end of parts?

		} // merge?

	} // titles



	//skin_write_type(node, list);

	dvdread_release(node);
	
	SAFE_FREE(node->dvdread_name);
	request_finishdir(node);
}
#else

 debugf("[dvdread] ERROR: compiled without libdvdread.\n");
 request_reply(node, 500, "Compiled without libdvdread");

#endif
}




void dvdread_get(request_t *node)
{
	debugf("[dvdread]  get requested for '%s'\n", node->disk_path);
#if WITH_DVDREAD
{
	int domain=0, title=0, part = 0;
	char *ext, *name;

	debugf("[dvdread] dvdread_dir '%s', dvdread_file '%s', dvdread_name '%s'\n", 
		   node->dvdread_directory ? node->dvdread_directory : "(null)",
		   node->dvdread_file ? node->dvdread_file : "(null)",
		   node->dvdread_name ? node->dvdread_name : "(null)");

	// not really needed clearnode takes care of it.
	dvdread_release(node);

	// Warning, this appears to use chdir().
	node->dvdread = (dvd_reader_t *)DVDOpen(node->disk_path);
	
	if (!node->dvdread) {
		debugf("[dvdread] failed to DVDOpen device '%s':%s\n",
			   node->disk_path,
			   strerror(errno));
		request_reply(node, 500, "DVDOpen failed to open");
		return;
	}

	// First we have to take the filename:
	// dvdread_file '/VIDEO_TS/VTS_02_0.IFO
	// and translate the .IFO to the dvd_domain
	// then the _02_ into title
	// and finally _0 into part, if any.
	if (!node->dvdread_file) {
		request_reply(node, 500, "ISO filename is NULL");
		return;
	}

	name = strrchr(node->dvdread_file, '/');
	ext = strrchr(node->dvdread_file, '.');

	if (!name || !ext) {
		request_reply(node, 500, "Unable to parse name or ext from request");
		return;
	}

	name++; // skip the "/"
	debugf("[dvdread] '%s'\n", name);
	node->dvdvobs = 1;
		
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
		node->dvdvobs = 0;
	} else if (!mystrccmp(".BUP", ext)) {
		domain = DVD_READ_INFO_BACKUP_FILE;
		node->dvdvobs = 0;
	}

	debugf("[dvdread] '%s' translates to domain %d, title %d, part %d\n",
		   node->dvdread_file, domain, title, part);


	// Open said thingy!
	node->dvdfile = (void *)DVDOpenFile((dvd_reader_t *)node->dvdread, 
										title, domain);
	if (!node->dvdfile) {
		debugf("[dvdread] DVDOpenFile(%p, %d, %d) failed\n",
			   node->dvdread, title, domain);
		request_reply(node, 500, "DVDOpenFile failed to open");
		return;
	}

	// lseek() if needed, non-vobs
	if (!node->dvdvobs && node->bytes_from) {
		// int? so can't handle files over 4.4GB, but perhaps nor can
		// UDF. Files, not VOBs
		DVDFileSeek((dvd_file_t *)node->dvdfile, (int) node->bytes_from);
	}

	if (node->dvdvobs) {
		// If they asked for a higher part, we need to skip forward
		// the size of the previous parts.
		dvd_stat_t dvdstat;
		int i;
		memset(&dvdstat, 0, sizeof(dvdstat));

		if (!DVDFileStat(node->dvdread, title, domain, &dvdstat)) {

			// If bytes_to is not specified, we make it the end of this
			// part.
			if (!node->bytes_to) {
				if (conf_dvdread_merge) 
					node->bytes_to = (lion64_t) dvdstat.size; 
				else
					node->bytes_to = (lion64_t) dvdstat.parts_size[ part-1 ]; 
			}
			
			// Add on the offset
			if (conf_dvdread_merge)
				node->bytes_send = 0;
			else
				for (i = 0; i < (part-1); i++) {
					node->bytes_send += (lion64_t) dvdstat.parts_size[ i ];
				}
		} // stat ok

	} // VOBs and "part" requested.

	debugf("[dvdread] Start: from %"PRIu64" -> %"PRIu64". Size %"PRIu64" bytes.\n",
		   node->bytes_from + node->bytes_send,
		   node->bytes_to + node->bytes_send,
		   node->bytes_to - node->bytes_from);


	// Tell main we want to send data.
	node->dvdread_parse = 1;
	node->current_event = REQUEST_EVENT_DVDREAD_SEND;

}
#endif
}

void dvdread_buffer_used(request_t *node)
{

	// We overload this variable as a general "paused" setting,
	// or rather "want_send".
	node->dvdread_parse = 0;

}

void dvdread_buffer_empty(request_t *node)
{

	node->dvdread_parse = 1;
	node->current_event = REQUEST_EVENT_DVDREAD_SEND;

}


#if WITH_DVDREAD
int dvdread_send_data(request_t *node)
{
	char dvdblock[DVD_VIDEO_LB_LEN];
	int i;
	lion64_t rbytes;
	lion64_t block, modulus = 0;

	if (!node->dvdread_parse) return 1; // paused, do nothing
	if (!node->dvdfile) return 1; // no dvdread file handle open, do nothing

	// Is this file, or VOB mode?
	if (!node->dvdvobs) {

		// For max iterations, and as long as "want data" is set:
		for (i = 0; 
			 (i < DVDREAD_MAX_ITERATIONS) && node->dvdread_parse;
			 i++) {

			if (dvdread_abort_read) {
				debugf("[dvdread] file closed, aborting\n");
				dvdread_abort_read = 0;
				return 0;
			}

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

				if (node->handle) {
					
					if ((node->type & REQUEST_TYPE_CLOSED)) {
						debugf("[dvdread] closing socket due to TYPE_CLOSED\n");
						// Close http socket
						lion_close(node->handle);
						return 0;
					}
					
					debugf("[dvdread] socket read restored %p\n",
						   node->handle);
					lion_enable_read(node->handle);
					
				} 

				// Stop everything.
				dvdread_release(node);
				return 0;

			}

			// We have data, send all, or some?
			if (node && node->handle) {
				node->bytes_sent += rbytes;
				if (!lion_send(node->handle, dvdblock, (int)rbytes))
					return 0;
			}

		} // MAX_ITERATIONS

		return 1;
	} // !vobs
	


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
		debugf("[dvdread] block %"PRIu64" modulus %"PRIu64"\n", block, modulus);

	// Lets start reading blocks!
	// For max iterations, and as long as "want data" is set:
	for (i = 0; 
		 (i < DVDREAD_MAX_ITERATIONS) && node->dvdread_parse;
		 i++) {

		if (dvdread_abort_read) {
			debugf("[dvdread] vobfile closed, aborting\n");
			dvdread_abort_read = 0;
			return 0;
		}

		// If we have already read past the "end", we simulate EOF.
		if (node->bytes_from >= node->bytes_to) {
			debugf("[dvdread] from >= to, simulating EOF\n");
			rbytes = 0;
		} else {
			rbytes = DVDReadBlocks(node->dvdfile,
								   block,
								   1,
								   (unsigned char *)dvdblock);
		}

		if (!rbytes) {
			debugf("[dvdread] dvdblock finished/failed\n");
			
			if (node->handle) {
				
				if ((node->type & REQUEST_TYPE_CLOSED)) {
					debugf("[dvdread] closing socket due to TYPE_CLOSED\n");
					// Close http socket
					lion_close(node->handle);
					return 0;
				}
				
				debugf("[dvdread] socket read restored %p\n",
					   node->handle);
				lion_enable_read(node->handle);
				
			} 
			
			// Stop everything.
			dvdread_release(node);
			return 0;
			
		} // !readblock


		// We have data, send all, or some?
		if (node && node->handle) {
			lion64_t tosend;

			tosend = (lion64_t)sizeof(dvdblock) - modulus;
			if (node->bytes_from + tosend >= node->bytes_to)
				tosend = node->bytes_to - node->bytes_from;

			//debugf("[dvdread] block %d modulus %d tosend %"PRIu64" bytes_from %"PRIu64"\n", 
			//				   block, modulus, tosend, node->bytes_from);

			node->bytes_sent += tosend;
			node->bytes_from += tosend;

			// Work out how much to "actually" send.
			// We want to send block-modulus
			// but if that goes past bytes_to, trim it further.

			if (!lion_send(node->handle, &dvdblock[modulus],
						   (int)tosend)) return 0;
		} else return 0;

		// Get ready for next block
		modulus = 0;
		block++;
		
	} // MAX_ITERATIONS

	return 1;
}
#endif



#if WITH_DVDREAD
int dvdread_resume_sub(lion_t *handle, void *arg1, void *arg2)
{
	request_t *node;


	if (lion_gettype(handle) != LION_TYPE_PIPE) return 1;

	if (lion_get_handler(handle) != request_handler) return 1;

	node = lion_get_userdata(handle);

	//debugf("[dvdread] resume_sub check %p:%p\n", handle, node);

	if (!node) return 1;

	if (!node->dvdfile) return 1;

	if (!node->dvdread_parse) return 1;

	debugf("[dvdread] resume_sub check %p:%p\n", handle, node);

	if (!dvdread_send_data(node)) return 0;

	return 1;
}
#endif



//
// For windows platforms, we can't use fifos/pipes, so we have to create
// a file to read, but we might read faster than the external script writes
// at least until the network buffers are full, so we tell lion not to close
// at EOF and disable read.. we need to re-enable read here to try again.
void dvdread_resume(void)
{

#if WITH_DVDREAD
	lion_find(dvdread_resume_sub, NULL, NULL);
#endif

}


void dvdread_rarlist(request_t *node)
{
#if WITH_DVDREAD
	debugf("[dvdread] RAR mode starting...\n");





#endif
}

void dvdread_rarget(request_t *node)
{
#if WITH_DVDREAD
	debugf("[dvdread] RAR GET mode starting...\n");





#endif
}

