#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"

#include "query.h"
#include "conf.h"
#include "parser.h"
#include "version.h"
#include "httpd.h"
#include "ssdp.h"
#define DEBUG_FLAG 256
#include "debug.h"


// In future we could support the source port being set, but for now...
static int               query_port             = 0;
static int               query_announce         = 1;


static query_match_t    *query_match_array       = NULL;
static int               query_match_count       = 0;
static query_manual_t   *query_manual_head       = NULL;

static int query_handler(lion_t *handle, void *user_data,
						int status, int size, char *line);


static void query_timer_callback(timers_t *timer, void *userdata)
{
	query_manual_t *manual = (query_manual_t *)userdata;
    query_t *node;

	if (!timer) return ;

    node = query_newnode();
    // node->location = strdup("http://127.0.0.1:2020/myiBoxUPnP/description.xml")
    // node->usn = strdup("*myiBoxUPnP*SyabasSTB*");

	SAFE_COPY(node->location, manual->url);
	SAFE_COPY(node->usn, manual->usn);

	debugf("[query] manual SSDP announce...\n");

    query_process(node);

}




int query_init(void)
{
	// Set up the manual announce now
	query_manual_t *tmp;

	for (tmp = query_manual_head; tmp; tmp = tmp->next) {

		// Arm timer
		tmp->timer = timers_new(tmp->frequency, 0,
								TIMERS_FLAG_RELATIVE|TIMERS_FLAG_REPEAT,
								query_timer_callback,
								(void *) tmp);
		if (!tmp) {
			printf("[query] failed to arm timer for manual SSDP\n");
		}

		// Execute timer now (so we don't have to wait for the first announce
		query_timer_callback(tmp->timer, (void *) tmp);

	} // for

	return 0;
}




void query_free(void)
{
	int i;
	query_manual_t *tmp, *next;

	// Free the match list, if any.
	for (i = 0; i < query_match_count; i++) {
		SAFE_FREE(query_match_array[i].match);
		SAFE_FREE(query_match_array[i].pattern);
	}

	query_match_count = 0;
	SAFE_FREE(query_match_array);

	for (tmp = query_manual_head; tmp; tmp = next) {
		next = tmp->next;
		SAFE_FREE(tmp->url);
		SAFE_FREE(tmp->usn);
		timers_cancel(tmp->timer);

		SAFE_FREE(tmp);
	}
}




query_t *query_newnode(void)
{
	query_t *result;

	result = malloc(sizeof(*result));
	if (!result) return NULL;

	memset(result, 0, sizeof(*result));

	return result;

}



void query_freenode(query_t *query)
{
    lion_t *tmp;

    tmp = query->handle;
    if (tmp) {
        query->handle = NULL;
        lion_disconnect(tmp);
    }

	SAFE_FREE(query->host);
	SAFE_FREE(query->cache_control);
	SAFE_FREE(query->location);
	SAFE_FREE(query->nt);
	SAFE_FREE(query->nts);
	SAFE_FREE(query->server);
	SAFE_FREE(query->usn);
	SAFE_FREE(query->path);
	SAFE_FREE(query->location_host);
	SAFE_FREE(query->man);
	SAFE_FREE(query->mx);
	SAFE_FREE(query->st);

	SAFE_FREE(query);
}









// CMD      : ANNOUNCE
// Required : url, usn, freq
// Optional :
//
void query_cmd_announce(char **keys, char **values,
						int items,void *optarg)
{
	char *url, *usn, *freq;
	query_manual_t *tmp;

	// Requireds
	url     = parser_findkey(keys, values, items, "URL");
	usn     = parser_findkey(keys, values, items, "USN");
	freq    = parser_findkey(keys, values, items, "FREQ");

	if (!url || !*url || !usn || !*usn || !freq || !*freq) {
		printf("      : [announce] missing required field: URL,USN,FREQ\n");
		//do_exit = 1;
		return;
	}

	tmp = (query_manual_t *) malloc(sizeof(*tmp));
	if (!tmp) {
		printf("      : [announce] out of memory\n");
		return ;
	}
	memset(tmp, 0, sizeof(*tmp));

	SAFE_COPY(tmp->url, url);
	SAFE_COPY(tmp->usn, usn);
	tmp->frequency = atoi(freq);

	// Insert into list
	tmp->next = query_manual_head;
	query_manual_head = tmp;


}


