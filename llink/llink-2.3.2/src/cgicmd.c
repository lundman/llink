#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#if HAVE_LIBGEN_H
#include <libgen.h>
#endif

#ifdef WIN32
#include <errno.h>
#else
#include <dirent.h> /* For what opendir() returns, see man opendir */
#endif // WIN32


#include "lion.h"
#include "misc.h"
#include "lfnmatch.h"

#define DEBUG_FLAG 2048
#include "debug.h"
#include "conf.h"
#include "parser.h"
#include "request.h"
#include "root.h"
#include "cgicmd.h"
#include "skin.h"

#ifdef WIN32
#include "win32.h"
#endif


int cgicmd_background_main(lion_t *parent_handle, void *user_data, void *arg);
void cgicmd_bg_del(lion_t *parent, int *do_quit, char *path);
void cgicmd_bg_unrar(lion_t *handle, int *do_quit, char *path);
void cgicmd_bg_script(lion_t *handle, int *do_quit, char *path);
int cgicmd_bg_subhandler(lion_t *handle, void *user_data, 
						 int status, int size, char *line);



THREAD_SAFE static cgicmd_script_t *cgicmd_script_head = NULL;





int cgicmd_init(void)
{

	return 1;
}


void cgicmd_free(void)
{
	cgicmd_script_t *next;

	while(cgicmd_script_head) {
		next = cgicmd_script_head->next;
		SAFE_FREE(cgicmd_script_head->name);
		SAFE_FREE(cgicmd_script_head->path);
		SAFE_FREE(cgicmd_script_head);
		cgicmd_script_head = next;
	}

}













//
// This is a handler for all background tasks. It is used to send the
// command to the child, and to receive occasional STATUS messages back.
//
int cgicmd_handler(lion_t *handle, void *user_data, 
							  int status, int size, char *line)
{
	char *cmd = (char *) user_data;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[cgicmd] child is alive: %s\n", 
			   cmd ? cmd : "(null)");

		// Child is ready, send the command over.
		if (cmd)
			lion_printf(handle, "%s\n", cmd);
		break;
	case LION_PIPE_FAILED:
		debugf("[cgicmd] child failed: %s\n",
			   cmd ? cmd : "(null)");
		SAFE_FREE(cmd);
		break;
	case LION_PIPE_EXIT:
		debugf("[cgicmd] child exited: %s\n",
			   cmd ? cmd : "(null)");
		SAFE_FREE(cmd);
		break;
	case LION_INPUT:
		debugf("[cgicmd] input '%s'\n", line);

		// STATUS messages back from child, update status.
		if ((size > 7) && !mystrccmp("STATUS:", line)) {
			request_set_status(&line[7]);
			break;
		}
		break;
	}
	return 0;
}


void cgicmd_start_cmd(char *cstr, char *path)
{
	// Command 'DEL "filename"'.
	int len;
	char *cmd;
	lion_t *handle;

	len = strlen(cstr) + // 'DEL' or 'UNRAR'
		1 +             // " " space.
		strlen(path) +  // /path/dest
		1;    // terminating null

	cmd = malloc(sizeof(char) * len);
	if (!cmd) return; // FIXME

	snprintf(cmd, len, "%s %s", cstr, path);

	handle = lion_fork(cgicmd_background_main, LION_FLAG_NONE,
									  (void *)cmd, NULL);
	if (!handle) {
		debugf("[cgicmd] failed to work eh..\n");
		// FIXME
		return;
	}

	lion_set_handler(handle, cgicmd_handler);

}







