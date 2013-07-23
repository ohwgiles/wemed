/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */


#include <gtk/gtk.h>
#include <stdlib.h>
#include <webkit2/webkit2.h>
#include <gmime/gmime.h>
#include <string.h>
#include "mimeapp.h"
#include "wemedpanel.h"
#include "openwith.h"

struct WemedPanel_S {
	GtkWidget* webview;
	GtkTextBuffer* headertext;
	WemedPanelHeaderCallback headers_changed;
	void* headers_changed_userdata;
	GMimeObject* current_obj;
	GtkWidget* progress_bar;
	GtkWidget* button_export;
	GtkWidget* button_open_default;
	GtkWidget* button_open_with;
	GtkWidget* button_apply;
	GtkWidget* button_revert;
	char* last_exec_path;
};

WebKitWebContext* ctx;

gboolean load_failed_cb(WebKitWebView *web_view, WebKitLoadEvent load_event, gchar *failing_uri, gpointer error, gpointer user_data) {
	(void) web_view; //unused
	(void) load_event; //unused
	(void) user_data; //unused
	printf("loading %s failed: %s\n", failing_uri, ((GError*)error)->message);
	return FALSE;
}

void wemed_panel_set_header_change_callback(WemedPanel* wp, WemedPanelHeaderCallback cb, void* ud) {
	wp->headers_changed = cb;
	wp->headers_changed_userdata = ud;
}

void load_document_part(WemedPanel* wp, GMimeObject* obj) {
	wp->current_obj = obj;

	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(wp->webview));
	webkit_web_context_clear_cache(ctx);
	//webkit_web_view_load_plain_text(wp->webview, "");

	if(GMIME_IS_MESSAGE(obj)) { 
// the subparts of this will be handled instead
		return;
	}

	char* str = g_mime_object_get_headers(obj);
	gtk_text_buffer_set_text(wp->headertext, str, strlen(str));
	free(str);

	if(GMIME_IS_PART(obj)) {
		GMimeContentType* ct = g_mime_object_get_content_type(obj);
		const char* content_type_name = g_mime_content_type_to_string(ct);

		printf("selected type name: %s\n", content_type_name);
		struct Application app = get_default_mime_app(content_type_name);
			printf("app name %s, path %s\n", app.name, app.exec);

		if(app.name) {
			char* new_button_label = malloc(strlen(app.name) + strlen("Open with ") + 1);
			sprintf(new_button_label, "Open with %s", app.name);
			free(app.name);
			gtk_button_set_label(GTK_BUTTON(wp->button_open_default), new_button_label);
			free(new_button_label);
			free(wp->last_exec_path);
			wp->last_exec_path = app.exec;
			gtk_widget_show(wp->button_open_default);
		} else {
			gtk_widget_hide(wp->button_open_default);
		}


		if(strcmp(content_type_name, "text/html") == 0) {
			printf("selected html\n");
			GMimeDataWrapper* mco = g_mime_part_get_content_object((GMimePart*)obj);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
			char* str = malloc(g_mime_stream_length(gms));
			g_mime_stream_read(gms, str, g_mime_stream_length(gms));
			g_mime_stream_reset(gms);
			webkit_web_view_load_html(WEBKIT_WEB_VIEW(wp->webview),  str, NULL);
			free(str);
			//gtk_widget_show(wp->webview);


		} else if(strncmp("image", content_type_name, 5) == 0) {
			int content_type_name_l = strlen(content_type_name);
			const char* content_encoding = g_mime_content_encoding_to_string(g_mime_part_get_content_encoding((GMimePart*)obj));
			int content_encoding_l = strlen(content_encoding);

			GMimeDataWrapper* mco = g_mime_part_get_content_object((GMimePart*)obj);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
			gint64 stream_length = g_mime_stream_length(gms);

			int header_length = 5 /*data:*/ + content_type_name_l + 1 /*;*/ + content_encoding_l + 1 /*,*/ ;
			char* str = malloc(header_length + stream_length + 1);
			sprintf(str, "data:%s;%s,", content_type_name, content_encoding);
			g_mime_stream_read(gms, &str[header_length], stream_length);
			str[header_length+stream_length] = '\0';
			g_mime_stream_reset(gms);
			webkit_web_view_load_uri(WEBKIT_WEB_VIEW(wp->webview),  str);
			free(str); 

			printf("selected a part!\n");
		} else {
			// don't know how to display this content, hide the widget
			gtk_widget_hide(wp->webview);
		}
	}
}

