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
#include <ctype.h>

#include <inttypes.h>
#ifdef WIN32
#include <io.h>
#endif


#include <errno.h>
#include <fcntl.h>

#include <libmms/mms.h>

#include "debug.h"
#include "unmms.h"
#include "lfnmatch.h"

//
// gcc -o unmms unmms.c debug.c lfnmatch.c -I /opt/local/include/ -I /opt/local/include/glib-2.0 -I /opt/local/lib/glib-2.0/include/ -L /opt/local/lib -lmms
//


extern int getopt();
extern char *optarg;
extern int optind;

static    int       do_exit      = 0;
static    uint64_t  seek         = 0;
static    task_t    task         = TASK_NONE;
static    char     *archive_name = NULL;  // ISONAME, but simulating unrar...
static    char     *extract_name = NULL;
static    char     *rar_name     = NULL;
static    char     *rar_exe      = NULL;



// Signal handler for INT and HUP
void exit_interrupt(void)
{
    do_exit = 1;
}


int main(int argc, char **argv)
{
    char *fname = NULL, *r;
    FILE *fout;


    // Read any command line arguments from our friend the user.
    arguments(argc, argv);


    if (task == TASK_NONE) options(argv[0]);

    // Catch some signals so can exit cleanly

    signal(SIGINT, exit_interrupt);
#ifndef WIN32
    signal(SIGHUP, exit_interrupt);
    signal(SIGPIPE, SIG_DFL); // We want to die when parent goes away.
#endif

    // libdvdnav tends to be a bit noisy, close stderr?
    if (!debug_on) {
#ifdef WIN32
		// libdvdnav has prints to stderr we can not get rid of. Win32
		// throw exceptions if you just close it.
		freopen("NUL:", "w", stderr);
#else
		freopen("/dev/null", "w", stderr);
#endif
	}


#ifdef WIN32
    // Windows has the idea that stdout/err is text-mode by default, so
    // we need to (try to) change it to binary
	if (_setmode( _fileno( stdout ), _O_BINARY ) == -1 ) {
        debugf("oops, failed to change stdout to binary - great\n");
    }
#endif


    switch(task) {

    case TASK_LIST:
        unmms_list(archive_name, rar_name);
        break;

    case TASK_EXTRACT_STDOUT:
        unmms_extract(archive_name, rar_name, extract_name, stdout);
        break;

    case TASK_EXTRACT_FILE:
        fname = strdup(extract_name);
        while ((r = strchr(fname, '/')) ||
               (r = strchr(fname, '\\')))
            *r = '_';
        fout = fopen(fname, "wb");
        if (fout) {
            unmms_extract(archive_name, rar_name, extract_name, fout);
            fclose(fout);
        }
        SAFE_FREE(fname);
        break;

    default:
        break;
    }

    SAFE_FREE(archive_name);
    SAFE_FREE(extract_name);
    SAFE_FREE(rar_name);
    SAFE_FREE(rar_exe);

    exit(0);

}



static void options(char *prog)
{

    printf("\n");
    printf("%s - an 'unrar' clone for MMS streams.\n", prog);
    printf("%s cmd [options] archive [filename]\n\n", prog);
    printf("  cmd:\n");
    printf("  v/l: list contents of archive\n");
    printf("    p: send contents of filename in archive to stdout\n");
    printf("    x: extract contents of filename in archive\n");
    printf("  options:\n");
    printf("     -sk <n>  : seek to offset byte-count. Eg -sk100 to start from byte 100\n");
    printf("     -R <file>: filename inside RAR-archive. -R filename.iso\n");
    printf("     -X <cmd> : Set direct path to unrar\n");
    printf("All other options are currently silently ignored\n");
    printf("\n");
    printf("%s p -sk 1234 filename.iso video.avi\n", prog);
    printf("%s p -R filename.iso -sk 1234 filename.rar video.avi\n", prog);
    printf("\n");
    exit(0);
}


