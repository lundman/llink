#define _WINDOWS_LEAN_AND_MEAN
#include <windows.h>

#include "autoconf.h"
#include "../systems.h"

int win32_lock(int fd, int oper)
{
	OVERLAPPED offset;
    DWORD flags = 0, len = 0xffffffff;

	if (oper | LOCK_EX)
		flags |= LOCKFILE_EXCLUSIVE_LOCK;
	if (oper | LOCK_NB)
		flags |= LOCKFILE_FAIL_IMMEDIATELY;

	memset (&offset, 0, sizeof(offset));
	/* Syntax is correct, len is passed for LengthLow and LengthHigh*/
    if (!LockFileEx((HANDLE)_get_osfhandle(fd), flags, 0, len, len, &offset))
    	return -1;
  
	return 0;
}
  
int win32_unlock(int fd)
{
	OVERLAPPED offset;
    DWORD len = 0xffffffff;
  
    memset (&offset, 0, sizeof(offset));
    /* Syntax is correct, len is passed for LengthLow and LengthHigh*/
    if (!UnlockFileEx((HANDLE)_get_osfhandle(fd), 0, len, len, &offset))
        return -1;
  
    return 0;
}

int flock(int fd, int oper)
{

	if (oper | LOCK_UN)
		return win32_unlock(fd);

	return win32_lock(fd, oper);
}
