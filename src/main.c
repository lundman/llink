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

#include "lion.h"
#include "misc.h"

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
#include "visited.h"
#include "cgicmd.h"
#include "cupnp.h"

#include "version.h"

#ifdef WIN32
// over-ride main, so we can do Service feature
#define main realmain
int windows_service = 0;
extern char *g_pzServiceName;
#endif

/*
** Jorgen Lundman <lundman@lundman.net> 2004/2005.
**
** This is an attempt to be a media streaming server compatible with the
** Buffalo Linktheater 2 Networked DVD Player, and
** IOData AVeL Linkplayer 2 Networked DVD Player.
** Syabas' Networked Media Tank, ie, Popcornhour, hdx, ..
** HDi's Dune
**
**
**
*/

extern int getopt();
extern char *optarg;
extern int optind;


int do_exit = 0;
static int foreground = 0;
static char *conf_pidfile = NULL;


void arguments(int argc, char **argv);





// Signal handler for INT and HUP
void exit_interrupt(void)
{
	do_exit = 1;
}




int main( int argc, char **argv )
{

	// Read any command line arguments from our friend the user.
	arguments(argc, argv);

	// Catch some signals so can exit cleanly

	signal(SIGINT, exit_interrupt);
#ifndef WIN32
	signal(SIGHUP,  exit_interrupt);
	signal(SIGTERM, exit_interrupt);
	signal(SIGPIPE, SIG_IGN);
    if (debug_on)
        setvbuf(stdout, NULL, _IONBF, 0);
#endif

	printf("%s - Jorgen Lundman v%s %s %s\n\n",
		argv ? argv[0] : "llink",
		   VERSION,
		   VERSION_STRING,
#if WITH_DVDREAD
		   "(libdvdread)"
#else

#if HAVE_CLINKC
		   "(libdvdnav, ClinkC)"
#else
		   "(libdvdnav)"
#endif
#endif
		   );


	lion_buffersize(conf_buffersize);
	//lion_buffersize(2352);

	if (lion_init()) {
		printf("Failed to initialize lion/networking\n");
		exit(-1);
	}

	debugf("\n");

	// Read configuration file, if any
	// Warning, calls lion_poll until done.
	conf_init();

	debugf("[main] initialising...\n");

	// Warning, calls lion_poll until done.
	mime_init();

	// Warning, calls lion_poll until done.
	skin_init();

	ssdp_init();

	httpd_init();

	request_init();

	root_init();

	llrar_init();

	visited_init();

	cgicmd_init();

#ifdef EXTERNAL
	external_init();
#endif

	printf("[main] ready!\n");

	// Background?
#ifndef WIN32
	if (!foreground) {
		if (fork()) exit(0);
		setsid();

        if (conf_pidfile) {
            FILE *fd;
            if ((fd = fopen(conf_pidfile, "w"))) {
                fprintf(fd, "%u\r\n", getpid());
                fclose(fd);
            }
        }
	}
#endif

    cupnp_init();


	// Scan for media
	if (conf_xmlscan) {
		signal(SIGINT, SIG_DFL);
		xmlscan_run();
		do_exit = 1;
	}


	query_init();

#ifdef WIN32
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif

	// The main loop
	while ( !do_exit ) {

        // Was 250,0 - we should probably let users control this in
        // conf settings.
		if (lion_poll(0, 1)<0) do_exit=1;

		request_events();

#ifdef EXTERNAL
		external_resume();
#endif

	}

	printf("\n[main] releasing resources...\n");

	query_free();

    cupnp_free();

#ifdef EXTERNAL
	external_free();
#endif

	root_free();

	cgicmd_free();

	visited_free();

	llrar_free();

	request_free();

	httpd_free();

	ssdp_free();

	skin_free();

	mime_free();

	conf_free();

#ifndef WIN32  // Crashed when releasing spawned processes, until i can fix:
	lion_free();
#endif

	debugf("[main] done.\n");

	return 0;

}