void cgicmd_delete(request_t *node, char *path)
{
	request_t *tnode;

	// Since we deal with disk items, we need to allocate a new
	// request_t node, set the path and call root_setpath() to have
	// it expanded to the real disk path.
	tnode = request_newnode();
	if (!tnode) return;

	tnode->path = strdup(path);
	if (!tnode->path) return;

	if (root_setpath(tnode)) {
		request_freenode(tnode);
		request_set_status("Delete failed: stat");
		return;
	}

	debugf("[cgicmd] DELETE on '%s'\n", tnode->disk_path);

	if (!S_ISDIR(tnode->stat.st_mode)) {
		// Delete FILE

		if (conf_del_file == CONF_DEL_NO) {
			request_set_status("File deletion disabled in conf");
			debugf("[cgicmd] File deletion disabled in conf\n");
			request_freenode(tnode);
			return;
		}
		if ((conf_del_file == CONF_DEL_PIN) &&
			(!root_isregistered(node))) {
			request_set_status("Enter PIN for deletion");
			debugf("[cgicmd] Missing PIN\n");
			request_freenode(tnode);
			return;
		}

		if (!unlink(tnode->disk_path)) {
			request_set_status("File deleted.");
		} else {
			request_set_status("File deletion failed");
		}
		request_freenode(tnode);
		return;
	}


	// Directory.

	if (conf_del_dir == CONF_DEL_NO) {
		request_set_status("Directory deletion disabled in conf");
		debugf("[cgicmd] Deletion disabled in conf\n");
		request_freenode(tnode);
		return;
	}
	if ((conf_del_dir == CONF_DEL_PIN) &&
		(!root_isregistered(node))) {
		request_set_status("Enter PIN for deletion");
		debugf("[cgicmd] Missing PIN\n");
		request_freenode(tnode);
		return;
	}
	
	if (!rmdir(tnode->disk_path)) {
		request_set_status("Directory deleted");
		request_freenode(tnode);
		return;
	}

	// If not recursive, return failure. If recursive, and error is
	// something like ENOTEMPTY, start recursinating!
	if (errno != ENOTEMPTY) {
		request_set_status("Directory deletion failed");
		request_freenode(tnode);
		return;
	}


	if (conf_del_recursive == CONF_DEL_NO) {
		request_set_status("Recursive directory deletion disabled in conf");
		debugf("[cgicmd] Recursive deletion disabled in conf\n");
		request_freenode(tnode);
		return;
	}
	if ((conf_del_recursive == CONF_DEL_PIN) &&
		(!root_isregistered(node))) {
		request_set_status("Enter PIN for recursive deletion");
		debugf("[cgicmd] Missing PIN\n");
		request_freenode(tnode);
		return;
	}

	// Start recursive deletion here.

	// @magic here
	cgicmd_start_cmd("DEL", tnode->disk_path);

	request_freenode(tnode);
	return;

}


//
// Spawn unrar, this only uses "bg" processing as it wants to 
// chdir first. This is really overly complicated.
//
void cgicmd_unrar(request_t *node, char *path)
{
	request_t *tnode;

	// Since we deal with disk items, we need to allocate a new
	// request_t node, set the path and call root_setpath() to have
	// it expanded to the real disk path.
	tnode = request_newnode();
	if (!tnode) return;

	tnode->path = strdup(path);
	if (!tnode->path) return;

	if (root_setpath(tnode)) {
		request_freenode(tnode);
		request_set_status("Unrar failed: stat");
		return;
	}

	debugf("[cgicmd] UNRAR on '%s'\n", tnode->disk_path);

	if (conf_unrar == CONF_DEL_NO) {
		request_set_status("Unrar disabled in conf");
		debugf("[cgicmd] Unrar disabled in conf\n");
		request_freenode(tnode);
		return;
	}
	if ((conf_unrar == CONF_DEL_PIN) &&
		(!root_isregistered(node))) {
		request_set_status("Enter PIN for unrar");
		debugf("[cgicmd] Enter PIN\n");
		request_freenode(tnode);
		return;
	}

	cgicmd_start_cmd("UNRAR", tnode->disk_path);

	request_freenode(tnode);
	return;

}




