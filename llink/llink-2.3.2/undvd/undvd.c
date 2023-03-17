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

#include "dvdread/dvd_udf.h"
#include "dvdread/dvd_reader.h"
#include "dvdnav/dvd_types.h"
//#include "dvd_reader.h"
#include "dvdread/nav_types.h"
#include "dvdread/ifo_types.h" /* For vm_cmd_t */
#include "dvdnav/dvdnav.h"
#include "dvdnav/dvdnav_events.h"
#undef stat // it defines it to stat64, and we do as well.

#include "debug.h"
#include "undvd.h"
#include "lfnmatch.h"
#include "rarinput.h"

// Alas, dvd_input.h is not installed by default.
int dvdinput_setup_ext(
                       dvd_input_t (*dvdinput_open)  (const char *),
                       int         (*dvdinput_read)  (dvd_input_t, void *, int, int),
                       int         (*dvdinput_seek)  (dvd_input_t, int),
                       int         (*dvdinput_close) (dvd_input_t),
                       int         (*dvdinput_title) (dvd_input_t, int),
                       char *      (*dvdinput_error) (dvd_input_t)
                       );

uint64_t DVDFileSize64( dvd_file_t *dvd_file );

extern int getopt();
extern char *optarg;
extern int optind;

static    int       do_exit      = 0;
static    uint64_t  seek         = 0;
static    task_t    task         = TASK_NONE;
static    char     *archive_name = NULL;  // ISONAME, but simulating unrar...
static    char     *extract_name = NULL;
static    char     *rar_name     = NULL;
          char     *rar_exe      = NULL;

// We can do the audio stream selection multiple ways:
// 0 - do nothing, send as is (VLC etc, can handle multiple streams)
// 1 - patch first audio packet to stream we actually want.
//     PCH/NMT picks the FIRST audio stream it seems only, can't switch.
// 2 - swap streams, first stream, and wanted streams changed IDs.
//     Some players will only play 1 stream and nothing else.
// 3 - strip out all undesired streams, if we figure out how to do that.
static    int       audio_solution = 1;
static    int       audio_seen     = 0; // Run-only-once variable


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
        undvd_list(archive_name, rar_name);
        break;

    case TASK_EXTRACT_STDOUT:
        undvd_extract(archive_name, rar_name, extract_name, stdout);
        break;

    case TASK_EXTRACT_FILE:
        fname = strdup(extract_name);
        while ((r = strchr(fname, '/')) ||
               (r = strchr(fname, '\\')))
            *r = '_';
        fout = fopen(fname, "wb");
        if (fout) {
            undvd_extract(archive_name, rar_name, extract_name, fout);
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
    printf("%s - an 'unrar' clone for DVD/ISO/IMG/VIDEO_TS images. DVD and Bluray\n", prog);
    printf("%s cmd [options] archive [filename]\n\n", prog);
    printf("  cmd:\n");
    printf("  v/l: list contents of archive\n");
    printf("    p: send contents of filename in archive to stdout\n");
    printf("    x: extract contents of filename in archive\n");
    printf("  options:\n");
    printf("     -sk <n>  : seek to offset byte-count. Eg -sk100 to start from byte 100\n");
    printf("     -R <file>: filename inside RAR-archive. -R filename.iso\n");
    printf("     -X <cmd> : Set direct path to unrar\n");
    printf("     -A <0,1> : Audio stream solution\n");
    printf("All other options are currently silently ignored\n");
    printf("\n");
    printf("%s p -sk 1234 filename.iso video.avi\n", prog);
    printf("%s p -R filename.iso -sk 1234 filename.rar video.avi\n", prog);
    printf("\n");
    printf("  Audio stream solution:\n");
    printf("   0 - no nothing, send full stream (VLC etc can handle multiple audio)\n");
    printf("   1 - patch first block only (PCH/NMT plays first stream encountered)\n");
    printf("If you want RAR streaming to have a chance of working, you need to get the\nspecial unrar with seek patched (option -sk)\n");
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

        case 'A':
            audio_solution = atoi(optarg);
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
    if (!rar_exe) {
        char *path;
        path = getenv("UNRAR_CMD");
        if (path) {
            rar_exe = strdup(path);
        } else {
#ifdef WIN32
            rar_exe = strdup("unrar.exe"); // Use $PATH
#else
            rar_exe = strdup("./unrar"); // Use $PATH
#endif
        }
    } // rar_exe
}




