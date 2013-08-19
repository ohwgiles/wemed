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
GMimeObject* mime_model_object_from_tree(MimeModel*, GtkTreeIter* iter);

GMimeObject* mime_model_update_header(MimeModel*, GMimeObject* obj, const char* new_header);
GMimeObject* mime_model_new_part(MimeModel* m, GMimeObject* parent_or_sibling, const char* fromfile);
gboolean mime_model_write_to_file(MimeModel* m, const char* filename);
void mime_model_reparse(MimeModel*);

void mime_model_free(MimeModel*);

#endif

