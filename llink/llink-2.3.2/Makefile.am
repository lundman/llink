SUBDIRS=lion src win32 unrar-3.7.8-seek undvd

EXTRA_DIST=LICENSE LICENSE.unrar LICENSE.libdvdread Example-playlist.txt README.txt README_Win32.txt README.upnp llink-120x120.jpg llink-120x120.png llink-48x48.jpg llink-48x48.png

install-data-hook:
	for i in llink-120x120.jpg llink-120x120.png llink-48x48.jpg llink-48x48.png; \
	do cp -f "$(srcdir)/$$i" "$(DESTDIR)/$(sysconfdir)/llink/" || echo OK ;\
	done
