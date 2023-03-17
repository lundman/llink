AC_INIT([llink],[2.3.2], [bug-report@lundman.net])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign])
# The big search for OpenSSL
# copied from openssh's configure.ac
tryssldir=""
AC_ARG_WITH(ssl-dir,
        [  --with-ssl-dir=PATH     Specify path to OpenSSL installation ],
        [
                if test "x$withval" != "xno" ; then
                        tryssldir=$withval
                fi
        ]
)
AC_PROG_CC

AC_CHECK_PROG(BUILD, build, build)

# Run ./configure in lion
AC_CONFIG_SUBDIRS([lion])

# Run ./configure in unrar
AC_CONFIG_SUBDIRS([unrar-3.7.8-seek])

#AC_CHECK_FILE([lion/src/lion.h], ,
#   AC_MSG_ERROR([lion.h not found in lion/src/ directory]))

CFLAGS="$CFLAGS -I../lion/src/ -I../lion/contrib/libdirlist/"

AC_TYPE_SIGNAL

AC_HEADER_TIME

AC_CHECK_HEADERS([stdarg.h varargs.h sys/time.h sys/types.h dirent.h sys/file.h sys/param.h sys/statfs.h sys/vfs.h sys/statvfs.h errno.h libgen.h pwd.h])

# mount depends on param
AC_CHECK_HEADERS([sys/mount.h], [], [], [#if HAVE_SYS_PARAM_H
                                         #include <sys/param.h>
                                         #endif])

AC_STRUCT_DIRENT_D_TYPE

AH_TEMPLATE([NEED_SYS_ERRLIST_DECLARED],
        [Define this if sys_errlist must be declared (if it exists).])

AC_SYS_LARGEFILE



AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([socket], [socket inet])
AC_SEARCH_LIBS([crypt], [crypt])
AC_SEARCH_LIBS([dlopen], [dl])

AC_CHECK_FUNC([snprintf],
                [AC_DEFINE(HAVE_SNPRINTF,1,[Presence of snprintf found])],
                [AC_MSG_WARN(Adding internal snprintf function)])

AC_CHECK_FUNC([strcasestr],
                [AC_DEFINE(HAVE_STRCASESTR,1,[Presence of strcasestr found])],
                [])

if test "x$ac_cv_prog_gcc" = xyes ; then
   CFLAGS="$CFLAGS -Wall -g "
fi


AC_CHECK_FUNCS([strtoul strtouq strtoull strtoll])

AC_ARG_WITH(dvdcss,
  [  --with-dvdcss=PATH      libdvdcss headers and libraries])
AC_ARG_WITH(dvdcss-tree,
  [  --with-dvdcss-tree=PATH libdvdcss tree for static linking])


dnl ------------------------------------------------------
dnl Where do you live, openssl?  And how do we call you?
AC_ARG_WITH([openssl],
            [AS_HELP_STRING([--without-openssl],[Disable openssl support])],
            [],
            [with_openssl=yes])

dnl If SSL was NOT disable, then go look for it.
if test x"$with_openssl" != x"no" ; then

AC_CACHE_CHECK([for OpenSSL directory], ac_cv_openssl_dir, [
  saved_LIBS="$LIBS"
  saved_LDFLAGS="$LDFLAGS"
  saved_CPPFLAGS="$CPPFLAGS"
  ssl_found=no
  for ssldir in $tryssldir "" $prefix /usr/local/openssl /opt/local /usr/lib/openssl /usr/local/ssl /usr/lib/ssl /usr/local /usr/athena /usr/pkg /opt /opt/openssl ; do
    LDFLAGS="$saved_LDFLAGS"
    LIBS="$saved_LIBS -lssl -lcrypto"

    # Skip the directory if it isn't there.
    if test ! -z "$ssldir" -a ! -d "$ssldir" ; then
       continue;
    fi
    if test ! -z "$ssldir" ; then
      if test -d "$ssldir/lib" ; then
        LDFLAGS="-L$ssldir/lib $LDFLAGS"
      else
        LDFLAGS="-L$ssldir $LDFLAGS"
      fi
      if test -d "$ssldir/include" ; then
        CPPFLAGS="-I$ssldir/include $CPPFLAGS"
      else
        CPPFLAGS="-I$ssldir $CPPFLAGS"
      fi
    fi
    # Can I link it?
    AC_TRY_LINK([#include <openssl/rand.h>],
       [ RAND_add((void*)0,0,0); ],
       [ openssl_linked=yes ], [ openssl_linked=no ])
    if test $openssl_linked = yes; then
       if test ! -z "$ssldir" ; then
         ac_cv_openssl_dir=$ssldir
       else
         ac_cv_openssl_dir="(system)"
       fi
       ssl_found=yes
       break
    fi
  done
  LIBS="$saved_LIBS"
  LDFLAGS="$saved_LDFLAGS"
  CPPFLAGS="$saved_CPPFLAGS"
  if test $ssl_found = no ; then
    AC_MSG_ERROR([Could not find a linkable OpenSSL. You can specify an explicit path using --with-ssl-dir])
  fi
])
AC_DEFINE([WITH_SSL], 1, [Define if we found OpenSSL - Legacy name])

LIBS="$LIBS -lssl -lcrypto"
if test "$ac_cv_openssl_dir" != "(system)"; then
  if test -d "$ac_cv_openssl_dir/lib" ; then
    LDFLAGS="-L$ac_cv_openssl_dir/lib $LDFLAGS"
    ssl_libdir="$ac_cv_openssl_dir/lib"
  else
    LDFLAGS="-L$ac_cv_openssl_dir $LDFLAGS"
    ssl_libdir="$ac_cv_openssl_dir"
  fi
  if test -d "$ac_cv_openssl_dir/include" ; then
    CPPFLAGS="-I$ac_cv_openssl_dir/include $CPPFLAGS"
  else
    CPPFLAGS="-I$ac_cv_openssl_dir $CPPFLAGS"
  fi
fi

if test -z $CROSS_COMPILE
then
AC_CACHE_CHECK([whether we need extra options to link OpenSSL],
               ac_cv_openssl_linker_option, [
    saved_LDFLAGS="$LDFLAGS"
    ssl_runs=no
    linked_with=nothing
    for ssl_extra in "" "-Wl,-R$ssl_libdir" "-R$ssl_libdir" ; do
      LDFLAGS="$ssl_extra $saved_LDFLAGS"
      AC_TRY_RUN([
#include <string.h>
#include <openssl/rand.h>
int main(void)
{
        char a[2048];
        memset(a, 0, sizeof(a));
        RAND_add(a, sizeof(a), sizeof(a));
        return(RAND_status() <= 0);
}
                        ],
                openssl_runs=yes, openssl_runs=no)
      if test $openssl_runs = yes ; then
        if test "$linked_with" = nothing; then
           linked_with="$ssl_extra"
        fi
        AC_TRY_RUN([
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
int main(void) {
return (OPENSSL_VERSION_NUMBER == SSLeay()) == 0;
}],
        right_version=yes, right_version=no)
        if test "$right_version" = yes; then
          if test -z "$ssl_extra" ; then
            ac_cv_openssl_linker_option='(none)'
          else
            ac_cv_openssl_linker_option=$ssl_extra
          fi
          ssl_runs=yes
          break
        fi
     fi
    done
    if test $ssl_runs = no ; then
      if test "$linked_with" = 'nothing' ; then
        AC_MSG_ERROR([Found linkable OpenSSL in $ac_cv_openssl_dir, but it doesn't run, even with -R.  Maybe specify another using --with-ssl-dir?])
      else
        if test -z "$linked_with" ; then
            ac_cv_openssl_linker_option='(none)'
        else
            ac_cv_openssl_linker_option=$linked_with
        fi
        AC_MSG_WARN([I managed to make OpenSSL link and run, but I couldn't make it link against the same version I found header files for.])
      fi
    fi
    LDFLAGS="$saved_LDFLAGS"
])

if test "$ac_cv_openssl_linker_option" != '(none)' ; then
   LDFLAGS="$ac_cv_openssl_linker_option $LDFLAGS"
fi
fi

dnl if NOSSL
fi

dnl static build?
AC_ARG_ENABLE(static,
   [  --enable-static         Enable static linking     [[default=no ]] ],
      static="$enableval", static="no")

AC_MSG_CHECKING([for --enable-static])
AC_MSG_RESULT($static)
if test $static == yes; then
  LDFLAGS="$LDFLAGS -static";
  AC_DEFINE([STATIC_BUILD], [], [Define when making a static build])
fi

AC_ARG_ENABLE(dvdcss,
[  --enable-dvdcss         dvdcss input module (default disabled)])
if test "${enable_dvdcss}" != "no"
then
  AC_ARG_WITH(dvdcss,
  [  --with-dvdcss=PATH      libdvdcss headers and libraries])
  AC_ARG_WITH(dvdcss-tree,
  [  --with-dvdcss-tree=PATH libdvdcss tree for static linking])


  if test -z "${with_dvdcss}"
  then
    if test -z "${with_dvdcss_tree}"
    then
      AC_CHECK_HEADERS(dvdcss/dvdcss.h,
        [
          LIBS="$LIBS -ldvdcss"
        ],[
          if test -n "${enable_dvdcss}"
          then
            AC_MSG_ERROR([cannot find libdvdcss headers])
          fi
        ])
    else
      AC_MSG_CHECKING(for libdvdcss.a in ${with_dvdcss_tree})
      real_dvdcss_tree="`cd ${with_dvdcss_tree} 2>/dev/null && pwd`"
      if test -z "${real_dvdcss_tree}"
      then
        dnl  The given directory can't be found
        AC_MSG_RESULT(no)
        AC_MSG_ERROR([cannot cd to ${with_dvdcss_tree}])
      fi
      if test -f "${real_dvdcss_tree}/src/.libs/libdvdcss.a"
      then
        dnl  Use a custom libdvdcss
        AC_MSG_RESULT(${real_dvdcss_tree}/src/.libs/libdvdcss.a)
                LIBS="$LIBS -ldvdcss"
                LDFLAGS="$LDFLAGS -L${real_dvdcss_tree}/src/.libs/"
                CPPFLAGS="$CPPFLAGS -I${real_dvdcss_tree}/src/"
      else
        dnl  The given libdvdcss wasn't built
        AC_MSG_RESULT(no)
        AC_MSG_ERROR([cannot find ${real_dvdcss_tree}/src/.libs/libdvdcss.a, make sure you compiled libdvdcss in ${with_dvdcss_tree}])
      fi
    fi
  else
    AC_MSG_CHECKING(for dvdcss headers in ${with_dvdcss})
    if test -f ${with_dvdcss}/dvdcss/dvdcss.h
    then
      dnl  Use ${with_dvdcss}/dvdcss/dvdcss.h
      AC_MSG_RESULT(yes)
          LIBS="$LIBS -ldvdcss"
          LDFLAGS="$LDFLAGS -L${with_dvdcss}/lib"
          CPPFLAGS="$CPPFLAGS -I${with_dvdcss}/include"
    else
      dnl  No libdvdcss could be found, sorry
      AC_MSG_RESULT(no)
      AC_MSG_ERROR([cannot find ${with_dvdcss}/include/dvdcss/dvdcss.h])
    fi
  fi
fi
AC_CHECK_HEADERS(dvdcss/dvdcss.h)
dnl end libdvdcss

dnl ---------------------------------------------
dnl threads
dnl ---------------------------------------------


AC_ARG_ENABLE(clinkc, [AC_HELP_STRING([--enable-clinkc],[enable clinkc library for UPNP])], have_clinkc=$enableval, have_clinkc=no)
if test "$have_clinkc" = "$enableval"; then
	AC_DEFINE(HAVE_CLINKC,1,[Define to enable clinkc])
fi
AM_CONDITIONAL(HAVE_CLINKC, test "$have_clinkc" = "$enableval")



AM_CONFIG_HEADER(config.h)
AC_CONFIG_FILES([Makefile src/Makefile src/skin/Makefile win32/Makefile win32/llink/Makefile src/skin/moon720/Makefile src/skin/aquarius_720/Makefile src/skin/aquarius_720/clt720img/Makefile src/skin/jukebox/Makefile src/skin/jukebox/image-hd/Makefile src/skin/SD_jukebox/Makefile src/skin/SD_jukebox/image-sd/Makefile src/skin/blackskin720/Makefile src/skin/kamaishi_skies/Makefile src/skin/nmt/Makefile src/skin/nmt/my-img/Makefile src/skin/nmt/nmt-img/Makefile src/skin/SD_nmt/Makefile src/skin/SD_nmt/my-img/Makefile src/skin/SD_nmt/nmt-img/Makefile src/skin/clterm720/Makefile src/skin/clterm720/clt720img/Makefile src/skin/upnp/Makefile undvd/Makefile undvd/win32/Makefile undvd/win32/undvd/Makefile])
AC_OUTPUT

