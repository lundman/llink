
lib_LIBRARIES=libgdbm.a libndbm.a libdbm.a

bin_PROGRAMS=testgdbm testdbm testndbm conv2gdbm

libgdbm_a_SOURCES=gdbmopen.c gdbmdelete.c gdbmfetch.c gdbmstore.c		\
                                   gdbmclose.c gdbmreorg.c gdbmseq.c	\
                                   gdbmsync.c gdbmerrno.c gdbmexists.c	\
                                   gdbmfdesc.c gdbmsetopt.c bucket.c	\
                                   falloc.c findkey.c global.c hash.c	\
                                   update.c version.c dbm.h extern.h	\
                                   gdbm.h gdbmconst.h gdbmdefs.h		\
                                   gdbmerrno.h getopt.h ndbm.h proto.h	\
                                   systems.h

libndbm_a_SOURCES=dbmopen.c dbmdelete.c dbmfetch.c dbmstore.c dbmseq.c \
                                   dbmclose.c dbmdirfno.c dbmpagfno.c dbmrdonly.c

libdbm_a_SOURCES=dbminit.c delete.c fetch.c store.c seq.c close.c

testgdbm_SOURCES=testgdbm.c
testgdbm_LDADD=libgdbm.a

testdbm_SOURCES=testdbm.c
testdbm_LDADD=libdbm.a libgdbm.a

testndbm_SOURCES=testndbm.c
testndbm_LDADD=libndbm.a libgdbm.a

conv2gdbm_SOURCES=conv2gdbm.c
conv2gdbm_LDADD=libdbm.a libgdbm.a

EXTRA_DIST=ChangeLog INSTALL NEWS README

SUBDIRS=gdbm
