//
// Scan the ROOTs we have for Media. Discard any Media with already existing
// .XML files. Scrape IMDB for information of each remaining Media.
// If information is acceptable, save in Media .XML file.
//
// Thank you IMDB for not having any useful API.
//

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"

#define DEBUG_FLAG 128
#include "debug.h"
#include "conf.h"
#include "ssdp.h"
#include "query.h"
#include "httpd.h"
#include "request.h"
#include "skin.h"
#include "root.h"
#include "mime.h"
#include "external.h"
#include "llrar.h"
#include "extra.h"
#include "xmlscan.h"
#include "parser.h"


#define XMLSCAN_INCREASE 100

static int      scanning          = 1;
static int      xmlscan_dirs      = 0;
static int      xmlscan_files     = 0;
static lfile_t **xmlscan_head      = NULL;  // array of lfile_t *
static int      xmlscan_allocated = 0; // memory allocation size
static int      xmlscan_entries   = 0;  // number of entries

static int  xmlscan_handler  (lion_t *, void *, int, int, char *);



// Called from request.c when a directory listing has finished.
void xmlscan_finishdir(request_t *node)
{
	// Dirlist complete
	scanning = 0;
}

// called from skin.c for each media we find.
void xmscan_addfile(request_t *node, skin_t *skin, lfile_t *file)
{
	lfile_t **tmp, *newf;

	if (!file) return;

	if (skin && (skin->process_cmd != REQUEST_PROCESS_NONE)) {
		debugf("  %s is of type with cmd= set - skipping\n", file->name);
		file_free(file);
		return;
	}


	// We have a new entry, check if it already has a XML file?
	if (extra_lookup(&node->extnfo, root_getroot(node->root_index),
					 "/", file, skin) > 0) {
		// has XML already
		debugf("  %s has an XML file - skipping\n", file->name);
		extra_release(&node->extnfo);
		file_free(file);
		return;
	}
	extra_release(&node->extnfo);

    if (skin) {
        if (mystrccmp(skin->name, "Movies") &&
            mystrccmp(skin->name, "Directory")) {
            debugf(" %s (%s) is not of TYPE 'Movies' nor 'Directory' - skipping\n",
                   file->name, skin->name);
            file_free(file);
            return;
        }
    }



	debugf("  %s has no XML file - processing\n", file->name);

	if (file->directory == YNA_YES)
		xmlscan_dirs++;
	else
		xmlscan_files++;

	// Allocate memory, when needed
	if (xmlscan_entries >= xmlscan_allocated) {
		tmp = (lfile_t **)realloc((void *)xmlscan_head,
								 sizeof(lfile_t *) *
								 (xmlscan_allocated + XMLSCAN_INCREASE));
		if (!tmp) {
			debugf("Oops,memory trouble\n");
			file_free(file);
			return;
		}

		xmlscan_head = tmp;
		xmlscan_allocated += XMLSCAN_INCREASE;
	}

	// Add to list
	newf = malloc(sizeof(*newf));
	if (!newf) return;

	file_dupe(newf, file);
	file_free(file);

	xmlscan_head[ xmlscan_entries ] = newf;
	xmlscan_entries++;

}





char *get_url(request_t *node, int search, char *title, char *sub, char *hostname)
{

	SAFE_FREE(node->tmpname);
	SAFE_FREE(node->cgi_file);
	SAFE_FREE(node->cgi_host);

	node->head_method = search;

	if (sub) {
		char *joined;
		joined = misc_strjoin(title, sub);
		node->cgi_file = request_url_encode(joined);
		SAFE_FREE(joined);
	} else {
		node->cgi_file = request_url_encode(title);
	}

	node->cgi_host = strdup(hostname);

	printf("Connecting to %s ... \n", hostname);

	node->handle = lion_connect(hostname, 80, 0, 0,
							   LION_FLAG_FULFILL, (void *)node);
	lion_set_handler(node->handle, xmlscan_handler);


	scanning = 1;
	while(scanning) {
		lion_poll(0, 1);
	} // poll

	printf("[imdb] gave us %"PRIu64" bytes, we saved %"PRIu64" bytes, for %s\n",
		   node->bytes_sent, node->bytes_size,
		   node->cgi_file);

	SAFE_FREE(node->cgi_file);
	SAFE_FREE(node->cgi_host);
	return node->tmpname;
}