void cid_loading_cb(WebKitURISchemeRequest* request, gpointer user_data) {
	GHashTable* hash = (GHashTable*) user_data;
	const gchar* path = webkit_uri_scheme_request_get_path(request);
	GMimePart* part = g_hash_table_lookup(hash, path);
	if(part) {
		GMimeContentType* ct = g_mime_object_get_content_type((GMimeObject*)part);
		const char* content_type_name = g_mime_content_type_to_string(ct);
		GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
		GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
		GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), FALSE);
		GMimeStream* stream_filter = g_mime_stream_filter_new(gms);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
		GMimeStream* memstream = g_mime_stream_mem_new();
		g_mime_stream_write_to_stream(stream_filter, memstream);
		g_mime_stream_reset(gms);
		GByteArray* arr = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(memstream));
		GBytes* b = g_byte_array_free_to_bytes(arr);
		gsize sz;
		gpointer* data = g_bytes_unref_to_data(b, &sz);
		g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(memstream), FALSE);
		webkit_uri_scheme_request_finish(request, g_memory_input_stream_new_from_data(data, sz, g_free), sz, content_type_name);
		g_object_unref(memstream);
	} else {
		printf("could not find %s in hash\n", path);
	}
}

void load_changed_cb(WebKitWebView *web_view, WebKitLoadEvent load_event, WemedPanel* wp) {
	(void) web_view; //unused
	switch(load_event) {
	case WEBKIT_LOAD_STARTED:
		gtk_widget_hide(wp->webview);
		gtk_widget_show(wp->progress_bar);
		break;
	case WEBKIT_LOAD_FINISHED:
		gtk_widget_hide(wp->progress_bar);
		gtk_widget_show(wp->webview);
		break;
	default:
		break;
	}
}

void progress_changed_cb(WebKitWebView* web_view, GParamSpec* pspec, WemedPanel* wp) {
	(void) pspec; //unused
	double p = webkit_web_view_get_estimated_load_progress(web_view);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(wp->progress_bar), p);
}

static void write_part_to_file(GMimePart* part, FILE* fp) {
	GMimeStream* gms = g_mime_data_wrapper_get_stream(g_mime_part_get_content_object(part));
	GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), FALSE);
	GMimeStream* stream_filter = g_mime_stream_filter_new(gms);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
	GMimeStream* filestream = g_mime_stream_file_new(fp);
	g_mime_stream_write_to_stream(stream_filter, filestream);
	g_mime_stream_reset(gms);
	g_object_unref(filestream);
}

static void open_default_cb(GtkButton* button, WemedPanel* wp) {
	(void) button; //unused
	char* tmpfile = strdup("wemed-tmpfile-XXXXXX");
	int fd = mkstemp(tmpfile);
	FILE* fp = fdopen(fd, "wb");
	write_part_to_file(GMIME_PART(wp->current_obj), fp);
	char* execpath = malloc(strlen(wp->last_exec_path) + strlen(tmpfile) + 2);
	sprintf(execpath, "%s %s", wp->last_exec_path, tmpfile);
	system(execpath);
	free(execpath);
	unlink(tmpfile); // be a tidy kiwi
}

static void open_with_cb(GtkButton* button, WemedPanel* wp) {
	(void) button; //unused
	const char* content_type_name = g_mime_content_type_to_string(g_mime_object_get_content_type(wp->current_obj));
	char* exec = open_with(NULL, content_type_name);
	if(!exec) return;

	char* tmpfile = strdup("wemed-tmpfile-XXXXXX");
	int fd = mkstemp(tmpfile);
	FILE* fp = fdopen(fd, "wb");
	write_part_to_file(GMIME_PART(wp->current_obj), fp);

	char* buffer = malloc(strlen(tmpfile) + strlen(exec) + 5);
	char* p;
	if( (p = strstr(exec, "%f")) || (p = strstr(exec, "%U")) || (p = strstr(exec, "%s"))) {
		p[1] = 's';
		sprintf(buffer, exec, tmpfile);
	} else {
		sprintf(buffer, "%s %s", exec, tmpfile);
	}

	system(buffer);
	free(buffer);
	unlink(tmpfile);
	free(exec);
}
		