//
// function for conf reading. Called when conf file has command specified.
//
// CMD      : USN
// Required : match
// Optional : pattern (default is '*')
//
void query_cmd_usn(char **keys, char **values,
			  int items,void *optarg)
{
	char *match, *pattern;
	query_match_t *tmp;

	// Requireds
	match     = parser_findkey(keys, values, items, "MATCH");

	if (!match || !*match) {
		printf("      : [usn] missing required field: MATCH\n");
		//do_exit = 1;
		return;
	}

	// Optionals
	pattern   = parser_findkey(keys, values, items, "PATTERN");

	// Create a new match node here.
	// Normally I "realloc" in chunks for efficiency, but since this is
	// during conf reading, and there most likely will not be many USN lines
	// realistically, it should not be too bad.


	tmp = (query_match_t *) realloc(query_match_array,
								   sizeof(query_match_t) *
								   (query_match_count + 1));

	if (!tmp) {
		debugf("      : out of memory allocating USN\n");
		return;
	}

	// Increase our counter
	query_match_count++;

	// Assign us in.
	query_match_array = tmp;

	// The new area
	tmp = &query_match_array[ query_match_count - 1 ]; // Nobody likes -1

	tmp->match = strdup(match);  // Do we need to strdup?

	if (pattern && *pattern)
		tmp->pattern = strdup(pattern);
	else
		tmp->pattern = strdup("*");


	debugf("      : [usn] recognising '%s' from '%s'\n",
		   tmp->match, tmp->pattern);

}







void query_process(query_t *node)
{
	char *ar, *ip, *port;
	int i;

	//debugf("[query] Got process request for location %s\n",
	//	   node->location);

	if (!node->location || !node->usn) {
		query_freenode(node);
		return;
	}



	// Parse the location string, and connect over.
	// "http://192.168.0.29:2020/myiBoxUPnP/description.xml"
	//
	// Can this then be user:pass as well?
	if (strncasecmp("http://", node->location, 7)) {
		debugf("[query] location does not start with http://\n");
		goto parse_error;
	}

	// Skip past the http:// part
	ar = &node->location[7];

	ip = misc_digtoken(&ar, ": \r\n");
	if (!ip || !*ip) goto parse_error;

	port = misc_digtoken(&ar, "/ \r\n");
	if (!port || !*port) goto parse_error;

	// "ar" now points to the "path" part of the url.
	node->path = strdup(ar);
	node->location_host = strdup(ip);
	node->location_port = atoi(port);



	// Do we want to check usn here and check its a device we know?
	for (i = 0; i < query_match_count; i++) {

		debugf("[query] matching '%s' with '%s'\n",
			   node->usn, query_match_array[i].match);

		if (!lfnmatch(query_match_array[i].match,
					  node->usn,
					  LFNM_CASEFOLD)) {

			// Now match with pattern
			// FIXME, this matches IP pattern based on the HTTP requests
			// host, and not the actual from IP. However, it IS the IP
			// we will connect to, so a rogue person can only make us
			// connect somewhere valid multiple times. However, that is
			// still bad.
			if (!lfnmatch(query_match_array[i].pattern,
						  node->location_host,
						  LFNM_CASEFOLD)) {

#if 0
				// Until we can sort out the timeout and ignoring repeats.
				static int firstonly = 0;

				if (firstonly) return;

				firstonly = 1;
#endif

				debugf("[query] connecting to device %s:%d\n",
					   ip, atoi(port));

				// Set up our life
				node->time = lion_global_time;

				// Issue connection
				node->handle = lion_connect(ip, atoi(port),
											ssdp_interface, query_port,
											LION_FLAG_FULFILL,
											(void *) node);

				lion_set_handler(node->handle, query_handler);

				return;

			} // match pattern

		} // match match

	} // for loop

	//debugf("[query] no match usn for '%s' - ignoring\n", node->usn);
	query_freenode(node);

	return;

 parse_error:
	debugf("[query] parse error\n");

	query_freenode(node);
	return ;

}






// "GET /myiBoxUPnP/description.xml?POSTSyabasiBoxURLpeername=totoro&peeraddr=192.168.0.11:8000& HTTP/1.1"
// "User-Agent: Jakarta Commons-HttpClient/2.0final"
// "Host: 192.168.0.29:2020"

