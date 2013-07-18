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
#include "parsemime.h"

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

GHashTable* mime_model_get_cid_hash(MimeModel* m) {
	return m->cidhash;
}

struct TreeInsertHelper {
	MimeModel* m;
	GtkTreeIter current;
	GtkTreeIter child;
	GMimeObject* current_multipart;
	GtkTreeIter lastparent;
};


#include <gdk-pixbuf/gdk-pixbuf.h>
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
		printf("multipart!\n");
		GMimeContentType* ct = g_mime_object_get_content_type(part);
		printf("(%s)\n", g_mime_content_type_to_string(ct));
		gtk_tree_store_append(h->m->store, &h->child, &h->current);
		gtk_tree_store_set(h->m->store, &h->child, 0, g_mime_content_type_to_string(ct), 1, part, -1);
		GtkTreeIter parent = h->current;
		h->current = h->child;
		h->current_multipart = part;
		g_mime_multipart_foreach(GMIME_MULTIPART(part), parse_mime_segment, h);
		h->current_multipart = up;
		printf("leaving multipart loop\n");
		h->current = parent;
	} else if (GMIME_IS_PART (part)) {
		printf("part\n");
		// icon
		GtkIconTheme* git = gtk_icon_theme_get_default();
		GMimeContentType* ct = g_mime_object_get_content_type(part);
		gtk_tree_store_append(h->m->store, &h->child, &h->current);
		const char* content_type_name = g_mime_content_type_to_string(ct);
		char* icon_name = strdup(content_type_name);
		for(char* p = strchr(icon_name,'/'); p != NULL; p = strchr(p, '/')) *p = '-';
		GdkPixbuf* icon = gtk_icon_theme_load_icon(git, icon_name, 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
		free(icon_name);
		
		// add to tree
		gtk_tree_store_set(h->m->store, &h->child, 0, g_mime_content_type_to_string(ct), 1, part, 2, icon, -1);

		// add to hash
		const char* cid = g_mime_part_get_content_id((GMimePart*)part);
		if(cid) {
			/*
			GMimeDataWrapper* gdw = g_mime_part_get_content_object(part);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(gdw);
			gint64 l = g_mime_stream_length(gms);
			struct Buffer* b = (struct Buffer*) malloc(sizeof(struct Buffer)+l);
			//char* b = malloc(l);
			b->len = l;
			g_mime_stream_read(gms, b->data, l);
			//GMimeContentType* ct = g_mime_object_get_content_type(part);

			//WebKitWebResource* wr = webkit_web_resource_new(b, l, cid, g_mime_content_type_to_string(ct), g_mime_content_encoding_to_string(g_mime_part_get_content_encoding(part)), NULL);
			*/
			g_hash_table_insert(h->m->cidhash, strdup(cid), part);
//			printf("added %s at %p\n",cid, b);
		}
	} else {
		printf("unknown type\n");
	}
}

gboolean is_content_disposition_inline(GtkTreeModel* gtm, GtkTreeIter* iter, gpointer user_data) {
	return TRUE;
	GValue v = {0};//gtk_value_new();
	gtk_tree_model_get_value(gtm, iter, 1, &v);
	GMimeObject* part = (GMimeObject*) g_value_get_pointer(&v);
	g_value_unset(&v);
	if(GMIME_IS_PART(part)) {
		const char* disposition = g_mime_object_get_disposition(part);
		if(disposition && strcmp(disposition, "inline") == 0) return FALSE;
//		printf("found section with disposition %s\n", disposition);
	}
	return TRUE;
}

MimeModel* mime_model_create_empty() {
	return NULL;
}

struct PartFinder {
	GtkTreeIter* iter;
	GMimeObject* obj;
};

gboolean find_part(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter, void* user_data) {
	struct PartFinder* p = (struct PartFinder*) user_data;
	GMimeObject* obj = NULL;
	gtk_tree_model_get(model, iter, 1, &obj, -1);
	printf("comparing %p with %p\n", obj, p->obj);
	if(obj == p->obj) {
		printf("matched!\n");
		p->iter = iter;
		return TRUE;
	}
	return FALSE;
}

gboolean mime_model_update_header(void* user_data, GMimeObject* part_old, const char* new_header) {
	MimeModel* m = user_data;
	GMimeStream* memstream = g_mime_stream_mem_new_with_buffer(new_header, strlen(new_header));
	GMimeParser* parse = g_mime_parser_new_with_stream(memstream);
	GMimeObject* part_new = g_mime_parser_construct_part(parse);
	if(!part_new) {
		printf("parsing failed\n"); // todo dialog box
		return FALSE;
	}

	printf("parsed headers: %s\n", g_mime_object_get_headers(part_new));
	if(GMIME_IS_MULTIPART(part_new) != GMIME_IS_MULTIPART(part_old) || GMIME_IS_PART(part_new) != GMIME_IS_PART(part_old)) {
		printf("apply failed, cannot change part type\n"); // todo dialog box
		return FALSE;
	}
	if(GMIME_IS_MULTIPART(part_new) && GMIME_IS_MULTIPART(part_old)) {
		// make sure the boundaries are equal
		const char* boundary_old = g_mime_multipart_get_boundary(GMIME_MULTIPART(part_old));
		const char* boundary_new = g_mime_multipart_get_boundary(GMIME_MULTIPART(part_new));
		printf("comparing %s with %s\n", boundary_old, boundary_new);
		if(strcmp(boundary_old, boundary_new) != 0) {
			printf("cannot modify boundary\n"); //todo dialog
			return FALSE;
		}
	}
	// if we got here, all is well, so we can replace the old headers with the new ones

	if(GMIME_IS_PART(part_new)) {
		g_mime_part_set_content_object(GMIME_PART(part_new), g_mime_part_get_content_object(GMIME_PART(part_old)));
	}

	struct PartFinder p;
	p.obj = part_old;
	gtk_tree_model_foreach(GTK_TREE_MODEL(m->store), find_part, &p);

	gtk_tree_store_set_value(m->store, p.iter, 1, part_new);

	//GMimePartIter* iter = g_mime_part_iter_new(part_old);
	//printf("want to replace %s\n", g_mime_part_iter_get_path(iter));
	//g_mime_part_iter_replace(iter, part_new);
	//g_mime_part_iter_free(iter);
	//mime_model_reparse(m);
	return TRUE;
}

void mime_model_reparse(MimeModel* m) {
	struct TreeInsertHelper h = {0};
	h.m = m;
	gtk_tree_store_clear(m->store);
	gtk_tree_store_append(m->store, &h.current, NULL);
	gtk_tree_store_set(m->store, &h.current, 0, m->name, 1, m->message, -1);
	//reparse_segment(
	parse_mime_segment(NULL, g_mime_message_get_mime_part(m->message), &h);
}

MimeModel* mime_model_create_from_file(const char* filename) {
	g_mime_init(0);
	MimeModel* m = malloc(sizeof(MimeModel));
	
	m->store = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_OBJECT);
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