void arguments(int argc, char **argv)
{
    int opt;
    char *prog;

    prog = argv[0];

    // First fish out the cmd part, as it is mandatory
    if (argc <= 1) options(prog);

    // First argument, character. Case-sensitive?
    switch(argv[1][0]) {
    case 'v':
    case 'l':
        task = TASK_LIST;
    break;
    case 'p':
        task = TASK_EXTRACT_STDOUT;
        break;
    case 'x':
        task = TASK_EXTRACT_FILE;
        break;
    default:
        printf("ERROR: Unknown command '%s'\n", argv[1]);
        options(prog);
    }


    // Skip the cmd argument
    argv++;
    argc--;

    // Silence getopt.
#if !defined (WIN32) && !defined (__APPLE__)
	opterr = 0;
#endif

	// Now scan for any optional switches.
    // Strictly speaking, this is incorrect. Unrar option is "-sk<arg>" when we
    // actually define "-s" and "-k<arg>". Oh well.
    while ((opt=getopt(argc, argv,
                       "hsk:R:dA:X:"
                       "vc:p:yfg:inul"// These are NOT really used, just to stop errors
                       )) != -1) {

        switch(opt) {

        case 'h':
            options(prog);
            break;

        case 'k':
            seek = strtoull(optarg, NULL, 10);
            break;

        case 'R':
            SAFE_FREE(rar_name);
            // Skip any leading "/" as they are not allowed.
            while(*optarg == '/') optarg++;
            rar_name = strdup(optarg);
            break;

        case 'X':
            SAFE_FREE(rar_exe);
            // Skip any leading "/" as they are not allowed.
            rar_exe = strdup(optarg);
            break;

        case 'd':
            debug_on++;
            break;

        default:
            break;

        }

    }
    argc -= optind;
    argv += optind;

    // argc and argv adjusted here.
    if (argc<1) {
        printf("ERROR: Missing archive name\n");
        options(prog);
    }

    // Extract to stdout requires filename (extract to file has implied all)
    if ((task == TASK_EXTRACT_STDOUT) ||
        (task == TASK_EXTRACT_FILE)) {
        if (argc<2) {
            printf("ERROR: Missing filename to extract\n");
            options(prog);
        }
        extract_name = strdup(argv[1]);
    }

    archive_name = strdup(argv[0]);

#if 1
    debugf("Seek: %"PRIu64"\n", seek);
    debugf("archive: %s\n", archive_name);
    if (extract_name)
        debugf("extract: %s\n", extract_name);
    if (rar_name)
        debugf("rarname: %s\n", rar_name);
#endif

    // No unrar set, give it a default.
    if (!rar_exe)
        rar_exe = strdup("unrar"); // Use $PATH

}

uint8_t *strip(uint8_t *s)
{
    uint8_t *r;
    if ((r = strchr(s, '\r'))) *r = 0;
    if ((r = strchr(s, '\n'))) *r = 0;
    return s;
}




//
// Same as above, but strict version. Can let anything through here.
//
char *url_encode(char *s)
{
	char *result = NULL;
	int len = 0;
	char *r, *r2=0;
	static char hex[] = "0123456789ABCDEF";

	// How much space to allocate?
	while(1) {

		r = s;

		while (*r) {

			switch(tolower(*r)) {
			case 'a':
			case 'b':
			case 'c':
			case 'd':
			case 'e':
			case 'f':
			case 'g':
			case 'h':
			case 'i':
			case 'j':
			case 'k':
			case 'l':
			case 'm':
			case 'n':
			case 'o':
			case 'p':
			case 'q':
			case 'r':
			case 's':
			case 't':
			case 'u':
			case 'v':
			case 'w':
			case 'x':
			case 'y':
			case 'z':
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '$':
			case '-':
			case '_':
			case '.':
			case '+':
			case '!':
			case '*':
			case '(':
			case ')':
			case ',':
                //case '/': // and slash man!
				len++;
				if (result) *r2++ = *r;
				break;
			default:
				len+=3;
				if (result) {
					*r2++ = '%';
					*r2++ = hex[((*r>>4)&0x0f)];
					*r2++ = hex[((*r)&0x0f)];
				}
			} // switch
			r++;
		} // while r

		if (result) {
			*r2 = 0;
			break;
		}

		if (!len) break;

		result = malloc(len + 1);

		if (!result) break;

		r2 = result;

	} // while 1

	return result;

}


char *hide_path(char *s)
{
    char *r;

    r = strrchr(s, '/');

    return ((r && r[1]) ? &r[1] : s);

}