static int query_handler(lion_t *handle, void *user_data,
						int status, int size, char *line)
{
	query_t *node = (query_t *) user_data;
	unsigned long myip;
	int myport;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[query] connected to remote device, issuing query for /%s\n",
			   node->path);
		//GET /myiBoxUPnP/description.xml?POSTSyabasiBoxURLpeername=totoro&peeraddr=192.168.0.11:8000& HTTP/1.1
		lion_getsockname(handle, &myip, &myport);
		// If they used bindif, it probably makes since to use it, instead of
		// the peeraddr, assuming the user knows what they want.
		if (ssdp_interface)
			myip=ssdp_interface;

		if (query_announce)
			lion_printf(handle,
						"GET /%s?POSTSyabasiBoxURLpeername=%s&peeraddr=%s:%d& HTTP/1.1\r\n"
						"User-Agent: llink v%s (build %d)\r\n"
						"Host: %s:%d\r\n"
						"\r\n",
						node->path,
						conf_announcename,
						lion_ntoa(myip),
						httpd_port,
						VERSION,
						VERSION_BUILD,
						node->location_host,
						node->location_port);
		else
			lion_printf(handle,
						"GET /%s HTTP/1.1\r\n"
						"User-Agent: llink v%s (build %d)\r\n"
						"Host: %s:%d\r\n"
						"\r\n",
						node->path,
						VERSION,
						VERSION_BUILD,
						node->location_host,
						node->location_port);

		if (!node || !node->handle) return 0;

		if (query_announce) {
			debugf("GET /%s?POSTSyabasiBoxURLpeername=%s&peeraddr=%s:%d& HTTP/1.1\r\n"
				   "User-Agent: llink v%s (build %d)\r\n"
				   "Host: %s:%d\r\n"
				   "\r\n",
				   node->path,
				   conf_announcename,
				   lion_ntoa(myip),
				   httpd_port,
				   VERSION,
				   VERSION_BUILD,
				   node->location_host,
				   node->location_port);
		} else {
			debugf("GET /%s HTTP/1.1\r\nUser-Agent: llink v%s (build %d)\r\nHost: %s:%d\r\n\r\n",
				   node->path,
				   VERSION,
				   VERSION_BUILD,
				   node->location_host,
				   node->location_port);
		}

		break;

	case LION_CONNECTION_LOST:
		debugf("[query] lost QUERY listening socket (%d:%s)\n",
			   size, line ? line : "");
	case LION_CONNECTION_CLOSED:
		node->handle = NULL;
		query_freenode(node);
		return 0;

	case LION_INPUT:
		//debugf("[query] >> '%s'\n", line);

		// Add code to parse the xml here. I'm not sure we really care
		// at the moment. For some reason, we can not "just" GET the
		// description.xml file, without issuing POST part. It sends back
		// 200 OK but no content. Only when we POST (and appear on the player
		// as available) do we get content, but then how can we decide if
		// we support this device?
		break;

	}

	return 0;
}


query_match_t *query_match_ip(char *ip)
{
	int i;

	for (i = 0; i < query_match_count; i++) {
		if (!lfnmatch(query_match_array[i].pattern,
					  ip,
					  LFNM_CASEFOLD)) {
			return &query_match_array[i];
		}
	}

	// Return default.
	return NULL;
}



