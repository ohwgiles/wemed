/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "mimemodel.h"
#include "mimeapp.h"

struct MimeModel_S {
	GtkTreeStore* store;
	GtkTreeModel* filter;
	GHashTable* cidhash;
	GMimeMessage* message;
	char* name;
};

GtkTreeModel* mime_model_get_gtk_model(MimeModel* m) {
	return GTK_TREE_MODEL(m->filter);
}

struct TreeInsertHelper {
	MimeModel* m;
	GtkTreeIter current;
	GtkTreeIter child;
	GMimeObject* current_multipart;
	GtkTreeIter lastparent;
};


GMimeObject* mime_model_object_from_tree(MimeModel*, GtkTreeIter* iter);

const char* mime_model_content_type(GMimeObject* obj) {
	return g_mime_content_type_to_string(g_mime_object_get_content_type(obj));
}

static void add_part_to_store(GtkTreeStore* store, GtkTreeIter* iter, GMimeObject* part) {
	// icon
	GtkIconTheme* git = gtk_icon_theme_get_default();
	GMimeContentType* ct = g_mime_object_get_content_type(part);
	const char* content_type_name = g_mime_content_type_to_string(ct);
	char* icon_name = strdup(content_type_name);
	for(char* p = strchr(icon_name,'/'); p != NULL; p = strchr(p, '/')) *p = '-';
	GdkPixbuf* icon = gtk_icon_theme_load_icon(git, icon_name, 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
	free(icon_name);

	const char* name = g_mime_part_get_filename(GMIME_PART(part)) ?: content_type_name;

	// add to tree
	gtk_tree_store_set(store, iter,
			MIME_MODEL_COL_OBJECT, part,
			MIME_MODEL_COL_ICON, icon,
			MIME_MODEL_COL_NAME, name,
			-1);
	g_object_unref(icon);
}

static void parse_mime_segment(GMimeObject *up, GMimeObject *part, gpointer user_data) {

	struct TreeInsertHelper* h = (struct TreeInsertHelper*) user_data;
	if(up != h->current_multipart) return;

	if (GMIME_IS_MESSAGE_PART (part)) {
		printf("message part...\n");
		GMimeMessage *message;
		GtkTreeIter parent = h->current;
		h->current = h->child;
		message = g_mime_message_part_get_message((GMimeMessagePart *) part);
		g_mime_message_foreach(message, parse_mime_segment, h);
		h->current = parent;
	} else if (GMIME_IS_MESSAGE_PARTIAL (part)) {
		printf("partial!\n");

	} else if (GMIME_IS_MULTIPART (part)) {
		GMimeContentType* ct = g_mime_object_get_content_type(part);
		GtkIconTheme* git = gtk_icon_theme_get_default();
		GdkPixbuf* icon = gtk_icon_theme_load_icon(git, "message", 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
		gtk_tree_store_append(h->m->store, &h->child, &h->current);
		gtk_tree_store_set(h->m->store, &h->child,
				MIME_MODEL_COL_OBJECT, part,
				MIME_MODEL_COL_ICON, icon,
				MIME_MODEL_COL_NAME, g_mime_content_type_to_string(ct),
				-1);
		g_object_unref(icon);
		GtkTreeIter parent = h->current;
		h->current = h->child;
		h->current_multipart = part;
		g_mime_multipart_foreach(GMIME_MULTIPART(part), parse_mime_segment, h);
		h->current_multipart = up;
		h->current = parent;
	} else if (GMIME_IS_PART (part)) {
		// add to hash
		const char* cid = g_mime_part_get_content_id((GMimePart*)part);
		gtk_tree_store_append(h->m->store, &h->child, &h->current);
		add_part_to_store(h->m->store, &h->child, part);
		if(cid) {
			g_hash_table_insert(h->m->cidhash, strdup(cid), part);
		}
	} else {
		printf("unknown type\n");
	}
}

gboolean is_content_disposition_inline(GtkTreeModel* gtm, GtkTreeIter* iter, gpointer user_data) {
	return TRUE;
	GValue v = {0};//gtk_value_new();
	gtk_tree_model_get_value(gtm, iter, MIME_MODEL_COL_OBJECT, &v);
	GMimeObject* part = (GMimeObject*) g_value_get_pointer(&v);
	g_value_unset(&v);
	if(GMIME_IS_PART(part)) {
		const char* disposition = g_mime_object_get_disposition(part);
		if(disposition && strcmp(disposition, "inline") == 0) return FALSE;
	}
	return TRUE;
}

MimeModel* mime_model_create_empty() {
	return NULL;
}

struct PartFinder {
	GtkTreeIter iter;
	GMimeObject* obj;
};

gboolean find_part(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter, void* user_data) {
	struct PartFinder* p = (struct PartFinder*) user_data;
	GMimeObject* obj = NULL;
	gtk_tree_model_get(model, iter, MIME_MODEL_COL_OBJECT, &obj, -1);
	if(obj == p->obj) {
		p->iter = *iter;
		return TRUE;
	}
	return FALSE;
}

static GtkTreeIter iter_from_obj(MimeModel* m, GMimeObject* part) {
	struct PartFinder p;
	p.obj = part;
	gtk_tree_model_foreach(GTK_TREE_MODEL(m->store), find_part, &p);
	return p.iter;
}

static GMimeObject* obj_from_iter(MimeModel* m, GtkTreeIter iter) {
	GValue v = {0};
	gtk_tree_model_get_value(GTK_TREE_MODEL(m->store), &iter, MIME_MODEL_COL_OBJECT, &v);
	return GMIME_OBJECT(g_value_get_pointer(&v));
}

static GtkTreeIter parent_node(MimeModel* m, GtkTreeIter child) {
	GtkTreeIter parent;
	gtk_tree_model_iter_parent(GTK_TREE_MODEL(m->store), &parent, &child);
	return parent;
}

void mime_model_update_content(MimeModel* m, GMimePart* part, const char* new_content, int len) {
	// encode content into memstream
	GMimeStream* encoded_content = g_mime_stream_mem_new();
	{
		GMimeStream* content = g_mime_stream_mem_new_with_buffer(new_content, len);
		GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), TRUE);
		GMimeStream* stream_filter = g_mime_stream_filter_new(content);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
		g_mime_stream_write_to_stream(stream_filter, encoded_content);
		g_object_unref(stream_filter);
		g_object_unref(basic_filter);
		g_object_unref(content);
	}
	GMimeDataWrapper* data = g_mime_data_wrapper_new_with_stream(encoded_content, g_mime_part_get_content_encoding(GMIME_PART(part)));
	g_mime_part_set_content_object(GMIME_PART(part), data);
	g_object_unref(encoded_content);
	g_object_unref(data);
}

