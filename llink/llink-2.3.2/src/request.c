#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
// We need strcasestr on linux;
#define __USE_GNU
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <errno.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#if HAVE_LIBGEN_H
#include <libgen.h>
#endif

#include "lion.h"
#include "misc.h"

#define DEBUG_FLAG 4
#include "debug.h"
#include "httpd.h"
#include "parser.h"
#include "request.h"
#include "query.h"
#include "conf.h"
#include "version.h"
#include "root.h"
#include "skin.h"
#include "mime.h"
#include "external.h"
#include "llrar.h"
#include "xmlscan.h"
#include "visited.h"
#include "cgicmd.h"
#include "cupnp.h"

#if WIN32
#include "win32.h"
#endif

extern int do_exit;



THREAD_SAFE static request_t *request_head = NULL;




int request_writefile_handler(lion_t *handle, void *user_data,
								 int status, int size, char *line);
static int request_post_handler(lion_t *handle, void *user_data,
                                int status, int size, char *line);


static time_t request_idle_time = 0;


void request_update_idle(void)
{
    time(&request_idle_time);
}

time_t request_get_idle(void)
{
    return request_idle_time;
}


int request_init(void)
{

	request_head = NULL;

    request_update_idle();
	return 0;
}




void request_free(void)
{
	request_t *runner;

	// We need to iterate all request nodes here, and free them
	while ((runner = request_head)) {
		request_freenode(runner);
	}

}




request_t *request_newnode(void)
{
	request_t *result;

	result = malloc(sizeof(*result));
	if (!result) return NULL;

	memset(result, 0, sizeof(*result));

	result->pass = 0;

	result->next = request_head;
	request_head = result;

	return result;

}



void request_freenode(request_t *node)
{
	lion_t *tmp = NULL;
	request_t *prev, *runner;

	debugf("[request] freenode %p (%s)\n",
		node, node && node->path ? node->path : "");

	node->current_event = REQUEST_EVENT_NEW;

	if (node->handle) {
		tmp = node->handle;
		node->handle = NULL;
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}

	if (node->althandle) {
		tmp = node->althandle;
		node->althandle = NULL;
		debugf("[request] closing althandle\n");
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}

	if (node->tmphandle) {
		tmp = node->tmphandle;
		node->tmphandle = NULL;
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}

	if (node->external_handle) {
		// We want the external handler to be called, so
		// we can clean up. We do almost no logic in the
		// handler, so it should be safe.
		lion_disconnect(node->external_handle);
	}

	if (node->roothandle) {
		tmp = node->roothandle;
		node->roothandle = NULL;
		debugf("[request] closing roothandle\n");
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}


    skin_cache_clear(node);


#ifdef WIN32
	if (node->pipe_handle)
		CloseHandle(node->pipe_handle);
	node->pipe_handle = NULL;
#endif


	extra_release(&node->extnfo);

	SAFE_FREE(node->path);
	SAFE_FREE(node->cgi);
	SAFE_FREE(node->rar_name);
	SAFE_FREE(node->rar_file);
	SAFE_FREE(node->rar_directory);
	SAFE_FREE(node->disk_path);

	SAFE_FREE(node->dvdread_file);

	SAFE_FREE(node->cgi_file);
	SAFE_FREE(node->history);
	SAFE_FREE(node->typename);

	SAFE_FREE(node->tmpname);

	SAFE_FREE(node->useragent);

    node->keep_path = NULL;
    node->keep_disk_path = NULL;
    node->llrar_cacher = NULL;
    node->llrar_cache_fill = 0;

#ifdef WITH_UNRAR_BUFFERS
    // If we have unrar buffers, free them all, and clear all values.
    {
        int i;
        for (i = 0;
             i < WITH_UNRAR_BUFFERS;
             i++) {
            SAFE_FREE( node->unrar_buffers[ i ] );
            node->unrar_offsets[ i ] = 0;
            node->unrar_sizes[ i ] = 0;
        }
        node->unrar_radix = 0;
    }
#endif

	// Unlink from linked-list
	for (runner = request_head, prev = NULL;
		 runner;
		 prev = runner, runner = runner->next) {

		if (runner == node) {
			if (!prev) request_head = runner->next;
			else prev->next = runner->next;
		}
	}

	SAFE_FREE(node);


}


//
// Reset all state between requests in a Keep-Alive session
//
static void request_clearnode(request_t *node)
{
	lion_t *tmp;

	debugf("[request] clearnode\n");

	node->current_event = REQUEST_EVENT_NEW;

	// In rar mode we allow altandle to keep existing for resume.
	if (!node->rar_name && node->althandle) {
		tmp = node->althandle;
		debugf("[request] releasing althandle\n");
		node->althandle = NULL;
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}

	// In roothandle mode, we can use keep-alives
	if (!node->cgi_host && node->roothandle) {
		tmp = node->roothandle;
		debugf("[request] releasing roothandle\n");
		node->roothandle = NULL;
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}

	if (node->tmphandle) {
		tmp = node->tmphandle;
		node->tmphandle = NULL;
		lion_set_userdata(tmp, NULL);
		lion_disconnect(tmp);
	}


	// Dont clear the file offset if we resume rars
	if (!node->rar_name)
		node->bytes_sent = 0;

	extra_release(&node->extnfo);
    skin_cache_clear(node);

	SAFE_FREE(node->path);
	SAFE_FREE(node->cgi);
	SAFE_FREE(node->disk_path);
	SAFE_FREE(node->tmpname);
	SAFE_FREE(node->rar_directory);
	SAFE_FREE(node->rar_file);
	SAFE_FREE(node->cgi_file);

	SAFE_FREE(node->dvdread_file);

	SAFE_FREE(node->history);
	SAFE_FREE(node->typename);

	SAFE_FREE(node->useragent);

	node->inheader = 0;
	node->type = 0; // safe to clear type? They should send "keep-alive" again
	node->head_method = 0;
	node->post_method = 0;
	node->subscribe_method = 0;
	node->unknown_request = 0;
	node->bytes_to = 0;
	node->bytes_from = 0;
	node->bytes_send = 0;
	node->bytes_size = 0;
	node->time_start = 0;
	node->process_function = 0;
	memset(&node->stat, 0, sizeof(node->stat));
	node->page_from = 0;
	node->page_to= 0;
	node->page_current = 0;
	node->root_index = 0;
	node->cache = NULL;
    node->cgi_stream = 0;
	node->playall = 0;
	node->viewall = 0;
	node->menu_mode = 0;
	node->client_is_syabas = 0;
    node->upnp_type = 0;
    node->send_range = 0;

	node->pass = 0;
    node->llrar_cacher = NULL;
    node->llrar_cache_fill = 0;

    node->transcode_id = 0;

    skin_set_skin(node, NULL);
}




//
// Take a string, and allocate a new string which is URL encoded,
// even if no encoding was needed, it is allocated.
// Is it worth running through once to know how much to allocate, or
// just allocate * 3 ? But to allocate *3 I need to call strlen anyway.
// (Although, some OS can do that based on longs)
//
// Only alphanumerics [0-9a-zA-Z], the special characters "$-_.+!*'(),"
char *request_url_encode(char *s)
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
                //case '\'':
			case '(':
			case ')':
			case ',':
				//case '&': // we need & so we can send cgi style urls
				//case '=': // we need & so we can send cgi style urls
			case '/': // and slash man!
				len++;
				if (result) *r2++ = *r;
				break;
			case '&': // Complete and utter hack, stand back!
			case '?': // Complete and utter hack, stand back!
				// If it is "&d=" we need to NOT encode it, as it comes from
				// rar support. Otherwise, encode it.
				if ((((r[1] == 'd') || (r[1] == 'f')) &&
					 (r[2] == '='))) {
					// leave it alone, IDENTICAL to code above
					len+=3;
					if (result) *r2++ = *r++;
					if (result) *r2++ = *r++;
					if (result) *r2++ = *r;
					break;
				}
				if ((((r[1] == 'd') || (r[1] == 'f')) &&
					 (r[2] == 'v') &&
					 (r[3] == '='))) {
					// leave it alone, IDENTICAL to code above
					len+=4;
					if (result) *r2++ = *r++;
					if (result) *r2++ = *r++;
					if (result) *r2++ = *r++;
					if (result) *r2++ = *r;
					break;
				}

				// It is different? FALL-THROUGH!

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
// Same as above, but strict version. Can let anything through here.
//
char *request_url_encode_strict(char *s)
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
char *request_url_decode(char *s)
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








