#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lion.h"
#include "misc.h"

#include "ssdp.h"
#include "query.h"
#include "parser.h"


#define DEBUG_FLAG 2
#include "debug.h"



// The main lion listen socket for this SSDP code
static lion_t            *ssdp_listener          = NULL;

static int                ssdp_port              = SSDP_DEFAULT_PORT;
       unsigned long      ssdp_interface         = 0;
static unsigned long      ssdp_multicast         = 0;
static int                ssdp_enabled           = 0;
       int                ssdp_upnp              = 0;
       char              *ssdp_upnp_uuid         = NULL;
       int                ssdp_upnp_port         = 8008;

static query_t           *parsing                = NULL;




static int ssdp_handler(lion_t *handle, void *user_data,
						int status, int size, char *line);





int ssdp_init(void)
{

	if (!ssdp_enabled)
		return 0;

	if (ssdp_listener) {
		lion_close(ssdp_listener);
		ssdp_listener = NULL;
	}


	debugf("[ssdp] creating listener...\n");


	ssdp_listener = lion_udp_new(&ssdp_port, ssdp_interface,
								 LION_FLAG_FULFILL,
								 NULL);
	lion_set_handler(ssdp_listener, ssdp_handler);


	// Set multicast membership
	if (!ssdp_multicast)
		ssdp_multicast = lion_addr(SSDP_DEFAULT_MULTICAST);

	lion_add_multicast(ssdp_listener, ssdp_multicast);


	return 0;
}




void ssdp_free(void)
{

    SAFE_FREE(ssdp_upnp_uuid);

	if (ssdp_listener) {
		lion_close(ssdp_listener);
	}

	ssdp_listener = NULL;

}




//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : SSDP
// Required :
// Optional : ON, multicast, port, bindif
//
void ssdp_cmd(char **keys, char **values,
			  int items,void *optarg)
{
	char *on,*multicast,*port,*bindif, *upnp, *upnpport;

	on        = parser_findkey(keys, values, items, "ON");
	multicast = parser_findkey(keys, values, items, "MULTICAST");
	port      = parser_findkey(keys, values, items, "PORT");
	bindif    = parser_findkey(keys, values, items, "BINDIF");
	upnp      = parser_findkey(keys, values, items, "UPNP");
	upnpport  = parser_findkey(keys, values, items, "UPNPPORT");

	// If we have "ON" present at all, irrelevant of its value, we assume ON.
	// There is no need to specify "OFF".
	if (on) {

		ssdp_enabled = 1;

		if (multicast && *multicast)
			ssdp_multicast = lion_addr(multicast);

		if (port && *port)
			ssdp_port = atoi(port);

		if (bindif && *bindif)
			ssdp_interface = lion_addr(bindif);

	}

    if (upnp) {
        ssdp_upnp = 1;
        if (*upnp) ssdp_upnp_uuid = strdup(upnp);
        if (upnpport && *upnpport) ssdp_upnp_port = atoi(upnpport);
    }

    if (!ssdp_upnp_uuid)
        ssdp_upnp_uuid = strdup(SSDP_DEFAULT_UUID);

    // Let's never have it NULL.
    if (!ssdp_upnp_uuid)
        ssdp_upnp_uuid = strdup(SSDP_DEFAULT_UUID);

    debugf("SSDP %s. UPNP %s\n",
           ssdp_enabled ? "enabled" : "disabled",
           ssdp_upnp ? "enabled" : "disabled");

}