void mime_model_part_replace(MimeModel* m, GMimeObject* part_old, GMimeObject* part_new) {
	GtkTreeIter it = iter_from_obj(m, part_old);

	GtkTreeIter parent = parent_node(m, it);
	GMimeMultipart* multipart = GMIME_MULTIPART(obj_from_iter(m, parent));
	int index = g_mime_multipart_index_of(multipart, part_old);
	GMimeObject* part_old_ = g_mime_multipart_replace(multipart, index, part_new); // already have this
	g_object_unref(part_old_);
	add_part_to_store(m->store, &it, part_new);
}

GMimeObject* mime_model_update_header(MimeModel* m, GMimeObject* part_old, const char* new_header) {
	GMimeStream* memstream = g_mime_stream_mem_new_with_buffer(new_header, strlen(new_header));
	GMimeParser* parse = g_mime_parser_new_with_stream(memstream);
	GMimeObject* part_new = g_mime_parser_construct_part(parse);
	if(!part_new) {
		printf("parsing failed\n"); // todo dialog box
		return NULL;
	}

	if(G_OBJECT_TYPE(part_new) != G_OBJECT_TYPE(part_old)) {
		printf("G_OBJECT_TYPE not equal: %d, %d\n", G_OBJECT_TYPE(part_new), G_OBJECT_TYPE(part_old));
		return NULL;
	}

	if(GMIME_IS_PART(part_new)) {
		g_mime_part_set_content_object(GMIME_PART(part_new), g_mime_part_get_content_object(GMIME_PART(part_old)));
	} else if(GMIME_IS_MULTIPART(part_new)) {
		for(int i = 0, n = g_mime_multipart_get_count(GMIME_MULTIPART(part_old)); i < n; ++i) {
			g_mime_multipart_add(GMIME_MULTIPART(part_new), g_mime_multipart_get_part(GMIME_MULTIPART(part_old), i));
		}
	}
	mime_model_part_replace(m, part_old, part_new);
	return part_new;
}

