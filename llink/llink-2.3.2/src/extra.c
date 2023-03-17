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
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include "lion.h"
#include "misc.h"

#define DEBUG_FLAG 64
#include "debug.h"
#include "file.h"
#include "extra.h"
#include "skin.h"
#include "request.h"
#include "root.h"

#define EXTRA_DEFAULT_ICON "/generic.jpg"


static int extra_firsttime = 1;
static char *extra_path = NULL;



void extra_default(extnfo_t *extnfo, char *);
int extra_load(char *, char *, extnfo_t *);


//
// We cheat and just use a static struct here.. since it is used in very
// short time, there should be no concurrency problem with multiple requests
//
int extra_lookup(extnfo_t *extnfo, char *path, char *root, lfile_t *file, void *skin)
{
	char buffer[1024];
	int dontclear = 0;
	char *filename;

	// Quick cache attempt
	if (file && file->name && extnfo->filename &&
		!mystrccmp(file->name, extnfo->filename)) {
		debugf("[extra] cache hit\n");
		dontclear = 1;
	}


	// Just in case
	if (!dontclear) {

		extra_release(extnfo);

		memset(extnfo, 0, sizeof(*extnfo));

	}

	// Either can be NULL, for virtual entries
	if (!path || !file) return -1;


	debugf("[extra] looking up '%s' '%s'\n", path, file->name);
	// [extra] looking up '/Users/lundman/ftp/data/' '/tmp/movies/Shooter/mymovies-front.jpg'

	if (dontclear) return 1;


	filename = strdup(file->name);


	// If the name ends with a valid extention, let's attempt to detect that
	// and if so, remove it. To avoid "thismovie.avi.xml" situations.
	if (skin && ((skin_t *)skin)->ext &&
        (strlen(((skin_t *)skin)->ext) > 2) &&
        strcasecmp("Directory", ((skin_t *)skin)->ext)) {
		// We had a skin, and it was defined by extentions (not a dir etc).
		char *r;
		if ((r = strrchr(hide_path(filename), '.')))
			*r = 0;
	}

	extnfo->filename = strdup(file->name);

	// We now go look for the extnfo file to read, and if that fails
	// load in a default.
	snprintf(buffer, sizeof(buffer), "%s/%s.xml", path, filename);
	if (extra_load(root, buffer, extnfo)) {
		SAFE_FREE(filename);
		return 1;
	}

	// First time, lets check if there is a TYPE redirect for xmls
	if (extra_firsttime) {
		extra_firsttime = 0;
		extra_path = skin_redirect(NULL, buffer); // FIXME
	}

	if (extra_path) {
		snprintf(buffer, sizeof(buffer), "%s/%s.xml", extra_path, filename);
		if (extra_load(extra_path, buffer, extnfo)) {
			// we only want the filename here, since it is in redirect area
			// we need to eat the full path
			if (strcmp(extnfo->icon_path, hide_path(extnfo->icon_path)))
				strcpy(extnfo->icon_path, hide_path(extnfo->icon_path));
			SAFE_FREE(filename);
			return 1;
		}
	}

	if (extra_path) {
		misc_stripslash(file->name);
		snprintf(buffer, sizeof(buffer), "%s/%s.xml", extra_path, hide_path(filename));
		if (extra_load(extra_path, buffer, extnfo)) {
			// we only want the filename here, since it is in redirect area
			// we need to eat the full path
			if (strcmp(extnfo->icon_path, hide_path(extnfo->icon_path)))
				strcpy(extnfo->icon_path, hide_path(extnfo->icon_path));
			SAFE_FREE(filename);
			return 1;
		}
	}


	snprintf(buffer, sizeof(buffer), "%s/%s/mymovies.xml", path, filename);
	if (extra_load(file->name, buffer, extnfo)) {
		SAFE_FREE(filename);
		return 1;
	}


	// Set default
	extra_default(extnfo, hide_path(filename));

	SAFE_FREE(filename);

	return 0;
}