#if 0
static char *time2str(uint64_t time)
{
    static char workstr[100];
    unsigned short int days,hours,mins,secs;;

    time /= 90000;
    days=(time/86400),hours,mins,secs;
    hours=((time-(days*86400))/3600);
    mins=((time-(days*86400)-(hours*3600))/60);
    secs=(time-(days*86400)-(hours*3600)-(mins*60));

    workstr[0]=(char)0;
    //if(days)
    //  snprintf(workstr,sizeof(workstr), "%dd",days);
    snprintf(workstr,sizeof(workstr),"%s%s%02d",workstr,(workstr[0])?":":"",hours);
    snprintf(workstr,sizeof(workstr),"%s%s%02d",workstr,(workstr[0])?":":"",mins);
    snprintf(workstr,sizeof(workstr),"%s%s%02d",workstr,(workstr[0])?":":"",secs);

    if (!days && !hours && !mins && !secs)
    snprintf(workstr,sizeof(workstr),"00:00:00");

    return(workstr);
}
#endif


char *lang2str(int32_t num, uint32_t lang)
{
    static char result[3];

    result[2] = 0;

    if (lang == 0xffff) return "??";

	if (!lang) {
		snprintf(result, sizeof(result), "%02u", num);
		return result;
	}

    result[0] = (lang >> 8) & 0xff;
    result[1] = lang & 0xff;

    return result;
}


char *format2str(uint16_t format)
{
    switch(format) {
    case DVDNAV_FORMAT_AC3:
        return "AC3";
    case DVDNAV_FORMAT_MPEGAUDIO:
        return "MPEGAudio";
    case DVDNAV_FORMAT_LPCM:
        return "LPCM";
    case DVDNAV_FORMAT_DTS:
        return "DTS";
    case DVDNAV_FORMAT_SDDS:
        return "SDDS";
    default:
        return "Unknown";
    }

}

//
// Attempt to get the number of bytes in a stream, quite a slow function.
//
uint64_t advd_title_getnumbytes(dvdnav_t *dvdnav)
{
    uint32_t pos, end;
	dvdnav_status_t     result;
	int                 event, len;
    unsigned char buffer[DVD_VIDEO_LB_LEN];
    unsigned char *block;
    int i = 0;


    while (1) {

        block = buffer;
		result = dvdnav_get_next_cache_block(dvdnav,
                                             &block, &event, &len);
		if(result == DVDNAV_STATUS_ERR)
            return 0;

        if (block != buffer)
            dvdnav_free_cache_block(dvdnav, block);


		switch(event) {
		case DVDNAV_CELL_CHANGE:
			debugf("Block %d CELL_CHANGE\n", i);
            pos = 0;
            len = 0;
            dvdnav_get_position(dvdnav, &pos, &end);
            debugf("pos %u and len %u\n", pos, end);
            if (end) return (end * DVD_VIDEO_LB_LEN);
			break;
        case DVDNAV_STILL_FRAME:
            dvdnav_still_skip(dvdnav);
            break;
		case DVDNAV_WAIT: //                     13
            dvdnav_wait_skip(dvdnav);
			break;
		case DVDNAV_BLOCK_OK:
		case DVDNAV_STOP:
            debugf("[advd] failed to get numbytes (%d)\n", event);
            return 0;
        default:
            break;
        }
        i++;
    }

    // not reached
    return 0;

}


