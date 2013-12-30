#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "lion.h"
#include "misc.h"

#include "request.h"
#include "skin.h"
#include "debug.h"

static int external_handler(lion_t *handle, void *user_data, 
							int status, int size, char *line);


#define LAUNCH_TEMPLATE "llink-external-fifo.XXXXXX"








int external_init(void)
{
	return 0;
}


void external_free(void)
{

}







//
// [1] Create fifo
// [2] Launch external command
// [3] Change ->disk_path to be fifo's name
//
int external_launch(request_t *node, skin_t *skin)
{
	char *tmpname;
	char *args;
	int ret, size;
#ifdef WIN32
	HANDLE readhandle;
#endif

	tmpname = strdup(LAUNCH_TEMPLATE);
	if (!mktemp(tmpname)) {
		request_reply(node, 400, "Temporary memory issue");
		SAFE_FREE(tmpname);
		return 1;
	}
	
	// Create it.
#ifndef WIN32
	ret = mkfifo(tmpname, 0600);

	if (ret) {
		request_reply(node, 400, "mkfifo failed");
		SAFE_FREE(tmpname);
		return 2;
	}
#else

	// Create a named pipe, where extern program "writes" to
	// and we (eventually) read from.

	readhandle = CreateNamedPipe( 
								 tmpname,             // pipe name 
								 PIPE_ACCESS_INBOUND, // read/write access 
								 PIPE_TYPE_BYTE |     // byte type pipe 
								 PIPE_READMODE_BYTE | // message-read mode 
								 PIPE_WAIT,           // blocking mode 
								 PIPE_UNLIMITED_INSTANCES, // max. instances  
								 65536,               // output buffer size 
								 65536,               // input buffer size 
								 NMPWAIT_USE_DEFAULT_WAIT, // client time-out 
								 NULL);         // default security attribute
	
	if (readhandle == INVALID_HANDLE_VALUE) {
		request_reply(node, 400, "CreateNamedPipe failed");
		SAFE_FREE(tmpname);
		return 10;
	}

	// Save it..
	node->pipe_handle = readhandle;

#endif


	// Launch application using it.
	// We need to build up the launcher string
	size = strlen(skin->args) + 2 + strlen(node->disk_path) + 3 +
		strlen(tmpname) + 1 + 1;

	args = (char *) malloc(size);
	if (!args) {
		request_reply(node, 400, "out of memory");
		SAFE_FREE(tmpname);
		return 2;
	}

    *args = 0;

	snprintf(args, size, "%s \"%s\" \"%s\"",
			 skin->args,
			 node->disk_path,
			 tmpname);
	


	// Incase we launch and fail, we call remove(disk_path) so
	// we want to clear it out here.
	SAFE_FREE(node->disk_path);

	// Launch
	debugf("[external] launching '%s'\n", args);

	node->external_handle = lion_system(args,
										1,
										LION_FLAG_NONE,
										(void *) node);

	if (node->external_handle)
		lion_set_handler(node->external_handle, external_handler);


	SAFE_FREE(args);

	if (!node->external_handle) {
		debugf("[external] spawn failed: %d:%s\n",
			   errno, strerror(errno));
		request_reply(node, 400, "failed to spawn external command");
		SAFE_FREE(tmpname);
		return 3;
	}


	// When we stream, or rather, send items without knowing its final
	// size, it is not possible to use Keep-Alive. We need to change the
	// request to Closed type.
	node->type |= REQUEST_TYPE_CLOSED;
	node->type &= ~REQUEST_TYPE_KEEPALIVE;



	// Change over the filename
	node->disk_path = tmpname;
	tmpname = NULL;

	return 0;

}









static int external_handler(lion_t *handle, void *user_data, 
							int status, int size, char *line)
{
	request_t *node;
	
	node = (request_t *) user_data;
	if (!node) return 0;


	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[external] command is executing...\n");
		break;


	case LION_PIPE_FAILED:
		debugf("[external] command failed: %s\n", 
			   line ? line : "(null)");
		/* fall-through */
	case LION_PIPE_EXIT:
		debugf("[external] command finished: %d\n", size);

		node->external_handle = NULL;

		if (node->disk_path) {
			// Lets hope this will always point to a tmp file.
			debugf("[external] deleting '%s'\n", node->disk_path);
			remove(node->disk_path);
		}

		break;

	case LION_BINARY:
	case LION_INPUT:
		debugf("[external] %s\n", line);
		break;

	case LION_BUFFER_EMPTY:
	case LION_BUFFER_USED:
		break;
	}

	return 0;
}






int external_resume_sub(lion_t *handle, void *arg1, void *arg2)
{
	request_t *node;

	if (lion_get_handler(handle) != request_handler) return 1;

	node = lion_get_userdata(handle);

	if (!node || !node->read_disabled) return 1;

	if (!node->althandle) return 1;

	debugf("[external] re-enabling read on '%s'\n", node->disk_path);

	lion_enable_read(node->althandle);

	return 1;
}




//
// For windows platforms, we can't use fifos/pipes, so we have to create
// a file to read, but we might read faster than the external script writes
// at least until the network buffers are full, so we tell lion not to close
// at EOF and disable read.. we need to re-enable read here to try again.
void external_resume(void)
{

	lion_find(external_resume_sub, NULL, NULL);

}

