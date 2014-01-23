/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "exec.h"
#include "mimeapp.h"

struct Application get_default_mime_app(const char* mimetype) {
	struct Application a = {0,0};
	char buffer[64];

	const char* xdgmime[] = { "xdg-mime", "query", "default", mimetype, 0 };
	if(exec_get(buffer, 63, "xdg-mime", xdgmime) < 0) return a;
	*strchrnul(buffer, '\n') = '\0';

	char* path = malloc(sizeof("/usr/share/applications/") + strlen(buffer) + 1);
	sprintf(path, "/usr/share/applications/%s", buffer);

	const char* grep_exec[] = { "grep", "^Exec=", path, 0 };
	if(exec_get(buffer, 63, "grep", grep_exec) < 0) return free(path), a;
	*strchrnul(buffer, '\n') = '\0';

	char executable_path[64] = {0};
	sscanf(buffer, "Exec=%48s", executable_path);

	const char* grep_name[] = { "grep", "^Name=", path, 0 };
	if(exec_get(buffer, 63, "grep", grep_name) < 0) return free(path), a;
	*strchrnul(buffer, '\n') = '\0';

	char *executable_name = &buffer[sizeof("Name=")-1];

	free(path);

	a.name = strdup(executable_name);
	a.exec = strdup(executable_path);

	return a;
}

char* get_file_mime_type(const char* filename) {
	char buffer[64] = {0};
	const char* args[] = { "xdg-mime", "query", "filetype", filename, 0 };
	if(exec_get(buffer, 63, "xdg-mime", args) < 0) return 0;
	return strdup(buffer);
}