static int ssdp_handler(lion_t *handle, void *user_data,
						int status, int size, char *line)
{
	char *ar, *token;

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[ssdp] is listening\n");
		break;

	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:
		ssdp_listener = NULL;
		debugf("[ssdp] closed/lost SSDP listening socket (%d:%s)\n",
			   size, line ? line : "gracefully closed");
		return 0;

	case LION_INPUT:
	  debugf("[ssdp] >> '%s'\n", line);

		// [ssdp] >> NOTIFY * HTTP/1.1
		// [ssdp] >> HOST: 239.255.255.250:1900
		// [ssdp] >> CACHE-CONTROL: max-age = 300
		// [ssdp] >> LOCATION: http://192.168.0.29:2020/myiBoxUPnP/description.xml
		// [ssdp] >> NT: upnp:rootdevice
		// [ssdp] >> NTS: ssdp:alive
		// [ssdp] >> SERVER: UCosII, UPnP/1.0, Syabas myiBox UPnP-stack, v1.0
		// [ssdp] >> USN: uuid:myiBoxUPnP::upnp:rootdevice
		// [ssdp] >>

		// We look for the NOTIFY line, and discard anything else.
		// If we find it, we parse until the blank line.
		// Upon receiving blank line, and if we parsed all required we call
		// the query functions.

		// We should receive all lines from one query without worry about
		// interleaving lines from another query since lion will keep parsing
		// the entire received packet until exhausted. If we had UDP
		// fragmentation, this will not be true, but then you have bigger
		// issues and this code will just fail, but gracefully.

		// If we received a valid line, AND we are NOT parsing anything
		if (line && *line &&
			!parsing) {

			ar = line;
			token = misc_digtoken(&ar, " \r\n");

			if (!token || !*token) break;

			if (!mystrccmp("NOTIFY", token) ||
				!mystrccmp("M-SEARCH", token)) {

				parsing = query_newnode();
				if (!parsing) break;

				lion_getpeername(handle, &parsing->ip, &parsing->port);

				//debugf("[ssdp] Parsing NOTIFY announcement from %s:%d .. \n ",
				//	   lion_ntoa(parsing->ip), parsing->port);
                if (ssdp_upnp &&
                    !mystrccmp("M-SEARCH", token)) {
                    parsing->m_search = 1;
                }

			}


			break;
		}

		// If we have a valid line, AND we ARE parsing, parse it.
		if (line && *line && parsing) {

			ar = line;
			token = misc_digtoken(&ar, " \r\n");

			if (!token || !*token) break;

			if (!mystrccmp("HOST:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->host = strdup(token);

			} else if (!mystrccmp("CACHE-CONTROL:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->cache_control = strdup(token);
				break;

			} else if (!mystrccmp("LOCATION:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->location = strdup(token);
				break;

			} else if (!mystrccmp("NT:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->nt = strdup(token);
				break;

			} else if (!mystrccmp("MAN:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->man = strdup(token);
				break;

			} else if (!mystrccmp("NTS:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->nts = strdup(token);
				break;

			} else if (!mystrccmp("MX:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->mx = strdup(token);
				break;

			} else if (!mystrccmp("ST:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->st = strdup(token);
				break;

			} else if (!mystrccmp("SERVER:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->server = strdup(token);
				break;

			} else if (!mystrccmp("USN:", token)) {

				token = misc_digtoken(&ar, "\r\n");
				if (!token) break;

				parsing->usn = strdup(token);
				break;

			} else {
				//debugf("[ssdp] Unparsed token '%s' and '%s'\n",
				//	   token, ar);
			}

			break;

		} // if line and parsing

		// If we have an empty line, AND we ARE parsing
		if (line && !*line && parsing) {

			// This call releases the "parsing" node into the care of query.c
			if (parsing->m_search) {
				query_msearch(parsing, ssdp_listener);
            } else {
 				query_process(parsing);
            }
			parsing = NULL;

		}


		break;
	}

	return 0;
}


//
// If we want to support PS3
//
// PS3:
// >> M-SEARCH * HTTP/1.1
// >> HOST: 239.255.255.250:1900
// >> ST: urn:schemas-upnp-org:device:MediaServer:1
// >> MAN: "ssdp:discover"
// >> MX: 2
// >> X-AV-Client-Info: av=5.0; cn="Sony Computer Entertainment"; mn="PLAYSTATION3"; mv="1.0";
//
// We reply with:
// << NOTIFY * HTTP/1.1
// << HOST:239.255.255.250:1900
// << Cache-Control: max-age= 600
// << LOCATION: http://192.168.1.1:80/upnp/igdrootdesc.xml
// << NT:uuid:13814000-1dd2-11b2-813c-00095b47e502
// << NTS:ssdp:alive
// << SERVER: Nucleus/1.13.20 UPnP/1.0 MR814/4.14 RC4
// << USN: uuid:13814000-1dd2-11b2-813c-00095b47e502