int request_handler(lion_t *handle, void *user_data,
					int status, int size, char *line)
{
	request_t *node;
	char *ar, *token;
	unsigned long ip;
	int port;


	node = (request_t *) user_data;


	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[request] is listening\n");

		lion_getpeername(handle, &ip, &port);

		// Are they allowed to talk to us?
		if (!query_match_ip(lion_ntoa(ip))) {
			debugf("[request] unauthorised connection from: %s:%d\n",
				   lion_ntoa(ip), port);
			lion_disconnect(handle);
			return 0;
		}



		node = request_newnode();
		if (!node) {
			debugf("[request] failed to allocate memory for connection.\n");
			lion_disconnect(handle);
			return 0;
		}

		// ASSign things to keep
		lion_set_userdata(handle, (void *)node);
		node->handle = handle;
		lion_getpeername(handle, &node->ip, &node->port);

		// FIXME - Check that they are allowed to connect to us.

		debugf("[request] New request from %s:%d %s\n",
			   lion_ntoa(node->ip), node->port, ctime(&lion_global_time));
		break;


	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:

		if (!node) return 0;

		debugf("[request] closed/lost %s:%d (%d:%s): %s\n",
			   lion_ntoa(node->ip), node->port,
			   size, line ? line : "gracefully closed",
			   ctime(&lion_global_time));

		if (node->handle) {
			float in, out;
			lion_get_cps(node->handle, &in, &out);
			debugf("[request] cps in: %.2f out: %.2f sent %"PRIu64" bytes\n",
				   in,out, node->bytes_sent);
			if (node->bytes_from > node->bytes_sent)
				debugf("[request] still lseeking\n");

		}


        // When sending a file is closed.
		visited_fileclosed(node);


		node->handle = NULL;  // Safety.
		node->current_event = REQUEST_EVENT_CLOSED;

		debugf("[request] setting %p event CLOSED\n", node);

		//X request_freenode(node);
		return 0;


	case LION_INPUT:
		// The request comes in here.
		// FIXME - user/pass auth

		if (!node) return 0;

		ar = line;
		// We expect first line of a request to be "GET".
		// Followed by a number of header lines
		// Finally ended with new line.

		debugf("[request] >> '%s'\n",line);

		// We have to started parsing a request, look for GET.
		// "GET /theme/buffalo/tv.jpg HTTP/1.0"
		if (line && *line && !node->inheader) {

			token = misc_digtoken(&ar, " \r\n");

			if (!token || !*token)
				break;

			if (!mystrccmp("GET", token) ||
				!mystrccmp("POST", token) ||
				!mystrccmp("SUBSCRIBE", token) ||
				!mystrccmp("HEAD", token)) {

				if (node->althandle)
					debugf("already going\n");
				// Clear out any old reference.
				request_clearnode(node);

				if (!mystrccmp("HEAD", token))
					node->head_method = 1;
				if (!mystrccmp("POST", token))
					node->post_method = 1;
				if (!mystrccmp("SUBSCRIBE", token))
					node->subscribe_method = 1;

				// Get path element.
				token = misc_digtoken(&ar, " &?\r\n");

				if (!token || !*token)
					break;


				// We need to go find "which" root we are in, incase there are
				// more than one specified.
				// Combine path and skin.
				//SAFE_FREE(node->path);
				//node->path = misc_strjoin(skin_root, token);

				// Path is fix above this.
				node->path = request_url_decode(token);

				//SAFE_COPY(node->path, token);


				node->inheader = 1;


				if ((misc_digtoken_optchar == '&') ||
					(misc_digtoken_optchar == '?')) {

					token = misc_digtoken(&ar, " \r\n");
					// Check if it has a "&". If so, that's cgi stuff.
					if (token && *token) {

						SAFE_COPY(node->cgi, token);

					}
				}

				// TODO Now grab "HTTP/1.x" element.
				if (strstr(ar, "HTTP/1.1")) {
					// Version 1.1 assumes Keep-Alive
					node->type &= ~REQUEST_TYPE_CLOSED;
					node->type |= REQUEST_TYPE_KEEPALIVE;
					debugf("keepalive\n");
				} else { // Version 1.0, assumes close
					node->type |= REQUEST_TYPE_CLOSED;
					node->type &= ~REQUEST_TYPE_KEEPALIVE;
					debugf("close\n");
				}

#if 1
				if (!node->cgi && (token = strchr(node->path, '?'))) {

					debugf("[request] dirty hack for encoded '?' in cgi\n");
					*token = 0;
					SAFE_COPY(node->cgi, &token[1]);
				}
#endif

				debugf("[request] GET for '%s' ('%s'): %s\n",
					   node->path,
					   node->cgi ? node->cgi : "",
					   ctime(&lion_global_time));

				break;

			} // GET

            // Not GET or HEAD, it is in fact an unknown command.
            debugf("[request] unknown command '%s' - will fail at end of header\n",
                   token);
            node->inheader = 1;
            node->unknown_request = 1;
            break;

		} // !inheader



		// We have a line, and we are in the header.
		if (line && *line && node->inheader) {

			// Grep out stuff here we are interested in.

			token = misc_digtoken(&ar, " \r\n");

			if (!token || !*token)
				break;


			if (!mystrccmp("Host:", token)) {

				// Do we care?
				// "Host: 192.168.0.11:8000"
				break;

			} else if (!mystrccmp("User-agent:", token) /*||
                                                          !mystrccmp("X-AV-Client-Info:", token)*/) {

				// "User-agent: Syabas/03-84-041206-02-LTI-232-000/02-LTI (uCOS-II v2.05;NOS;KA9Q; Res1280x720,HiColor; www.syabas.com)"

                SAFE_FREE(node->useragent);
                node->useragent = strdup(ar);


				if (strstr(ar, "Syabas"))
					node->client_is_syabas = 1;

                // Set the skin we are supposed to use with this request.
                skin_set_skin(node, ar);


				// Lets pull out the res, that could be useful.
				// Using strstr is a bad idea.
                // This can probably be retired. We never use the resolution
                // and now do pattern matches on user-agent anyway.
				ar = strstr(ar, "Res");

				if (!ar) break;

				// Skip "Res"
				ar += 3;

				node->width = strtol(ar, &ar, 10);

				// Skip "x"
				ar += 1;

				node->height = strtol(ar, NULL, 10);

				debugf("[request] Res set to %dx%d\n",
					   node->width, node->height);

				break;



			} else if (!mystrccmp("Accept:", token)) {

				// "Accept: */*"
				break;


			} else if (!mystrccmp("Content-Length:", token)) {

				// "Content-Length: 269"
                // We only care about that in POST.
                if (node->post_method) {
                    node->bytes_size = (lion64_t) strtoull(ar, NULL, 10);
                    debugf("[request] POST expecting %"PRIu64" bytes.\n",
                           node->bytes_size);
                }
				break;


			} else if (!mystrccmp("Connection:", token)) {

				// "Connection: Keep-Alive"
				// If it is keep-alive we let them keep issuing
				// requests, otherwise, we close it after sending
				// the file.
				// RFC of HTTP/1.1 say all connections are Keep-Alive unless
				// specifically set to "close". Reflect this here.
				// HTTP/1.0 did not really define it, but it was assumed to
				// be all connections are "close" unless set to Keep-Alive

				token = misc_digtoken(&ar, " \r\n");

				if (!token || !*token)
					break;


				if (!mystrccmp("Keep-Alive", token)) {
					node->type &= ~REQUEST_TYPE_CLOSED;
					node->type |= REQUEST_TYPE_KEEPALIVE;
				} else { // Keep-Alive
					node->type |= REQUEST_TYPE_CLOSED;
					node->type &= ~REQUEST_TYPE_KEEPALIVE;
				} // closed

				debugf("[request] connection type: %x\n",
					   node->type);
				break;

			} else if (!mystrccmp("Cache-Control:", token)) {

				// "Cache-Control: no-cache"
				break;

			} else if (!mystrccmp("Pragma:", token)) {

				// "Pragma: no-cache"
				break;

			} else if (!mystrccmp("Range:", token)) {

				// "Range: bytes=0-"
                node->send_range = 1;

				// This we need to deal with. A from and to
				// part to send.
				ar = strchr(ar, '=');
				if (!ar) break;

				// Skip past "="
				ar++;

				// Skip any potential spaces
				while(*ar == ' ') ar++;

				// do we have a digit?
				if (isdigit(*ar))
					node->bytes_from = strtoull(ar, &ar, 10);
				else
					node->bytes_from = 0; // from BOF

				// Skip any potential spaces
				while(*ar == ' ') ar++;

				// Skip "-"
				ar++;

				// Skip any potential spaces
				while(*ar == ' ') ar++;

				// do we have a digit?
				if (isdigit(*ar))
					node->bytes_to = strtoull(ar, &ar, 10);
				else
					node->bytes_to = 0; // to EOF
				// What if you want just byte 0? I think http specs say
				// ranges: bytes=0-0 to mean that?
				// That's to send nothing

				if (node->bytes_to > node->bytes_from)
					node->bytes_send = node->bytes_to - node->bytes_from + 1;
				else
					node->bytes_send = 0;

				break;

			} else {

				debugf("[request] Unhandled HTTP header '%s' with '%s'\n",
					   token, ar);

			} // Long line if header ifs



			break;

		} // inheader


		// Check for end of header.
		if (line && !*line && node->inheader) {


			node->inheader = 0;


            // Handle unknown requests
            if (node->unknown_request) {
                debugf("[request] sending error due to unknown HTTP request\n");

                node->unknown_request = 0;
                lion_printf(node->handle,
                            "HTTP/1.1 501 Method Not Implemented\r\n"
                            "Date: Mon, 10 Aug 2009 00:49:05 GMT\r\n"
                            "Server: llink/%s\r\n"
                            "Allow: GET,HEAD\r\n"
                            "Content-Length: 209\r\n"
                            "Connection: close\r\n"
                            "Content-Type: text/html; charset=iso-8859-1\r\n"
                            "X-Pad: avoid browser bug\r\n"
                            "\r\n"
                            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
                            "<html><head>\r\n"
                            "<title>501 Method Not Implemented</title>\r\n"
                            "</head><body>\r\n"
                            "<h1>Method Not Implemented</h1>\r\n"
                            "<p>FNAR to /poo not supported.<br />\r\n"
                            "</p>\r\n"
                            "</body></html>\r\n",
                            VERSION);
                node->current_event = REQUEST_EVENT_CLOSED;
                break;
            }


            if (node->post_method) {
                debugf("[request] POST method. upnp cmd '%s'. EVENT=POST_READDATA\n", "fixme");
                node->post_method = 0;

                // As weird as it seems, the UPNP code is done in "default" skin
                // this is so that *.rar type is still defined. It has deep hooks
                // To change to UPNP types just before output.
                // However, getting the XML files, we do set UPNP in upnp.c
                // SetCommand.
                skin_set_skin(node, NULL);

                // Change handler here.
                //node->current_event = REQUEST_EVENT_POST_READDATA;
                lion_set_handler(node->handle, request_post_handler);

                // Bastards! Technically you dont HAVE to send newlines
                // with POST, as we already say to read exact number of bytes.
                // Thank you Intels UPNP Validation tool!
                debugf("[request] switching to binary\n");

                lion_enable_binary(node->handle);
                break;
            }


			// SUBSCRIBE, we just reply OK - for now
            /*
 SUBSCRIBE dude HTTP/1.1
   Host: iamthedude:203
   NT: <upnp:toaster>
   Callback: <http://blah/bar:923>
   Scope: <http://iamthedude/dude:203>
   Timeout: Infinite

   HTTP/1.1 200 O.K.
   Subscription-ID: uuid:kj9d4fae-7dec-11d0-a765-00a0c91e6bf6
   Timeout: Second-604800
            */

			if (node->subscribe_method) {
				debugf("[request] SUBSCRIBE: early reply\n");
				lion_printf(node->handle,
							"HTTP/1.1 200 OK\r\n"
                            //"412 Precondition Failed\r\n"
							"Content-Length: 0\r\n\r\n"
							);
                node->current_event = REQUEST_EVENT_CLOSED;
                break;
			}

			debugf("[request] signalling EVENT_ACTION for %p: disable read\n", node);
			lion_disable_read(node->handle);
			node->current_event = REQUEST_EVENT_ACTION;
			//X request_action(node);

		} // end of header



		break;

	case LION_BINARY:
		break;

	case LION_BUFFER_EMPTY:
		// In DVDREAD mode, we send the events there.
		// Normally we always enable it here, however in rar mode when
		// we pause unrar waiting for the next request, we better stay
		// paused.
		if (node->rar_name &&
			node->bytes_to &&
			(node->bytes_sent >= node->bytes_to)) {
				debugf("[request] skipping buffer empty event due to paused unrar\n");
				break;
			}

		if (node && node->althandle)
			lion_enable_read(node->althandle);
		break;

	case LION_BUFFER_USED:
		// In DVDREAD mode, we send the events there.
		if (node && node->althandle)
			lion_disable_read(node->althandle);
		break;

	}

	return 0;
}




