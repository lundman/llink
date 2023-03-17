/************************************************************
*
*	CyberLink for C
*
*	Copyright (C) Satoshi Konno 2005
*
*	File: cdms_filesys.c
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

#include "cdms_filesys.h"
#include <cybergarage/upnp/std/av/cmediaserver.h>
#include <cybergarage/net/cinterface.h>

#include <stdio.h>
#include <ctype.h>

#if defined(WIN32)
#include <windows.h>
#include <tchar.h>
#if !defined(WINCE)
#include <sys/stat.h>
#include <conio.h>
#endif
#else
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif
#include <string.h>

#include "lion.h"
#include "misc.h"
#include "request.h"
#include "cupnp.h"

#define DEBUG_FLAG 4096
#include "debug.h"

// In cupnp.c
void cupnp_get_contents(CgUpnpAvServer *dms,
                        CgUpnpAvContentList *parentCon,
                        char *contentID);

/****************************************
* cg_upnp_dms_filesys_actionlistner
****************************************/

BOOL cg_upnpav_dms_filesys_actionlistner(CgUpnpAction *action)
{
	CgUpnpAvServer *dms;
	CgUpnpDevice *dev;
	char *contentID;
	CgUpnpAvContent *content;
	//char *contentDir;
	char *actionName;
    char *syabas = NULL;

	actionName = cg_upnp_action_getname(action);
	if (cg_strlen(actionName) <= 0)
		return FALSE;


	/* Browse */
	if (cg_streq(actionName, CG_UPNPAV_DMS_CONTENTDIRECTORY_BROWSE)) {
		contentID = cg_upnp_action_getargumentvaluebyname(action, CG_UPNPAV_DMS_CONTENTDIRECTORY_BROWSE_OBJECT_ID);
		if (cg_strlen(contentID) <=0)
			return FALSE;

		dev = cg_upnp_service_getdevice(cg_upnp_action_getservice(action));
		if (!dev)
			return FALSE;

		dms = (CgUpnpAvServer *)cg_upnp_device_getuserdata(dev);
		if (!dms)
			return FALSE;

		cg_upnpav_dms_lock(dms);

        debugf("[cupnp] Looking for content '%s'\n", contentID);

#if 1 // SYABAS bug, will fixed in next release 201110+
		content = cg_upnpav_dms_findcontentbyid(dms, contentID);
		if (!content) {
            // Syabas has a bug where it actually URL-decodes the contentID,
            // see Mantis Bug#550. So, as a last ditch effort, we URL encode the
            // contentID here, and see if it works.
            syabas = umisc_url_encode(contentID);
			debugf("[cupnp] Syabas, trying to find '%s'\n", syabas);
            content = cg_upnpav_dms_findcontentbyid(dms, syabas);

            if (content) {
                contentID = syabas;
                cg_upnp_action_setargumentvaluebyname(action, CG_UPNPAV_DMS_CONTENTDIRECTORY_BROWSE_OBJECT_ID, syabas);
            }
		}
#endif

#if 1   // Microsoft soft roots.
        // When MS talks to a MS compliant server (fex from xbox360) MS will send
        // requests to "7" to mean "albums". We will simply short-cut them all
        // to "0", ie, root.
        {
            int id;
            id = atoi(contentID);
            if ((id >= 1) &&
                (id < 20)) {
                debugf("[cupnp] Redirecting Microsoft root '%s' to 0.\n", contentID);

                content = cg_upnpav_dms_findcontentbyid(dms, "0");
                syabas = strdup("0");
                contentID = syabas;
                cg_upnp_action_setargumentvaluebyname(action, CG_UPNPAV_DMS_CONTENTDIRECTORY_BROWSE_OBJECT_ID, syabas);

            }
        }
#endif

        if (!content) {
            debugf("[cupnp] could not find content '%s' -- giving up.\n", contentID);
            SAFE_FREE(syabas);
            cg_upnpav_dms_unlock(dms);
            return FALSE;
        }


        cupnp_get_contents(dms, content, contentID);


        cg_upnpav_dms_unlock(dms);
        SAFE_FREE(syabas);

        request_update_idle();

    }

    return FALSE;
}


#endif