int undvd_open_image(dvdnav_t **dvdnav,
                     dvd_reader_t **dvdread,
                     char *archive,
                     char *rarname,
                     char *filename)
{

    // Start libdvdnav, should we use it with unrar, or vanilla?
    if (rarname) {
        // Change to use RAR input methods
#ifndef UNDVD_NO_UDF250
        dvdinput_setup_ext(rarinput_open,
                           rarinput_read,
                           rarinput_seek,
                           rarinput_close,
                           rarinput_title,
                           rarinput_error);

        // Pass along the name in archive
        rarinput_name_in_rar = strdup(rarname);

    } else {
        // Change back to standard IO
        dvdinput_setup_ext(NULL, NULL, NULL, NULL, NULL, NULL);
#endif

    }

    if (dvdnav) {
        dvdnav_open(dvdnav, archive_name);
        // Open might fail if its bluray/UDF2.50
        if (!*dvdnav && dvdread) {
            *dvdread = DVDOpen(archive_name);
        }

    } else {
        *dvdread = DVDOpen(archive_name);
    }

    if ((!dvdnav || !*dvdnav) && (!dvdread || !*dvdread)) {
        fprintf(stderr, "ERROR: Failed to open archive '%s': %s\r\n",
                archive_name, strerror(errno));
        if (rar_name)
            fprintf(stderr, "(Contained inside RAR archive '%s')\r\n",
                    rar_name);
        return -1;
    }


    // Set some dvdnav specific things..
    if (dvdnav && *dvdnav) {
        if (dvdnav_set_readahead_flag(*dvdnav,  1) !=
            DVDNAV_STATUS_OK) {
            debugf("Error on dvdnav_set_readahead_flag: %s\n",
                   dvdnav_err_to_string(*dvdnav));
        }
        /* set the language */
        if (dvdnav_menu_language_select(*dvdnav, "en") !=
            DVDNAV_STATUS_OK ||
            dvdnav_audio_language_select(*dvdnav, "en") !=
            DVDNAV_STATUS_OK ||
            dvdnav_spu_language_select(*dvdnav, "en") !=
            DVDNAV_STATUS_OK) {
            debugf("Error on setting languages: %s\n",
                   dvdnav_err_to_string(*dvdnav));
        }
        if (dvdnav_set_PGC_positioning_flag(*dvdnav, 1) !=
            DVDNAV_STATUS_OK) {
            debugf("Error on dvdnav_set_PGC_positioning_flag: %s\n",
                   dvdnav_err_to_string(*dvdnav));
        }
    } // if dvdnav


    return 0;
}


void undvd_close_image(dvdnav_t **dvdnav,
                       dvd_reader_t **dvdread)

{
    if (*dvdnav)
		dvdnav_close(*dvdnav);
    if (*dvdread)
		DVDClose(*dvdread);

    *dvdnav  = NULL;
    *dvdread = NULL;
}


//
// "List" the contents of a DVD
//
void undvd_list(char *archive, char *rarname)
{
	dvd_reader_t *dvdread = NULL;
	dvdnav_t *dvdnav = NULL;
    int32_t numTitles, numStreams;
    uint64_t *times = NULL, duration, bytes;


    if (undvd_open_image(&dvdnav, &dvdread,
                         archive,
                         rarname,
                         NULL)) {
        return;
    }


    printf("UNDVD 3.8 freeware    Copyright (c) 2009- Jorgen Lundman\n\n");
    printf("Archive %s\n\n", archive_name);
    printf("Pathname/Comment\n");
    printf("                  Size   Packed Ratio  Date   Time     Attr      CRC   Meth Ver\n");
    printf("-------------------------------------------------------------------------------\n");


    // DVD image uses libdvdnav:

    if (dvdnav) {

        // We can enumerate the titles?
        if (dvdnav_get_number_of_titles(dvdnav,
                                        &numTitles) == DVDNAV_STATUS_OK) {
            int32_t numParts = 0;
            int title;

            debugf("dvdnav reports %d titles.\n", numTitles);
            // iterate all titles
            for (title = 1; title <= numTitles; title++) {

                if (dvdnav_get_number_of_parts(dvdnav,
                                               title, &numParts)
                    == DVDNAV_STATUS_OK) {

                    debugf("    title %d has %d parts\n", title, numParts);

                    duration = 0;
                    if (dvdnav_describe_title_chapters(dvdnav,
                                                       title, &times,
                                                       &duration) > 0) {
                        free(times);
                    }

                    // Is the empty directory entries strictly needed?
                    printf(" title_%02u\n",
                           title);

                    printf("  %12"PRIu64" %12"PRIu64"   0%% %02u-%02u-%02u %02u:%02u drw-r--r-- 00000000 m0 2.0\n",
                           0LL, 0LL,
                           1,12,9, // date
                           12,15);   // time

                    // Now then, for each title, pull out streams
                    if (dvdnav_title_play(dvdnav, title) == DVDNAV_STATUS_OK) {

                        // Get bytes...
                        bytes = advd_title_getnumbytes(dvdnav);

                        // For some reason, if you ask for the
						// last blocks, they just aren't there. So we
						// shorten the movie. Note its 10 blocks, like seek
                        if (bytes > 20480LL)
                            bytes -= 20480LL;


                        // For each audio stream:

                        duration = 0;
                        if (dvdnav_describe_title_chapters(dvdnav,
                                                           title, &times,
                                                           &duration) > 0) {
                            free(times);
                        }

                        for (numStreams = 0; numStreams <= 8; numStreams++) {
                            audio_attr_t audio_attr;
                            int32_t logical;

                            if ((logical = dvdnav_get_audio_logical_stream(dvdnav, numStreams))
                                < 0) break;

                            dvdnav_get_audio_attr(dvdnav, logical,
                                                  &audio_attr);

                            // directory1/directory2/file2.avi
                            // 7    17 242% 04-12-07 14:11 -rw-r--r-- 16B28489 m3b 2.9
                            printf(" title_%02u/audio_%02u_%s_%s_%uch.mpg\n",
                                   title, numStreams,
                                   lang2str(numStreams, audio_attr.lang_code),
                                   format2str(dvdnav_audio_stream_format(dvdnav,
                                                                         logical)),
                                   dvdnav_audio_stream_channels(dvdnav,
                                                                logical));
                            //     Size Packed Ratio  Date  Time  Attr  CRC  Meth Ver
                            printf("  %12"PRIu64" %12"PRIu64" 100%% %02u-%02u-%02u %02u:%02u -rw-r--r-- 00000000 m0 2.0\n",
                                   bytes, bytes,
                                   1,12,9, // date
                                   12,15);   // time




                        } // for streams

                    } // play title == OK

                } // get parts

            } // for titles

        } // get titles

    } // dvdnav?




    // UDF2.50, Bluray etc, uses dvdread
    if (dvdread) {

        dvd_list_recurse(dvdread, "/");

    } // dvdread




        printf("-------------------------------------------------------------------------------\n");

    // Cleanup and finish
    undvd_close_image(&dvdnav, &dvdread);

}



