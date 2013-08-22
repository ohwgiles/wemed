#ifndef MIMEMODEL_H
#define MIMEMODEL_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */

struct MimeModel_S;
typedef struct MimeModel_S MimeModel;

enum {
	MIME_MODEL_COL_OBJECT,
	MIME_MODEL_COL_ICON,
	MIME_MODEL_COL_NAME,
	MIME_MODEL_NUM_COLS
};

MimeModel* mime_model_create_empty();
MimeModel* mime_model_create_from_file(const char*);

GtkTreeModel* mime_model_get_gtk_model(MimeModel*);
GHashTable* mime_model_get_cid_hash(MimeModel*);
GMimeObject* mime_model_object_from_tree(MimeModel*, GtkTreeIter* iter);
const char* mime_model_content_type(GMimeObject* obj);

char* mime_model_part_content(GMimePart* part);

GMimeObject* mime_model_update_header(MimeModel*, GMimeObject* obj, const char* new_header);
void mime_model_update_content(MimeModel*, GMimeObject* obj, const char* new_content);
GMimeObject* mime_model_new_node(MimeModel* m, GMimeObject* parent_or_sibling, const char* content_type, const char* content);
void mime_model_write_part(GMimePart* part, FILE* fp);
gboolean mime_model_write_to_file(MimeModel* m, const char* filename);
void mime_model_reparse(MimeModel*);
char* mime_model_object_from_cid(GObject* emitter, const char* cid, gpointer user_data);

void mime_model_free(MimeModel*);

#endif