//
// "List" the contents of a mms file, either just one URL, or parse HTML
//
void unmms_list(char *archive, char *rarname)
{
    FILE *fd;
    uint32_t numStream = 0;
    uint8_t buffer[1024];
    uint64_t bytes;
    char *url;
    char *base, *r;


    printf("UNMMS 3.71 beta 1 freeware    Copyright (c) 2009- Jorgen Lundman\n\n");
    printf("Archive %s\n\n", archive_name);
    printf("Pathname/Comment\n");
    printf("                  Size   Packed Ratio  Date   Time     Attr      CRC   Meth Ver\n");
    printf("-------------------------------------------------------------------------------\n");


    if ((fd = fopen(archive, "r"))) {
        // Open the file

        base = strdup(hide_path(archive));
        if ((r = strrchr(base, '.'))) *r = 0;

        while(fgets(buffer, sizeof(buffer), fd)) {

            strip(buffer);

            // If it starts with mms:// lets print it
            if (!lfnmatch("mms://*", buffer, LFNM_CASEFOLD)) {

                numStream++;

                // Need a filesize, anything will do
                bytes = (uint64_t) strlen(buffer);

                //url = url_encode(buffer);

                printf(" %s_stream_%02u.asf\n",
                       base,
                       numStream);

                //     Size Packed Ratio  Date  Time  Attr  CRC  Meth Ver
                printf("  %12"PRIu64" %12"PRIu64" 100%% %02u-%02u-%02u %02u:%02u -rw-r--r-- 00000000 m0 2.0\n",
                       bytes, bytes,
                       1,12,9, // date
                       12,15);   // time

                //SAFE_FREE(url);

            } // mms://

        } // fgets

        SAFE_FREE(base);
        fclose(fd);

    } // fopen


    printf("-------------------------------------------------------------------------------\n");

}


#if 0
    // Is the empty directory entries strictly needed?
    printf(" title_%02u\n",
           title);

    printf("  %12"PRIu64" %12"PRIu64"   0%% %02u-%02u-%02u %02u:%02u drw-r--r-- 00000000 m0 2.0\n",
           0LL, 0LL,
           1,12,9, // date
           12,15);   // time
#endif





// =========================================================================
// ========================================================================
// =======================================================================
// ======================================================================
// =====================================================================
// ====================================================================
// ===================================================================



char *undos(char *s)
{
    char *r;

    r = s;

    while((r = strchr(r, '\\')))
        *r = '/';

    return s;
}



char *geturl(char *archive, uint32_t stream)
{
    FILE *fd;
    unsigned char buffer[1024];
    uint32_t streamNum = 0;

    fd = fopen(archive, "r");
    if (!fd) return NULL;

    while(fgets(buffer, sizeof(buffer), fd)) {

        strip(buffer);

        // If it starts with mms:// lets print it
        if (!lfnmatch("mms://*", buffer, LFNM_CASEFOLD)) {

            streamNum++;

            if (streamNum == stream) {
                fclose(fd);
                return strdup(buffer);
            }

        }

    }

    fclose(fd);

}




void unmms_extract(char *archive, char *rarname,
                   char *extractname, FILE *ioptr)
{
    uint32_t stream = 0;
    char *r, *url;
    mms_t *mms;
    unsigned char buffer[1024];
    unsigned int red, total;


    debugf("Attempting to extract '%s' from '%s' (in '%s') from position %"PRIu64"...\n",
           extractname,
           archive,
           rarname ? rarname : "",
           seek);


    //url = url_decode(extractname);

    undos(extractname); // convert \ to /

    // test_stream_01.asf
    r = strstr(extractname, "_stream_");

    if (r && (sscanf(r,
                     "_stream_%02u.",
                     &stream) == 1)) {

        debugf("  stream %02u\n", stream);

    } // sscanf


    if (!stream) {
        printf("File '%s' does not contain stream %02u not found.\n",
               extractname, stream);
        exit(5);
    }


    url = geturl(archive, stream);

    if (!url) {
        printf("Unable to get URL for stream %02u from file '%s'.\n",
               stream, extractname);
        exit(4);
    }


    debugf("URL '%s'\n", url);


    mms = mms_connect(NULL, NULL, url, 128 * 1024);

    if (!mms) {
        printf("Unable to connect to URL, stop.\n");
        exit(3);
    }

    debugf("Connected\n");

    while((red = mms_read(NULL, mms, buffer, sizeof(buffer))) > 0) {

        fwrite(buffer, red, 1, ioptr);

        total += red;

        debugf("Get %d bytes...\n", red);

    }

    // Probably not reached, since we will only quit from SIGPIPE
    debugf("Streamed %lu bytes.\n", total);

    mms_close(mms);
    SAFE_FREE(url);

    exit(0);
}