void query_msearch(query_t *node, lion_t *replysock)
{
    //	char *ar, *ip, *port;
	//int i;
	unsigned long myip;
    int tmpport;
	//return;

	// Our IP, use HTTP bind, if set, otherwise
	// use SSDP query, if set, otherwise
	// getsockname
	if (ssdp_interface)
		myip=ssdp_interface;
	else
		myip=httpd_myip();

    // As it happens, you can not call getsockname when you receive a
    // broadcast dgram. We can change to use recvfrom() and ask for OOB
    // data (IP_PKTINFO/IP_RECVDSTADDR/IP_RECVIF). Which may not be portable?
    // We can also open a new UDP socket, then call connect() to make it
    // connection-oriented socket. Then we can call getsockname().
    if (!myip) {
        lion_t *tmp;
        int tort = 0; // random port.
        tmp = lion_udp_new(&tort, 0, LION_FLAG_FULFILL, NULL);
        if (tmp) {
            lion_udp_connect(tmp, node->ip, node->port);
            lion_getsockname(tmp, &myip, &tmpport);
            lion_close(tmp);
        }
        debugf("[query] fudged getsockname replies: %s:%d\n",
               lion_ntoa(myip), httpd_port);
    } // myip


#if 0
#if defined(IP_PKTINFO)
    err = setsockopt(*sktPtr, IPPROTO_IP, IP_PKTINFO, &kOn, sizeof(kOn)); //Linux
#elif defined(IP_RECVDSTADDR)
    err = setsockopt(*sktPtr, IPPROTO_IP, IP_RECVDSTADDR, &kOn, sizeof(kOn)); //BSD
#elif defined(IP_RECVIF)
    err = setsockopt(*sktPtr, IPPROTO_IP, IP_RECVIF, &kOn, sizeof(kOn)); // Solaris
#endif
    if (err < 0)
        perror("setsockopt - IP_PKTINFO/IP_RECVDSTADDR/IP_RECVIF");
#endif

	debugf("[query] Got M-SEARCH from %s:%d\n",
		   lion_ntoa(node->ip), node->port);

    /* ** example from xbox360 and whsUpnp
HTTP/1.1 200 OK
ST:urn:schemas-upnp-org:service:ContentDirectory:1
Location:http://192.168.0.199:41955/device.xml
USN:uuid:8b75c4e0-ce46-462c-a4bd-b0185902b2a1::urn:schemas-upnp-org:service:ContentDirectory:1
Server:Microsoft-Windows-NT/5.1 UPnP/1.0 UPnP-Device-Host/1.0
Cache-Control:max-age=1800
Ext:
    */



    if (node->st && !lfnmatch("*ContentDirectory:1*",
                  node->st,
                  LFNM_CASEFOLD)) {
#if 0
        lion_printf(replysock,
                    "HTTP/1.1 200 OK\r\n"
                    "ST:urn:schemas-upnp-org:service:ContentDirectory:1\r\n"
                    "Location: http://%s:%d/?upnp=scpd.xml\r\n"
                    "USN:uuid:%s::urn:schemas-upnp-org:service:ContentDirectory:1\r\n"
                    "SERVER: llink media streamer %s\r\n"
                    "Cache-Control:max-age=1800\r\n"
                    "Content-Length: 0\r\n"
                    "Ext:\r\n\r\n",
                    lion_ntoa(myip), httpd_port,
                    ssdp_upnp_uuid,
                    VERSION);

        debugf(
               "HTTP/1.1 200 OK\r\n"
               "ST:urn:schemas-upnp-org:service:ContentDirectory:1\r\n"
               "Location: http://%s:%d/?upnp=scpd.xml\r\n"
               "USN:uuid:%s::urn:schemas-upnp-org:service:ContentDirectory:1\r\n"
               "SERVER: llink media streamer %s\r\n"
               "Cache-Control:max-age=1800\r\n"
               "Ext:\r\n\r\n",
               lion_ntoa(myip), httpd_port,
               ssdp_upnp_uuid,
               VERSION);
#endif


    } // ContentDirectory

    if (node->st && !lfnmatch("*ConnectionManager:1*",
                  node->st,
                  LFNM_CASEFOLD)) {
#if 0
        lion_printf(replysock,
                    "HTTP/1.1 200 OK\r\n"
                    "ST:urn:schemas-upnp-org:service:ConnectionManager:1\r\n"
                    "Location: http://%s:%d/?upnp=conmgr.xml\r\n"
                    "USN:uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1\r\n"
                    "SERVER: llink media streamer %s\r\n"
                    "Cache-Control:max-age=1800\r\n"
                    "Content-Length: 0\r\n"
                    "Ext:\r\n\r\n",
                    lion_ntoa(myip), httpd_port,
                    ssdp_upnp_uuid,
                    VERSION);

        debugf(
               "HTTP/1.1 200 OK\r\n"
               "ST:urn:schemas-upnp-org:service:ConnectionManager:1\r\n"
               "Location: http://%s:%d/?upnp=conmgr.xml\r\n"
               "USN:uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1\r\n"
               "SERVER: llink media streamer %s\r\n"
               "Cache-Control:max-age=1800\r\n"
               "Ext:\r\n\r\n",
               lion_ntoa(myip), httpd_port,
               ssdp_upnp_uuid,
               VERSION);
#endif


    } // ContentDirectory

    if (node->st && !lfnmatch("*MediaServer:1*",
                  node->st,
                  LFNM_CASEFOLD)) {

#if 0
        lion_printf(replysock,
                    "HTTP/1.1 200 OK\r\n"
                    "ST:urn:schemas-upnp-org:device:MediaServer:1\r\n"
                    "Location: http://%s:%d/?upnp=llink.xml\r\n"
                    "USN:uuid:%s::urn:schemas-upnp-org:device:MediaServer:1\r\n"
                    "SERVER: llink media streamer %s\r\n"
                    "Cache-Control:max-age=1800\r\n"
                    "Content-Length: 0\r\n"
                    "Ext:\r\n\r\n",
                    lion_ntoa(myip), httpd_port,
                    ssdp_upnp_uuid,
                    VERSION);
        debugf(
               "HTTP/1.1 200 OK\r\n"
               "ST:urn:schemas-upnp-org:device:MediaServer:1\r\n"
               "Location: http://%s:%d/?upnp=llink.xml\r\n"
               "USN:uuid:%s::urn:schemas-upnp-org:device:MediaServer:1\r\n"
               "SERVER: llink media streamer %s\r\n"
               "Cache-Control:max-age=1800\r\n"
               "Ext:\r\n\r\n",
               lion_ntoa(myip), httpd_port,
               ssdp_upnp_uuid,
               VERSION);
#endif

    }

    if (node->st && !lfnmatch("*upnp:rootdevice*",
                  node->st,
                  LFNM_CASEFOLD)) {

#if 0
        debugf("[upnp] MX sleeping...\n");
        if (node->mx) sleep(random() % atoi(node->mx));
#endif

#if 0

        lion_printf(replysock,
                    "HTTP/1.1 200 OK\r\n"
                    "ST:upnp:rootdevice\r\n"
                    "Location: http://%s:%d/?upnp=llink.xml\r\n"
                    "USN:uuid:%s::upnp:rootdevice\r\n"
                    "SERVER: llink media streamer %s\r\n"
                    "Cache-Control:max-age=1800\r\n"
                    "Content-Length: 0\r\n"
                    "Ext:\r\n\r\n",
                    lion_ntoa(myip), httpd_port,
                    ssdp_upnp_uuid,
                    VERSION);
        debugf(
               "HTTP/1.1 200 OK\r\n"
			   "ST:upnp:rootdevice\r\n"
			   "Location: http://%s:%d/?upnp=llink.xml\r\n"
			   "USN:uuid:%s::upnp:rootdevice\r\n"
               "SERVER: llink media streamer %s\r\n"
               "Cache-Control:max-age=1800\r\n"
               "Ext:\r\n\r\n",
               lion_ntoa(myip), httpd_port,
               ssdp_upnp_uuid,
               VERSION);
#endif

        // rootdevice will also have discover. Lets only reply once.
        query_freenode(node);
        return;
    }

    if (node->man && !lfnmatch("*ssdp:discover*",
                               node->man,
                               LFNM_CASEFOLD)) {
        // << NOTIFY * HTTP/1.1
// << HOST:239.255.255.250:1900
// << Cache-Control: max-age= 600
// << LOCATION: http://192.168.1.1:80/upnp/igdrootdesc.xml
// << NT:uuid:13814000-1dd2-11b2-813c-00095b47e502
// << NTS:ssdp:alive
// << SERVER: Nucleus/1.13.20 UPnP/1.0 MR814/4.14 RC4
// << USN: uuid:13814000-1dd2-11b2-813c-00095b47e502

        // MX - we are supposed to wait a random time up to MX:
        // before replying, as per spec. We should implement this with
        // timer callbacks?
#if 0
        debugf("[upnp] discover MX sleeping...\n");
        if (node->mx) sleep(random() % atoi(node->mx));
#endif

#if 0
        debugf("[upnp] sendnig rootdevice, AND MediaServer packets\n");
        debugf("[upnp] sending 200 OK MediaServer\n");
        lion_printf(replysock,
                    "HTTP/1.1 200 OK\r\n"
                    "HOST: %s\r\n"
                    "NT: uuid:%s\r\n"
                    "NTS: ssdp:alive\r\n"
                    "USN:uuid:%s::urn:schemas-upnp-org:device:MediaServer:1\r\n"
                    "Location: http://%s:%d/?upnp=llink.xml\r\n"
                    "Cache-Control:max-age=600\r\n"
                    "EXT: \r\n\r\n",
                    node->host,
                    ssdp_upnp_uuid,
                    ssdp_upnp_uuid,
                    lion_ntoa(myip), httpd_port
                    );

        debugf("[upnp] sending NOTIFY rootdevice:alive\n");
        lion_printf(replysock,
                    "NOTIFY * HTTP/1.1\r\n"
                    "HOST: %s\r\n"
                    "NT: upnp:rootdevice\r\n"
                    "NTS: ssdp:alive\r\n"
                    "USN: uuid:%s::upnp:rootdevice\r\n"
                    "Location: http://%s:%d/?upnp=llink.xml\r\n"
                    "Cache-Control:max-age=600\r\n"
                    "EXT: \r\n\r\n",
                    node->host,
                    ssdp_upnp_uuid,
                    lion_ntoa(myip), httpd_port
                    );
        debugf("[upnp] sending NOTIFY ConnectionManager\n");
        lion_printf(replysock,
                    "NOTIFY * HTTP/1.1\r\n"
                    "HOST: %s\r\n"
                    "NT: urn:schemas-upnp-org:service:ConnectionManager:1\r\n"
                    "NTS: ssdp:alive\r\n"
                    "USN: uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1\r\n"
                    "Location: http://%s:%d/?upnp=conmgr.xml\r\n"
                    "Cache-Control:max-age=600\r\n"
                    "EXT: \r\n\r\n",
                    node->host,
                    ssdp_upnp_uuid,
                    lion_ntoa(myip), httpd_port
                    );

        debugf(
                    "NOTIFY * HTTP/1.1\r\n"
                    "HOST: %s\r\n"
                    "NT: uuid:%s\r\n"
                    "NTS: ssdp:alive\r\n"
                    "USN: uuid:%s\r\n"
                    "Location: http://%s:%d/?upnp=llink.xml\r\n"
                    "Cache-Control:max-age=600\r\n"
                    "EXT: \r\n\r\n",
                    node->host,
                    ssdp_upnp_uuid,
                    ssdp_upnp_uuid,
                    lion_ntoa(myip), httpd_port
                    );

#endif
    }


	query_freenode(node);

}