//
// This function will search through "buffer" for first instance of "tag",
// (ick, strstr!) and then attempt to parse out data.
// If "tag" ends with character '>' it will take all information until
// first '<'.
// If "tag" does not end with '>' we initially ignore all characters, until
// first '>', and take anything until "<". But, should that be only whitespace
// we will keep going.
// All that repeated up-to "num" times.
//
// We can also use "minitag", which is searched for after "tag".
// and finally, if doing more than "1", we can use "endtag" to indicate the
// early end of a list.
//
int find_str(char *buffer, char **result, int num, char *tag, char *minitag, char *endtag)
{
	char *locator, *r, *end, *crnl, *endptr = NULL;
	int hits = 0;
	int look4gt;
	int number;

	debugf("[locator] looking for '%s'\n", tag);

	if (!(locator = strcasestr(buffer, tag))) return hits;

	// Go to the end of the tag
	locator = &locator[ strlen(tag) ];

	// minitag? if so, make tag be minitag
	if (minitag) tag = minitag;


	for (number = 0; number < num; number++ ) {

		if (minitag) {
			// tag and minitag is the same here, but minitag is only set
			// if we want minitags. tag is always set.
			if (!(locator = strcasestr(locator, minitag))) return hits;
			// Go to the end of the tag
			locator = &locator[ strlen(minitag) ];
		}

		// If we have an endtag, look it up.
		if (endtag && !endptr)
			endptr = strcasestr(locator, endtag); // NULL if none, = EOF

		// We found it, which style? (tag or minitag)
		if (tag[ strlen(tag) -1 ] == '>')
			look4gt = 0;
		else
			look4gt = 1;

		//debugf("  tag found\n");


		while(1) {

			// If end ptr is on a '"' character, we take the string as is.
			if (tag[ strlen(tag) -1 ] == '"') {

				r = locator; // Start of string

				// Find end of string
				if (!(end = strchr(r, '"'))) return hits;
				*end = 0;  // WARNING, this has to be restored!

				break;
			}

			// gone too far?
			if (endptr && (locator >= endptr)) return hits;


			// If we are to look for '>' do so first.
			if (look4gt) {

				if (!(r = strchr(locator, '>'))) return hits;
				r++;

			} else {

				r = locator;

			}

			// "r" is now looking at the start. Take everything up until the
			// first "<". But if this is only whitepace, go back up.
			if (!(end = strchr(r, '<'))) return hits;
			*end = 0;  // WARNING, this has to be restored!

			//debugf("  checking whitespace '%s' \n", r);

			// Check the string for non-white space.
			if ((strpbrk(r, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) &&
				strcmp(r, "&nbsp;")) {
				// We have a hit!
				break;
			}

			//debugf("  trying again\n");

			*end = '<';
			end++;
			locator = end;
			look4gt = 1;
		} // while 1, keep looking for valid strings.

		// Eat any newlines. Fix me to actually remove them.
		for (crnl = r; (crnl = strpbrk(crnl, "\r\n")); ) *crnl = ' ';
		while(*r == ' ') r++;

	// We have 1 valid string.
		result[hits] = strdup(r);
		debugf("[imdb]: '%s' returning %d:'%s'\n", tag, number, r);

		*end = '<';
		end++;
		hits++;

		locator = end;
		look4gt = 1;

	} // for number < num

	return hits;

}


void save_icon(request_t *node, char *root)
{
	// node.extnfo.icon_path =
	// http://ia.media-imdb.com/images/M/MV5BMjAzNDkzOTQ5MV5BMl5BanBnXkFtZTcwODA1MTU1MQ@@._V1._SY400_SX600_.jpg
	char *url, *ar, *host, *port = NULL, *r, *fullpath;
	char buffer[1024];
	int fd, wrote;

	url = node->extnfo.icon_path;

	if (!url) return;

	if (!strncasecmp("http://", url, 7)) {

		ar = &url[7];
		host = misc_digtoken(&ar, "/:\r\n");
		if (host) {
			// Optional port?
			if (misc_digtoken_optchar == ':')
				port = misc_digtoken(&ar, "/\r\n");
			if (!port || !*port)
				port = "80";

			// Copy out the host name
			host = strdup(host);

			// We want that slash back
			ar--;
			*ar = '/';

			// Get icon into tmpname
			get_url(node, 0, ar, NULL, host);

			if (node->bytes_size > 1024) {

				// Create filename, get original extension.
				if ((r = strrchr(ar, '.')))
					snprintf(buffer, sizeof(buffer), "%s%s",
							 node->scanfile->name,
							 r);
				else
					snprintf(buffer, sizeof(buffer), "%s.jpg",
							 node->scanfile->name);
				SAFE_FREE(node->extnfo.icon_path);

				debugf("[xmlscan] Assuming icon filename to be '%s'\n", buffer);

				// Set the filename to use.
				node->extnfo.icon_path = strdup(buffer);

				// build the real path
				fullpath = misc_strjoin(root, buffer);

				debugf("[xmlscan] opening icon file '%s'\n", fullpath);

				fd = open(fullpath, O_CREAT|O_TRUNC|O_WRONLY
#ifdef WIN32
						  |O_BINARY
#endif
						  , 0644);
				if (fd>0) {
					wrote = write(fd, node->tmpname, (int)node->bytes_size);
					close(fd);
				} else {
					printf("[xmscan] opening icon: %s\n", strerror(errno));
				}

				printf("Saved icon: %s\n", fullpath);
				SAFE_FREE(fullpath);
				return;

			}

		} // if host
	} // starts with http://

	SAFE_FREE(node->extnfo.icon_path);

}






void save_xml(request_t *node, char *url)
{
	FILE *fd;
	char buffer[1024], *extra_path = NULL;
	int i;

	snprintf(buffer, sizeof(buffer), "%s.xml", node->scanfile->name);

	if (conf_xmlscan == 2) {
		extra_path = skin_redirect(node->skin_type, buffer);
		if (!extra_path) {
			printf("It appears you asked to use redirect, but I found no redirect for *.xml\n");
			printf("Perhaps you should add a redirect TYPE to the conf file\n");
			return;
		}

		snprintf(buffer, sizeof(buffer), "%s/%s.xml", extra_path,
				 node->scanfile->name);

	}

	// get the icon
	save_icon(node, extra_path);



	printf("Writing '%s' ...\n", buffer);
	fd = fopen(buffer, "w");
	if (!fd) {
		printf("Sorry, I was unable to open file '%s': %s\n", buffer,
			   strerror(errno));
		return;
	}

#define S(X) ((X) ? (X) : "")



	fprintf(fd,
			"<title>\r\n"
			"  <ID>%s</ID>\r\n"
			"  <Country>%s</Country>\r\n"
			"  <LocalTitle>%s</LocalTitle>\r\n"
			"  <OriginalTitle>%s</OriginalTitle>\r\n"
			"  <ReleaseDate>%s</ReleaseDate>\r\n"
			"  <RunningTime>%s</RunningTime>\r\n",
			url,
			S(node->extnfo.country),
			S(node->extnfo.title_local),
			S(node->extnfo.title),
			S(node->extnfo.date),
			S(node->extnfo.length));

	fprintf(fd,
			"  <TagLine>%s</TagLine>\r\n"
			"  <Description><![CDATA[%s]]></Description>\r\n"
			"  <Rating>%s</Rating>\r\n"
			"  <IMDbUrl>http://www.imdb.com/title/%s/</IMDbUrl>\r\n",
			S(node->extnfo.tagline),
			S(node->extnfo.description),
			S(node->extnfo.rating),
			url);

	if (node->extnfo.icon_path)
		fprintf(fd,
				"  <Covers>\r\n"
				"    <Front>%s</Front>\r\n"
				"  </Covers>\r\n",
				hide_path(S(node->extnfo.icon_path)));


	fprintf(fd,
			"  <Genres>");
	for (i = 0; (i < EXTRA_NUM_GENRE) && node->extnfo.genre[i]; i++)
		fprintf(fd,
				"%s, ", node->extnfo.genre[i]);
	fprintf(fd,
			"</Genres>\r\n");

	fprintf(fd,
			"  <Persons>\r\n");
	for (i = 0; (i < EXTRA_NUM_DIRECTOR) && node->extnfo.director[i]; i++)
		fprintf(fd,
				"    <Person>\r\n"
				"      <Name>%s</Name>\r\n"
				"      <Type>Director</Type>\r\n"
				"    </Person>\r\n",
				node->extnfo.director[i]);
	for (i = 0; (i < EXTRA_NUM_CAST) && node->extnfo.cast[i]; i++)
		fprintf(fd,
				"    <Person>\r\n"
				"      <Name>%s</Name>\r\n"
				"      <Type>Actor</Type>\r\n"
				"    </Person>\r\n",
				node->extnfo.cast[i]);
	fprintf(fd,
			"  </Persons>\r\n"
			"</Title>\r\n");

	fclose(fd);

}



static char *xmlscan_host   = NULL;
static char *xmlscan_strip  = NULL;



void xmlscan_imdb(char **keys, char **values,
				  int items,void *optarg)
{
	char *host, *strip;

	// Required
	host      = parser_findkey(keys, values, items, "HOST");
	strip     = parser_findkey(keys, values, items, "STRIP");

	if (!host || !strip) {
		printf("      : [imdb] missing required field: HOST, STRIP\n");
		return;
	}


	xmlscan_host   = strdup(host);
	xmlscan_strip  = strdup(strip);


}




char *xmlscan_filtername(char *source)
{
	char *build, *token, *ar, last_char_match = 0, *work, *ar2, *ext ;
	int max, nomatch;

	// The new strings will never be longer than the original.
	build = strdup(source); // Is this bad coding habits?
	work = strdup(source); // Is this bad coding habits?
	max = strlen(build);
	*build = 0;

	ar = work;

	while((token = misc_digtoken(&ar, " ._-"))) {

		if (misc_digtoken_optchar)
			last_char_match = misc_digtoken_optchar;

		// Check if it matches crap we want to filter.
		ar2 = xmlscan_strip;

		while((ext = misc_digtoken(&ar2, "/"))) {

			nomatch = lfnmatch(ext,
							   token,
							   LFNM_CASEFOLD);

			// If we matched something, put the "/" so we can
			// keep matching.
			if ((ar2 > xmlscan_strip) && misc_digtoken_optchar)
				ar2[-1] = misc_digtoken_optchar;

			if (!nomatch) {
				// Keyword is known, skip and look for another.
				token = NULL;
				break;
			}

		} // while ext

		if (!token) continue;

		// We are to keep this.
		// Add a space if this isn't the first one.
		if (*build != 0)
			strncat(build, " ", max);
		strncat(build, token, max);

	} // while tokens

	SAFE_FREE(work);

	// If the very last word was -GROUP we will eat one.
	debugf("Checking is last token was -token '%c'\n", last_char_match);

	if (last_char_match == '-')
		if ((ar=strrchr(build, ' ')))
			*ar = 0;


	printf("Using filtered name '%s'\n", build);

	return build;
}






void xmlscan_run(void)
{
	static request_t node; // Fake node
	int entry, i;
	char *next, *r, *p, *url, buffer[1024],*tt_str;

	if (!xmlscan_host)
		xmlscan_host = strdup("www.imdb.com");

	memset(&node, 0, sizeof(node));

	// Get listing of all entries (jukebox does not do subdirectories, so
	// we need only deal with ROOT).
	// Then check for existing .XML files.
	// for all entries without .XML file, attempt to query imdb.
	// then create XML file.

	printf("Welcome to llink XML scan mode, listing directories ... \n");

	scanning = 1;

	// root list needs a handle
	node.tmpname = strdup(TMP_TEMPLATE);
	if (!mktemp(node.tmpname)) {
		printf("Memory issues\n");
		return;
	}

	node.tmphandle = lion_open(node.tmpname,
								O_WRONLY | O_CREAT /*| O_EXCL*/,
								0600,
								LION_FLAG_NONE,
								(void *) &node);
	if (!node.tmphandle) {
		printf("Failed to open a temporary file\n");
		return;
	}

	lion_disable_read(node.tmphandle);
	//lion_enable_trace(node.tmphandle);

	// List everything in root
	// turn of pagination
	node.playall = 1; // make it do a playlist to stop pagination
	node.pass = 0;

    // Set the default skin type
    skin_set_skin(&node, "");

	root_list(&node, "/");

	while(scanning) {
		lion_poll(0, 1);
	}

    //    node.pass = 1;
    //skin_write

	SAFE_FREE(node.tmpname);

	printf("Done. I found %d director%s and %d file%s.\n",
		   xmlscan_dirs,  xmlscan_dirs == 1 ? "y" : "ies",
		   xmlscan_files, xmlscan_files == 1 ? "" : "s");

	// Right, loop through all entries
	for (entry = 0; entry < xmlscan_entries; entry++) {
		char *filtered;

		node.scanfile = xmlscan_head[ entry ];

		printf("\n%s\n", node.scanfile->name);

		filtered = xmlscan_filtername(node.scanfile->name);

		// main_get_url(node, 1=search, "search / tt123456", "fullcredits");
		get_url(&node, 1, filtered, NULL, xmlscan_host);

		SAFE_FREE(filtered);

		// <a href="/title/tt0477852/">The Savages</a> (1967)</td></tr><tr>
		// <td valign="top"><img src="/images/b.gif" alt="" height="1" width="23">
		// </td><td align="right" valign="top">3.</td><td valign="top">
		// <a href="/title/tt0821807/">Velurebi</a> (1979)<br>&nbsp;aka <em>"The Savages"</em> - <em>(English title)</em>


		// imdb can give us two types of replies here. One is a page listing
		// possible hits, we find that by looking for <title>IMDb  Search
		// the other is a direct link, which will have <title> The movie name

		//if ((strcasestr(node.tmpname, "<title>IMDB "))) {
		printf("\n");

		// Did we get anything?
		if (!node.tmpname) {
			printf("[imdb] gave us nothing -- skipping\n");
			continue;
		}

		next = node.tmpname;
		while((r=strcasestr(next, "<a href=\"/title/tt"))) {

			*r = ' '; // corrupt it, no need really.. but...

			// Look for the end.
			r += strlen("<a href=\"/title/");
			// Move along until we hit a non digit
			for (p = &r[2]; isdigit(*p); p++) /* nothing */ ;
			// null terminate it
			*p = 0;
			p++;
			// Print it:
			url = r;
			//printf("==> %-10s: ", r);

			// Find the name. Search for first ">" after "p".
			if ((r = strchr(p, '>'))) {
				// Start of name, find end of name.
				r++;
				if ((p = strchr(r, '<'))) {
					// terminate:
					*p = 0;
					p++;
					// print it.
					// if it is too short, we skip this one.
					if (strlen(r) > 2) {
						printf("==> %-10s: %-30s :", url, r);
					} else {
						next = p;
						continue;
					}

				} // name end

			} // name start

			// Next part too, usually the year
			// Find the name. Search for first ">" after "p".
			if ((r = strchr(p, '>'))) {
				// Start of name, find end of name.
				r++;
				if ((p = strchr(r, '<'))) {
					// terminate:
					*p = 0;
					p++;
					// print it.
					printf("%-30s \n", r);

				} // name end

			} // name start


			if (r > p)
				next = r;
			else
				next = p;

		} // while strstr


		printf("\nEnter movie to use (tt0123456): \n");
		fgets(buffer, 1024, stdin);
		if (buffer[0] != 't' && buffer[1] != 't') {
			printf("Your input did not start with 'tt'\n");
			continue;
		}

		for (p = &buffer[2]; isdigit(*p); p++) /* empty */ ;
		*p = 0;

		tt_str = strdup(buffer);
		printf("Using '%s' for '%s'\n", tt_str, node.scanfile->name);
		snprintf(buffer, sizeof(buffer), "/title/%s", tt_str);

		// Go get everything we want now, and save it to disk.
		// main_get_url(node, 1=search, "search / tt123456", "fullcredits");
		get_url(&node, 0, buffer, "combined", xmlscan_host);

		// main_find_str will look for str, then skin everything until first
		// ">" [1] then pull out words until "<". If it finds only white space
		// it will keep going until it has something.
		// [1] Unless search ends on ">".
		if (!find_str(node.tmpname, (char **)&node.extnfo.director,
						   EXTRA_NUM_DIRECTOR,
						   ">Director:<",
						   "<a href=\"/name/nm",
						   "</div>"))
			find_str(node.tmpname, (char **)&node.extnfo.director,
						  EXTRA_NUM_DIRECTOR,
						  ">Directors:<",
						  "<a href=\"/name/nm",
						  "</div>");

		find_str(node.tmpname, (char **)&node.extnfo.cast, EXTRA_NUM_CAST,
					  ">Cast<",
					  "<a href=\"/name/nm",
					  "</div>");

		find_str(node.tmpname, &node.extnfo.country, 1, ">Country:<",
					  NULL,NULL);
		node.extnfo.title_local = strdup(node.scanfile->name); // really?
		find_str(node.tmpname, &node.extnfo.title, 1, "<title>",
					  NULL, NULL);
		find_str(node.tmpname, &node.extnfo.length, 1, ">Runtime:<",
					  NULL, NULL);
		find_str(node.tmpname, &node.extnfo.tagline, 1, ">Tagline:<",
					  NULL, NULL);
		if (!find_str(node.tmpname, &node.extnfo.description, 1,
						   ">Plot Outline:<", NULL, NULL))
			find_str(node.tmpname, &node.extnfo.description, 1,
						  ">Plot Summary:<", NULL, NULL);
		find_str(node.tmpname, (char **)&node.extnfo.genre, EXTRA_NUM_GENRE,
					  ">Genre:<",
					  "<a href=\"/Sections/Genres/",
					  "</div>");

		find_str(node.tmpname, &node.extnfo.date, 1,
					  ">Release Date:<", NULL, NULL);

		find_str(node.tmpname, &node.extnfo.rating, 1,
					  ">User Rating:<", NULL, NULL);

		//
		// Fetch the icon, if we can.
		// <a name="poster" href="/media/rm50040832/tt0442933"
		//  src="http://ia.media-imdb.com/images/M/MV5BMjAzNDkzOTQ5MV5BMl5BanBnXkFtZTcwODA1MTU1MQ@@._V1._SY140_SX100_.jpg"
		// /media/rm50040832/tt0442933 ->
		//       http://ia.media-imdb.com/images/M/MV5BMjAzNDkzOTQ5MV5BMl5BanBnXkFtZTcwODA1MTU1MQ@@._V1._SY400_SX600_.jpg
		//
		// Attempt to find small icon
		find_str(node.tmpname, &node.extnfo.icon_path, 1,
					  "<a name=\"poster\"", "src=\"", NULL);

		// Attempt to fetch large icon, this will lose the current www page
		// so we do this last.
		if (find_str(node.tmpname, &r, 1,
						  "<a name=\"poster\"", "href=\"", NULL)) {
			// We should have "/media/rm50040832/tt0442933"
			get_url(&node, 0, r, NULL, xmlscan_host);
			// <center><table id="principal">
			// <tr><td valign="middle" align="center">
			// <img oncontextmenu="return false;" galleryimg="no"
			// onmousedown="return false;" onmousemove="return false;"
			//src="http://ia.media-imdb.com/images/M/MV5BMjAzNDkzOTQ5MV5BMl5BanBnXkFtZTcwODA1MTU1MQ@@._V1._SY400_SX600_.jpg">
			//</td></tr>

			if (node.tmpname) {

				if (find_str(node.tmpname, &r, 1,
								  "<table id=\"principal\">", " src=\"",
								  NULL)) {
					SAFE_FREE(node.extnfo.icon_path);
					node.extnfo.icon_path = strdup(r);
				} // found big icon (probably)
			} // got new media page
		} // poster tag for media page

		// We have URL to icon, go get the actual icon too, later..

		printf("\n");

		// Display values, and ask for acceptance?
		printf("%-10s : %s\n", tt_str,       S(node.extnfo.title));
		printf("%-10s : %s\n", "localtitle", S(node.extnfo.title_local));
		printf("%-10s : %s\n", "country",    S(node.extnfo.country));
		printf("%-10s : %s\n", "length",     S(node.extnfo.length));
		printf("%-10s : %s\n", "rating",     S(node.extnfo.rating));
		printf("%-10s : %s\n", "tagline",    S(node.extnfo.tagline));
		printf("%-10s : ", "genre");
		for (i = 0; ((i < EXTRA_NUM_GENRE) && node.extnfo.genre[i]); i++)
			printf("%s, ", node.extnfo.genre[i]);
		printf("\n");
		printf("%-10s : %s\n", "released", S(node.extnfo.date));
		printf("director%s  : ",node.extnfo.director[1] ? "s" : " ");
		for (i = 0; ((i < EXTRA_NUM_DIRECTOR) && node.extnfo.director[i]); i++)
			printf("%s, ", node.extnfo.director[i]);
		printf("\n");
		printf("%-10s : ", "cast");
		for (i = 0; ((i < EXTRA_NUM_CAST) && node.extnfo.cast[i]); i++)
			printf("%s, ", node.extnfo.cast[i]);
		printf("\n");
		printf("%-10s : %s\n", "description", S(node.extnfo.description));
		printf("%-10s : %s\n", "icon url", S(node.extnfo.icon_path));

		printf("\nHappy? ([Y]es, [R]etry, [S]kip or [Q]uit)\n");
		if (!fgets(buffer, sizeof(buffer), stdin)) break;

		if (tolower(*buffer) == 'q') break;
		if (tolower(*buffer) == 'y') {
			save_xml(&node, tt_str);
			continue;
		}
		if (tolower(*buffer) == 'r') {
			entry--;
			continue;
		}
		// 's' and everything else is skip.
		//if (strlower(*buffer) == 's') continue;
		printf("Skipping.\n");
		SAFE_FREE(tt_str);
		extra_release(&node.extnfo);

	} // all entries


	// Free all and exit
	// for entries, file_free(file); etc
	lion_close(node.tmphandle);
	SAFE_FREE(node.tmpname);

}

#undef S    // unpollute






// http://www.imdb.com/find?s=tt&q=beowulf&x=10&y=8
static int xmlscan_handler(lion_t *handle, void *user_data,
								int status, int size, char *line)
{
	request_t *node = (request_t *) user_data;
	char *p;

	// If node isn't set, it's b0rken.
	if (!node) return 0;

	switch(status) {

	case LION_CONNECTION_CONNECTED:
		debugf("[root] connected to remote http, issuing GET\n");

		if (node->head_method)
			lion_printf(handle,
						"GET /find?s=tt&q=%s&x=10&y=8 HTTP/1.0\r\n"
						"Host: %s\r\n",
						node->cgi_file,
						node->cgi_host);
		else
			lion_printf(handle,
						"GET %s HTTP/1.0\r\n"
						"Host: %s\r\n",
						node->cgi_file,
						node->cgi_host);

		lion_printf(handle, "Connection: close\r\n");
		lion_printf(handle, "\r\n");
		node->inheader = 1;
		node->bytes_sent = 0;
		node->bytes_size = 0;

		break;

	case LION_CONNECTION_LOST:
	case LION_CONNECTION_CLOSED:
		scanning = 0;
		node->handle = NULL;
		break;

	case LION_INPUT:
		if (line && !*line && node->inheader) {
			// in header, end of header.
			debugf("end of header\n");
			node->inheader = 0;
			lion_enable_binary(handle); // Changed to chunked
			break;
		}
		if (line && *line && node->inheader) {
			// In header
			debugf("[imdb]: %s\n", line);
			// "HTTP/1.1 200 OK"
			if (!strncmp("HTTP/1.", line, 7))
				if (strcmp("HTTP/1.1 200 OK", line) &&
					strcmp("HTTP/1.0 200 OK", line)) {

					// Failed.
					printf("[imdb] returned failure indicator. Sorry\n");
					lion_close(handle);
				}

			break;
		}
		if (line && *line && !node->inheader) {
			debugf("uhoh, received data in body mode: %s\n", line);
		}
		break;

	case LION_BINARY:
		//printf("%*.*s\n", size, size, line);


		if (size > 0) {
			p = (char *)realloc(node->tmpname,
								(unsigned int)node->bytes_size + size);
			if (p) {
				node->tmpname = p;
				memcpy(&p[node->bytes_size], line, size);
				node->bytes_size += (lion64_t) size;
			} // realloc ok

			node->bytes_sent += (lion64_t) size;
		}

		break;

	}

	return 0;
}


