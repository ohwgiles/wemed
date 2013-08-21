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

G_DEFINE_TYPE(WemedPanel, wemed_panel, GTK_TYPE_PANED);
#define GET_D(o) WemedPanelPrivate* d = (G_TYPE_INSTANCE_GET_PRIVATE ((o), wemed_panel_get_type(), WemedPanelPrivate))

// signals
enum {
	WP_SIG_HEADERS,
	WP_SIG_LAST
};

static gint wemed_panel_signals[WP_SIG_LAST] = {0};

// the private elements
typedef struct {
	GtkWidget* webview;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	WebKitWebContext* ctx;
	GtkWidget* progress_bar;
} WemedPanelPrivate;

static gboolean load_failed_cb(WebKitWebView *web_view, WebKitLoadEvent load_event, gchar *failing_uri, gpointer error, gpointer user_data) {
	(void) web_view; //unused
	(void) load_event; //unused
	(void) user_data; //unused
	printf("loading %s failed: %s\n", failing_uri, ((GError*)error)->message);
	return FALSE;
}

static void cid_loading_cb(WebKitURISchemeRequest* request, gpointer user_data) {
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

static void load_changed_cb(WebKitWebView *web_view, WebKitLoadEvent load_event, WemedPanel* wp) {
	GET_D(wp);
	(void) web_view; //unused
	switch(load_event) {
		case WEBKIT_LOAD_STARTED:
			gtk_widget_hide(d->webview);
			gtk_widget_show(d->progress_bar);
			break;
		case WEBKIT_LOAD_FINISHED:
			gtk_widget_hide(d->progress_bar);
			gtk_widget_show(d->webview);
			break;
		default:
			break;
	}
}

static void progress_changed_cb(WebKitWebView* web_view, GParamSpec* pspec, WemedPanel* wp) {
	GET_D(wp);
	(void) pspec; //unused
	double p = webkit_web_view_get_estimated_load_progress(web_view);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), p);
}

static void hide_progress_bar(GtkWidget* container, GtkWidget* bar) {
	(void) container;
	gtk_widget_hide(bar);
}

static gboolean headers_updated_cb(GtkWidget* headerview, GdkEvent* event, WemedPanel* wp) {
	GET_D(wp);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(d->headertext, &start, &end);
	gchar* new_header = gtk_text_buffer_get_text(d->headertext, &start, &end, TRUE);
	g_signal_emit(wp, wemed_panel_signals[WP_SIG_HEADERS], 0, new_header);
	return FALSE;
}

static void wemed_panel_init(WemedPanel* wp) {
	GET_D(wp);

	GtkPaned* paned = GTK_PANED(wp);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (paned), GTK_ORIENTATION_VERTICAL);

	// configure the webkit context
	d->ctx = webkit_web_context_get_default();
	webkit_web_context_set_cache_model(d->ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

	// create and configure all our widgets
	d->headerview = gtk_text_view_new();
	d->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->headerview));
	d->webview = webkit_web_view_new();
	d->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(d->progress_bar), "Loading");

	// connect everything
	g_signal_connect(G_OBJECT(d->webview), "load-failed", G_CALLBACK(load_failed_cb), NULL);
	g_signal_connect(G_OBJECT(d->webview), "load-changed", G_CALLBACK(load_changed_cb), wp);
	g_signal_connect(G_OBJECT(d->webview), "notify::estimated-load-progress", G_CALLBACK(progress_changed_cb), wp);
	g_signal_connect(G_OBJECT(d->headerview), "focus-out-event", G_CALLBACK(headers_updated_cb), wp);
	g_signal_connect(G_OBJECT(paned), "show", G_CALLBACK(hide_progress_bar), d->progress_bar);

	// layout
	GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->headerview);
	gtk_paned_pack1(GTK_PANED(paned), scroll, FALSE, FALSE);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->webview);
	gtk_paned_pack2(GTK_PANED(paned), scroll, TRUE, TRUE);
}

static void wemed_panel_class_init(WemedPanelClass* class) {
	g_type_class_add_private(class, sizeof(WemedPanelPrivate));
	wemed_panel_signals[WP_SIG_HEADERS] = g_signal_new(
			"headers-changed",
			G_TYPE_FROM_CLASS ((GObjectClass*)class),
			G_SIGNAL_RUN_FIRST,
			0, // v-table offset
			NULL,
			NULL,
			NULL, //marshaller
			G_TYPE_NONE, // return tye
			1, // num args
			G_TYPE_STRING); // arg types
}

GtkWidget* wemed_panel_new() {
	return g_object_new(wemed_panel_get_type(), NULL);
}

void wemed_panel_set_cid_table(WemedPanel* wp, GHashTable* hash) {
	GET_D(wp);
	//webkit_web_context_register_uri_scheme(d->ctx, "cid", cid_loading_cb, hash, NULL);
}

void wemed_panel_load_part(WemedPanel* wp, GMimeObject* obj, const char* content_type_name) {
	GET_D(wp);
	wemed_panel_clear(wp);

	if(GMIME_IS_MESSAGE(obj)) {
		// the subparts of this will be handled instead
		return;
	}

	char* str = g_mime_object_get_headers(obj);
	gtk_text_buffer_set_text(d->headertext, str, strlen(str));
	free(str);

	gtk_widget_set_sensitive(d->headerview, TRUE);

	if(GMIME_IS_PART(obj)) {
		if(strcmp(content_type_name, "text/html") == 0) {
			GMimeDataWrapper* mco = g_mime_part_get_content_object((GMimePart*)obj);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
			gint64 len = g_mime_stream_length(gms);
			char* str = malloc(g_mime_stream_length(gms)+1);
			g_mime_stream_read(gms, str, len);
			str[len] = '\0';
			g_mime_stream_reset(gms);
			printf("read %lld bytes\n", len);
			webkit_web_view_load_html(WEBKIT_WEB_VIEW(d->webview),  str, NULL);
			free(str);
			gtk_widget_show(d->webview);
		} else if(strncmp("image", content_type_name, 5) == 0) {
			GMimeDataWrapper* mco = g_mime_part_get_content_object((GMimePart*)obj);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
			gint64 len = g_mime_stream_length(gms);
			const char* content_encoding = g_mime_content_encoding_to_string(g_mime_part_get_content_encoding((GMimePart*)obj));
			int header_length = 5 /*data:*/ + strlen(content_type_name) + 1 /*;*/ + strlen(content_encoding) + 1 /*,*/ ;
			char* str = malloc(header_length + len + 1);
			sprintf(str, "data:%s;%s,", content_type_name, content_encoding);
			g_mime_stream_read(gms, &str[header_length], len);
			str[header_length + len] = '\0';
			g_mime_stream_reset(gms);
			printf("read %lld bytes\n", len);
			webkit_web_view_load_uri(WEBKIT_WEB_VIEW(d->webview),  str);
			free(str);
			gtk_widget_show(d->webview);
		} else {
			// don't know how to display this content, hide the widget
			// TODO show "cannot display this content"
			printf("cannot display type %s\n", content_type_name);
		}
	}
}

void wemed_panel_clear(WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(d->webview));
	gtk_widget_hide(d->webview);
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(d->headertext, &start);
	gtk_text_buffer_get_end_iter(d->headertext, &end);
	gtk_text_buffer_delete(d->headertext, &start, &end);
	gtk_widget_set_sensitive(d->headerview, FALSE);
}
