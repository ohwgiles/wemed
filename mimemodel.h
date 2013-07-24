#ifndef MIMEMODEL_H
#define MIMEMODEL_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */

struct MimeModel_S;
typedef struct MimeModel_S MimeModel;


MimeModel* mime_model_create_empty();
MimeModel* mime_model_create_from_file(const char*);

GtkTreeModel* mime_model_get_gtk_model(MimeModel*);
GHashTable* mime_model_get_cid_hash(MimeModel*);

GMimeObject* mime_model_update_header(void*, GMimeObject* obj, const char* new_header);
void mime_model_reparse(MimeModel*);

void mime_model_free(MimeModel*);

#endif