static void export_cb(GtkButton* button, WemedPanel* wp) {
	(void) button; //unused
	const char* file = "/tmp/testoutput";
	FILE* fp = fopen(file, "wb");
	write_part_to_file(GMIME_PART(wp->current_obj), fp);
}

static void hide_progress_bar(GtkWidget* container, GtkWidget* bar) {
	(void) container;
	gtk_widget_hide(bar);
}

static void headers_changed_cb(GtkTextBuffer* text, WemedPanel* wp) {
	gtk_widget_set_sensitive(wp->button_apply, TRUE);
	gtk_widget_set_sensitive(wp->button_revert, TRUE);
}

static void headers_apply_cb(GtkButton* apply, WemedPanel* wp) {
	if(wp->headers_changed) {
		GtkTextIter start, end;
		gtk_text_buffer_get_bounds(wp->headertext, &start, &end);
		gchar* new_header = gtk_text_buffer_get_text(wp->headertext, &start, &end, TRUE);
		GMimeObject* new_part = (*wp->headers_changed)(wp->headers_changed_userdata, wp->current_obj, new_header);
		if(new_part) {
			load_document_part(wp, new_part);
			printf("header successfully updated\n");
		} else {
			printf("failed to update header\n");
		}
	}
}

WemedPanel* wemed_panel_create(GtkWidget* parent, GHashTable* cidhash) {
	// create and initialise our data structure
	WemedPanel* wp = (WemedPanel*) malloc(sizeof(WemedPanel));
	wp->last_exec_path = 0;

	// configure the webkit context
	ctx = webkit_web_context_get_default();
	webkit_web_context_set_cache_model(ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	webkit_web_context_register_uri_scheme(ctx, "cid", cid_loading_cb, cidhash, NULL);

	// create and configure all our widgets
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget* hdbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			GtkWidget* headers = gtk_text_view_new();
			wp->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(headers));
			GtkWidget* bvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
				wp->button_apply = gtk_button_new_with_label("Apply");
				gtk_widget_set_sensitive(wp->button_apply, FALSE);
				wp->button_revert = gtk_button_new_with_label("Revert");
				gtk_widget_set_sensitive(wp->button_revert, FALSE);
		wp->webview = webkit_web_view_new();
		wp->progress_bar = gtk_progress_bar_new();
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(wp->progress_bar), "Loading");
		GtkWidget* buttonlayout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			wp->button_export = gtk_button_new_with_label("Export segment");
			wp->button_open_default = gtk_button_new_with_label("Open with default handler");
			wp->button_open_with = gtk_button_new_with_label("Open with...");

	// connect everything

	g_signal_connect(G_OBJECT(wp->webview), "load-failed", G_CALLBACK(load_failed_cb), NULL);
	g_signal_connect(G_OBJECT(wp->webview), "load-changed", G_CALLBACK(load_changed_cb), wp);
	g_signal_connect(G_OBJECT(wp->webview), "notify::estimated-load-progress", G_CALLBACK(progress_changed_cb), wp);
	g_signal_connect(G_OBJECT(wp->headertext), "changed", G_CALLBACK(headers_changed_cb), wp);
	g_signal_connect(G_OBJECT(wp->button_apply), "clicked", G_CALLBACK(headers_apply_cb), wp);
	g_signal_connect(G_OBJECT(vbox), "show", G_CALLBACK(hide_progress_bar), wp->progress_bar);
	g_signal_connect(G_OBJECT(wp->button_export), "clicked", G_CALLBACK(export_cb), wp);
	g_signal_connect(G_OBJECT(wp->button_open_default), "clicked", G_CALLBACK(open_default_cb), wp);
	g_signal_connect(G_OBJECT(wp->button_open_with), "clicked", G_CALLBACK(open_with_cb), wp);


	// layout
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Headers:"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hdbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hdbox), headers, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(hdbox), bvbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bvbox), wp->button_apply, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(bvbox), wp->button_revert, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Content:"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), wp->webview, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), wp->progress_bar, TRUE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), buttonlayout, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttonlayout), wp->button_open_default, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttonlayout), wp->button_open_with, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttonlayout), wp->button_export, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(parent), vbox, TRUE, TRUE, 0);
	return wp;
}








