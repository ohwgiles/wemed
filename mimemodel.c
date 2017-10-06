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

extern GtkIconTheme* system_icon_theme;

struct MimeModel_S {
	GtkTreeStore* store;
	GtkTreeModel* filter;
	GMimeObject* message;
	gboolean filter_enabled;
};

static void add_part_to_store(MimeModel* m, GtkTreeIter* iter, GMimeObject* part) {
	GdkPixbuf* icon;
	const char* name;
	if(GMIME_IS_PART(part)) {
		char* icon_name = strdup(mime_model_content_type(part));
		name = g_mime_part_get_filename(GMIME_PART(part)) ?: strdup(icon_name);
		for(char* p = strchr(icon_name,'/'); p != NULL; p = strchr(p, '/')) *p = '-';
		icon = gtk_icon_theme_load_icon(system_icon_theme, icon_name, 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
		free(icon_name);
	} else {
		icon = gtk_icon_theme_load_icon(system_icon_theme, "package", 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
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



static gboolean is_content_disposition_inline(GtkTreeModel* gtm, GtkTreeIter* iter, gpointer user_data) {
	MimeModel* m = (MimeModel*) user_data;
	if(!m->filter_enabled) return TRUE;
	GMimeObject* part = obj_from_iter(m, *iter);
	if(GMIME_IS_PART(part)) {
		const char* disposition = g_mime_object_get_disposition(part);
		if(disposition && strcmp(disposition, "inline") == 0) return FALSE;
	}
	return TRUE;
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
		gtk_tree_store_append(h->m->store, &h->child, &h->parent);
		add_part_to_store(h->m, &h->child, part);
	} else {
		printf("unknown type!\n");
	}
}



void mime_model_create_blank_email(MimeModel* m) {
	g_mime_object_append_header(m->message, "To", "");
	g_mime_object_append_header(m->message, "From", "");
	g_mime_object_append_header(m->message, "Subject", "");
	g_mime_object_append_header(m->message, "MIME-Version", "1.0");
	// force boundary generation
	g_mime_multipart_get_boundary(GMIME_MULTIPART(m->message));
	// add a multipart/alternative with text and html parts
	GMimeObject* alternative = mime_model_new_node(m, m->message, "multipart/alternative");
	GMimeObject* related = mime_model_new_node(m, alternative, "multipart/related");
	mime_model_new_node(m, related, "text/html");
	mime_model_new_node(m, alternative, "text/plain");
}


void mime_model_update_content(MimeModel* m, GMimePart* part, GString content) {
	// encode content into memstream
	GMimeStream* encoded_content = g_mime_stream_mem_new();
	{
		GMimeStream* content_stream = g_mime_stream_mem_new_with_buffer(content.str, content.len);
		GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), TRUE);
		GMimeStream* stream_filter = g_mime_stream_filter_new(content_stream);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
		g_mime_stream_write_to_stream(stream_filter, encoded_content);
		g_object_unref(stream_filter);
		g_object_unref(basic_filter);
		g_object_unref(content_stream);
	}
	GMimeDataWrapper* data = g_mime_data_wrapper_new_with_stream(encoded_content, g_mime_part_get_content_encoding(GMIME_PART(part)));
	g_mime_part_set_content_object(GMIME_PART(part), data);
	g_object_unref(encoded_content);
	g_object_unref(data);
}

void mime_model_part_replace(MimeModel* m, GMimeObject* part_old, GMimeObject* part_new) {
	GtkTreeIter it = iter_from_obj(m, part_old);

	GtkTreeIter parent = parent_node(m, it);
	if(parent.stamp == 0) {
		// trying to replace the root element (which has no parent)
		m->message = part_new;
	} else {
		GMimeMultipart* multipart = GMIME_MULTIPART(obj_from_iter(m, parent));
		int index = g_mime_multipart_index_of(multipart, part_old);
		g_mime_multipart_replace(multipart, index, part_new); // already have this
	}
	g_object_unref(part_old);
	add_part_to_store(m, &it, part_new);
}

// changing the header can have large consequences; this function
// just creates a new part based on the new header and the old contents
GMimeObject* mime_model_update_header(MimeModel* m, GMimeObject* part_old, GString new_header) {
	GMimeStream* memstream = g_mime_stream_mem_new_with_buffer(new_header.str, new_header.len);
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

GMimeObject* mime_model_find_mixed_parent(MimeModel* m, GMimeObject* part) {
	GtkTreeIter parent_iter;
	parent_iter = parent_node(m, iter_from_obj(m, part));
	if(parent_iter.stamp == 0)
		return NULL;
	GMimeObject* parent = obj_from_iter(m, parent_iter);
	GMimeContentType* ct = g_mime_object_get_content_type(parent);
	if(g_mime_content_type_is_type(ct,"multipart","related") || g_mime_content_type_is_type(ct,"multipart","mixed"))
			return parent;
	return mime_model_find_mixed_parent(m, parent);
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


MimeModel* mime_model_new(GString content) {
	MimeModel* m = g_new0(MimeModel, 1);

	m->store = gtk_tree_store_new(MIME_MODEL_NUM_COLS, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	m->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(m->store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(m->filter), is_content_disposition_inline, m, NULL);
	m->filter_enabled = FALSE;

	if(content.str) {
		GMimeStream* gfs = g_mime_stream_mem_new_with_buffer(content.str, content.len);
		GMimeParser* parser = g_mime_parser_new_with_stream(gfs);
		GMimeMessage* message = g_mime_parser_construct_message(parser);
		m->message = g_mime_message_get_mime_part(message);
	} else {
		m->message = (GMimeObject*) g_mime_multipart_new();
	}

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

GtkTreeModel* mime_model_get_gtk_model(MimeModel* m) {
	return GTK_TREE_MODEL(m->filter);
}

GMimeObject* mime_model_root(MimeModel* m) {
	return m->message;
}

void mime_model_filter_inline(MimeModel* m, gboolean en) {
	m->filter_enabled = en;
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(m->filter));
}

const char* mime_model_content_type(GMimeObject* obj) {
	return g_mime_content_type_to_string(g_mime_object_get_content_type(obj));
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
		g_object_unref(m->message);
		g_object_unref(m->filter);
		g_free(m);
	}
}

GByteArray* mime_model_object_from_cid(GObject* emitter, const char* cid, gpointer user_data) {
	MimeModel* m = user_data;
	GMimeObject* part = g_mime_multipart_get_subpart_from_content_id(GMIME_MULTIPART(m->message), cid);
	// TODO: directly from bytearray
	GString str = mime_model_part_content(part, TRUE);
	return g_byte_array_new_take((guint8*)str.str, str.len);
}

static gint64 stream_test_len(GMimeStream* stream) {
	GMimeStream* null_stream = g_mime_stream_null_new();
	gint64 len = g_mime_stream_write_to_stream(stream, null_stream);
	g_mime_stream_reset(stream);
	g_object_unref(null_stream);
	return len;
}

static gsize write_stream_to_mem(GMimeStream* stream, gchar* ptr, gint len) {
	GByteArray* arr = g_byte_array_new_take((guint8*)ptr, len);
	GMimeStream* mem_stream = g_mime_stream_mem_new_with_byte_array(arr);
	g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(mem_stream), FALSE);
	g_mime_stream_reset(stream);
	g_mime_stream_write_to_stream(stream, mem_stream);
	g_mime_stream_reset(stream);
	GBytes* bytes = g_byte_array_free_to_bytes(arr);
	gsize sz;
	g_bytes_unref_to_data(bytes, &sz);
	ptr[sz] = '\0'; // can finally terminate here
	g_object_unref(mem_stream);
	return sz;
}
static void gstring_allocate(GString* str, gsize len) {
	str->str = g_malloc(len);
	str->len = len;
	str->allocated_len = len;
}

GString mime_model_part_headers(GMimeObject* obj) {
	GString ret;
	ret.str = g_mime_object_get_headers(obj);
	ret.len = ret.allocated_len = strlen(ret.str);
	return ret;
}

GString mime_model_part_content(GMimeObject* obj, gboolean in_data_uri) {
	GString ret = {0, 0, 0};
	if(!GMIME_IS_PART(obj))
		return ret;

	GMimePart* part = GMIME_PART(obj);

	GMimeDataWrapper* data_obj = g_mime_part_get_content_object(part);
	if(data_obj == NULL) // empty part
		return ret;

	GMimeStream* source = g_mime_data_wrapper_get_stream(data_obj);
	g_mime_stream_reset(source);
	GMimeContentEncoding encoding = g_mime_part_get_content_encoding(part);

	GMimeFilter* decoding_filter = g_mime_filter_basic_new(encoding, FALSE);
	GMimeFilter* encoding_filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_BINARY, TRUE);
	GMimeStream* stream_filter = g_mime_stream_filter_new(source);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), decoding_filter);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), encoding_filter);
	// get the length of the decoded data
	gint64 decoded_length = stream_test_len(stream_filter);
	// by allocating our own array we control its length
	gstring_allocate(&ret, decoded_length+1); // null termination
	write_stream_to_mem(stream_filter, ret.str, decoded_length);
	g_object_unref(decoding_filter);
	g_object_unref(encoding_filter);
	g_object_unref(stream_filter);

	return ret;
}