//
// CMD      : SCRIPT
// Required : name, path
// Optional : pin
//
void cgicmd_addscript(char **keys, char **values,
					  int items,void *optarg)
{
	char *name, *path, *pin;
	cgicmd_script_t *script;

	// Required
	name    = parser_findkey(keys, values, items, "NAME");
	path    = parser_findkey(keys, values, items, "PATH");

	if (!name || !*name || !path || !*path) {
		printf("      : [cgicmd] missing required fields: NAME, PATH\n");
		return;
	}

	pin    = parser_findkey(keys, values, items, "PIN");

	script = (cgicmd_script_t *)malloc(sizeof(*script));
	if (!script) {
		printf("      : [cgicmd] out of memory for new SCRIPT\n");
		return;
	}

	memset(script, 0, sizeof(*script));

	script->name = strdup(name);
	script->path = strdup(path);

	if (pin && !mystrccmp("yes", pin))
		script->pin_required = CONF_DEL_PIN;

	// Stat path to confirm it exists?
	script->next = cgicmd_script_head;
	cgicmd_script_head = script;

	debugf("[cgicmd] added SCRIPT '%s'\n", name);
}





void cgicmd_userexec(request_t *node, char *name, char *path)
{
	cgicmd_script_t *script;
	request_t *tnode;
	char buffer[1024];
	lion_t *ret;

	// Attempt to locate said script
	for (script = cgicmd_script_head;
		 script;
		 script=script->next) {
		if (!mystrccmp(name, script->name)) break;
	}
	
	if (!script) {
		return;
	}

	// Since we deal with disk items, we need to allocate a new
	// request_t node, set the path and call root_setpath() to have
	// it expanded to the real disk path.
	tnode = request_newnode();
	if (!tnode) return;

	tnode->path = strdup(path);
	if (!tnode->path) return;

	if (root_setpath(tnode)) {
		request_freenode(tnode);
		request_set_status("Script failed: stat");
		return;
	}

	debugf("[cgicmd] SCRIPT '%s' on '%s'\n", name, tnode->disk_path);

	if ((script->pin_required == CONF_DEL_PIN) &&
		(!root_isregistered(node))) {
		request_set_status("Enter PIN for Script");
		debugf("[cgicmd] Enter PIN\n");
		request_freenode(tnode);
		return;
	}

	snprintf(buffer, sizeof(buffer), "%s %s", 
			 script->path, tnode->disk_path);

	debugf("[cgicmd_bg_script] spawning '%s'\n", buffer);

	ret = lion_system(buffer, 1, LION_FLAG_NONE, NULL);
	if (ret) lion_set_handler(ret, cgicmd_bg_subhandler);
	else {
		debugf("[cgicmd_bg_script] failed to launch script.\n");
	}
	request_freenode(tnode);
	return;


}
















/* ************************************************************************ *
 * ************************************************************************ *
 *                                                                          *
 * Child process code. Below code is run as another process.                *
 *                                                                          *
 * ************************************************************************ *
 * ************************************************************************ */

int cgicmd_background_handler(lion_t *handle, void *user_data, 
							  int status, int size, char *line)
{
	int *do_quit = (int *)user_data;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[cgicmd_child] parent is alive\n");
		break;
	case LION_PIPE_FAILED:
		debugf("[cgicmd_child] parent failed, and so shall I\n");
		*do_quit = 2;
		break;
	case LION_PIPE_EXIT:
		debugf("[cgicmd_child] parent exited, and so shall I\n");
		*do_quit = 1;
		break;
	case LION_INPUT:
		debugf("[cgicmd_child] input '%s'\n", line);

		// Look for commands from parents. Call the function.
		if ((size > 4) && !strncasecmp("DEL ", line, 4)) {
			// Move handle to myhandle, so we can pass by reference, this is
			// then set to NULL if there was output errors. (socket was closed)
			debugf("[cgicmd_child] got DELETE request...\n");
			cgicmd_bg_del(handle, do_quit, &line[4]);
			break;
		}
		if ((size > 6) && !strncasecmp("UNRAR ", line, 6)) {
			// Move handle to myhandle, so we can pass by reference, this is
			// then set to NULL if there was output errors. (socket was closed)
			debugf("[cgicmd_child] got UNRAR request...\n");
			cgicmd_bg_unrar(handle, do_quit, &line[6]);
			break;
		}
		break;
	}

	return 0;
}