/*
SUBSCRIBE /_urn:microsoft.com:serviceId:X_MS_MediaReceiverRegistrar_event HTTP/1.1
HOST: 192.168.11.10:8001
CALLBACK: <http://192.168.11.24:49152/>
NT: upnp:event
TIMEOUT: Second-1801


[192.168.1.160:49839]:
HTTP/1.1 200 OK<CRLF>
CACHE-CONTROL: max-age=1200<CRLF>
DATE: Sun, 27 Sep 2009 14:50:59 GMT<CRLF>
LOCATION: http://192.168.1.160:5001/description/fetch<CRLF>
SERVER: Windows_7-x86-6.1, UPnP/1.0, PMS/1.10.51<CRLF>
ST: urn:schemas-upnp-org:device:MediaServer:1<CRLF>
EXT: <CRLF>
USN: uuid:2ca5c147-b812-312d-b726-2e8857dbd173::urn:schemas-upnp-org:device:MediaServer:1<CRLF>
Content-Length: 0<CRLF><CRLF>
*/

/*
POST /_urn:upnp-org:serviceId:ContentDirectory_control HTTP/1.1
HOST: 210.172.146.228:5001
CONTENT-LENGTH: 269
CONTENT-TYPE: text/xml; charset="utf-8"
SOAPACTION: "urn:schemas-upnp-org:service:ContentDirectory:1#GetSortCapabilities"
USER-AGENT: Linux/2.6.15-sigma, UPnP/1.0, Portable SDK for UPnP devices/1.6.6

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<s:Body><u:GetSortCapabilities
xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"></u:GetSortCapabilities>
</s:Body>
</s:Envelope>


*/