void extra_release(extnfo_t *extnfo)
{
	int i;

	SAFE_FREE(extnfo->filename);
	SAFE_FREE(extnfo->icon_path);
	SAFE_FREE(extnfo->title);
	SAFE_FREE(extnfo->title_local);
	SAFE_FREE(extnfo->country);
	SAFE_FREE(extnfo->length);
	SAFE_FREE(extnfo->tagline);
	SAFE_FREE(extnfo->description);
	SAFE_FREE(extnfo->date);
	SAFE_FREE(extnfo->rating);
	SAFE_FREE(extnfo->imdburl);
	for (i = 0; i < EXTRA_NUM_DIRECTOR; i++) {
		SAFE_FREE(extnfo->director[i]);
	}
	for (i = 0; i < EXTRA_NUM_CAST; i++) {
		SAFE_FREE(extnfo->cast[i]);
	}
	for (i = 0; i < EXTRA_NUM_GENRE; i++) {
		SAFE_FREE(extnfo->genre[i]);
	}

	extnfo->person_type = 0;
}


void extra_default(extnfo_t *extnfo, char *title)
{

	extnfo->icon_path = strdup(EXTRA_DEFAULT_ICON);
	extnfo->title = strdup(title);
	extnfo->person_type = 0;
	return;

}


void extra_dupe(extnfo_t *dst, extnfo_t *src)
{
	int i;

	SAFE_COPY(dst->filename,    src->filename);
	SAFE_COPY(dst->icon_path,   src->icon_path);
	SAFE_COPY(dst->title,       src->title);
	SAFE_COPY(dst->title_local, src->title_local);
	SAFE_COPY(dst->country,     src->country);
	SAFE_COPY(dst->length,      src->length);
	SAFE_COPY(dst->tagline,     src->tagline);
	SAFE_COPY(dst->description, src->description);
	SAFE_COPY(dst->date,        src->date);
	SAFE_COPY(dst->rating,      src->rating);
	SAFE_COPY(dst->imdburl,     src->imdburl);

	for (i = 0; i < EXTRA_NUM_DIRECTOR; i++) {
		SAFE_COPY(dst->director[i], src->director[i]);
	}
	for (i = 0; i < EXTRA_NUM_CAST; i++) {
		SAFE_COPY(dst->cast[i],     src->cast[i]);
	}
	for (i = 0; i < EXTRA_NUM_GENRE; i++) {
		SAFE_COPY(dst->genre[i],    src->genre[i]);
	}

}


static void extra_alloc_levels(char ***level_tag, int level, int *allocated)
{
	char **tmp;

	// We increase the allocation in steps of 10.
	*allocated += 10;

	tmp = (char **)realloc(*level_tag, sizeof(char *) * *allocated);

	if (!tmp) {
		// Out of memory!
		*allocated -= 10;
		return;
	}

	debugf("[extra] %p area allocated for %d (size %d)\n",
		   tmp, *allocated, (int)(sizeof(char *) * *allocated));

	// Assign our new pointer.
	*level_tag = tmp;

	// Clear any new ptrs, keep the old.
	// The next line is wrong, but as we do not actually need the ptrs to
	// be NULL, as we only use "level" number, and they are immediately set...
	//memset(&tmp[level + 1], 0, sizeof(char *) * (*allocated - level + 1));

}




