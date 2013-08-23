/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */


#include <gtk/gtk.h>
#include <stdlib.h>
#include <webkit/webkit.h>
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
	WP_SIG_CONTENT,
	WP_SIG_GET_CID,
	WP_SIG_LAST
};

static gint wemed_panel_signals[WP_SIG_LAST] = {0};

// the private elements
typedef struct {
	GtkWidget* webview;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	GMimeObject* last_part;
	//WebKitWebContext* ctx;
	GtkWidget* progress_bar;
	gboolean html;
} WemedPanelPrivate;
#if 0
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
#endif

//static void load_changed_cb(WebKitWebView *web_view, WebKitLoadEvent load_event, WemedPanel* wp) {
static void load_changed_cb(WebKitWebView *web_view, WebKitLoadStatus status, WemedPanel* wp) {

	//GET_D(wp);
	return;
#if 0
	printf("load_changed_cb\n");
	(void) web_view; //unused
	switch(status) {
		case WEBKIT_LOAD_PROVISIONAL:
			gtk_widget_hide(d->webview);
			gtk_widget_show(d->progress_bar);
			break;
		case WEBKIT_LOAD_FINISHED:
			gtk_widget_hide(d->progress_bar);
			gtk_widget_show(d->webview);
			break;
		case WEBKIT_LOAD_FAILED:
			printf("failed...\n");
			break;
		default:
			break;
	}
#endif
}

static void progress_changed_cb(GObject* web_view, GdkEvent* e, WemedPanel* wp) {
	GET_D(wp);
	gdouble p;
	g_object_get(web_view, "progress", &p, NULL);
	//(void) pspec; //unused
	//double p = webkit_web_view_get_estimated_load_progress(web_view);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), p);
	if(p == 1) {
		gtk_widget_show(d->webview);
		gtk_widget_hide(d->progress_bar);
	}
}

static void hide_progress_bar(GtkWidget* container, GtkWidget* bar) {
	(void) container;
	gtk_widget_hide(bar);
}

char* wemed_panel_get_headers(WemedPanel* wp) {
	GET_D(wp);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(d->headertext, &start, &end);
	return gtk_text_buffer_get_text(d->headertext, &start, &end, TRUE);
}

char* wemed_panel_get_text_content(WemedPanel* wp, gboolean is_html) {
	GET_D(wp);
	gboolean source_view = webkit_web_view_get_view_source_mode(WEBKIT_WEB_VIEW(d->webview));
	WebKitDOMDocument* dom_doc = webkit_web_view_get_dom_document(WEBKIT_WEB_VIEW(d->webview));
	WebKitDOMHTMLElement* doc_element = WEBKIT_DOM_HTML_ELEMENT(webkit_dom_document_get_document_element(dom_doc));
	char* result;
	if(is_html && !source_view)
		result = webkit_dom_html_element_get_outer_html(doc_element);
	else
		result = webkit_dom_html_element_get_inner_text(doc_element);
	return result;
}

static void resource_request_starting_cb(WebKitWebView *web_view, WebKitWebFrame *web_frame, WebKitWebResource *web_resource, WebKitNetworkRequest *request, WebKitNetworkResponse *response, WemedPanel* wp) {
	GET_D(wp);
	// todo optionally disable external links
	const char* uri = webkit_network_request_get_uri(request);
	if(strncmp(uri, "cid:", 4) == 0) {
		char* image_data = 0;
		g_signal_emit(wp, wemed_panel_signals[WP_SIG_GET_CID], 0, &uri[4], &image_data);
		webkit_network_request_set_uri(request, image_data);
		free(image_data);
	} else {
		//webkit_network_request_set_uri(request, "about:blank");
	}
}

static void wemed_panel_init(WemedPanel* wp) {
	GET_D(wp);

	d->last_part = 0;
	GtkPaned* paned = GTK_PANED(wp);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (paned), GTK_ORIENTATION_VERTICAL);

	// configure the webkit context
	//d->ctx = webkit_web_context_get_default();
	//webkit_web_context_set_cache_model(d->ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	//webkit_set_cache_model(WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER); // put in main

	// create and configure all our widgets
	d->headerview = gtk_text_view_new();
	d->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->headerview));
	d->webview = webkit_web_view_new();
	//webkit_web_view_load_uri(WEBKIT_WEB_VIEW(d->webview), "http://www.gnome.org");
	d->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(d->progress_bar), "Loading");

	// connect everything
	//g_signal_connect(G_OBJECT(d->webview), "load-failed", G_CALLBACK(load_failed_cb), NULL);
	//g_signal_connect(G_OBJECT(d->webview), "notify::load-status", G_CALLBACK(load_changed_cb), wp);
	g_signal_connect(G_OBJECT(d->webview), "notify::progress", G_CALLBACK(progress_changed_cb), wp);
	g_signal_connect(G_OBJECT(d->webview), "resource-request-starting", G_CALLBACK(resource_request_starting_cb), wp);
	g_signal_connect(G_OBJECT(paned), "show", G_CALLBACK(hide_progress_bar), d->progress_bar);

	// layout
	GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->headerview);
	gtk_paned_pack1(GTK_PANED(paned), scroll, FALSE, FALSE);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->webview);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(box), d->progress_bar, FALSE, FALSE, 0);
	gtk_paned_pack2(GTK_PANED(paned), box, TRUE, TRUE);
	//gtk_paned_pack2(GTK_PANED(paned), d->progress_bar, TRUE, TRUE);
}

static void wemed_panel_class_init(WemedPanelClass* class) {
	g_type_class_add_private(class, sizeof(WemedPanelPrivate));
	wemed_panel_signals[WP_SIG_GET_CID] = g_signal_new(
			"cid-requested",
			G_TYPE_FROM_CLASS ((GObjectClass*)class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			0, // v-table offset
			NULL,
			NULL,
			NULL, //marshaller
			G_TYPE_STRING, // return tye
			1, // num args
			G_TYPE_STRING); // arg types
}

GtkWidget* wemed_panel_new() {
	return g_object_new(wemed_panel_get_type(), NULL);
}

void wemed_panel_load_doc(WemedPanel* wp, WemedPanelDocType type, const char* headers, const char* content) {
	GET_D(wp);
	wemed_panel_clear(wp);

	gtk_text_buffer_set_text(d->headertext, headers, strlen(headers));
	gtk_widget_set_sensitive(d->headerview, TRUE);

	webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), FALSE);

	if(type == WEMED_PANEL_DOC_TYPE_TEXT_HTML) {
		webkit_web_view_load_string(WEBKIT_WEB_VIEW(d->webview), content, "text/html", NULL, NULL);
		webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
		gtk_widget_show(d->progress_bar);
	} else if(type == WEMED_PANEL_DOC_TYPE_TEXT_PLAIN) {
		webkit_web_view_load_string(WEBKIT_WEB_VIEW(d->webview), content, "text/plain", NULL, NULL);
		webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
		gtk_widget_show(d->progress_bar);
	} else if(type == WEMED_PANEL_DOC_TYPE_IMAGE) {
		webkit_web_view_load_uri(WEBKIT_WEB_VIEW(d->webview), content);
		gtk_widget_show(d->progress_bar);
	}

}
void wemed_panel_show_source(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	webkit_web_view_set_view_source_mode(WEBKIT_WEB_VIEW(d->webview), en);
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
