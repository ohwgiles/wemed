#ifndef MIMEMODEL_H
#define MIMEMODEL_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gmime/gmime.h>
#include <gtk/gtktreemodel.h>

struct _MimeModel;
struct _MimeModelClass;
typedef struct _MimeModel MimeModel;
typedef struct _MimeModelClass MimeModelClass;

// columns in the table representing a MIME part
enum {
	MIME_MODEL_COL_OBJECT,
	MIME_MODEL_COL_ICON,
	MIME_MODEL_COL_NAME,
	MIME_MODEL_NUM_COLS
};

MimeModel* mime_model_new(GString from_content);
void mime_model_create_blank_email(MimeModel* m);

GtkTreeModel* mime_model_get_gtk_model(MimeModel*);
void mime_model_filter_inline(MimeModel*, gboolean);

const char *mime_model_content_type(GMimeObject* obj);

GMimeObject* mime_model_root(MimeModel*);

GString mime_model_part_content(GMimeObject* part);
GString mime_model_part_headers(GMimeObject* part);

GMimeObject* mime_model_update_header(MimeModel*, GMimeObject* obj, GString new_header);

GMimeObject* mime_model_find_mixed_parent(MimeModel* m, GMimeObject* part);

void mime_model_update_content(MimeModel*, GMimePart* obj, GString new_content);

GMimeObject* mime_model_new_node(MimeModel* m, GMimeObject* parent_or_sibling, const char* content_type);

// writes a part to a file, decoding base64 etc
void mime_model_write_part(GMimePart* part, FILE* fp);

void mime_model_set_part_content(GMimePart* part, FILE* fp);

void mime_model_part_remove(MimeModel* m, GMimeObject* part);

// write the whole message to a file in MIME format
gboolean mime_model_write_to_file(MimeModel* m, FILE* fp);

GByteArray* mime_model_object_from_cid(GObject* emitter, const char* cid, gpointer user_data);

void mime_model_free(MimeModel*);

#endif