//
// This function will loop through a string, looking for characters we
// need to escape. If none are found, it returns the same string.
// If some are found, we will allocate a new string, build it, and
// free the old.
//
char *extra_quotestr(char *s)
{
	char *result, *r, *found, savechr, *t;
	int num_quote;

	num_quote = 0;

	for (r = s;
		 r && *r && (found = strpbrk(r, EXTRA_QUOTECHARS));
		 r = &found[1])
		num_quote++;  // count number of characters we need to quote.

	// Nothing to quote, just return
	if (!num_quote) return s;

	// We need to quote! Allocate some memory
	debugf("[extra] quotestr allocating %ld + %d\n", strlen(s), num_quote);

	result = (char *) malloc(sizeof(char) * (strlen(s) + num_quote + 1));
	if (!result) return s; // Maybe not correct, but we are out of memory

	for (r = s, t = result;
		 r && *r && (found = strpbrk(r, EXTRA_QUOTECHARS));
		 r = &found[1]) {

		savechr = *found;
		*found = 0; // Terminate here, so copying is easier.

		// copy over this bit
		sprintf(t, "%s\\%c", r, savechr);
		// advance t for next round.
		t = &t[ strlen(r) + 2 ];

		// We technically do not need to restore the string.
		*found = savechr;
	}
	// Copy the remainder as well.
	if (r && *r)
		sprintf(t, "%s", r);


	SAFE_FREE(s);

	debugf("[extra] quotestr '%s'\n", result);

	return result;
}






//
// We will receive a word at a time for each part we are interested in
// so build up a string, growing buffers as needed, then once we get a null
// assign it to the correct extnfo item.
//
#define EXTRA_DATA(str, item)          \
		if (!mystrccmp(tag, (str))) {  \
			if (!input) {              \
                build = extra_quotestr(build); \
				extnfo->item = build;  \
				build = NULL;          \
				allocated = 0;         \
				return;                \
			}                          \
			interested = 1;            \
			break;                     \
		}

void extra_string_data(extnfo_t *extnfo, int level, char *tag, char *root,
					   char *input)
{
	static char *build = NULL;
	static int allocated = 0;
	int interested = 0;
	int len, i;

	switch(level) {

	case -1: // Can't happen?
		return ;

	case 0: // <Title>
		// Just to be sure we start from scratch
		SAFE_FREE(build);
		allocated = 0;
		return;

	case 1: // <Country> <LocalTitle> <OriginalTitle> <RunningTime>
		    // <TagLine> <Description> <Genres>
		//debugf("[extra] Data level %d '%s' -> '%s'\n", level, tag,
		//	   input ? input : "");

		EXTRA_DATA("OriginalTitle", title);
		EXTRA_DATA("LocalTitle",    title_local);
		EXTRA_DATA("Country",       country);
		EXTRA_DATA("RunningTime",   length);
		EXTRA_DATA("TagLine",       tagline);
		EXTRA_DATA("Description",   description);
		EXTRA_DATA("Genres",        genre[0]);
		EXTRA_DATA("ReleaseDate",   date);
		EXTRA_DATA("Rating",        rating);
		EXTRA_DATA("IMDbUrl",       imdburl);

		if (!mystrccmp(tag, "Discs")) {
			SAFE_FREE(build);
			allocated = 0;
		}
		break;

	case 2: // <Covers>+<Front> <Covers>+<Back>
		//debugf("[extra] Data level %d '%s' -> '%s'\n", level, tag,
		//	   input ? input : "");

		// With icons, we need to do extra work, due to the need to add
		// full path.
		if (!mystrccmp(tag, "Front")) {
			if (!input && build) {
				if (*build == '/')
					extnfo->icon_path = build;
				else {
					extnfo->icon_path = misc_strjoin(root, build);
					SAFE_FREE(build); //xx
				}
				build = NULL;
				allocated = 0;
				return;
			}
			interested = 1;
		}

		if (!mystrccmp(tag, "Person")) {
			// Finished a "Person" read. Actually add it in.

			if (build) {
				switch (extnfo->person_type) {
				case 0: // No type yet, wait for it.
					debugf("[extra] error, read Name tag but no Type: %s\n",
						   build);
					return;
				case 1:
					for (i = 0; i < EXTRA_NUM_CAST; i++) {
						if (!extnfo->cast[i]) {
							extnfo->cast[i] = extra_quotestr(build);
							break;
						}
					} // for
					break;
				case 2:
					for (i = 0; i < EXTRA_NUM_DIRECTOR; i++) {
						if (!extnfo->director[i]) {
							extnfo->director[i] = extra_quotestr(build);
							break;
						}
					}
					break;
				}
			}

			// Clear all the work for next time.
			build = NULL;
			allocated = 0;
			extnfo->person_type = 0;
			return;
		}
		break;

		// <Persons>
		//   <Person>
		//     <Name>
        //     <Type>
	case 3:
		// Check previous tag is Person, or we will get "Disc 1" which also
		// have "Name" field. Alas, I made tag_levels be private, so fix me.
		// For now I cheat, and clear "build" when we get Discs close-tag.
		if (!mystrccmp(tag, "Name")) {
			// Have we finished this Tag? If so, store it.
			if (!input && build) {
				return;
			}
			interested = 1;
		}

		if (!mystrccmp(tag, "Type")) {
			if (input) {
				if (!mystrccmp("Director", input))
					extnfo->person_type = 2;
				else
					extnfo->person_type = 1;
			}
		}
		break;

	default:
		return;

	} // switch level

	// We got input we might potentially be interested in.
	if (!interested) return;

	// New data.
	len = build ? strlen(build) : 0;

	//debugf("[extra] interested set, for another %d (allocated %d at %p): '%s' + '%s'\n",
	//	   len, allocated, build, build ? build : "", input);

	// Allocate more memory?
	if ((strlen(input) + 1 + 1 /* space + nul */ ) >= (allocated - len)) {
		char *tmp;
		int more;

		more = strlen(input) + 1 + 1; // How much more to allocate
		// but lets allocate by larger strings, to limit the number of calls
		// to realloc
		if (more < 64) more = 64;

		tmp = realloc(build, allocated + more);
		if (!tmp) return;
		// Null terminate if it was the first alloc.
		//debugf("[extra] realloc %p, first %s - more %d\n",
		//	   tmp, build ? "no" : "yes", more);

		if (!build) *tmp = 0;
		build = tmp;
		allocated += more;
	}

	// We can speed strcat up here.
	// Only add space if there is nothing there to start with
	//	debugf("[extra] before '%s' -- ", build);
	if (*build)
		strncat(build, " ", allocated);
	strncat(build, input, allocated);

	//debugf("after '%s'\n", build);
}
#undef EXTRA_DATA



