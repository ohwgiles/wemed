
#ifndef MIMEAPP_H
#define MIMEAPP_H

struct Application {
	char* name;
	char* exec;
};

struct Application get_default_mime_app(const char* content_type);


#endif

