/************************************************************
*
*	CyberLink for C
*
*	Copyright (C) Satoshi Konno 2005
*
*	File: cdms_filesys.h
*
*	Revision:
*       05/11/05
*               - first release.
*
************************************************************/

#ifndef _CG_CLINKC_MEDIASERVER_FILESYS_H_
#define _CG_CLINKC_MEDIASERVER_FILESYS_H_

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_CLINKC

#include <cybergarage/upnp/std/av/cmediaserver.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct _CgUpnpMediaFileSystemContentData {
	char *pubdir;
} CgUpnpMediaFileSystemContentData;

#define  CG_UPNP_MEDIA_FILESYS_RESURL_PATH "content"
#define  CG_UPNP_MEDIA_FILESYS_RESURL_MAXLEN (CG_NET_IPV6_ADDRSTRING_MAXSIZE + CG_MD5_STRING_BUF_SIZE + 64)

CgUpnpAvServer *cg_upnp_dms_filesys_new();
void cg_upnpav_dms_filesys_delete(CgUpnpAvServer *dms);

#define cg_upnpav_dms_filesys_start(dms) cg_upnpav_dms_start(dms)
#define cg_upnpav_dms_filesys_stop(dms) cg_upnpav_dms_stop(dms)

BOOL cg_upnpav_dms_filesys_http_listener(CgHttpRequest *httpReq);
BOOL cg_upnpav_dms_filesys_actionlistner(CgUpnpAction *action);

#ifdef  __cplusplus
}
#endif

#endif

#endif