GMimeObject* mime_model_new_node(MimeModel* m, GMimeObject* parent_or_sibling, const char* content_type_string, const char* filename) {
	printf("parent_or_sibling: %s\n", g_mime_content_type_to_string(g_mime_object_get_content_type(parent_or_sibling)));
	GMimeMultipart* parent_part = NULL;
	GtkTreeIter parent_iter;


	GMimeContentType* content_type = NULL;
	if(content_type_string)
		content_type = g_mime_content_type_new_from_string(content_type_string);
	else if(filename) {
		char* mime_type = get_file_mime_type(filename);
		content_type = g_mime_content_type_new_from_string(mime_type);
		free(mime_type);
	}
	GMimeObject* new_node = g_mime_object_new(content_type);
	g_object_unref(content_type);

	if(GMIME_IS_MULTIPART(parent_or_sibling)) {
		parent_iter = iter_from_obj(m, parent_or_sibling);
		parent_part = GMIME_MULTIPART(parent_or_sibling);
	} else {
		parent_iter = parent_node(m, iter_from_obj(m, parent_or_sibling));
		parent_part = GMIME_MULTIPART(obj_from_iter(m, parent_iter));
	}

	if(GMIME_IS_MULTIPART(new_node)) {
		// force boundary string generation
		g_mime_multipart_get_boundary(GMIME_MULTIPART(new_node));
	}

	if(GMIME_IS_PART(new_node) && filename) {
		FILE* fp = fopen(filename, "rb");
		if(!fp) return NULL;
		fseek(fp, 0, SEEK_END);
		int len = ftell(fp);
		rewind(fp);
		char* new_content = malloc(len);
		fread(new_content, 1, len, fp);
		fclose(fp);
		g_mime_object_set_content_disposition(new_node, g_mime_content_disposition_new_from_string(GMIME_DISPOSITION_ATTACHMENT));
		g_mime_part_set_filename(GMIME_PART(new_node), &strrchr(filename,'/')[1]);
		// this is a nasty hack to determine the proper encoding. the file is first
		// stored in the mime tree in its binary form, then the call to
		// g_mime_object_encode sets the best encoding method. the second call
		// to mime_model_update_content is required to actually save the data in
		// the mime tree in the encoded (not raw binary) format
		mime_model_update_content(m, GMIME_PART(new_node), new_content, len);
		g_mime_object_encode(new_node, GMIME_ENCODING_CONSTRAINT_7BIT);
		mime_model_update_content(m, GMIME_PART(new_node), new_content, len);
		free(new_content);
	}
	g_mime_multipart_add(parent_part, new_node);
	GtkTreeIter result;
	gtk_tree_store_append(m->store, &result, &parent_iter);
	add_part_to_store(m->store, &result, new_node);
	return new_node;
}

void mime_model_reparse(MimeModel* m) {
	struct TreeInsertHelper h = {0};
	h.m = m;
	gtk_tree_store_clear(m->store);
	gtk_tree_store_append(m->store, &h.current, NULL);
	gtk_tree_store_set(m->store, &h.current,
			MIME_MODEL_COL_OBJECT, m->message,
			MIME_MODEL_COL_NAME, m->name,
			-1);
	//reparse_segment(
	parse_mime_segment(NULL, g_mime_message_get_mime_part(m->message), &h);
}

MimeModel* mime_model_create_from_file(const char* filename) {
	MimeModel* m = malloc(sizeof(MimeModel));

	m->store = gtk_tree_store_new(MIME_MODEL_NUM_COLS, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	m->cidhash = g_hash_table_new(g_str_hash, g_str_equal);
	m->name = index(filename, '/') ? strdup(strrchr(filename, '/')) : strdup(filename);

	FILE* fp = fopen(filename, "rb");
	if(!fp) return NULL;

	GMimeStream* gfs = g_mime_stream_file_new(fp);
	if(!gfs) return NULL;

	GMimeParser* parser = g_mime_parser_new_with_stream(gfs);
	if(!parser) return NULL;

	m->message = g_mime_parser_construct_message(parser);
	if(!m->message) return NULL;

	mime_model_reparse(m);
	m->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(m->store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(m->filter), is_content_disposition_inline, NULL, NULL);
	return m;
}

gboolean write_parts_to_stream(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter, gpointer data) {
	GMimeStream* stream = (GMimeStream*) data;
	GValue v = {0};
	gtk_tree_model_get_value(model, iter, MIME_MODEL_COL_OBJECT, &v);
	GMimeObject* obj = g_value_get_pointer(&v);

	g_mime_object_write_to_stream(obj, stream);
	g_mime_stream_flush(stream);
	g_value_unset(&v);
	return FALSE;
}

void mime_model_write_part(GMimePart* part, FILE* fp) {
	GMimeStream* gms = g_mime_data_wrapper_get_stream(g_mime_part_get_content_object(part));
	g_mime_stream_reset(gms);
	GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), FALSE);
	GMimeStream* stream_filter = g_mime_stream_filter_new(gms);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
	GMimeStream* filestream = g_mime_stream_file_new(fp);
	g_mime_stream_write_to_stream(stream_filter, filestream);
	fflush(fp);
	g_mime_stream_reset(gms);
	g_object_unref(filestream);
}