int extra_load(char *root, char *path, extnfo_t *extnfo)
{
	FILE *file;
	char buffer[1024], *ar, *token = NULL;
	int level = -1;
	int level_allocated = 0;
	char **level_tag = NULL;
	int len, i;

	// Unfortunately, we can't really load this with lion, without fork()ing
	// and all that. So regular IO it is.

	debugf("[extra] looking for file '%s' ... \n", path);


	file = fopen(path, "r");
	if (!file) return 0;


	// Give us some room.
	extra_alloc_levels(&level_tag, level, &level_allocated);


	while(fgets(buffer, sizeof(buffer), file)) {

		misc_strip(buffer);

		// Tiny XML parser coming up.
		ar = buffer;

		// Fish out the next tag.
		while ((token = misc_digtoken(&ar, "<> \t"))) {

			len = strlen(token);


			if (!mystrccmp("![CDATA[]]", token)) continue;

			if (!strncasecmp("![CDATA[", token, 8)) {

				//debugf("[extra] Extra work for CDATA tag\n");

				// Un-terminate the string
				if (ar && *ar) { // in case its "CDATA[\n". (kpziegler)
					ar--;
					*ar = misc_digtoken_optchar;
				}
				ar = &token[8];
				token[7] = 0;
				// Ick, modifying another modules variables.
				misc_digtoken_optchar = '>';
			}


			// Token, or text?
			if (misc_digtoken_optchar == '>') {

				// Fudge in the end CDATA token
				// This eats the last word in description, call
				// input on it here?
				if ((level >= 0) &&
					!mystrccmp(level_tag[level], "![CDATA") &&
					len && (token[len - 1] == ']')) {
					//token = "/![CDATA"; // Static string!! FIXME
					token = strdup("/![CDATA"); // memory leak
				}


				// Is it a open/close shotcut tag? Like
				// "<Description />" if so, we do not care.
				if (len &&
					(token[len - 1] == '/')) continue;

				// Is it a starting tag?
				if (len &&
					(*token != '/')) {

					level++; // Go one deeper

					//debugf("[extra] XML %*.*sstart tag '%s' (allocated %d:%p)\n",
					//  level, level, indent, token, level_allocated, level_tag);

					// Allocate more room?
					if (level >= level_allocated)
						extra_alloc_levels(&level_tag, level, &level_allocated);
					if (level >= level_allocated)
						continue;

					level_tag[level] = strdup(token);

					// Go back to parse
					continue;

				} // Starting tag

				// If there is anything to read before any starting tag we
				// just ignore it.
				if (level == -1) continue;

				// Is it a closing tag?
				if ((*token == '/') &&
					level_tag[level] &&
					!mystrccmp(&token[1], level_tag[level])) {

					//	debugf("[extra] XML  %*.*sstop tag '%s'\n",
					//	   level, level, indent, level_tag[level]);

					// Send data with NULL to terminate it.
					extra_string_data(extnfo, level, level_tag[level], root,
									  NULL);

					//debugf("[extra] XL  %*.*sreleasing %p\n", level, level,
					//	   indent, level_tag[level]);

					SAFE_FREE(level_tag[level]);

					level--;
					continue;
				} // closing tag

			} // if starts with '<'


			// If there is anything to read before any starting tag we
			// just ignore it.
			if (level == -1) continue;


			// It is neither open, nor close tag, so just data, if it is
			// data we care about, we should add it to the data field.

			// Build the input string.
			extra_string_data(extnfo, level, level_tag[level], root, token);

		} // while token

	} // while fgets

	// Release memory
	for (i = 0; i <= level; i++) {
		SAFE_FREE(level_tag[i]);
	}
	SAFE_FREE(level_tag);

	fclose(file);

	// Extra checks. Apparently MyMovies adds a icon path even when there is no
	// icon, which is rather pathetic. So we attempt to stat() said icon
	// if it does not exist, we will change it to $dirname.jpg, since then
	// redirects has a better chance to work.
	debugf("[extra]: icon_path set to '%s'. root '%s' and path '%s' \n",
		   extnfo->icon_path ? extnfo->icon_path : "(null)",
		   root, path);
	// [extra]: icon_path set to 'Starwars/mymovies-front.jpg'. root 'Starwars'
	// and path '/Users/lundman/ftp/data/tmp/movies//Starwars/mymovies.xml'

	// No path set? Set it to dirname, so redirect has a chance.
	if (!extnfo->icon_path && root) {
		snprintf(buffer, sizeof(buffer), "%s.jpg", root);
		extnfo->icon_path = strdup(buffer);
		return 1;
	}

	// icon_path set, check if it exists
	{
		request_t *snode;
		int failed;
		snode = request_newnode();
		if (snode) {
			snode->path = strdup(extnfo->icon_path);
			debugf("[extra] testing icon path: '%s'\n", snode->path);
			failed = root_setpath(snode);

			if (failed) {
				// check redirects as well
				snode->path = strdup(extnfo->icon_path);
				if (!request_redirect(snode)) {

					debugf("[extra] bad icon_path, defaulting to %s.jpg\n", root);
					SAFE_FREE(extnfo->icon_path);
					snprintf(buffer, sizeof(buffer), "%s.jpg", root);
					extnfo->icon_path = strdup(buffer);
				} // redirect

			} // failed

            // BUG! This clears the cache for 'snode' but this also
            // clears skin_tvid, which is global!
			request_freenode(snode);

		} // snode
	}

	return 1;
}