char *strjoin(char *a, char *b)
{
  char *result;
  int len_a, extra_slash;

  // Check if a ends with / or, b starts with /, if so dont
  // add a '/'.
  len_a = strlen(a);

  if ((a[ len_a - 1 ] != '/') && b[0] != '/')
    extra_slash = 1;
  else
    extra_slash = 0;



  result = (char *) malloc(len_a + strlen(b) + 1 + extra_slash);

  if (!result) {
    perror("malloc");
    exit(2);
  }

  if (extra_slash)
    sprintf(result, "%s/%s", a, b);
  else
    sprintf(result, "%s%s", a, b);

  return result;
}


void dvd_list_recurse(dvd_reader_t *dvdread, char *directory)
{
    dvd_dir_t *dvd_dir;
    dvd_dirent_t *dvd_dirent;
    char *path = NULL;

#ifndef UNDVD_NO_UDF250
    dvd_dir = DVDOpenDir(dvdread, directory);
    if (!dvd_dir) return;

    while((dvd_dirent = DVDReadDir(dvdread, dvd_dir))) {

        if (!strcmp((char *)dvd_dirent->d_name, ".")) continue;
        if (!strcmp((char *)dvd_dirent->d_name, "..")) continue;

        // Is it a regular file?
        if (dvd_dirent->d_type == DVD_DT_REG) {

            if (!strcmp("/", directory))
                printf(" %s\n",
                       dvd_dirent->d_name);
            else
                printf(" %s/%s\n",
                       &directory[1],
                       dvd_dirent->d_name);

            //     Size Packed Ratio  Date  Time  Attr  CRC  Meth Ver
            printf("  %12"PRIu64" %12"PRIu64" 100%% %02u-%02u-%02u %02u:%02u -rw-r--r-- 00000000 m0 2.0\n",
                   dvd_dirent->d_filesize,
                   dvd_dirent->d_filesize,
                   1,12,9, // date
                   12,15);   // time

        } else if (dvd_dirent->d_type == DVD_DT_DIR) {

            if (!strcmp("/", directory))
                printf(" %s\n",
                       dvd_dirent->d_name);
            else
                printf(" %s/%s\n",
                       &directory[1],
                       dvd_dirent->d_name);

            //     Size Packed Ratio  Date  Time  Attr  CRC  Meth Ver
            printf("  %12"PRIu64" %12"PRIu64"   0%% %02u-%02u-%02u %02u:%02u drw-r--r-- 00000000 m0 2.0\n",
                   0LL,
                   0LL,
                   1,12,9, // date
                   12,15);   // time

            path = strjoin(directory, (char *)dvd_dirent->d_name);
            dvd_list_recurse(dvdread, path);
            free(path); path = NULL;

        } // DIR

    } // while readdir

    DVDCloseDir(dvdread, dvd_dir);
#endif

} // dvd_dir










// =========================================================================
// ========================================================================
// =======================================================================
// ======================================================================
// =====================================================================
// ====================================================================
// ===================================================================



