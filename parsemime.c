#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include <string.h>
#include "parsemime.h"

struct TreeInsertHelper {
	GtkTreeStore* ts;
	GtkTreeIter current;
	GtkTreeIter child;
	GMimeObject* current_multipart;
	GtkTreeIter lastparent;
	GHashTable* hash;
};
#include <gdk-pixbuf/gdk-pixbuf.h>
void parse_mime_segment(GMimeObject *up, GMimeObject *part, gpointer user_data) {

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
		gtk_tree_store_append(h->ts, &h->child, &h->current);
		gtk_tree_store_set(h->ts, &h->child, 0, g_mime_content_type_to_string(ct), 1, part, -1);
		GtkTreeIter parent = h->current;
		h->current = h->child;
		h->current_multipart = part;
		g_mime_multipart_foreach(part, parse_mime_segment, h);
		h->current_multipart = up;
		printf("leaving multipart loop\n");
		h->current = parent;
	} else if (GMIME_IS_PART (part)) {
		printf("part\n");
		// icon
		GtkIconTheme* git = gtk_icon_theme_get_default();
		GMimeContentType* ct = g_mime_object_get_content_type(part);
		gtk_tree_store_append(h->ts, &h->child, &h->current);
		const char* content_type_name = g_mime_content_type_to_string(ct);
		char* icon_name = strdup(content_type_name);
		for(char* p = strchr(icon_name,'/'); p != NULL; p = strchr(p, '/')) *p = '-';
		GdkPixbuf* icon = gtk_icon_theme_load_icon(git, icon_name, 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
		free(icon_name);
		
		// add to tree
		gtk_tree_store_set(h->ts, &h->child, 0, g_mime_content_type_to_string(ct), 1, part, 2, icon, -1);

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
			g_hash_table_insert(h->hash, strdup(cid), part);
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

MimeModel parse_mime_file(const char* filename) {
	MimeModel m = {0};
	FILE* fp = fopen(filename, "rb");
	if(!fp) return m;

	g_mime_init(0);
	
	GMimeStream* gfs = g_mime_stream_file_new(fp);
	if(!gfs) return m;

	GMimeParser* parser = g_mime_parser_new_with_stream(gfs);
	if(!parser) return m;

	GMimeMessage* message = g_mime_parser_construct_message(parser);
	if(!message) return m;

	struct TreeInsertHelper h = {0};
	h.ts = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_OBJECT);
	gtk_tree_store_append(h.ts, &h.current, NULL);
	gtk_tree_store_set(h.ts, &h.current, 0, filename, 1, message, -1);

	h.hash = g_hash_table_new(g_str_hash, g_str_equal);

	//g_mime_message_foreach(message, parse_mime_segment, &h);
	parse_mime_segment(NULL, g_mime_message_get_mime_part(message), &h);

	m.model = gtk_tree_model_filter_new(GTK_TREE_MODEL(h.ts), NULL);
	m.cidhash = h.hash;

	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(m.model), is_content_disposition_inline, NULL, NULL);

	return m;
}

