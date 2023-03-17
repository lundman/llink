#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lion.h"
#include "misc.h"

#include "debug.h"
#include "httpd.h"
#include "parser.h"
#include "request.h"
#include "conf.h"


// The main lion listen socket for this HTTPD code
static lion_t            *httpd_listener          = NULL;

       int                httpd_port              = HTTPD_DEFAULT_PORT;
static unsigned long      httpd_interface         = 0;

static char              *httpd_user              = NULL;
static char              *httpd_pass              = NULL;



static int httpd_handler(lion_t *handle, void *user_data,
						int status, int size, char *line);





int httpd_init(void)
{


	if (httpd_listener) {
		lion_close(httpd_listener);
		httpd_listener = NULL;
	}


	debugf("[httpd] creating listener...\n");


	httpd_listener = lion_listen(&httpd_port, httpd_interface,
								 LION_FLAG_FULFILL,
								 NULL);
	lion_set_handler(httpd_listener, httpd_handler);

	return 0;
}




void httpd_free(void)
{

	if (httpd_listener) {
		lion_close(httpd_listener);
	}

	httpd_listener = NULL;

	SAFE_FREE(httpd_user);
	SAFE_FREE(httpd_pass);

}




//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : HTTP
// Required : port
// Optional : name, bindif, user, pass
//
void httpd_cmd(char **keys, char **values,
			  int items,void *optarg)
{
	char *port;
	char *name, *bindif, *user, *pass, *pin, *bsize;


	// Required
	port      = parser_findkey(keys, values, items, "PORT");

	if (!port || !*port) {
		printf("      : [http] missing required field: PORT\n");
		//do_exit = 1;
		return;
	}

	// Optional
	name      = parser_findkey(keys, values, items, "NAME");
	bindif    = parser_findkey(keys, values, items, "BINDIF");
	user      = parser_findkey(keys, values, items, "USER");
	pass      = parser_findkey(keys, values, items, "PASS");
	pin       = parser_findkey(keys, values, items, "PIN");
	bsize     = parser_findkey(keys, values, items, "BUFFERSIZE");


	httpd_port = atoi(port);

	if (name && *name)
		SAFE_COPY(conf_announcename, name);

	if (bindif && *bindif)
		httpd_interface = lion_addr(bindif);

	if (user && *user)
		SAFE_COPY(httpd_user, user);

	if (pass && *pass)
		SAFE_COPY(httpd_pass, pass);

	if (pin && *pin)
		SAFE_COPY(conf_pin, pin);

	if (bsize && atoi(bsize) > 0) {
		conf_buffersize = atoi(bsize);
        lion_buffersize(conf_buffersize);
    }
}






unsigned long httpd_myip(void)
{
	unsigned long myip;
	int myport;

	if (httpd_interface)
		myip=httpd_interface;
	else
		lion_getsockname(httpd_listener, &myip, &myport);

	return myip;
}








static int httpd_handler(lion_t *handle, void *user_data,
						int status, int size, char *line)
{

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[httpd] is listening\n");
		break;

	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:
		httpd_listener = NULL;
		debugf("[httpd] closed/lost HTTPD listening socket (%d:%s)\n",
			   size, line ? line : "gracefully closed");
		return 0;

	case LION_INPUT:
	case LION_BINARY:
		// There is no input on a listening socket.
		break;

	case LION_CONNECTION_NEW:
		// New connection. Accept it and assign it to its own handler.
		lion_set_handler(
						 lion_accept(
									 handle,
									 0,
									 LION_FLAG_FULFILL,
									 NULL,
									 NULL,
									 NULL),
						 request_handler);
		break;

	}

	return 0;
}





