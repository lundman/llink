#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
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

#define DEBUG_FLAG 4096
#include "debug.h"
#include "httpd.h"
#include "parser.h"
#include "request.h"
#include "upnp.h"
#include "conf.h"
#include "skin.h"

#if WIN32
#include "win32.h"
#endif

/*
 * This is really quite terrible. Such a hacky attempt to support minimal UPNP.
 * But when I started this, the official UPNP libraries were huge, and had large
 * dependencies. Perhaps these days there are smaller solutions available.
 */

void upnp_setcommand(request_t *node, char *cmd)
{

    return;

    if (!cmd || !*cmd) return;

    debugf("[upnp] set command '%s'\n", cmd);

    if (!mystrccmp("llink.xml", cmd)) {
        // Change the request to the download of the skin/upnp/llink.xml file
        // It is loaded as a skin, so that macros will work, and be
        // expanded upon fetch.
        SAFE_FREE(node->path);
        // Simulate directory listing.
        node->path = strdup("/");
        debugf("[upnp] changed to llink.xml get\n");
        // This call is probably not required. We use skin_find_all
        skin_set_skin(node, "UPNP");
        node->upnp_type = UPNP_ROOTXML;
    }

    if (!mystrccmp("scpd.xml", cmd)) {
        // Change the request to the download of the skin/upnp/llink.xml file
        SAFE_FREE(node->path);
        node->path = strdup("/scpd.xml");
        debugf("[upnp] changed to scpd.xml get\n");
        skin_set_skin(node, "UPNP");
    }

#if 0
    if (!mystrccmp("root.xml", cmd)) {
        // Change the request to the download of the skin/upnp/llink.xml file
        SAFE_FREE(node->path);
        node->path = strdup("/root.xml");
        debugf("[upnp] changed to root.xml get\n");
        skin_set_skin(node, "UPNP");
    }
#endif

    if (!mystrccmp("conmgr.xml", cmd)) {
        // Change the request to the download of the skin/upnp/llink.xml file
        SAFE_FREE(node->path);
        node->path = strdup("/conmgr.xml");
        debugf("[upnp] changed to conmgr.xml get\n");
        skin_set_skin(node, "UPNP");
    }

}

void upnp_parseinput(request_t *node, char *line)
{
	char *r, *path;

    return;

    debugf("[upnp] input; \n%s\n", line);

    // FIXME, this needs to do real parsing. For now, we
    // do some ugly things and guess what they want. Yay.

    if (strstr(line, "SortCapabilities")) {
        debugf("[upnp] guessing GetSortCapabilities!\n");
        node->upnp_type = UPNP_GETSORTCAPABILITIES;
    }
    if (strstr(line, "SearchCapabilities")) {
        debugf("[upnp] guessing GetSearchCapabilities!\n");
        node->upnp_type = UPNP_GETSEARCHCAPABILITIES;
    }
    if (strstr(line, "BrowseDirectChildren")) {
        debugf("[upnp] guessing BrowseDirectChildren! upnp_type set to %d\n",
               UPNP_BROWSEDIRECTCHILDREN);
        node->upnp_type = UPNP_BROWSEDIRECTCHILDREN;
    }
    if (strstr(line, "GetSystemUpdateID")) {
        debugf("[upnp] guessing GetSystemUpdateID! upnp_type set to %d\n",
               UPNP_GETSYSTEMUPDATEID);
        node->upnp_type = UPNP_GETSYSTEMUPDATEID;
    }
    if (strstr(line, "ContentDirectory_event")) {
        debugf("[upnp] guessing BrowseDirectChildren!\n");
        node->upnp_type = UPNP_BROWSEDIRECTCHILDREN;
    }
    // BrwoseMetadata works identical to BrowseDirectChildren, except no list data.
    if (strstr(line, "BrowseMetadata")) {
        debugf("[upnp] guessing BrowseMetadata!\n");
        node->upnp_type = UPNP_BROWSEMETADATA;
    }

	// What directory do they want, pray tell?
	//<ObjectID>00Lokien</ObjectID>
    // xbox bug sends "ContainerID" ?
    if ((r = strstr(line, "<ObjectID>")) ||
        (r = strstr(line, "<ContainerID>"))) {

		// Skip past obj-id
        r = strchr(r, '>');
        if (!r) return;
        r++;
		// get the path, ignoring leading whitespace, up to "<" or EOL
		// which ever is first. Spaces in paths will be URL encoded. Yay!
		//path = misc_digtoken(&r, " <\r\n");
		path = misc_digtoken(&r, " <&?\r\n");
		if (path && *path) {
			char *token;

			SAFE_FREE(node->path);

			// UPnPs root is 0.
			if (!mystrccmp("0", path)) {

                debugf("[upnp] Directory wanted 0 ie \"/\"\n");
                node->path = strdup("/");

                // misc_digtoken nul-terminates, patch buffer back
                if ((r > line) && misc_digtoken_optchar)
                    r[-1] = misc_digtoken_optchar;

            } else {

                debugf("[upnp] Directory wanted '%s'\n", path);

                // Skip leading "/"
                //while(*path=='/') path++;
                node->path = request_url_decode(path);

                // We need to check for "CGI" part in the URL here too.

                // misc_digtoken nul-terminates, patch buffer back
                if ((r > line) && misc_digtoken_optchar)
                    r[-1] = misc_digtoken_optchar;

                debugf("upnp: fixup 1\n%s\n", line);


                if ((misc_digtoken_optchar == '&') ||
                    (misc_digtoken_optchar == '?')) {
                    char *cgi;

                    // Copy the string so we can destroy it.
                    cgi = strdup(r);
                    r = cgi;

                    token = misc_digtoken(&r, " <\r\n");
                    // Check if it has a "&". If so, that's cgi stuff.
                    if (token && *token) {

                        SAFE_COPY(node->cgi, token);

                    }

                    SAFE_FREE(cgi);

                } //if optchar

                debugf("upnp: fixup 2\n%s\n", line);

                // We can not call cgi_parse() here, as it would
                // set bytes_size if rar_file, and POST uses bytes_size
                // to count down the request.

                debugf("upnp: finsih 1\n%s\n", line);
            } // if "0"->"/"
		}
	} // ObjectID


	//<StartingIndex>0</StartingIndex>' (remaining 152 - read 32)
    if ((r = strstr(line, "<StartingIndex>"))) {
		node->page_from = atoi(&r[15]);
        debugf("[upnp] guessing StartingIndex! %d\n", node->page_from);
        //sleep(2);
    }


	//<RequestedCount>20</RequestedCount>' (remaining 118 - read
    if ((r = strstr(line, "<RequestedCount>"))) {
        // This needs to be page_from + RequestedCount, but we can't rely on
        // the order in the XML, so we add them up later.

        // However, BrowseMetadata should always return ONE item only.
        // But you request it with Index=0 Count=0.
        if (node->upnp_type == UPNP_BROWSEMETADATA) {
            node->page_to = node->page_from + 1;
        } else {
            node->page_to = atoi(&r[16]);
            if (node->page_to)
                node->page_to += node->page_from;

        }
        // Fix up the page_to
        //        if (node->page_to) {
        //  node->page_to += node->page_from;
        //}

        debugf("[upnp] guessing RequestedCount! %d\n", node->page_to);
    }

}


