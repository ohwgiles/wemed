#ifndef MIMEAPP_H
#define MIMEAPP_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
 
struct Application {
	char* name;
	char* exec;
};

struct Application get_default_mime_app(const char* content_type);

char* get_file_mime_type(const char* filename);

#endif

