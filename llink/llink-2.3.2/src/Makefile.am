
SUBDIRS=skin

bin_PROGRAMS=llink

llink_SOURCES=conf.c conf.h debug.c debug.h external.c external.h file.c \
   file.h httpd.c httpd.h main.c mime.c mime.h parser.c parser.h \
   process.c process.h query.c query.h request.c request.h root.c root.h \
   skin.c skin.h ssdp.c ssdp.h version.h llrar.c llrar.h extra.c extra.h \
   xmlscan.c xmlscan.h visited.h visited.c cgicmd.c cgicmd.h \
   cupnp.c cupnp.h cdms_filesys.c cdms_filesys.h cdms_filesys_http.c

llink_CPPFLAGS = -I$(top_srcdir)/lion/src -I$(top_srcdir)/lion/contrib/libdirlist
llink_LDADD=$(top_builddir)/lion/src/liblion.a $(top_builddir)/lion/contrib/libdirlist/libdirlist.a

llinkdir=$(prefix)/etc

llink_LDADD+= -lgdbm

if HAVE_CLINKC
llink_LDADD += -lclinkc -lclinkcav
endif


EXTRA_DIST=llink.conf win32.c win32.h jukebox.conf win32_dirent.h
EXTRA_DIST+=getopt.c w32service.c blackskin720.conf myscript.sh mime.types


# Automatically increase build version if found.
all-local:
	@if test -n "$(BUILD)"; then "$(BUILD)" ; fi

tags:
	etags $(llink_SOURCES) win32.c win32.h getopt.c w32service.c

install-data-hook:
	@$(INSTALL_PROGRAM) -d "$(DESTDIR)/$(sysconfdir)/llink"
	@echo $(INSTALL_PROGRAM) -d "$(DESTDIR)/$(sysconfdir)/llink"
	@cp -Rf "$(srcdir)/skin" "$(DESTDIR)/$(sysconfdir)/llink" || echo OK
	@if test -f "$(DESTDIR)/$(sysconfdir)/llink/llink.conf"; then \
		echo "$(DESTDIR)/$(sysconfdir)/llink/llink.conf" "exists, I will NOT over-write it" ;\
	else \
		$(INSTALL_PROGRAM) "$(srcdir)/blackskin720.conf" "$(DESTDIR)/$(sysconfdir)/llink" ;\
		$(INSTALL_PROGRAM) "$(srcdir)/jukebox.conf" "$(DESTDIR)/$(sysconfdir)/llink" ;\
		$(INSTALL_PROGRAM) "$(srcdir)/llink.conf" "$(DESTDIR)/$(sysconfdir)/llink" ;\
		echo "The default conf file has been installed, please modify $(DESTDIR)/$(sysconfdir)/llink/llink.conf as required." ;\
	fi
	@echo "You can start llink by using: "
	@echo "$(prefix)/bin/llink -w $(prefix)/etc/llink"