//
// This handler is here only to catch events from spawned processes, which
// we don't really do anything with.
//
int cgicmd_bg_subhandler(lion_t *handle, void *user_data, 
						 int status, int size, char *line)
{
	int *do_quit = (int *)user_data;

	switch(status) {

	case LION_PIPE_RUNNING:
		debugf("[cgicmd_subhandler] process is alive\n");
		break;
	case LION_PIPE_FAILED:
		debugf("[cgicmd_subhandler] process failed\n");
		if (do_quit) *do_quit = 2;
		break;
	case LION_PIPE_EXIT:
		debugf("[cgicmd_subhandler] process finished\n");
		if (do_quit) *do_quit = 1;
		break;
	case LION_INPUT:
		// Look for "STATUS:" ?
		debugf("[cgicmd_subhandler] '%s'\n", line);
		if ((size > 7) && !strncasecmp("STATUS:", line, 7))
			request_set_status(&line[7]);
		break;
	}

	return 0;
}



int cgicmd_background_main(lion_t *parent_handle, void *user_data, void *arg)
{
	int do_quit = 0;

	lion_set_handler(parent_handle, cgicmd_background_handler);
	lion_set_userdata(parent_handle, (void *) &do_quit);

	while(!do_quit) {
		if (lion_poll(0, 1) < 0) break;
	}

	lion_close(parent_handle);
	parent_handle = NULL;

	debugf("[cgicmd] done, quitting gracefully.\n");
	lion_exitchild(0);
	return(0); // ignore the warning
}






int cgicmd_wipe_recursive(char *fullpath)
{

	DIR *dirp;
	struct dirent *dp;
	struct stat sb;
	char path[1024];

    // Recurse through dir, add up bytes for each user encountered
	// opendir()
	if ((dirp=opendir(fullpath))==NULL)
		return 0;

	// while (readdir())
	while ((dp = readdir(dirp)) != NULL) {
		snprintf(path, sizeof(path), "%s/%s", fullpath, dp->d_name); 
		if (lstat(path, &sb))
			continue;
        // If dir, drop back into it and wipe... 
		if (S_ISDIR(sb.st_mode) && strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) {
			cgicmd_wipe_recursive(path);

		// Otherwise, find the size, store it in the wipedata array and frag if
		// necessary
		} else if (!S_ISDIR(sb.st_mode)) {

			remove(path);

		}

	}

	closedir(dirp);

	// Delete the directory if required
	rmdir(fullpath);

	return 1; 

}



void cgicmd_bg_del(lion_t *handle, int *do_quit, char *path)
{

	if (!*do_quit)
		lion_printf(handle, "STATUS:Started recursive delete of '%s'\n",
					path);
	
	cgicmd_wipe_recursive(path);

	if (!*do_quit)
		lion_printf(handle, "STATUS:Completed recursive delete of '%s'\n",
					path);

	*do_quit = 1;
}

void cgicmd_bg_unrar(lion_t *handle, int *do_quit, char *path)
{
	char buffer[1024], *unrar, cwd[1024];
	lion_t *ret;
	
	if (!*do_quit)
		lion_printf(handle, "STATUS:Started unrar of '%s'\n",
					path);
	
	// Check if unrar is specified with full path, if so, just run it
	// if not, get PWD and concat.
	unrar = skin_get_rar();
	if (
		(*unrar == '/') 
#ifdef WIN32
		|| ( (unrar[0] && (unrar[1] == ':') ) ) 
#endif
		) { // Full path

		*cwd = 0;

	} else {

		getcwd(cwd, sizeof(cwd) - 1);
		misc_stripslash(cwd);  // Not sure there is one
		strcat(cwd, "/");      // Now add one for sure.

	}

	// Change to destination directory
	chdir(dirname(path));

	snprintf(buffer, sizeof(buffer), "%s%s x -y -- %s", 
			 cwd, unrar, path);

	debugf("[cgicmd_bg_unrar] spawning '%s': from dir '%s'\n", buffer,
		   dirname(path));

	ret = lion_system(buffer, 1, LION_FLAG_NONE, (void *)do_quit);
	if (ret) lion_set_handler(ret, cgicmd_bg_subhandler);
	
}


