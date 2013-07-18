
#ifndef PARSEMIME_H
#define PARSEMIME_H


struct MimeModel_S;
typedef struct MimeModel_S MimeModel;


MimeModel* mime_model_create_empty();
MimeModel* mime_model_create_from_file(const char*);

GtkTreeModel* mime_model_get_gtk_model(MimeModel*);
GHashTable* mime_model_get_cid_hash(MimeModel*);

gboolean mime_model_update_header(void*, GMimeObject* obj, const char* new_header);
void mime_model_reparse(MimeModel*);

void mime_model_free(MimeModel*);

#endif