void upnp_reply(request_t *node)
{
    debugf("[upnp] reply\n");

    return;

    // Sanity, should not happen
    if (!node->tmphandle) return;

    switch(node->upnp_type) {
    case UPNP_GETSORTCAPABILITIES:
		debugf("[upnp] GetSortCapabilities reply\n");

        lion_printf(node->tmphandle,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
                    "<u:GetSortCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\r\n"
                    "<SortCaps>None</SortCaps>\r\n"
                    "</u:GetSortCapabilitiesResponse>\r\n"
                    "</s:Body> </s:Envelope>\r\n\r\n"
                    );

		debugf(
			   "200 OK\r\n"
			   "Content-Length: 374\r\n"
			   "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
			   "Server: llink-%s (I'm just making stuff up)\r\n"
			   "\r\n"
			   "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
			   "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
			   "<u:GetSortCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\r\n"
			   "<SortCaps>None</SortCaps>\r\n"
			   "</u:GetSortCapabilitiesResponse>\r\n"
			   "</s:Body> </s:Envelope>\r\n\r\n",
			   VERSION);

        break;

    case UPNP_GETSEARCHCAPABILITIES:
		debugf("[upnp] GetSearchCapabilities reply\n");

        lion_printf(node->tmphandle,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
                    "<u:GetSearchCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SearchCaps>None</SearchCaps></u:GetSearchCapabilitiesResponse>\r\n"
                    "</s:Body> </s:Envelope>\r\n\r\n"
                    );

		debugf(
			   "200 OK\r\n"
			   "Content-Length: 374\r\n"
			   "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
			   "Server: llink-%s (I'm just making stuff up)\r\n"
			   "\r\n"
			   "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
			   "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
               "<u:GetSearchCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SearchCaps>None</SearchCaps></u:GetSearchCapabilitiesResponse>\r\n"
			   "</s:Body> </s:Envelope>\r\n\r\n",
			   VERSION);

        break;

    case UPNP_GETSYSTEMUPDATEID:
		debugf("[upnp] GETSYSTEMUPDATEID reply\n");

        lion_printf(node->tmphandle,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
                    "<u:GetSystemUpdateIDResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Id>0</Id></u:GetSystemUpdateIDResponse>\r\n"
                    "</s:Body> </s:Envelope>\r\n\r\n"
                    );
        break;

    case UPNP_BROWSEMETADATA:
		debugf("[upnp] BROWSEMETADATA reply\n");

        lion_printf(node->tmphandle,
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
"<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"&gt;&lt;"
"container id=\"0\" parentID=\"\" restricted=\"1\" searchable=\"1\"&gt;&lt;"
"dc:title&gt;Root&lt;/dc:title&gt;&lt;dc:creator&gt;Unknown&lt;/dc:creator&gt;&lt;upnp:genre&gt;Unknown&lt;/upnp:genre&gt;&lt;upnp:class&gt;object.container&lt;/upnp:class&gt;&lt;/container&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>1</NumberReturned><TotalMatches>1</TotalMatches><UpdateID>0</UpdateID></u:BrowseResponse>"
                    "</s:Body> </s:Envelope>\r\n\r\n"
                    );
        break;


	case UPNP_BROWSEDIRECTCHILDREN:
		debugf("[upnp] Browse: \n");



        break;

    default:
        break;

    }

    // Unknown, error, who cares?
#if 0
    lion_printf(node->handle,
                "HTTP/1.1 200 OK\r\n"
                "Server: llink-%s\r\n"
                "Content-Length: 0\r\n"
                "\r\n",
				VERSION);
#endif


    debugf("[upnp] finished. Handing back to request\n");
    request_finishdir(node);

}


