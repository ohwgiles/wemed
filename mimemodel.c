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
	GMimeObject* message;
	GtkIconTheme* icon_theme;
	gboolean filter_enabled;
};

GtkTreeModel* mime_model_get_gtk_model(MimeModel* m) {
	return GTK_TREE_MODEL(m->filter);
}


void mime_model_filter_inline(MimeModel* m, gboolean en) {
	m->filter_enabled = en;
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(m->filter));
}

const char* mime_model_content_type(GMimeObject* obj) {
	return g_mime_content_type_to_string(g_mime_object_get_content_type(obj));
}

static void add_part_to_store(MimeModel* m, GtkTreeIter* iter, GMimeObject* part) {
	GdkPixbuf* icon;
	const char* name;
	if(GMIME_IS_PART(part)) {
		char* icon_name = strdup(mime_model_content_type(part));
		name = g_mime_part_get_filename(GMIME_PART(part)) ?: strdup(icon_name);
		for(char* p = strchr(icon_name,'/'); p != NULL; p = strchr(p, '/')) *p = '-';
		icon = gtk_icon_theme_load_icon(m->icon_theme, icon_name, 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
		free(icon_name);
	} else {
		icon = gtk_icon_theme_load_icon(m->icon_theme, "message", 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
		name = mime_model_content_type(part);
	}

	// add to tree
	gtk_tree_store_set(m->store, iter,
			MIME_MODEL_COL_OBJECT, part,
			MIME_MODEL_COL_ICON, icon,
			MIME_MODEL_COL_NAME, name,
			-1);
	g_object_unref(icon);
}

gboolean is_content_disposition_inline(GtkTreeModel* gtm, GtkTreeIter* iter, gpointer user_data) {
	MimeModel* m = (MimeModel*) user_data;
	if(!m->filter_enabled) return TRUE;
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

static MimeModel* mime_model_create() {
	MimeModel* m = malloc(sizeof(MimeModel));
	m->filter_enabled = 0;
	m->icon_theme = gtk_icon_theme_get_default();

	m->store = gtk_tree_store_new(MIME_MODEL_NUM_COLS, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	m->cidhash = g_hash_table_new(g_str_hash, g_str_equal);
	m->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(m->store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(m->filter), is_content_disposition_inline, m, NULL);

	return m;
}

MimeModel* mime_model_create_blank() {
	MimeModel* m = mime_model_create();
	// add a blank multipart node to the document
	GtkTreeIter root;
	m->message = GMIME_OBJECT(g_mime_multipart_new());
	gtk_tree_store_append(m->store, &root, NULL);
	add_part_to_store(m, &root, GMIME_OBJECT(m->message));
	return m;
}

MimeModel* mime_model_create_email() {
	MimeModel* m = mime_model_create();
	// get user and hostname to generate a sample From email address
	char host[HOST_NAME_MAX];
	gethostname(host, HOST_NAME_MAX);
	char from[512];
	sprintf(from, "%s <%s@%s>", getlogin(), getlogin(), host);
	
	m->message = GMIME_OBJECT(g_mime_multipart_new());
	g_mime_object_append_header(m->message, "To", "Example <example@example.com>");
	g_mime_object_append_header(m->message, "From", from);
	g_mime_object_append_header(m->message, "MIME-Version", "1.0");
	g_mime_object_append_header(m->message, "Subject", "(no subject)");
	// force boundary generation
	g_mime_multipart_get_boundary(GMIME_MULTIPART(m->message));
	// add a multipart/alternative with text and html parts
	GtkTreeIter root;
	gtk_tree_store_append(m->store, &root, NULL);
	add_part_to_store(m, &root, m->message);
	GMimeObject* alternative = mime_model_new_node(m, m->message, "multipart/alternative");
	mime_model_new_node(m, alternative, "text/html");
	mime_model_new_node(m, alternative, "text/plain");
	return m;
}


// At present it seems the only way to get a GtkTreeIter from
// a GMimeObject* is a brute-force match...
struct PartFinder {
	GtkTreeIter iter;
	GMimeObject* obj;
};
static gboolean find_part(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter, void* user_data) {
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
	printf("saving %p!\n", part);
	GMimeStream* encoded_content = g_mime_stream_mem_new();
	{
		char* content = (char*) new_content; // const dropped so we can do a conditional free
		const char* charset = g_mime_object_get_content_type_parameter(GMIME_OBJECT(part), "charset");
		printf("%p charset %s\n", part, charset);
		/*
		if(charset && strcmp("utf8", charset) != 0) {
			printf("save: converting %p from utf8 to %s\n", part, charset);
			gsize sz;
			char* converted = g_convert(new_content, len, charset, "utf8", NULL, &sz, NULL);
			if(converted) {
				len = sz;
				content = converted; // now we're dealing with a string that needs to be freed
			} else printf("Conversion failed\n");
		}*/
		GMimeStream* content_stream = g_mime_stream_mem_new_with_buffer(content, len);
		GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), TRUE);
		GMimeStream* stream_filter = g_mime_stream_filter_new(content_stream);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
		g_mime_stream_write_to_stream(stream_filter, encoded_content);
		g_object_unref(stream_filter);
		g_object_unref(basic_filter);
		g_object_unref(content_stream);
		if(new_content != content)
			free(content);
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
	add_part_to_store(m, &it, part_new);
}

// changing the header can have large consequences; this function
// just creates a new part based on the new header and the old contents
GMimeObject* mime_model_update_header(MimeModel* m, GMimeObject* part_old, const char* new_header) {
	GMimeStream* memstream = g_mime_stream_mem_new_with_buffer(new_header, strlen(new_header));
	GMimeParser* parse = g_mime_parser_new_with_stream(memstream);
	GMimeObject* part_new = g_mime_parser_construct_part(parse);
	if(!part_new)
		return NULL;

	// if, for example, the user attempts to change a multipart
	// object into a part object, fail
	if(G_OBJECT_TYPE(part_new) != G_OBJECT_TYPE(part_old))
		return NULL;

	if(GMIME_IS_PART(part_new)) {
		// set the content object of the new part to the old part
		GMimeContentEncoding enc_old = g_mime_part_get_content_encoding(GMIME_PART(part_old));
		GMimeContentEncoding enc_new = g_mime_part_get_content_encoding(GMIME_PART(part_new));
		if(enc_old != enc_new) {
			GMimeStream* new_data = g_mime_stream_mem_new();
			GMimeFilter* decode = g_mime_filter_basic_new(enc_old, FALSE);
			GMimeFilter* encode = g_mime_filter_basic_new(enc_new, TRUE);
			GMimeStream* original = g_mime_data_wrapper_get_stream(g_mime_part_get_content_object(GMIME_PART(part_old)));
			g_mime_stream_reset(original);
			GMimeStream* filter = g_mime_stream_filter_new(original);
			g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter), decode);
			g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter), encode);
			g_mime_stream_write_to_stream(filter, new_data);
			g_mime_stream_reset(new_data);
			GMimeDataWrapper* wrapper = g_mime_data_wrapper_new_with_stream(new_data, enc_new);
			g_mime_part_set_content_object(GMIME_PART(part_new), wrapper);
			g_object_unref(new_data);
			g_object_unref(decode);
			g_object_unref(encode);
			g_object_unref(filter);
			g_object_unref(wrapper);
		} else
		g_mime_part_set_content_object(GMIME_PART(part_new), g_mime_part_get_content_object(GMIME_PART(part_old)));
	} else if(GMIME_IS_MULTIPART(part_new)) {
		// move all the mime parts from the old multipart to the new multipart
		for(int i = 0, n = g_mime_multipart_get_count(GMIME_MULTIPART(part_old)); i < n; ++i) {
			g_mime_multipart_add(GMIME_MULTIPART(part_new), g_mime_multipart_get_part(GMIME_MULTIPART(part_old), i));
		}
	}
	mime_model_part_replace(m, part_old, part_new);
	return part_new;
}

