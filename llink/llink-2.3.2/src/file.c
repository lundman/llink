// $Id: file.c,v 1.8 2009/09/08 13:41:52 lundman Exp $
//
// Jorgen Lundman 18th March 2004.
//
//
//
#if HAVE_CONFIG_H
#include <config.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "misc.h"

#include "file.h"
#include "debug.h"


char *hide_path(char *s)
{
	char *r;

	r = strrchr(s, '/');

	return ((r && r[1]) ? &r[1] : s);

}



int file_parse(lfile_t *result, char *line)
{
	char *ar, *perm, *thingy, *user, *group, *size, *name;
	char date[14];
	yesnoauto_t directory;
	yesnoauto_t soft_link = YNA_NO;
	int len;

	//debugf("  [file] '%s'\n", line);

	if (!line) return -1;

	memset(result, 0, sizeof(*result));


	// drwxr-xr-x   7  root     root         1024 Mar  3 18:36 ..
	// -rw-r--r--   1  ftp      ftp             5 Nov 10 11:42 Return
	// drwxr-xr-x F0   lftpd    lftpd           0 Mar 27  1999 Cd2
	// -rw-r--r-- D0   lftpd    lftpd           0 Mar 27  1999 sample.j
	// drwxrwxrwx   1  user     group           0 Jun 22  1999 old
	// -rwxrwxrwx   1  user     group     5327872 Nov 30  1998 q2-3.20-x86.exe
	// dr-xr-xr-x   1  owner    group           0 Feb 25  0:31 developr
	// -r-xr-xr-x   1  owner    group        7983 Jan 28  1999 dirmap.htm
	//
	// Bollox, some FTP sites don't have that initial argument.
	//

	ar = (char *)line;

	if (!(perm = misc_digtoken(&ar, " \t\r\n"))) goto parse_error;

	if (!mystrccmp("total", perm)) {  // it's the "total XX" line, ignore it.
		file_free(result);
		return -1;
	}

	if (strlen(perm) != 10) { // We assume it's always this length
		debugf("[%s] Unknown data, permission section isn't strlen 10 :'%s' - '%s'\n",
			   "file", perm, ar);
		goto parse_error;
	}



	// Parse out the rest

	if (!(thingy = misc_digtoken(&ar, " \t\r\n"))) goto parse_error;
	if (!(user   = misc_digtoken(&ar, " \t\r\n"))) goto parse_error;
	if (!(group  = misc_digtoken(&ar, " \t\r\n"))) goto parse_error;
	if (!(size   = misc_digtoken(&ar, " \t\r\n"))) goto parse_error;


	// Bollox!
	// Here, if size is actually a month "Feb" etc, we have
	// one less argument than expected!!
	// So we try to back track somewhat...
	//
	if ((strlen(size) == 3) &&
		isalpha(size[0]) &&
		isalpha(size[1]) &&
		isalpha(size[2])) {

		// Back the date into area, decrementing a char * hmmm :)
		*(--ar) = ' ';
		*(--ar) = size[2];
		*(--ar) = size[1];
		*(--ar) = size[0];

    size  = group;
    group = user;
    user  = thingy;

	}



	while (*ar == ' ') ar++;  // skip any whitespace to the date section.

	strncpy(date, ar, 12);
	date[12] = 0;


	if (strlen(date) != 12)
		goto parse_error;


	// The rest is file/dir name.
	// This is faulty. Technically would parse filenames starting with
	// a space incorrectly. " hello" would be "hello".

	name = &ar[12];

	while (*name == ' ') name++;


#if 0
	printf("Parsed line: \n");
	printf("\tperm:    '%s'\n", perm);
	printf("\tthingy:  '%s'\n", thingy);
	printf("\tuser:    '%s'\n", user);
	printf("\tgroup:   '%s'\n", group);
	printf("\tsize:    '%s'\n", size);
	printf("\tdate:    '%s'\n", date);
	printf("\tname:    '%s'\n", name);
#endif


	// Check it is either a file or a dir. Ignore all other

	switch (tolower(perm[0])) {

	case 'd':
		directory = YNA_YES;
		break;

	case '-':
	case 'b': // block device.       For device nodes etc.
	case 'c': // character device.
		directory = YNA_NO;
		break;


	case 'l':
        // Ok we need to handle links as well
        directory = YNA_AUTO;
        soft_link = YNA_YES;
		break;

	default:
		// Skip all others
		goto parse_error;
	}



	// Always ignore . and ..
	// really? Also, return failure, not success
	len = strlen(name);
	if (!strcmp(name, ".") || !strcmp(name, "..")) return 1;
	if ((len >= 2) && !strcmp(&name[len-2], "/.")) return 1;
	if ((len >= 3) && !strcmp(&name[len-3], "/..")) return 1;

	result->size = (lion64u_t) strtoull(size, NULL, 10);

	result->directory = directory;
	result->soft_link = soft_link;

	result->name = strdup(name);
	result->user = strdup(user);
	result->group = strdup(group);
	result->date = misc_getdate(date);
	strcpy(result->perm, perm);


	//debugf("[file] parsed '%s' ok\n", name);

	// Return node.
	return 0;

 parse_error:
	debugf("[file] unable to parse input: '%s'\n", ar ? ar : "(null)");
	file_free(result);
	return -1;

}


void file_free(lfile_t *file)
{

	if (!file) return;

	SAFE_FREE(file->name);
	SAFE_FREE(file->user);
	SAFE_FREE(file->group);

}


void file_dupe(lfile_t *new, lfile_t *src)
{

	// Copy the whole thing
	memcpy(new, src, sizeof(*new));

	// But re-allocate all strings.
    // Fix me, cores if NULL
	new->name  = strdup(src->name);
	new->user  = strdup(src->user);
	new->group = strdup(src->group);

}
