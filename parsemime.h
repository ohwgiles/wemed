
#ifndef PARSEMIME_H
#define PARSEMIME_H

typedef struct {
	GtkTreeModel* model;
	GHashTable* cidhash;
} MimeModel;

MimeModel parse_mime_file(const char*);

#endif