GMimeObject* mime_model_new_node(MimeModel* m, GMimeObject* parent_or_sibling, const char* content_type_string) {
	GMimeMultipart* parent_part = NULL;
	GtkTreeIter parent_iter;


	GMimeContentType* content_type = g_mime_content_type_new_from_string(content_type_string);
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

	if(GMIME_IS_PART(new_node)) {
		if(strncmp(content_type_string, "text/", 5) != 0)
			g_mime_object_set_content_disposition(new_node, g_mime_content_disposition_new_from_string(GMIME_DISPOSITION_ATTACHMENT));
		GMimeStream* mem = g_mime_stream_mem_new();
		GMimeDataWrapper* data = g_mime_data_wrapper_new_with_stream(mem, GMIME_CONTENT_ENCODING_DEFAULT);
		g_mime_part_set_content_object(GMIME_PART(new_node), data);
		g_object_unref(data);
		g_object_unref(mem);
	}
	g_mime_multipart_add(parent_part, new_node);
	GtkTreeIter result;
	gtk_tree_store_append(m->store, &result, &parent_iter);
	add_part_to_store(m, &result, new_node);
	return new_node;
}

struct TreeInsertHelper {
	MimeModel* m;
	GMimeObject* multipart;
	GtkTreeIter parent;
	GtkTreeIter child;
};
static void populate_tree(GMimeObject *up, GMimeObject *part, gpointer user_data) {
	struct TreeInsertHelper* h = (struct TreeInsertHelper*) user_data;
	// halt the auto-recursion
	if(up != h->multipart) return;
	
	if(GMIME_IS_MULTIPART(part)) {
		gtk_tree_store_append(h->m->store, &h->child, &h->parent);
		add_part_to_store(h->m, &h->child, part);
		h->multipart = part;
		GtkTreeIter grandparent = h->parent;
		h->parent = h->child;
		// manual recurse
		g_mime_multipart_foreach(GMIME_MULTIPART(part), populate_tree, h);
		h->parent = grandparent;
		h->multipart = up;
	} else if (GMIME_IS_PART (part)) {
		// add to hash
		const char* cid = g_mime_part_get_content_id((GMimePart*)part);
		gtk_tree_store_append(h->m->store, &h->child, &h->parent);
		add_part_to_store(h->m, &h->child, part);
		if(cid) {
			g_hash_table_insert(h->m->cidhash, strdup(cid), part);
		}
	} else {
		printf("unknown type!\n");
	}
}

