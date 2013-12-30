
bin_PROGRAMS=undvd

undvd_SOURCES=undvd.c undvd.h debug.c debug.h lfnmatch.h lfnmatch.c

undvd_CPPFLAGS=-I$(top_srcdir)/libdvdnav-4.1.2/src
undvd_LDADD=$(top_srcdir)/libdvdnav-4.1.2/src/.libs/libdvdnav.a
undvd_LDADD+=$(top_srcdir)/libdvdnav-4.1.2/src/dvdread/.libs/libdvdread.a

EXTRA_DIST=README.txt

SUBDIRS=win32