int request_post_handler(lion_t *handle, void *user_data,
                         int status, int size, char *line)
{
	request_t *node;

	node = (request_t *) user_data;


	switch(status) {

	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:

		if (!node) return 0;

		debugf("[request] closed/lost %s:%d (%d:%s): %s\n",
			   lion_ntoa(node->ip), node->port,
			   size, line ? line : "gracefully closed",
			   ctime(&lion_global_time));


		node->handle = NULL;  // Safety.
		node->current_event = REQUEST_EVENT_CLOSED;

		debugf("[request] POST setting %p event CLOSED\n", node);

		//X request_freenode(node);
		return 0;


	case LION_INPUT:
	case LION_BINARY:
		if (!node) return 0;

		if (size <= 0) return 0;

		// Alas, we do not know if they sent "\n" or "\r\n", and how to
		// subtract from content-length.
		debugf("[request] POST: recv '%s' (remaining %"PRIu64" - read %d)\n",
			   line, node->bytes_size, size);

		// Attempt to guess if it is \n or \r\n. We will need to change
		// to BINARY to get exact byte-count.
		// Since we are now IN binary, we know exactly how many bytes we got.
		//size += 2; // "\r\n"

		// Have we finished?
		if (node->bytes_size <= (lion64_t) size) {

            lion_disable_binary(node->handle);

            debugf("[request] POST reading finished - switched to text mode\n");
            node->bytes_size = 0;
            lion_set_handler(node->handle, request_handler);

			debugf("[request] POST -> EVENT_ACTION for %p: disable read\n", node);
			lion_disable_read(node->handle);
			node->current_event = REQUEST_EVENT_ACTION;

            // request_action opens tmpfile, and gets ready to put information
            // in it (dirlisting, or static) then sends it across.

            if (node->cgi)
                request_cgi_parse(node);

            debugf("[request] Directory: '%s' cgi: '%s'\n",
                   node->path,
                   node->cgi ? node->cgi : "");

            return 0;
        }

        // Subtract read amount.
        node->bytes_size -= (lion64_t) size;
        break;

    } // switch

    return 0;
}






