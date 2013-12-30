#if HAVE_CONFIG_H
#include <config.h>
#endif


#include "debug.h"

//	printf("    1    general debug messages\n");
//	printf("    2    ssdp debug\n");
//	printf("    4    http requests\n");
//	printf("    8    unrar messages\n");
//	printf("   16    skin messages\n");
//	printf("   32    root messages\n");
//	printf("   64    extnfo messages\n");
//	printf("  128    xmlscan messages\n");
//	printf("  256    query messages, replies to ssdp\n");
//	printf("  512    libdvdread messages\n");
//	printf(" 1024    visited db messages\n");
//	printf(" 2048    cgicmd messages\n");
//	printf(" 4096    upnp messages\n");

int debug_on = 0;

