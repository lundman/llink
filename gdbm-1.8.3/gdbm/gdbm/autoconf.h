// Special autoconf.h file used only for Win32 builds.
#define _CRT_SECURE_NO_WARNINGS
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_FSYNC 1
#define HAVE_FCNTL_H 1
#define HAVE_FLOCK 1

#include <io.h>

#define lseek _lseek
#define write _write
#define unlink _unlink
#define fsync(X) do { } while(0)