// Apparently MSVC++ don't like these defines
#define Xgetu32(b, l) ({													\
			uint32_t x = (b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]);	\
			b+=4;														\
			l-=4;														\
			x;															\
		})
#define getu32(b, l) (uint32_t)(b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]); b+=4; l-=4;

#define Xgetu16(b, l) ({													\
			uint16_t x = (b[0] << 8 | b[1]);							\
			b+=2;														\
			l-=2;														\
			x;															\
		})
#define getu16(b, l) (uint16_t)(b[0] << 8 | b[1]); b+=2;l-=2;


#define Xgetu8(b, l) ({													\
			uint8_t x = b[0];											\
			b+=1;														\
			l-=1;														\
			x;															\
		})
#define getu8(b, l) b[0]; b++; l--;													\


// This function is shamelessly lifted from lonelycoder's showtime.
// (Andreas Ãman)
int
dvd_pes_block(uint32_t audio_stream, uint32_t sc, uint8_t *buf, int len,
              uint8_t *sc_ptr)
{
    uint8_t flags, hlen, x;

    x     = getu8(buf, len);
    flags = getu8(buf, len);
    hlen  = getu8(buf, len);

    if(len < hlen)
        return 1;

    if((x & 0xc0) != 0x80) {
        /* no MPEG 2 PES */
        // usually padding packets
        if (sc != 0x1be)
            debugf("Not PES packet %x, remaining %d\n", x, len);
        return 2;
    }

    if((flags & 0xc0) == 0xc0) {
        if(hlen < 10)
            return 3;

        //pts = getpts(buf, len);
        //dts = getpts(buf, len);
        buf += 10;
        len -= 10;

        hlen -= 10;

    } else if((flags & 0xc0) == 0x80) {
        if(hlen < 5)
            return 4;

        //dts = pts = getpts(buf, len);
        buf += 5;
        len -= 5;

        hlen -= 5;
    }

    buf += hlen;
    len -= hlen;

    if(sc == PRIVATE_STREAM_1) {
        if(len < 1)
            return 6;

        sc_ptr = buf;
        sc = getu8(buf, len);

        //printf("Private stream 1 substream %x\n", sc);

    }

    if(sc > 0x1ff)
        return 8;

    if(sc >= 0x1e0 && sc <= 0x1ef) {
        //codec_id  = CODEC_ID_MPEG2VIDEO;
        //debugf("Video stream %x\n", sc - 0x1e0);

    } else if((sc >= 0x80 && sc <= 0x9f) ||
              (sc >= 0x1c0 && sc <= 0x1df)) {

        // audio
        //if((sc & 7) != track)
        //  return NULL;

        switch(sc) {
        //case 0x80 ... 0x87:  // MSVC++ can't do "..."
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
            //codec_id = CODEC_ID_AC3;
            //printf("Audio AC3 stream %x\n", sc-0x80);
            switch(audio_solution) {
            case 0: // Nothing
                break;
            case 1: // Patch packet 1
                if (!audio_seen) {
                    *sc_ptr = 0x80+audio_stream;
                    audio_seen = 1;
                }
                break;
            case 2: // Swap all streams
                break;
            }

	//        case 0x88 ... 0x9f:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x8f:
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0x98:
        case 0x99:
        case 0x9a:
        case 0x9b:
        case 0x9c:
        case 0x9d:
        case 0x9e:
        case 0x9f:
            //codec_id = CODEC_ID_DTS;
            //printf("Audio DTS stream %x\n", sc-0x88);
            switch(audio_solution) {
            case 0: // Nothing
                break;
            case 1: // Patch packet 1
                if (!audio_seen) {
                    *sc_ptr = 0x88+audio_stream;
                    audio_seen = 1;
                }
                break;
            case 2: // Swap all streams
                break;
            }
            break;

	//        case 0x1c0 ... 0x1df:
		case 0x1c0:
		case 0x1c1:
		case 0x1c2:
		case 0x1c3:
		case 0x1c4:
		case 0x1c5:
		case 0x1c6:
		case 0x1c7:
		case 0x1c8:
		case 0x1c9:
		case 0x1ca:
		case 0x1cb:
		case 0x1cc:
		case 0x1cd:
		case 0x1ce:
		case 0x1cf:
		case 0x1d0:
		case 0x1d1:
		case 0x1d2:
		case 0x1d3:
		case 0x1d4:
		case 0x1d5:
		case 0x1d6:
		case 0x1d7:
		case 0x1d8:
		case 0x1d9:
		case 0x1da:
		case 0x1db:
		case 0x1dc:
		case 0x1dd:
		case 0x1de:
		case 0x1df:

            //codec_id = CODEC_ID_MP2;
            //printf("Audio MP2 stream %x\n", sc-0x1c0);
            switch(audio_solution) {
            case 0: // Nothing
                break;
            case 1: // Patch packet 1
                if (!audio_seen) {
                    sc_ptr[0] = 0x00;
                    sc_ptr[1] = 0x00;
                    sc_ptr[2] = 0x01;
                    sc_ptr[3] = 0xc0+audio_stream;
                    audio_seen = 1;
                }
                break;
            case 2: // Swap all streams
                break;
            }

            break;
        default:
            return 9;
        }

    } else if (sc >= 0x20 && sc <= 0x3f) {

        // subtitle
        //printf("Subtitle stream %x\n", sc-0x20);

    } else {
        debugf("Unknown startcode %x\n", sc);
        return 10;
    }


  return 0;
}






