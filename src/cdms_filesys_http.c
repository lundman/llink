/************************************************************
*
*	CyberLink for C
*
*	Copyright (C) Satoshi Konno 2005
*
*	File: cdms_http.c
*
*	Revision:
*       05/11/05
*               - first release.
*
************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_CLINKC

#include <cybergarage/util/clog.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "cdms_filesys.h"

#include "lion.h"
#include "misc.h"

#include "request.h"
#include "cupnp.h"

#define DEBUG_FLAG 4096
#include "debug.h"

#if !defined(WINCE)
#include <sys/stat.h>
#endif

#include <string.h>
#include <ctype.h>


#define CUPNP_CLOSE

/**********************************************************************
* cg_upnp_dms_filesys_http_listener
**********************************************************************/
BOOL cg_upnpav_dms_filesys_http_listener(CgHttpRequest *httpReq)
{
	char *httpURI;
	char *pubDir;
	CgUpnpAvServer *dms;
	CgUpnpDevice *dev;

    // Let's lower it considerably, will not really affect the initial connection
    // but the connection=keep-alive situations.
    cg_socket_settimeout(cg_http_request_getsocket(httpReq), 15);

	dev = (CgUpnpDevice *)cg_http_request_getuserdata(httpReq);

	if (!dev) {
		cg_http_request_postbadrequest(httpReq);
#ifdef CUPNP_CLOSE
        cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
		return TRUE;
	}

	dms = (CgUpnpAvServer *)cg_upnp_device_getuserdata(dev);
	if (!dms) {
		cg_http_request_postbadrequest(httpReq);
#ifdef CUPNP_CLOSE
        cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
		return TRUE;
	}

    httpURI = cg_http_request_geturi(httpReq);
    if (cg_strlen(httpURI) <= 0) {
        cg_http_request_postbadrequest(httpReq);
#ifdef CUPNP_CLOSE
        cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
        return TRUE;
    }

    //debugf("   http_listener called: '%s'\n", httpURI);

    // Skip leading "/" in "/content"
    while (*httpURI == '/') httpURI++;





    // Check it starts with the "icon" part
    if (!strncmp(httpURI, "icon/",
                strlen("icon/"))) {
        BOOL isHeadRequest;
        // Try to get icons.
        char *filename;
        CgSocket *sock;
        struct stat fileStat;
        uint64_t fileSize;
        int red;
        char buffer[1024];
        int fd;
        CgHttpResponse *httpRes;

        filename = hide_path(&httpURI[5]);

        if (!cg_streq(filename, "llink-120x120.jpg") &&
            !cg_streq(filename, "llink-120x120.png") &&
            !cg_streq(filename, "llink-48x48.jpg") &&
            !cg_streq(filename, "llink-48x48.png")) {
            cg_http_request_postbadrequest(httpReq);
#ifdef CUPNP_CLOSE
            cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
            return TRUE;
        }

        //printf("Sending icon '%s'\n", filename);

        fd = open(filename, O_RDONLY
#ifdef O_BINARY
                  |O_BINARY
#endif
                  );
        if ((fd < 0) || (fstat(fd, &fileStat) != 0)) {
            cg_http_request_postbadrequest(httpReq);
#ifdef CUPNP_CLOSE
            cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
            return TRUE;
        }

        fileSize = (uint64_t )fileStat.st_size;
        isHeadRequest = cg_http_request_isheadrequest(httpReq);
        httpRes = cg_http_response_new();
        cg_http_response_setstatuscode(httpRes, CG_HTTP_STATUS_OK);
        cg_http_response_setcontenttype(httpRes,
                                        ((filename[12] == 'j') ||
                                         (filename[14] == 'j')) ?
                                        "image/jpeg" : "image/png");
        sock = cg_http_request_getsocket(httpReq);
        cg_http_response_setcontentlength(httpRes, fileSize);
        cg_http_request_postresponse(httpReq, httpRes);

        while (!isHeadRequest && (fileSize > 0)) {
            red = read(fd, buffer, sizeof(buffer));
            if (red > 0) {
                cg_socket_write(sock, buffer, red);
            } else {
                break;
            }
        }
        close(fd);
        cg_http_response_delete(httpRes);
#ifdef CUPNP_CLOSE
        cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
        return TRUE;
    }





    // Check it starts with the "contents" part
    if (strncmp(httpURI, CG_UPNP_MEDIA_FILESYS_RESURL_PATH,
                strlen(CG_UPNP_MEDIA_FILESYS_RESURL_PATH))) {

        // If the mediaDevice is an xbox, we MUST send certain ModelName.
        // If the mediaDevice is Syabas, we MUST NOT send certain ModelName.
        // Joy.
        // SERVER: Xbox/2.0.20158.0 UPnP/1.0 Xbox/2.0.20158.0
        // SERVER:dashboard/1.0 UpnP/1.0 xbox/2.0
        // Xbox/2.0.6683.0 UPnP/1.0 Xbox
        pubDir = cg_http_request_getheadervalue(httpReq, "SERVER");

        cg_upnpav_dms_lock(dms);
        if (pubDir && strcasestr(pubDir,
                       "xbox")) {
            debugf("[server] XBox client detected.\n");
            cg_upnp_device_setmodelname(dev, "Windows Media Connect");
        } else {
            //cg_upnp_device_setmodelname(dev, "Windows Media Connect");
            cg_upnp_device_setmodelname(dev, "CyberGarage Media Server");
        }
        cg_upnpav_dms_unlock(dms);

        // The cupnp.c to populate the content of a Browse request need to have
        // access to User-Agent, but is passed nothing put "CgAction *" and
        // the ContentID. This poses a problem, how do we save the correct
        // User-Agent.
        // Attempt to guess if it is a Browse request, and if so, find the
        // ContentID and attach the User-Agent.
        pubDir = cg_http_request_getheadervalue(httpReq, "USER-AGENT");
        // Does it have X-AV-Client-Info ?
        if (cg_http_request_getheadervalue(httpReq, "X-AV-Client-Info"))
            pubDir = cg_http_request_getheadervalue(httpReq, "X-AV-Client-Info");
        {
            char *str = NULL;
            //str = cg_socket_getaddress(cg_http_request_getsocket(httpReq));
            CgNetworkInterfaceList *netIfList;
            CgNetworkInterfaceList *netIf;

            netIfList = cg_net_interfacelist_new();
            cg_net_gethostinterfaces(netIfList);
            netIf = cg_net_interfacelist_gets(netIfList);
            if (netIf) {
                str = strdup(cg_net_interface_getaddress(netIf));
            }
            cg_net_interfacelist_delete(netIfList);

            cupnp_add_useragent((void *)cg_thread_self(), pubDir, str);
            SAFE_FREE(str);
        }

        cg_upnp_device_httprequestrecieved(httpReq);

#ifdef XCUPNP_CLOSE // Lets keep-alive going on the dirlisting channels
        cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
        return TRUE;  // We have handled it, do nothing.
    } // is not content URL



    debugf("[cdms_filesys_http] Odd, /content queried on wrong HTTP port: '%s'\n",
           httpURI);

    // This part is not used, since all content URLs go to llink HTTP port.
    cg_http_request_postbadrequest(httpReq);
#ifdef CUPNP_CLOSE
    cg_http_request_setconnection(httpReq, CG_HTTP_CLOSE);
#endif
    return TRUE;
}




#endif