static void options(char *prog)
{

	printf("\n");
	printf("%s - Media server engine.\n", prog);
	printf("%s [-hd] [-f file] [...]\n\n", prog);
	printf("  options:\n");

	printf("  -h          : display usage help (this output)\n");
	printf("  -d          : stay in foreground, do not detach\n");
	printf("  -v <int>    : enable verbose debug information, 0 to list levels\n");
	printf("  -f <file>   : set configuration file to read (default: %s\n",
		   CONF_DEFAULT_FILE);
	printf("  -w <path>   : Change to directory after starting\n");
	printf("  -p <path>   : PID file location\n");
	printf("  -x          : Scan ROOTs for media to generate XML files for jukebox skin\n");
	printf("  -X          : Same as -x, but XML files are saved in redirect path\n");
	printf("  -s          : Syabas subtitle bug that sends filename.sub$garbage\n");
	printf("  -L          : disable LinkTheater fix, don't close keep-alive for errors\n");
	printf("  -V          : Force sending of optional childCount in UPNP. Required by VLC.\n");

#ifdef WIN32
	printf("\n  -I          : Install as a Service (start at boot)\n");
	printf("  -U          : Uninstall as a Service\n");
	printf("  -N          : Specify Service name, default 'llink'\n");
	printf("( -S          : Start as Service)\n");
	printf("\n The argument order for adding as a Service is important,\n");
	printf("by default it uses cwd as the path, so if you want to specify\n");
	printf("another directory first, use -w path before -I. All other arguments");
	printf("need to follow -I or they are not saved and used by Services\n");
	printf("%s [-w path] -I [other arguments]\n", prog);
#endif

	printf("\n\n(c) Jorgen Lundman <lundman@lundman.net>\n\n");

	exit(0);

}


static void debug_options(void)
{

	printf("  level  debug description\n");
	printf("    0    this message\n");
	printf("    1    general debug messages\n");
	printf("    2    ssdp debug\n");
	printf("    4    http requests\n");
	printf("    8    unrar messages\n");
	printf("   16    skin messages\n");
	printf("   32    root messages\n");
	printf("   64    extnfo messages\n");
	printf("  128    xmlscan messages\n");
	printf("  256    query messages (replies to SSDP)\n");
	printf("  512    libdvdread messages\n");
	printf(" 1024    visited db messages\n");
	printf(" 2048    cgicmd messages\n");
	printf(" 4096    upnp messages\n");


}


void arguments(int argc, char **argv)
{
	int opt;

	while ((opt=getopt(argc, argv,
					   "hdv:f:w:xXsLp:V"
#ifdef WIN32
					   "IUN:P"
#endif
					   )) != -1) {

		switch(opt) {

		case 'h':
			options(argv[0]);
			break;

		case 'd':
			foreground = 1;
			break;

		case 'v':
			debug_on=atoi(optarg);
			if (!debug_on) debug_options();
			break;

		case 'f':
			SAFE_DUPE(conf_file, optarg);
			break;

		case 'w':
			chdir(optarg);
			break;

		case 'p':
			SAFE_DUPE(conf_pidfile, optarg);
			break;

		case 'x':
			conf_xmlscan = 1;
			printf("This needs to be implemented, use -X\n");
			break;

		case 'X':
			conf_xmlscan = 2;
			break;

		case 's':
			conf_fixup_syabas_subtitles = 1;
			break;

		case 'L':
			conf_fixup_nokeepalive = 0;
			break;

		case 'V':
			conf_fixup_childCount = 1;
			break;

#ifdef WIN32
		case 'I':
			// When started as service, we demand -P dir before the rest, which
			// includes the -I we have to ignore.
			if (!windows_service) {
				windows_service = 1;
				InstallService(argc,argv);
				_exit(0);
			}
			break;
		case 'N':
			SAFE_DUPE(g_pzServiceName, optarg);
			break;
		case 'U':
			UninstallService();
			exit(0);
			break;
		case 'P':
			windows_service = 1;
			break;
#endif

		default:
			printf("Unknown option. '%c'\n", opt);
			options(argv[0]);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	// argc and argv adjusted here.



}



//
// This is the default lion handler.
//
// It is only defined for linking purposes and we do not use this design-
// style when using lion. Rather, we call lion_set_andler() for each handle
// we have.
//
int lion_userinput(lion_t *handle, void *user_data,
				 int status, int size, char *line)
{

	debugf("[main] default handler called for %8p status %d size %d line %s\n",
		   handle, status, size, line ? line : "(null)");

	return 0;
}