//
// Reading file handler. When open, lseek, then send data.
//
static int request_file_handler(lion_t *handle, void *user_data,
								int status, int size, char *line)
{
	request_t *node;
	lion64_t gap = 0;
	int do_close;

	do_close = 0;

	node = (request_t *) user_data;
	if (!node) return 0;


	switch(status) {

	case LION_FILE_OPEN:
		debugf("[request] file open\n");

		node->time_idle  = lion_global_time;
		node->time_start = lion_global_time;

		break;


	case LION_FILE_FAILED:
		debugf("[request] file failed: %d:%s\n",
			   size, line ? line : "unknown");

	case LION_FILE_CLOSED:
		debugf("[request] reached EOF\n");
	// Special code for external commands. If we get EOF, but we still
		// have the external command executing, we tell Lion NOT to close
		// the file, by putting it into disabled for read. Then we re-enable it
		// "a little later" and try again.
		if (node->external_handle) {
			lion_disable_read(node->althandle);
			node->read_disabled = 1;
			debugf("[request] telling lion to ignore EOF\n");
			return 0;
		}



		// File is done, enable reading off this socket.

		// Was it a temporary file?
		if (node->tmpname) {
			debugf("[request] deleting tmpfile\n");
			remove(node->tmpname);
			SAFE_FREE(node->tmpname);
		} else {
            // When sending a file is closed.
            visited_fileclosed(node);
        }

        // Clear out the altnode.
		debugf("[request] clearing althandle\n");
		node->althandle = NULL;


		if (node->handle) {

		  debugf("[request] socket read restored\n");
		  lion_enable_read(node->handle);

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

	  // Send all, or just part based on byte range?

	  if (node->bytes_send) {

		gap = node->bytes_send - node->bytes_sent;

		if ((lion64_t)size >= gap) {
		  // Too much
		  size = (unsigned int) gap;
		  debugf("[request] shortening read to %d due to range given.\n",
				 size);

		  do_close = 1;

		} // if shorten

	  } // range set


		// If we have block-io, we need to iterate here until all
		// is exhausted.

		// Update how much we've gone through on disk.
		node->bytes_sent += (lion64_t)size;

		//debugf("req: in data %d - %"PRIu64"\n", size, node->bytes_sent);

		//	debugf("[req: sending %d\n", size);

		// The lion_output call below can actually end up sending a
		// LOST event and trigger to free the node.
		if (size) {
		  if (lion_output(node->handle, line, size) == -1) {
			debugf("[request] file socket write failed.\n");
			lion_disconnect(node->althandle);
			break;
		  }
		}

		// If we've sent enough, close file
		if (do_close) {
		  debugf("[request] closing file due to range limit\n");
		  lion_disconnect(node->althandle); // WTF...AAAHHHH biggest bug ever. Was "handle" 20080731
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
// tmpfile, usually filled from dirlist, playlists, unrar etc.
//
int request_writefile_handler(lion_t *handle, void *user_data,
								 int status, int size, char *line)
{
	request_t *node;

	node = (request_t *) user_data;
	if (!node) return 0;


	switch(status) {

	case LION_FILE_OPEN:
		debugf("[request] tmpfile open: %d\n", lion_fileno(handle));

		node->current_event = REQUEST_EVENT_WFILE_OPENED;

		// Once root_list is complete, it will call request_complete();

		break;


	case LION_FILE_CLOSED:
		debugf("[request] tmpfile closed\n");
		node->current_event = REQUEST_EVENT_WFILE_CLOSED;

		node->tmphandle = NULL;
		break;


	case LION_FILE_FAILED:
		debugf("[request] tmpfile failed: %d:%s\n",
			   size, line ? line : "unknown");

		node->tmphandle = NULL;
		break;



		// Should be no line data from files
	case LION_BINARY:
	case LION_INPUT:
		break;

	case LION_BUFFER_EMPTY:
	case LION_BUFFER_USED:
		break;
	}

	return 0;
}



//
// If we get arguments following a "&" we parse them out now. But we need
// to leave ->cgi untouched as other functions will use it. (see unrar)
//
void request_cgi_parse(request_t *node)
{
	char *parsing, *ar, *token, *key, *val;
    char *rest = NULL;

	if (!node->cgi || !*node->cgi) return;

	//parsing = strdup(node->cgi);
    parsing = request_url_decode(node->cgi);

	ar = parsing;

	while((token = misc_digtoken(&ar, "&?\r\n"))) {

        debugf("** token is '%s'\n", token);

		key = misc_digtoken(&token, "=\r\n");
		val = misc_digtoken(&token, "\r\n");

		if (!mystrccmp("from", key)) {
			if (val)
				node->page_from = atoi(val);
			continue;
		}

 		// &to=15         : stop page listing at 15
		if (!mystrccmp("to", key)) {
			if (val)
				node->page_to = atoi(val);
			continue;
		}

 		// &d=directory/  : unrar directory path
		if (!mystrccmp("d", key)) {
			if (val)
				node->rar_directory = request_url_decode(val);
			continue;
		}

 		// &code=1234  :
		if (!mystrccmp("code", key)) {
			if (val)
				node->cgi_secret = atoi(val);
			continue;
		}

 		// &stream=1  :
		if (!mystrccmp("stream", key)) {
			if (val)
				node->cgi_stream = atoi(val);
			continue;
		}

 		// &pin=1234  :
		if (!mystrccmp("pin", key)) {
			if (val) {
				if (!strcmp(conf_pin, val)) {
 					debugf("[request] accepting valid pin\n");
 					root_register_ip(node);
 					continue;
 				}
 				debugf("[request] ignoring incorrect pin\n");
				root_deregister_ip(node);
 			}
			continue;
		}

 		// &menu  :
		if (!mystrccmp("menu", key)) {
 			debugf("[request] menu requested\n");
 			node->menu_mode = 1;
 			continue;
 		}

 		// &quit  :
		if (!mystrccmp("quit", key)) {
 			debugf("[request] quit requested\n");
 			do_exit = 1;
 			continue;
 		}

 		// &proxy=url  :
		if (!mystrccmp("proxy", key))
			if (val) {
 				char *ar2, *host, *port = NULL;
 				char *str;
 				// http://movies.apple.com/movies/fox/hortonhearsawho/hortonhearsawho-tlrp_h480p.mov

				str = request_url_decode(val);

 				if (!strncasecmp("http://", str, 7)) {

					ar2 = &str[7];
					host = misc_digtoken(&ar2, "/:\r\n");
					if (host) {
						// Optional port?
						if (misc_digtoken_optchar == ':')
							port = misc_digtoken(&ar2, "/\r\n");
						if (!port || !*port)
							port = "80";

						SAFE_FREE(node->cgi_host);
						node->cgi_host = strdup(host);
						node->cgi_port = atoi(port);
						node->cgi_file = strdup(ar2);
					} // if host
				} // starts with http://

				SAFE_FREE(str);
				continue;
			} // could get proxy=??? bit

		// &f=12345,file  : unrar file with size 12345
		if (!mystrccmp("f", key)) {
			if (val) {
				node->bytes_size = (lion64u_t) strtoull(val, &rest, 10);
                // Sometimes C200 (upnp mode) will URL encode the comma
				//if ((token = strchr(val, ',')) )
                if (rest) {
                    if (*rest == ',') {
                        rest++;
                    } else {
                        if ((rest[0] == '%') &&
                            (rest[1] == '2') &&
                            (rest[2] == '2')) {
                            rest += 3;
                        }
                    }
                    // Skip any slashes in file name.
                    while (*rest == '/') rest++;

					node->rar_file = request_url_decode(rest);

                }
 			}
 			continue;
 		} // f=size,file

		// &fv=12345,file  : dvdread file with size 12345
		if (!mystrccmp("fv", key)) {
			if (val) {
				node->bytes_size = (lion64u_t) strtoull(val, NULL, 10);
				if ((token = strchr(val, ',')))
					node->dvdread_file = request_url_decode(token + 1);
 			}
 			continue;
 		} // fv=size,file

		// &transcode=12345,/file  : transcode=$id,/$fakename
		if (!mystrccmp("transcode", key)) {
			if (val) {

                node->transcode_id = strtoul(val, &rest, 10);

                if (rest) {
                    if (*rest == ',') {
                        rest++;
                    }
                }
                debugf("[request] transcode id %u set (%s)\n",
                       node->transcode_id, rest ? rest : "");
 			}
 			continue;
 		} // transcode=id,file

 		// &focus=10  :
		if (!mystrccmp("focus", key)) {
			if (val)
				node->page_focus = atoi(val);
			continue;
		}

 		// &playall=10  : Play all files (from file 10).
		if (!mystrccmp("playall", key)) {
			if (val) {
				node->playall = atoi(val);
 				node->page_from = node->playall-1; // Start "page" from where we want
 				node->page_to = 10000; // Some large number to "play all".
 			}
			continue;
		}

		if (!mystrccmp("viewall", key)) {
			if (val) {
				node->viewall = atoi(val);
 				node->page_from = node->playall-1; // Start "page" from where we want
 				node->page_to = 10000; // Some large number to "play all".
 			}
			continue;
		}

 		// &h=20,30,0,10  : History (prev page we came from &from=10)
		if (!mystrccmp("h", key)) {
			if (val)
				node->history = strdup(val);
			continue;
		}

		// &type=Music  : for playlists, only play certain types.
		if (!mystrccmp("type", key)) {
			if (val)
				node->typename = strdup(val);
			continue;
		}

 		// &s=2         : Set sorting style. 0-x
		if (!mystrccmp("s", key)) {
			if (val)
				skin_setsort(node, atoi(val));
			continue;
		}

		// CGI commands should only be processed once for dirlistings (and
		// skipped when we send the tmpfile with dirlist contents)
		if (!node->tmpname) {

			// &del=path    : Delete said path, is encoded.
			if (!mystrccmp("del", key)) {
				if (val) {
					char *str;
					str = request_url_decode(val);
					cgicmd_delete(node, str);
					SAFE_FREE(str);
				}
				continue;
			}
			// &unrar=path    : Delete said path, is encoded.
			if (!mystrccmp("unrar", key)) {
				if (val) {
					char *str;
					str = request_url_decode(val);
					cgicmd_unrar(node, str);
					SAFE_FREE(str);
				}
				continue;
			}


			// We allow users to specify their own scripts as well, so we now
			// need to go check if this happens to match one of those.
			if (key) cgicmd_userexec(node, key, val);
		}

	}

	SAFE_FREE(parsing);

	debugf("[request] cgi parse: from %d, to %d, rar_directory '%s', rar_file '%s'\n",
		   node->page_from, node->page_to,
		   node->rar_directory ? node->rar_directory : "",
		   node->rar_file ? node->rar_file : "");
	debugf("[request] cgi parse: dvdread_file '%s'\n",
		   node->dvdread_file ? node->dvdread_file : "");

}








static int request_sendsize(request_t *node)
{
	lion64u_t send_size;
	//
	// If we are in process mode, like bin2mpg, we need to approximately
	// compute the real size.
	//

	if (node->cgi_stream)
		return 1;


	send_size = 0;
	if (node->bytes_send) {
		send_size = node->bytes_send;
	} else {
		if (node->bytes_to)
			send_size = node->bytes_to - node->bytes_from;
		else
			send_size = node->bytes_size - node->bytes_from;
	}


	switch(node->process_function) {
	case REQUEST_PROCESS_BIN2MPG:
		process_bin2mpg_size(&send_size);
		break;

	case REQUEST_PROCESS_EXTERNAL:
		// When we stream, we don't know the final size, so lets send
		// something really large.. Can we just skip sending size at all?
		node->bytes_from = 0;
		node->bytes_send = 0;
		debugf("[request] skipping Content-Length\n");
		return 1;
		//send_size = 0x7fffffffffffffff;
		//send_size = 0;
		break;

	default:
		break;
	}

	if (node->bytes_size || node->bytes_send) {
		if (lion_printf(node->handle,
					"Content-Length: "
#ifdef PRIu64
					"%"PRIu64
#else
#warning "Unknown u64 print switch"
					"%llu"
#endif
					"\r\n",
					// Sometimes it is valid to say 0 bytes is comming.
					send_size) <= 0) return 0;

		debugf("Content-Length: %"PRIu64"\n",
			   send_size);

	} else {
		if (lion_printf(node->handle,
					"Content-Length: 0\r\n") <= 0) return 0;
	}

	return 1;
}






void request_reply(request_t *node, int code, char *reason)
{
	// HTTP/1.1 200 OK
	// ETag: W/"378-1104140311046"
	// Last-Modified: Mon, 27 Dec 2004 09:38:31 GMT
	// Content-Type: text/html
	// Content-Length: 378
	// Date: Mon, 27 Dec 2004 09:39:22 GMT
	// Server: Apache-Coyote/1.1
	// Connection: Keep-Alive
    char *mime = NULL;
	//node->cgi_stream = 1;

	debugf("[request] sending reply code %d:%s: %s\n",
		   code, reason,
		   ctime(&lion_global_time));

	if (node->cgi_stream) {
		if (lion_printf(node->handle,
                        "HTTP/1.0 %03d %s\r\n"
                        "Server: llink-daemon/%s (build %d)\r\n",
                        code,
                        reason ? reason : "",
                        VERSION,
                        VERSION_BUILD
                        ) <= 0) return;

	} else {
        if (lion_printf(node->handle,
                        "HTTP/1.1 %03d %s\r\n"
                        "Server: llink-daemon/%s (build %d)\r\n",
                        code,
                        reason ? reason : "",
                        VERSION,
                        VERSION_BUILD
                        ) <= 0) return;
    }

	debugf(		"HTTP/1.1 %03d %s\r\n"
				"Server: llink-daemon/%s (build %d)\r\n",
				code,
				reason ? reason : "",
				VERSION,
				VERSION_BUILD
				);



	if (code <= 299) {


        // Hack - it seems the Buffalo DVDPlayer, even though it asks
        // for ranges to start at the EOF mark, and we would send it 0 bytes back
		// does not like that. So we fudge it to always send 1 byte. This
		// only happens if you manage to pause it precisely before we
		// send the EOF. (roughly 10s of video to go).
		if (((node->bytes_from == node->bytes_size) ||
			 ((node->bytes_to) && (node->bytes_from == node->bytes_to))) &&
			node->stat.st_size) {
			debugf("[request] Buffalo hack: modifying length to 1 byte.\n");
			node->bytes_from--;
			node->bytes_send++;
		}



        //if (node->bytes_from || node->bytes_to) {
        if (node->send_range) {
		lion64_t bytes_to;
		// x-y/t

		bytes_to = node->bytes_to;

		if (!node->bytes_to)
		  bytes_to = node->bytes_size - 1;

        if (!node->cgi_stream) {

            if (lion_printf(node->handle,
                            "Accept-Ranges: bytes\r\n"
                            "Content-Range: bytes %"PRIu64"-%"PRIu64"/%"PRIu64"\r\n",
                            node->bytes_from, bytes_to, node->bytes_size) <= 0) return;

            debugf(	  "Accept-Ranges: bytes\r\n"
                      "Content-Range: bytes %"PRIu64"-%"PRIu64"/%"PRIu64"\r\n",
                      node->bytes_from, bytes_to, node->bytes_size);

        } else {
            node->bytes_to = 0;
            node->bytes_from = 0;
        }

	  } // send content-range?

	  // HEAD range0-0 still sends 206
	  // HEAD is keep-alive here, they seem to close?

	} // code 200s


	if (!request_sendsize(node)) return;



	// If tmpname is set, we are sending over .html for a dirlisting.
	// Otherwise, lookup the mime type if known. This does not strictly
	// seem neccessary.. but of course, only if it is a valid fetch.
	// Any errors is just html.
#if 1
	if (node->tmpname || (code >= 300)) {


        mime = "text/html";
        if (node->playall)
            mime = mime_type(".pls");

        if (lion_printf(node->handle, "Content-Type: %s\r\n", mime) <= 0)
            return;

		debugf("[request] Content-Type: %s\n", mime);

	} else {

        // mime does not return NULL;
        mime = mime_type(node->rar_file ? node->rar_file : node->path);
        if (node->transcode_id)
            mime = mime_type(skin_transcode_getext(node->transcode_id));

		if(lion_printf(node->handle, "Content-Type: %s\r\n",
					mime) <= 0) return;
		debugf("[request] Content-Type: %s\n", mime);
	}
#endif


	// work around Buffalo LinkTheater bug. All errors are "closed"
	if (conf_fixup_nokeepalive) {
		if ((code >= 300) && (node->type & REQUEST_TYPE_KEEPALIVE)) {
			debugf("[request] changing request to closed (due to -L)\n");
			node->type &= ~REQUEST_TYPE_KEEPALIVE;
			node->type |= REQUEST_TYPE_CLOSED;
		}
	}


	// Keep-Alive is implied.
	if (!(node->type & REQUEST_TYPE_KEEPALIVE)) {

	  if (lion_printf(node->handle,
				  "Connection: %s\r\n",
				  (node->type & REQUEST_TYPE_KEEPALIVE) ? "Keep-Alive" : "close") <= 0) return;

	  debugf(
			 "Connection: %s\r\n",
			 (node->type & REQUEST_TYPE_KEEPALIVE) ? "Keep-Alive" : "close");

	}



	if (lion_output(node->handle, "\r\n", 2) <= 0) return;

	// Do we close it now? Errors, and Closed type.
	if ((code >= 300) && (node->type & REQUEST_TYPE_CLOSED)) {
		debugf("[request] type closed, doing so now\n");
		lion_close(node->handle);
		node->handle = NULL;
		return;
	}

	// HEAD mode, we do little processing, so just fix things here
	if (node->head_method) {

		// If we opened a file, just close it. The
		// close handle sorts out the listening socket.
		if (node->althandle) {
			lion_close(node->althandle);
			return;
		}

		// In rar mode, or non-file mode, we need to fix the http socket.
		// If we have a socket and its close-type, do so now.
		if ((node->type & REQUEST_TYPE_CLOSED) && node->handle) {
			lion_close(node->handle);
			node->handle = NULL;
			return;
		}

		// If it is keep-alive, start listening again
		debugf("[request] socket read restored, HEAD mode\n");
		lion_enable_read(node->handle);
		return;
	}


	// If we are to send a file, we don't turn reading back on until the file
	// is done. For all other replies, we turn it on now.
	// socket to client that is.

	if (node->handle && (code >= 300)) {
		debugf("[request] socket read restored\n");
		lion_enable_read(node->handle);
		return;
	}

	// We are to send the file now.
	if (node->althandle) {
		debugf("[request] File enable_read set\n");
		lion_enable_read(node->althandle);
	}

}



//
// Check if name exists, if not, also check rar_file
//
int request_redirect(request_t *node)
{
	char *tmpname = NULL, *str;

	debugf("[request] testing for redirects '%s' ... \n", node->path);

	// Check based on ext in filename, get a new root path back.
	if ((str = skin_redirect(node->skin_type, node->path))) {

		// Join str and basename of node->path
		tmpname = misc_strjoin(str, hide_path(node->path));
		misc_stripslash(tmpname);

		debugf("[request] testing '%s' ... \n", tmpname);

		if (!stat(tmpname, &node->stat)) {

			node->disk_path = tmpname;

			debugf("[request] found '%s'\n", tmpname);

			return 1;
		} // can stat

		SAFE_FREE(tmpname);
	}



	// rar_file ?
	if (!node->rar_file) return 0;

	if ((str = skin_redirect(node->skin_type, node->rar_file))) {

		tmpname = misc_strjoin(str, node->rar_file);
		misc_stripslash(tmpname);

		debugf("[request] testing rar_file '%s' ... \n", tmpname);

		if (!stat(tmpname, &node->stat)) {

			node->disk_path = tmpname;

			debugf("[request] found '%s'\n", tmpname);

			return 1;
		} // can stat

		SAFE_FREE(tmpname);


	} // skin_redirect


	// dvdread_file ?
	if (!node->dvdread_file) return 0;

	if ((str = skin_redirect(node->skin_type, node->dvdread_file))) {

		tmpname = misc_strjoin(str, node->dvdread_file);
		misc_stripslash(tmpname);

		debugf("[request] testing dvdread_file '%s' ... \n", tmpname);

		if (!stat(tmpname, &node->stat)) {

			node->disk_path = tmpname;

			debugf("[request] found '%s'\n", tmpname);

			return 1;
		} // can stat

		SAFE_FREE(tmpname);


	} // skin_redirect


	return 0;

}


int request_redirect_rar(request_t *node)
{
	char *str, *tmpname = NULL;

	if (!node->rar_file) return 0;

	if ((str = skin_redirect(node->skin_type, node->rar_file))) {

		if ((str = dirname(node->disk_path))) {
			tmpname = misc_strjoin(str, node->rar_file);
			misc_stripslash(tmpname);

			debugf("[request] testing rar directory for file '%s' ... \n",
				   tmpname);

			if (!stat(tmpname, &node->stat)) {

				node->disk_path = tmpname;

				debugf("[request] found '%s'\n", tmpname);

				return 1;
			} // can stat


			SAFE_FREE(tmpname);
		} // dirname()
	} // is redirect



	if (!node->dvdread_file) return 0;

	if ((str = skin_redirect(node->skin_type, node->dvdread_file))) {

		if ((str = dirname(node->disk_path))) {
			tmpname = misc_strjoin(str, node->dvdread_file);
			misc_stripslash(tmpname);

			debugf("[request] testing dvdread directory for file '%s' ... \n",
				   tmpname);

			if (!stat(tmpname, &node->stat)) {

				node->disk_path = tmpname;

				debugf("[request] found '%s'\n", tmpname);

				return 1;
			} // can stat


			SAFE_FREE(tmpname);
		} // dirname()
	} // is redirect

	return 0;
}



static int ffmpeg_extract_handler(lion_t *handle, void *user_data,
								 int status, int size, char *line)
{
	request_t *node = (request_t *) user_data;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[request] connected to ffmpeg process\n");
		break;

	case LION_PIPE_FAILED:
		debugf("[request] ffmpeg failed to launch: %d:%s\n",
			   size, line ? line : "");

		node->althandle = NULL;
		request_reply(node, 500, line ? line : "Error with ffmpeg");
		break;

	case LION_PIPE_EXIT:
		debugf("[request] ffmpeg finished: %d\n", size);
		if (size == -1) {
			lion_want_returncode(handle);
			break;
		}
		node->althandle = NULL;
        if (!node->handle) break;

		debugf("[request] socket read restored\n");
		lion_enable_read(node->handle);

		if ((node->type & REQUEST_TYPE_CLOSED)) {
			debugf("[request] closing socket due to TYPE_CLOSED\n");
			lion_close(node->handle);
		}

        // Actually, we will always close it here. We do not know the
        // size of the stream, so it is always CLOSED.
        lion_close(node->handle);
		break;

	case LION_BINARY:

		if (lion_output(node->handle, line, size) == -1) {
			debugf("[request] socket write failed.\n");
			break;
		}

		break;

	} // switch status

	return 0;
}



//
// This is called when we have finished parsing a http request and we need
// to action it. Either it is a file, which we just send, or if it is a
// directory, we list it into a temporary file, then call this function
// again (this time with the tmp file)
//
void request_action(request_t *node)
{
	struct stat stbf;
	skin_t *skin = NULL;
	int dirlist;
    char *p;

	debugf("[request] actioning request for %s\n",
		   node->path);

    request_update_idle();

	if (conf_fixup_syabas_subtitles) {
		int i;
		for (i = 0; node->path[i]; i++)
			if ((node->path[i+0] == '.') &&
				(node->path[i+1] == 's') &&
				(node->path[i+2] == 'u') &&
				(node->path[i+3] == 'b') &&
				(node->path[i+4]))
				node->path[i+4] = 0;
	}


	// Check if we have everything needed?
	if (!node->path || !*node->path) {
		request_reply(node, 500, "Incorrect HTTP request");
		return;
	}


	// If we have cgi set, lets attempt to parse it.
	if (node->cgi)
		request_cgi_parse(node);



	// Check for redirects? If rar_file, setpath works, because its for the
	// rar file, but we are after the .srt anyway. If it is a normal file
	// we will change it to (only) an existing file
	if (request_redirect(node)) {
		node->process_function = REQUEST_PROCESS_NONE;
		SAFE_FREE(node->rar_file);
		SAFE_FREE(node->dvdread_file);
        debugf("redirect clears dvdread_file\n");
	}


    // We now also allow paths like "dir/file.rar/dir/file.avi", but only just.
    // Look for ".rar/" and if found, separate path. But only if the correct
    // CGI RAR names were NOT given. (Proper CGI override hacks)
    if (!node->rar_directory && !node->rar_file &&
        (p = strcasestr(node->path, ".rar/"))) {
        char *dir;

        // Terminate node->path after file.rar
        p[4] = 0;
        // Move p to RAR name part.
        p = &p[5];

        // Does it have directory component?
        dir = strrchr(p, '/');
        if (!dir) {
            node->rar_file = strdup(p);
        } else {
            *dir = 0;
            node->rar_directory = strdup(p);
            node->rar_file = strdup(&dir[1]);
        }

        debugf("[request] direct RAR path detected. path '%s', rar_directory '%s' and rar_file '%s'\n",
               node->path,
               node->rar_directory ? node->rar_directory : "",
               node->rar_file ? node->rar_file : "");
    }


	// Fix up the proper real path. Except if it is "/"
	// This should set "disk_path". However, it may already be set
	// by the tmpfile code. If so, we just use that.
	if (node->path && root_setpath(node)) {
		request_reply(node, 500, "Illegal path");
		return;
	}

	// Alas, we can not test for this inside request_redirect, as it
	// needs setpath to be called. We now try to replace the file.rar
	// name with rar_file=file.srt, if redirection is set.
	// We also try to remove the rar name, and add on the rar_file
	request_redirect_rar(node);


	// We will here stop inputing reading on the html socket, incase the
	// requestor sends a whole bunch in a go. The reading is enabled in the
	// http reply code. If its a closed connection, we can leave read on
	// to detect failures sooner.
	debugf("[request] disabling socket input for duration of process\n");
	lion_disable_read(node->handle);



	// Now, is it a directory, if so, we send a directory listing.
	// Also, if it is of type UNRAR, we also send directory listing.
	// (will .rar inside .rar work? I guess not)

	// This will set the skin node, and process_function
	node->process_function = 0;
	skin = skin_process_cmd(node);

	// But if we are sending a directory listing
	// For rar mode, we need to check the cgi portion to see if we are to
	// send a file, or a directory.
	// Default to sending file
	dirlist = 0;

	debugf("[request] checking for dirlist: '%s' stat %d %0x\n",
		   node->disk_path, node->stat.st_mode, node->stat.st_mode);

	// Plain directory listing?
	if (!mystrccmp("/", node->disk_path))
		dirlist = 1;


	// It is technically true that "VIDEO_TS" is a dir, but we are asking
	// for dvdread_file (VOB file inside) ... Now DVD is handled as RAR
	if (S_ISDIR(node->stat.st_mode) && !node->rar_file)
		dirlist = 2;

	// Rar mode:
	// If we have a directory
	// or we do not have a file (root listing)
	if (node->process_function == REQUEST_PROCESS_UNRAR) {
		if (node->rar_directory &&
			(!node->rar_file)) { // dvdread_file MAY be set.
			dirlist = 3;
        }
    }


	// menu-mode is a simulated dirlist. But not if we have a tmpfile,then
	// it is time to send it.
	if (node->menu_mode && !node->tmpname)
		dirlist = 5;



	// If ROOT is a device type, AND this isn't a dirlist reply.
	if (root_is_dvdread(node->root_index) && !node->tmpname) {
		node->process_function = REQUEST_PROCESS_UNRAR;
		// If it is not file a file, then it is a dirlist.
		if (!node->rar_file)
			dirlist = 6;
	}



    // Special case, ISO inside RAR, if enabled:
	if (conf_dvdread_rariso) {

        // If "fv=1234,file.iso" is set, that means we want to RARISO
        if (!node->tmpname && node->dvdread_file && !node->rar_file) {
            debugf("[request] dvdread_file '%s' -> changing to RARISO\n",
                   node->dvdread_file);
			node->process_function = REQUEST_PROCESS_RARISO;
            dirlist = 7;
        }

        // A one time event is when we click on a *.iso file inside RAR, we
        // then convert that f=file.iso, to fv=file.iso, and type to RARISO.
		if (!node->tmpname && node->rar_file && !node->dvdread_file &&
			(skin = skin_findbymatch(node->skin_type, node->rar_file, 1)) &&
			!mystrccmp("DVD_Directory", skin->name) &&
			!node->dvdread_file) {

			debugf("[request] unrar(ISO) dirlist mode! Moving rar_file to dvdread_file\n");
			node->process_function = REQUEST_PROCESS_RARISO;

            // We need to move the ISO name,
            // rar_file -> dvdread_file, so that
            // in future, rar_file can refer to a file inside
            SAFE_FREE(node->dvdread_file);
            node->dvdread_file = node->rar_file;

			dirlist = 17;
		}



        if (!node->tmpname && node->dvdread_file && node->rar_file) {
            debugf("[request] unrar(ISO) get mode!\n");
			node->process_function = REQUEST_PROCESS_RARISO;
        }

	} // rariso

    // Should we look for "index.html" in this directory?
    // Don't send index.html if we are UPNP'ing.
    if (dirlist && conf_send_index && (node->upnp_type == UPNP_NONE)) {
        char *findex;

        // If they are in root "/", then just adding "/index.html" will clearly
        // not work.
        // If I changed the dirlist logic puzzle above to skip future tests
        // I could simply test "dirlist == 1" here.
        debugf("[request] checking for index-redirect '%s' in %s\n",
               conf_send_index, node->disk_path);

        if (!mystrccmp("/", node->disk_path)) {
            // Look up the first ROOT we find and use that.
            char *rootpath;
            rootpath = root_getroot(0);

            findex = misc_strjoin(rootpath, conf_send_index); //"index.html"

        } else {

            findex = misc_strjoin(node->disk_path, conf_send_index); //"index.html"

        }

        if (findex && !stat(findex, &node->stat)) {
            // Fix in-disk path
            SAFE_FREE(node->disk_path);
            node->disk_path = findex;
            // Fix logical path, used for mime-type and MACROS
            findex = misc_strjoin(node->path, conf_send_index); //"index.html"
            SAFE_FREE(node->path);
            node->path = findex;

            dirlist = 0;
            findex = NULL;
            debugf("[request] %s found, sending file instead.\n",
                   conf_send_index);
        }

        SAFE_FREE(findex);
    }


	if (dirlist) {

		// If the path is a directory:
		// 1. Open temporary file
		// 2. Read in skin's template html,
		//    and replace macros with actual values, while writing to 1.
		// 3. Call root_list(path) to generate directory contents entries,
		//    and write contents to 1.
		// 4. Close 1.
		// 5. Re-open 1. in read mode and send over socket.
		// 6. Close 1, delete 1.

		node->tmpname = strdup(TMP_TEMPLATE);
		if (!mktemp(node->tmpname)) {
			request_reply(node, 400, "Temporary memory issue");
			return;
		}


		debugf("[request] tmpname '%s': dirlist %d: page_from %d, page_to %d\n",
               node->tmpname, dirlist,
               node->page_from, node->page_to);


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
			char szCwd[100];
			getcwd(szCwd, sizeof(szCwd));

			lion_printf(node->handle, "errored: wd is %s and error %d, name %s err %s\r\n",
				szCwd, errno, node->tmpname, strerror(errno));
			request_reply(node, 400, "Temporary file issue");
			return;
		}


		// Set handler
		lion_set_handler(node->tmphandle, request_writefile_handler);

		// Disable read since we only write to this file.
		lion_disable_read(node->tmphandle);

		// We CAN NOT write to the tmphandle YET, as lion does
		// not guarantee this until FILE_OPEN event.

		// The code that WAS here has moved "THERE".

		return;

	} // dirlist ?


	// Check if we have a process command defined for this type of file
	if (!node->tmpname) {

		// If we are "external" command, we need to launch it now.

		// External command flow.
		// [1] Create ondisk pipe
		// [2] launch external command
		// [3] assign "disk_path" name to pipe.
		if (node->process_function == REQUEST_PROCESS_EXTERNAL) {

			if (external_launch(node, skin)) // calls _reply()
				return;

		}

	}


	// If the path is a file, check if "cgi" field has a particular task
	// like "playlist_add" etc. Otherwise, just send it.

	debugf("[request] sending file\n");

	// Attempt to open file.

	// cgi proxy mode?
	if (node->cgi_host && node->cgi_file) {
		// proxy sends its own header reply, so we dont do any more
		// processing here.
		root_proxy(node);
		return;
	}


    // File request with playall set, we send a playlist with this
    // file instead.
    if (node->playall) {
        FILE *fd;

        debugf("Making a playlist instead; playall=1: rar_file '%s'\n",
               node->rar_file);

		node->tmpname = strdup(TMP_TEMPLATE);
		if (!mktemp(node->tmpname)) {
			request_reply(node, 400, "Temporary memory issue");
			return;
		}
        fd = fopen(node->tmpname, "w");
        if (fd) {
			int myport;
			unsigned long myip;
            char *str;
			lion_getsockname(node->handle, &myip, &myport);
			str = request_url_encode(node->path);
            fprintf(fd, "[playlist]\r\n");
            fprintf(fd, "File1=http://%s:%d/%s%s%s\r\n",
                    lion_ntoa(myip),
                    httpd_port,
                    str,
                    node->rar_file ? "/" : "",
                    node->rar_file ? node->rar_file : "");
            fprintf(fd, "Name1=%s\r\n",
                    node->path);
            fprintf(fd, "Length=-1\r\n\r\nNumberOfEntries=1\r\nVersion=2\r\n");
            fclose(fd);
            SAFE_FREE(str);
        }
        node->process_function = REQUEST_PROCESS_NONE;
        SAFE_COPY(node->disk_path, node->tmpname);
        debugf("[request] disk path now '%s'\n", node->disk_path);
    }



    // Transorcery magic?
    if (node->transcode_id &&
        (p = skin_transcode_id2args(node->transcode_id))) {
        char buffer[2048], url[1024];
        unsigned long myip;
        int myport;
        char *path, *rar_file=NULL, *rar_directory=NULL;

        debugf("[request] transcoding detected\n");

        lion_getsockname(node->handle, &myip, &myport);
        //build mega commandline
        //replicating the code to build url, yet again?

        path = request_url_encode(node->path);


        // Build the SOURCE URL
        if (node->rar_directory || node->rar_file) {
            // RAR file
            if (node->rar_directory)
                rar_directory = request_url_encode(node->rar_directory);
            if (node->rar_file)
                rar_file = request_url_encode(node->rar_file);

            snprintf(url, sizeof(url),
                     "http://%s:%d/%s?f=%"PRIu64",%s/%s",
                     lion_ntoa(myip),
                     httpd_port,
                     path,
                     node->bytes_size,
                     rar_directory ? rar_directory : "",
                     rar_file ? rar_file : "");

            SAFE_FREE(rar_directory);
            SAFE_FREE(rar_file);

        } else {
            // Plain file
            snprintf(url, sizeof(url),
                     "http://%s:%d/%s",
                     lion_ntoa(myip),
                     httpd_port,
                     path);
        }


        // We assume that the "args" given to us from users, in the transcode line,
        // will contain one '%s', which will be filled by the source URL.
        snprintf(buffer, sizeof(buffer),
                 p,
                 url);

        SAFE_FREE(path);

        debugf("[request] transcode executing '%s' \n", buffer);

		node->althandle = lion_system(buffer, 0 /* stderr*/ ,
                                      LION_FLAG_NONE, node);

		if (!node->althandle) {
			debugf("[request] failed to launch ffmpeg\n");
            lion_printf(node->handle, "HTTP/1.1 500 ffmpeg process failed\r\n\r\n");
            lion_close(node->handle);
            return;
        }

		// Set extract handler
		lion_set_handler(node->althandle, ffmpeg_extract_handler);
		// Set binary mode
		lion_enable_binary(node->althandle);
		// Disable read until we are ready
		lion_disable_read(node->althandle);

        //node->bytes_from = 0;
        //node->bytes_to = 0;
        //node->bytes_size = 0; // Size of 0, force closed, dont send size.
        //node->type |= ~REQUEST_TYPE_CLOSED;
        node->cgi_stream = 1;
        //node->process_function = REQUEST_PROCESS_NONE;

    } else

        // We do not open files in rar mode, already open.
        if ((node->process_function == REQUEST_PROCESS_UNRAR) &&
		node->rar_file) {

        // If we already have RAR size, or can look it up, go straight to
        // sending
        if (!node->bytes_size &&
            !(node->bytes_size = llrar_getsize(node))) {

            debugf("[request] asking RAR for filesize\n");
            node->process_function = 0;
            skin = skin_process_cmd(node);

            // DVD device, we need to get undvd
            if (root_is_dvdread(node->root_index))
                skin = skin_findbyname(node->skin_type, "DVD_Directory", 1);

            node->llrar_cache_fill = 1;
            llrar_list(node, skin ? skin->args : skin_get_rar());
            return;
        }

        if (!skin || !skin->args) {

            // Might be path/VIDEO_TS/, so eat trailing slash
            misc_stripslash(node->path);
            // Look up skin by name.
            skin = skin_findbymatch(node->skin_type, hide_path(node->path), 1);

        }

        if (root_is_dvdread(node->root_index)) {
                debugf("[request] appears to be DvD device, woohoo\n");
                skin = skin_findbyname(node->skin_type, "DVD_Directory", 1);
        }


        if (llrar_get(node, skin && skin->args ? skin->args : skin_get_rar())) {
            //request_reply(node, 500, "unrar failed");
            return;
        }



  	} else if ((node->process_function == REQUEST_PROCESS_RARISO) &&
		node->dvdread_file&&node->rar_file) {

        // If we already have RAR size, or can look it up, go straight to
        // sending
        if (!node->bytes_size &&
            !(node->bytes_size = llrar_getsize(node))) {

            debugf("[request] asking RAR for filesize\n");
            node->process_function = 0;
            skin = skin_process_cmd(node);

            // DVD device, we need to get undvd
            if (root_is_dvdread(node->root_index))
                skin = skin_findbyname(node->skin_type, "DVD_Directory", 1);

            node->llrar_cache_fill = 1;
            llrar_list(node, skin ? skin->args : skin_get_rar());
            return;
        }

        // RARISO, is RAR mode too, but we need to check which executable to
        // spawn
        skin = skin_findbymatch(node->skin_type, node->dvdread_file, 1);

        if (llrar_get(node, skin && skin->args ? skin->args : skin_get_rar())) {
            //request_reply(node, 500, "unrar failed");
            return;
        }


		// Not rar mode, then we open a file. (head returns with !althandle)
	} else if (!node->althandle) {

		// Stat the file, could be useful
		// Also, stat when it is a RAR is wrong.
        // We stat() before open now to detect the case where the
        // path is a DIRECTORY, as apparently some OS will let us open it.
		if (!stat(node->disk_path, &stbf)) {
			node->bytes_size = (lion64_t) stbf.st_size;
		} else {
			debugf("[request] stat(%s) failed: %s. \n",
				   node->disk_path, strerror(errno));
		}

        if (S_ISDIR(stbf.st_mode)) {
			request_reply(node, 404, "No such file");
			return;
		}


		node->althandle = lion_open(node->disk_path,
									O_RDONLY,
									0400,
									LION_FLAG_NONE,
									(void *) node);

		if (!node->althandle) {
			request_reply(node, 404, "No such file");
			return;
		}

		// Set handler
		lion_set_handler(node->althandle, request_file_handler);

		// Disable read until we are ready
		lion_disable_read(node->althandle);


		// Do we lseek?
		if (node->bytes_from) {

			//			debugf("[request] lseek to %"PRIu64" filesize %"PRIu64"\n",
			//node->bytes_from, node->bytes_size);

#ifdef WIN32
			__int64 pos;
			debugf("[request] _lseeki64(%s, %"PRIu64");\n", node->path, node->bytes_from);
			if ((pos = _lseeki64(lion_fileno(node->althandle),
							(__int64)node->bytes_from,
							SEEK_SET)) != (__int64)node->bytes_from) {
				debugf("[request] win32 lseek failed: %s (%"PRIu64" != %"PRIu64")\n", strerror(errno),
					node->bytes_from, pos);
//				lion_disconnect(node->althandle);
//				return 0;
				debugf("\n");
			}
			if ((pos=_lseeki64(lion_fileno(node->althandle),(__int64)0, SEEK_CUR)) != (__int64)node->bytes_from)
				debugf("failed %d: %d\n", lion_fileno(node->althandle), errno);
#else
			debugf("[request] lseek(%s, %"PRIu64");\n", node->path, node->bytes_from);
			if (lseek(lion_fileno(node->althandle),
					  node->bytes_from,
					  SEEK_SET) == -1) {
				debugf("[request] lseek failed: %s\n", strerror(errno));
				lion_disconnect(node->althandle);
				return;
			}
#endif

		} //bytes_from

		// Turn on binary mode
		lion_setbinary(node->althandle);





	} //althandle

	// Make sure it is a file?
	// FIXME

	debugf("[request] stat(%s) for size: %"PRIu64" bytes. sizeof(st_size) is %ld (we want 8!)\n",
		   node->disk_path,
		   node->bytes_size,
		   sizeof(stbf.st_size));

	// Too far?
	if (node->bytes_from && (node->bytes_from >= node->bytes_size)) {
		debugf("[request] Range error: %"PRIu64" >= %"PRIu64". \n",
			   node->bytes_from, node->bytes_size);
		node->bytes_from = 0;
		node->bytes_size = 0;
		request_reply(node, 416, "Requested Range Not Satisfiable");
		return;
	}



	// If it is a stream, zero out the byte range.
    if (node->cgi_stream) {
		node->bytes_from = 0;
		node->bytes_to   = 0;
		node->bytes_send = 0;
		node->type &= ~REQUEST_TYPE_KEEPALIVE;
		node->type |= REQUEST_TYPE_CLOSED;
        node->send_range = 0;
		debugf("[request] skipping Content-Length, stream=1\n");
    }



	// Are we sure this gets sent before we receive the event from LION
	// the the file is open? This _should_ be the case.
	if (node->send_range)
		request_reply(node, 206, "Partial Content");
	else
		request_reply(node, 200, "OK");

}



//
// Once a dir listing request is finished into the tmpfile
// we send tail, then close it, and re-open it for sending.
//
void request_finishdir(request_t *node)
{
	// scan mode
	if (conf_xmlscan) {
		xmlscan_finishdir(node);
		return;
	}

	// If we are writing &playall, and potentially started some unrar list
	// we need to let that finish, which then calls here.
	if (node->althandle && node->rar_name) return;


    if (node->llrar_cache_fill) {
        node->llrar_cache_fill = 0;
		node->current_event = REQUEST_EVENT_RAR_NEEDSIZE; // events are one-shot, always clear.
        return;
    }

	// Write tail, and possibly more passes.

    // Dump the cache to a directory listing, if we are not in UPNP mode,
    // or if we are in UPNP directory listing.
    if (!node->menu_mode)
        // Write tail section, this might kick off more listings, so keep
        // calling it, and we return here each time.
        if (skin_write_tail(node))
            return;

	// Disable menu_mode now, so that the 2nd call to request_action
	// can pass.
	debugf("[request] clearing menu mode, closing tmpfile and waiting for WFILE_CLOSED event.\n");
	node->menu_mode = 0;

	// root changes it for dirlisting purposes. Not that the handler is
	// important.
	lion_set_handler(node->tmphandle, request_writefile_handler);



	// Here an issue, we need to close the file, and re-open it for
	// reading. We can not use reopen or similar in lion, as we want
	// to be Windows compatible. But we do want to avoid a race condition
	// when someone deletes the file, and creates a symlink to a file we
	// aren't supposed to read.
	//
	// Stat it now, and then again once open
	//fstat( lion_fileno( node->tmphandle ), &node->stat );
	lion_close( node->tmphandle );

	// The WFILE_CLOSE event will be set here.
}




void request_events(void)
{
	request_t *node, *next;
	request_event_t current_event;

	for (node = request_head;
		 node;
		 node = next) {
		next = node->next;

	// request nodes.
		current_event = node->current_event;
		node->current_event = REQUEST_EVENT_NEW; // events are one-shot, always clear.
		if ((current_event != REQUEST_EVENT_NEW) &&
			(current_event != REQUEST_EVENT_DVDREAD_SEND))
			debugf("[request] *** process events : %p has event %d\n", node, current_event);

		switch (current_event) {

		case REQUEST_EVENT_ACTION:
			debugf("[request] event calling request_action\n");
			request_action(node);
			break;

		case REQUEST_EVENT_CLOSED:
			debugf("[request] event calling request_freenode\n");
			request_freenode(node);
			break;

		case REQUEST_EVENT_WFILE_OPENED:
			debugf("[request] event calling WFILE OPEN\n");
			//X
			// dirlist mode, and we have opened the tmpfile. Kick in dirlist.
			if (node->menu_mode) {
				char buffer[1024];
				snprintf(buffer, sizeof(buffer),
						 "%crw-rw-rw- 1 user  group  %"PRIu64" May 27 11:05 %s",
						 S_ISDIR(node->stat.st_mode) ? 'd' : '-',
						 (lion64_t) node->stat.st_size,
						 node->path);
				skin_write_menu(node, buffer);
				request_finishdir(node);
				return ;
			}

			node->pass = 0; // The cache only pass.
			// Send out the head.html file.
            // This is now done from INSIDE skin_write_tail()!!
			//skin_write_head(node);

			// Send out the contents of the directory.
			switch (node->process_function) {
				skin_t *skin;

			case REQUEST_PROCESS_UNRAR:
				node->process_function = 0;
				skin = skin_process_cmd(node);

				// DVD device, we need to get undvd
				if (root_is_dvdread(node->root_index))
					skin = skin_findbyname(node->skin_type, "DVD_Directory", 1);

				llrar_list(node, skin ? skin->args : skin_get_rar());
				break;
			case REQUEST_PROCESS_RARISO:
				debugf("[request] listing unrar(ISO): '%s'\n",
                       node->dvdread_file);
				//advd_list(node->rar_file, node->disk_path, node);
                // skin of node, will be unrar
                // skin of rar_file (filename.iso) will be undvd
				//skin = skin_process_cmd(node);
				skin = skin_findbymatch(node->skin_type, node->dvdread_file, 1);
				//node->process_function = REQUEST_PROCESS_RARISO;
				llrar_list(node, skin->args);
				break;

			default:

                // Regular dirlisting it is
				root_list(node, node->disk_path);
				break;
			} // switch

			break; // WFILE_OPENED


		case REQUEST_EVENT_WFILE_CLOSED:

            if (node->upnp_type == UPNP_CUPNP) {
                debugf("[request] cupnp mode, releasing other thread to take over\n");
                node->upnp_type = UPNP_CUPNP_READY;
                cupnp_resume(node);
                break;
            }


			if (stat(node->tmpname, &node->stat )) {
				debugf("[request] Warning - failed to stat tmpfile '%s': %s\n",
					   node->tmpname, strerror(errno));
			} else if (!node->stat.st_size) {
				debugf("[request] Warning - tmpfile '%s' is 0 bytes, this won't end well.\n", node->tmpname);
			}

			debugf("[request] reopening tmpfile\n");


			// path has whatever the original query path was
			// tmpname has the full path of the temporary file we want to send
			// disk_path has the real root of "path", which is no longer needed,
			// so we assign over tmpname to simulate a filesend.
			// But we keep tmpname so we know to delete the file once we are done.

			SAFE_COPY(node->disk_path, node->tmpname);

            request_action(node);

			break; // WFILE_CLOSED

            // POST events require us to read input until CONTENT-LENGTH.
		case REQUEST_EVENT_POST_READDATA:
            break;

        case REQUEST_EVENT_RAR_NEEDSIZE:
            // Could we get RAR size?
            //llrar_dumpcache();
            node->bytes_size = llrar_getsize(node);
            debugf("[request] RAR gotsize %"PRIu64"\n", node->bytes_size);

            if (node->bytes_size) {
                request_action(node);
            } else {
                request_reply(node, 404, "No such file");
            }
            break;

		default:
			break;
		} // switch event

	} // for request nodes.

}


THREAD_SAFE static char *status_message = NULL;
THREAD_SAFE static time_t status_time = 0;

void request_set_status(char *msg)
{

	SAFE_FREE(status_message);
	status_message = strdup(msg);
	status_time = lion_global_time;

}


// We will also need vararg form of this function
char *request_get_status(void)
{

	if (status_time &&
		(lion_global_time > status_time) &&
		(lion_global_time - status_time >= 60)) {
		SAFE_FREE(status_message);
		status_time = 0;
	}

	if (status_message)
		return status_message;

	if (debug_on&DEBUG_FLAG)
		return "Debug: Status OK";

	return "";

}