static void
dvd_ps_block(uint32_t audio_stream, uint8_t *buf, int len)
{
	uint32_t startcode;
	int pes_len;
    int result;
    int8_t stuff = buf[13] & 7;
    uint8_t *sc_ptr;

	buf += 14;
	len -= 14;

	if(stuff != 0)
		return;

	while(len > 0) {

		if(len < 4)
			break;

        sc_ptr = buf;
		startcode = getu32(buf, len);
		pes_len = getu16(buf, len);

		if(pes_len < 3)
			break;
        //
        // Private stream 1 (Audio and subpictures)
        //
        // * sub-stream 0x20 to 0x3f are subpictures
        // * sub-stream 0x80 to 0x87 are audio (AC3, DTS, SDDS)
        // * sub-stream 0xA0 to 0xA7 are LPCM audio
        // case 0x80 ... 0x87:
        //   codec_id = CODEC_ID_AC3;
        // case 0x88 ... 0x9f:
        //   codec_id = CODEC_ID_DTS;
        // case 0x1c0 ... 0x1df:
        //   codec_id = CODEC_ID_MP2;
        //
		switch(startcode) {
		case PADDING_STREAM:
		case PRIVATE_STREAM_1:
		case PRIVATE_STREAM_2:
		//		case 0x1c0 ... 0x1df: // DVDs only allow 8
		case 0x1c0:
		case 0x1c1:
		case 0x1c2:
		case 0x1c3:
		case 0x1c4:
		case 0x1c5:
		case 0x1c6:
		case 0x1c7:
		case 0x1c8:
		case 0x1c9:
		case 0x1ca:
		case 0x1cb:
		case 0x1cc:
		case 0x1cd:
		case 0x1ce:
		case 0x1cf:
		case 0x1d0:
		case 0x1d1:
		case 0x1d2:
		case 0x1d3:
		case 0x1d4:
		case 0x1d5:
		case 0x1d6:
		case 0x1d7:
		case 0x1d8:
		case 0x1d9:
		case 0x1da:
		case 0x1db:
		case 0x1dc:
		case 0x1dd:
		case 0x1de:
		case 0x1df:
		//		case 0x1e0 ... 0x1ef: // DVDs only allow 1
		case 0x1e0:
		case 0x1e1:
		case 0x1e2:
		case 0x1e3:
		case 0x1e4:
		case 0x1e5:
		case 0x1e6:
		case 0x1e7:
		case 0x1e8:
		case 0x1e9:
		case 0x1ea:
		case 0x1eb:
		case 0x1ec:
		case 0x1ed:
		case 0x1ee:
		case 0x1ef:
			result = dvd_pes_block(audio_stream, startcode, buf, pes_len, sc_ptr);
			break;

		default:
            debugf("*** <klaxon> No idea %x </klaxon>\n", startcode);
			return;
		}

        len -= pes_len;
        buf += pes_len;

	}
}