GMimePart* mime_model_read_part(MimeModel* m, FILE* fp, const char* content_type, GMimePart* part) {
	GMimeStream* file_stream = g_mime_stream_file_new(fp);
	GMimeStream* encoding_stream = g_mime_stream_filter_new(file_stream);

	GMimeStream* content_stream;
	if(part != NULL) { // the part exists already
		GMimeStream* content_stream = g_mime_data_wrapper_get_stream(g_mime_part_get_content_object(part));
		GMimeFilter* encoding_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), FALSE);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(encoding_stream), encoding_filter);
		g_mime_stream_write_to_stream(content_stream, encoding_stream);
	return part;
	} else {
		GMimeFilter* encoding_filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_DEFAULT, FALSE);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(encoding_stream), encoding_filter);
		GMimeParser* parse = g_mime_parser_new_with_stream(encoding_stream);
		GMimeObject* part_new = g_mime_parser_construct_part(parse);
		mime_model_part_replace(m, GMIME_OBJECT(part), part_new);
		return GMIME_PART(part_new);
	}
}

gboolean mime_model_write_to_file(MimeModel* m, const char* filename) {
	FILE* fp = fopen(filename, "wb");
	if(!fp) return FALSE;

	GMimeStream* gfs = g_mime_stream_file_new(fp);
	
	if(!gfs) return FALSE;
	g_mime_object_write_to_stream(GMIME_OBJECT(m->message), gfs);
	g_object_unref(gfs);

	return TRUE;
}

void mime_model_free(MimeModel* m) {
	if(m) {
		g_object_unref(m->store);
		g_hash_table_unref(m->cidhash);
		free(m->name);
		g_object_unref(m->message);
		//todo delete the filter
		free(m);
	}
}

char* mime_model_object_from_cid(GObject* emitter, const char* cid, gpointer user_data) {
	MimeModel* m = user_data;
	GHashTable* hash = (GHashTable*) m->cidhash;
	GMimePart* part = g_hash_table_lookup(hash, cid);
	if(part) {
		GMimeContentType* ct = g_mime_object_get_content_type((GMimeObject*)part);
		const char* content_type_name = g_mime_content_type_to_string(ct);
		GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
		GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
		g_mime_stream_reset(gms);
		gint64 len = g_mime_stream_length(gms);
		const char* content_encoding = g_mime_content_encoding_to_string(g_mime_part_get_content_encoding(part));
		int header_length = 5 /*data:*/ + strlen(content_type_name) + 1 /*;*/ + strlen(content_encoding) + 1 /*,*/ ;
		char* str = malloc(header_length + len + 1);
		sprintf(str, "data:%s;%s,", content_type_name, content_encoding);
		g_mime_stream_read(gms, &str[header_length], len);
		str[header_length + len] = '\0';
		g_mime_stream_reset(gms);
		return str;
	} else {
		printf("could not find %s in hash\n", cid);
		return NULL;
	}
}
char* mime_model_part_content(GMimePart* part) {
	enum { plaintext, html, image, other };
	const char* content_type_name = mime_model_content_type(GMIME_OBJECT(part));
	char* str = 0;

	int type =
		(strcmp(content_type_name, "text/plain") == 0)? plaintext:
		(strcmp(content_type_name, "text/html") == 0)? html:
		(strncmp(content_type_name, "image/", 6) == 0)? image: other;

	GMimeStream* null_stream = g_mime_stream_null_new();
	GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
	gint64 decoded_length = g_mime_data_wrapper_write_to_stream(mco, null_stream);
	g_object_unref(null_stream);

	if(type < image) {
		// playing some tricky games
		str = malloc(decoded_length+1);
		GByteArray* arr = g_byte_array_new_take((guint8*)str, decoded_length);
		GMimeStream* mem_stream = g_mime_stream_mem_new_with_byte_array(arr);
		g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(mem_stream), FALSE);
		g_mime_data_wrapper_write_to_stream(mco, mem_stream);
		GBytes* bytes = g_byte_array_free_to_bytes(arr);
		gsize sz;
		char* raw = g_bytes_unref_to_data(bytes, &sz);
		printf("raw: %p, str: %p\n", raw, str);
		str[decoded_length] = '\0';
		g_object_unref(mem_stream);

	} else if(type < other) {
		GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
		g_mime_stream_reset(gms);
		gint64 len = g_mime_stream_length(gms);
		const char* content_encoding = g_mime_content_encoding_to_string(g_mime_part_get_content_encoding(part));
		int header_length = 5 /*data:*/ + strlen(content_type_name) + 1 /*;*/ + strlen(content_encoding) + 1 /*,*/ ;
		str = malloc(header_length + len + 1);
		sprintf(str, "data:%s;%s,", content_type_name, content_encoding);
		g_mime_stream_read(gms, &str[header_length], len);
		str[header_length + len] = '\0';
	}

	return str;
}