MimeModel* mime_model_create_from_file(FILE* fp) {
	MimeModel* m = mime_model_create();

	GMimeStream* gfs = g_mime_stream_file_new(fp);
	if(!gfs) return NULL;

	GMimeParser* parser = g_mime_parser_new_with_stream(gfs);
	if(!parser) return NULL;

	GMimeMessage* message = g_mime_parser_construct_message(parser);
	if(!message) return NULL;

	m->message = g_mime_message_get_mime_part(message);
	struct TreeInsertHelper h = {0};
	h.m = m;
	gtk_tree_store_append(m->store, &h.parent, NULL);
	add_part_to_store(m, &h.parent, m->message);
	if(GMIME_IS_MULTIPART(m->message)) {
		h.multipart = m->message;
		g_mime_multipart_foreach(GMIME_MULTIPART(m->message), populate_tree, &h);
	}

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

gboolean mime_model_write_to_file(MimeModel* m, FILE* fp) {
	GMimeStream* gfs = g_mime_stream_file_new(fp);
	
	if(!gfs) return FALSE;
	g_mime_object_write_to_stream(GMIME_OBJECT(m->message), gfs);
	g_object_unref(gfs);

	return TRUE;
}

void mime_model_part_remove(MimeModel* m, GMimeObject* part) {
	GtkTreeIter iter = iter_from_obj(m, part);
	GtkTreeIter parent = parent_node(m, iter);
	GMimeMultipart* multipart = GMIME_MULTIPART(obj_from_iter(m, parent));
	gtk_tree_store_remove(m->store, &iter);
	g_mime_multipart_remove(multipart, part);
}

void mime_model_free(MimeModel* m) {
	if(m) {
		g_object_unref(m->store);
		g_hash_table_unref(m->cidhash);
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

	if(type < image) {
		// get the length of the decoded data
		GMimeStream* null_stream = g_mime_stream_null_new();
		GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
		gint64 decoded_length = g_mime_data_wrapper_write_to_stream(mco, null_stream);
		g_object_unref(null_stream);
		// by allocating our own array we control its length
		str = malloc(decoded_length+1); // null termination
		// but it is a lengthy procedure to get control back
		GByteArray* arr = g_byte_array_new_take((guint8*)str, decoded_length);
		GMimeStream* mem_stream = g_mime_stream_mem_new_with_byte_array(arr);
		g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(mem_stream), FALSE);
		g_mime_data_wrapper_write_to_stream(mco, mem_stream);
		GBytes* bytes = g_byte_array_free_to_bytes(arr);
		gsize sz;
		g_bytes_unref_to_data(bytes, &sz);
		str[decoded_length] = '\0'; // can finally terminate here
		g_object_unref(mem_stream);
	} else if(type < other) {
		// always return this data in base64 format so it can be displayed as a data: URI
		GMimeContentEncoding encoding = g_mime_part_get_content_encoding(part);
		if(encoding == GMIME_CONTENT_ENCODING_BASE64) {
			// optimisation: no conversion required
			GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
			g_mime_stream_reset(gms);
			gint64 len = g_mime_stream_length(gms);
			const char* content_encoding = g_mime_content_encoding_to_string(g_mime_part_get_content_encoding(part));
			int header_length = 5 /*data:*/ + strlen(content_type_name) + 1 /*;*/ + strlen(content_encoding) + 1 /*,*/ ;
			str = malloc(header_length + len + 1);
			sprintf(str, "data:%s;%s,", content_type_name, content_encoding);
			g_mime_stream_read(gms, &str[header_length], len);
			str[header_length + len] = '\0';
		} else {
			// first do a dummy conversion to get space requirements
			GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
			GMimeStream* content_stream = g_mime_data_wrapper_get_stream(mco);
			g_mime_stream_reset(content_stream);
			GMimeFilter* decoding_filter = g_mime_filter_basic_new(encoding, FALSE);
			GMimeFilter* encoding_filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_BASE64, TRUE);
			GMimeStream* stream_filter = g_mime_stream_filter_new(content_stream);
			g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), decoding_filter);
			g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), encoding_filter);
			// first do a dummy conversion to get space requirements
			GMimeStream* null_stream = g_mime_stream_null_new();
			int len = g_mime_stream_write_to_stream(stream_filter, null_stream);
			g_object_unref(null_stream);
			// now do the real thing
			g_mime_stream_reset(content_stream);
			g_mime_stream_reset(stream_filter);
			int header_length = 5 /*data:*/ + strlen(content_type_name) + 8 /*;base64,*/;
			str = malloc(header_length + len + 1); // null termination
			sprintf(str, "data:%s;base64,", content_type_name);
			// functionise this? it's so messy
			GByteArray* arr = g_byte_array_new_take((guint8*)&str[header_length], len);
			GMimeStream* mem_stream = g_mime_stream_mem_new_with_byte_array(arr);
			g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(mem_stream), FALSE);
			g_mime_stream_write_to_stream(stream_filter, mem_stream);
			GBytes* bytes = g_byte_array_free_to_bytes(arr);
			gsize sz;
			g_bytes_unref_to_data(bytes, &sz);
			str[header_length + len] = '\0'; // can finally terminate here
			g_object_unref(decoding_filter);
			g_object_unref(encoding_filter);
			g_object_unref(stream_filter);
			g_object_unref(mem_stream);
		}

	}

	return str;
}