uint64_t dvdnav_getblock(dvdnav_t *dvdnav, uint64_t bnum,
                              unsigned char **block,
                              uint64_t *seek_block,
                              int32_t audio_stream)
{
	dvdnav_status_t     result;
	int                 event, len;
    int i;
    uint8_t *blockptr;
    uint32_t pos = 0, xlen = 0;
    uint32_t seeking = 0;
    int32_t tt = 0,ptt=0;

    i = (int) bnum;

    while (1) {

#define CACHE
#ifdef CACHE
        // Remember the buffer position, only so we can cache free
        blockptr = *block;

		result = dvdnav_get_next_cache_block(dvdnav,
                                             block, &event, &len);
#else
		result = dvdnav_get_next_block(dvdnav,
                                             block, &event, &len);
#endif
		if(result != DVDNAV_STATUS_OK) return 0;

		switch(event) {
		case DVDNAV_BLOCK_OK:

            // Are we seeking to a block? Is we called search() we are now
            // "close" to the block, so silently discard blocks until
            // we get there
            if (seeking) {

                dvdnav_get_position(dvdnav, &pos, &xlen);
                debugf("Got block %u %u\n", pos, xlen);

                if ((uint64_t)pos < *seek_block)
                    break; // Discard block and read next

                // We are were we here!
                debugf("Finally at seek position!\n");
                *seek_block = 0;
                seeking = 0;
            }

            if (*seek_block && !seeking) {
				debugf("finally seeking towards block %"PRIu64"\n", *seek_block);
                dvdnav_sector_search(dvdnav, *seek_block+10, SEEK_SET);
                //dvdnav_sector_search(dvdnav, *seek_block, SEEK_SET);
                seeking = 1;
                break;
            }


            dvdnav_get_position(dvdnav, &pos, &xlen);
            debugf("Saving block %u %u: %u\n", pos, xlen, len);

            // Now, patch it for the audio stream.
			dvd_ps_block(audio_stream, *block, len);
            return len;
			break;

		case DVDNAV_NOP: //                       1
			debugf("Block %d NOP\n", i);
			break;
		case DVDNAV_STILL_FRAME: //               2
			debugf("Block %d STILL_FRAME\n", i);
            dvdnav_still_skip(dvdnav);
			break;
		case DVDNAV_SPU_STREAM_CHANGE: //         3
			debugf("Block %d SPU_STREAM_CHANGE\n", i);
			break;
		case DVDNAV_AUDIO_STREAM_CHANGE: //       4
			debugf("Block %d AUDIO_STREAM_CHANGE\n", i);
			break;
		case DVDNAV_VTS_CHANGE: //                5
			debugf("Block %d VTS_CHANGE\n", i);
			break;
		case DVDNAV_CELL_CHANGE: //               6
			debugf("Block %d CELL_CHANGE: %"PRIu64"\n", i,
                   dvdnav_get_current_time(dvdnav));

#if 0
            // Ok, we can only seek after we receive a CELL_CHANGE
            // but seeking only gets us "close".
            if (*seek_block && !seeking) {
				debugf("finally seeking towards block %"PRIu64"\n", *seek_block);
                dvdnav_sector_search(dvdnav, *seek_block, SEEK_SET);
                seeking = 1;
            }
#endif
			break;
		case DVDNAV_NAV_PACKET: //                7
			debugf("Block %d NAV_PACKET\n", i);

            // If we are in title 0, that means we are in MENU.
            dvdnav_current_title_info(dvdnav, &tt, &ptt);
            if (tt == 0) {
                debugf("Changed title to %d, therefor finished.\n", tt);
#ifdef CACHE
                if (blockptr != *block)
                    dvdnav_free_cache_block(dvdnav, blockptr);
#endif
                return 0;
            }
			break;
		case DVDNAV_STOP: //                      8
			debugf("Block %d STOP, hammer time\n", i);
            // If it was a cache block, give it back.
#ifdef CACHE
            if (blockptr != *block)
                dvdnav_free_cache_block(dvdnav, blockptr);
#endif
            return 0;
			break;
		case DVDNAV_HIGHLIGHT: //                 9
			debugf("Block %d HIGHLIGHT\n", i);
			break;
		case DVDNAV_SPU_CLUT_CHANGE: //          10
			debugf("Block %d SPU_CLUT_CHANGE\n", i);
			break;
		case DVDNAV_HOP_CHANNEL: //              12
			debugf("Block %d HOP_CHANNEL\n", i);
			break;
		case DVDNAV_WAIT: //                     13
			debugf("Block %d WAIT\n", i);
            dvdnav_wait_skip(dvdnav);
			break;

		default:
			debugf("unknown event %d\n", event);
			break;
		} // switch

        // If it was a cache block, give it back.
#ifdef CACHE
        if (blockptr != *block)
            dvdnav_free_cache_block(dvdnav, blockptr);
#endif

	} // while forever

    return 0;
}


char *undos(char *s)
{
    char *r;

    r = s;

    while((r = strchr(r, '\\')))
        *r = '/';

    return s;
}




