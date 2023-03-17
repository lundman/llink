
bin_PROGRAMS=undvd

undvddir=$(prefix)/etc

undvd_SOURCES=undvd.c undvd.h debug.c debug.h lfnmatch.h lfnmatch.c rarinput.h rarinput.c

undvd_CPPFLAGS=`dvdread-config --cflags` `dvdnav-config --cflags`
undvd_LDFLAGS=`dvdread-config --libs` `dvdnav-config --libs`

EXTRA_DIST=README.txt

SUBDIRS=win32

# We also install 'undvd' in the main llink, for nicety.
install-data-hook:
	@$(INSTALL_PROGRAM) -C "$(builddir)/undvd" "$(DESTDIR)/$(sysconfdir)/llink"