void undvd_extract(char *archive, char *rarname,
                   char *extractname, FILE *ioptr)
{
	dvd_reader_t *dvdread = NULL;
	dvdnav_t *dvdnav = NULL;
    int32_t title, stream;
    uint64_t block = 0, seek_block, rbytes, modulus;
    unsigned char dvdblock[DVD_VIDEO_LB_LEN];
    unsigned char *blockptr = NULL;


    debugf("Attempting to extract '%s' from '%s' (in '%s') from position %"PRIu64"...\n",
           extractname,
           archive,
           rarname ? rarname : "",
           seek);


    if (undvd_open_image(&dvdnav, &dvdread,
                         archive,
                         rarname,
                         extractname)) {
            return;
        }


    undos(extractname); // convert \ to /


    if (dvdnav) { // DVD, parse out the title and audio stream

        if (sscanf(extractname, "title_%02u/audio_%02u_",
                   &title,
                   &stream) != 2) {
            printf("WARNING: Unable to extract title and audio stream from filename '%s'.\n",
                   extractname);
            goto finish;
        }

        debugf("title %02u audio stream %02u\n", title, stream);

        if (dvdnav_title_play(dvdnav, title) != DVDNAV_STATUS_OK) {
            printf("ERROR:libdvdnav says there is no title %02u\n", title);
            goto finish;
        }


        // With dvdnav we can seek to the block we want, but only after the
        // first CELL_CHANGE. Set it up to do so.
        seek_block = (uint64_t) (seek / DVD_VIDEO_LB_LEN);
		modulus = (seek) - (seek_block * 2048LL);
        debugf("Seek block set to %"PRIu64" modulus %"PRIu64".\n",
               seek_block,
               modulus);

        // if we ask to seek to 2052, we get block 1, starts byte 2048,
        // so modulus is "4", as the first block we get, we need to
        // start +4 bytes, for len-4. Otherwise, modulus is generally 0.


        // Read blocks until finished, really...

        block = 0; // Always start from the top, seek when we can.
        while (1) {

            blockptr = dvdblock;
            rbytes = dvdnav_getblock(dvdnav,
                                     block,
                                     &blockptr,
                                     &seek_block,
                                     stream);
            if (!rbytes) break; // All done.

            // Send it here
            fwrite(&blockptr[modulus],
                   DVD_VIDEO_LB_LEN - modulus,
                   1,
                   ioptr);
            modulus = 0;


            // If the blockptr returned was from cache, release our handle
#ifdef CACHE
            if (blockptr != dvdblock)
                dvdnav_free_cache_block(dvdnav, blockptr);
#endif
            block++;  // Is this even used.

			//            if (block>1500) break;

        } // forever

    } // dvdnav



    // dvdread, or udf2.50, or bluray
    if (dvdread) {
        dvd_file_t *dvdfile = NULL;
        uint64_t totalsize;

#ifndef UNDVD_NO_UDF250
        dvdfile = DVDOpenFilename(dvdread, extractname);
#endif
        if (!dvdfile) {
            printf("ERROR: Failed to locate '%s' inside '%s'\n",
                   extractname, archive);
            goto finish;
        }


        totalsize = DVDFileSize64(dvdfile);
        debugf("Total filesize to read %"PRIu64" bytes.\n",
               totalsize);

        // Read and send, really.
        seek_block = (uint64_t) (seek / DVD_VIDEO_LB_LEN);
		modulus = (seek) - (seek_block * 2048LL);
        debugf("Seek block set to %"PRIu64" modulus %"PRIu64".\n",
               seek_block,
               modulus);

        // Read blocks
        while (1) {

            // This always reads 2048
            rbytes = DVDReadBlocks(dvdfile,
                                   seek_block,
                                   1,
                                   dvdblock);
            if (!rbytes) break;

            rbytes *= 2048; // Size read in bytes

            // How much to write, 2048 or is file smaller?
            if (totalsize < DVD_VIDEO_LB_LEN)
                rbytes = totalsize;

            fwrite(&dvdblock[modulus],
                   rbytes - modulus,
                   1,
                   ioptr);
            modulus = 0;

            seek_block++; // Increase to next block.

            //if (seek_block > 300) break;

            totalsize -= rbytes;

            if (!totalsize) break;

        } // while forever

        // Close file
        DVDCloseFile(dvdfile);

    }





 finish:
    debugf("cleanup and exit..\n");

    // Cleanup and finish
    undvd_close_image(&dvdnav, &dvdread);
}


