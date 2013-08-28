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
	WP_SIG_GET_CID,
	WP_SIG_LAST
};

static gint wemed_panel_signals[WP_SIG_LAST] = {0};

// the private elements
typedef struct {
	GtkWidget* webview;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	GtkWidget* progress_bar;
	gboolean load_remote;
} WemedPanelPrivate;

// called when WebKit loads part of the HTML content. Used to dynamically
// update the position of the GTK progress bar
static void progress_changed_cb(GObject* web_view, GdkEvent* e, WemedPanel* wp) {
	GET_D(wp);
	gdouble p;
	g_object_get(web_view, "progress", &p, NULL);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), p);
	if(p == 1) { // loading is complete
		gtk_widget_show(d->webview); // this will also hide the progress bar
	}
}

// handler connected to webview::show, automatically hide the progress
// bar when the WebKit widget has finished loading
static void hide_progress_bar(GtkWidget* container, GtkWidget* bar) {
	gtk_widget_hide(bar);
}

char* wemed_panel_get_headers(WemedPanel* wp) {
	GET_D(wp);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(d->headertext, &start, &end);
	return gtk_text_buffer_get_text(d->headertext, &start, &end, TRUE);
}

char* wemed_panel_get_content(WemedPanel* wp, gboolean as_html_source) {
	GET_D(wp);
	gboolean source_view = webkit_web_view_get_view_source_mode(WEBKIT_WEB_VIEW(d->webview));
	WebKitDOMDocument* dom_doc = webkit_web_view_get_dom_document(WEBKIT_WEB_VIEW(d->webview));
	WebKitDOMHTMLElement* doc_element = WEBKIT_DOM_HTML_ELEMENT(webkit_dom_document_get_document_element(dom_doc));
	char* result;
	if(as_html_source && !source_view)
		result = webkit_dom_html_element_get_outer_html(doc_element);
	else
		result = webkit_dom_html_element_get_inner_text(doc_element);
	return result;
}

// this callback is called by WebKit when loading of external resources
// is requested. A handler allows us to provide images by content-id and
// perform optional tasks like blocking remote content
static void resource_request_starting_cb(WebKitWebView *web_view, WebKitWebFrame *web_frame, WebKitWebResource *web_resource, WebKitNetworkRequest *request, WebKitNetworkResponse *response, WemedPanel* wp) {
	GET_D(wp);
	// todo optionally disable external links
	const char* uri = webkit_network_request_get_uri(request);
	if(strncmp(uri, "cid:", 4) == 0) {
		char* image_data = 0;
		g_signal_emit(wp, wemed_panel_signals[WP_SIG_GET_CID], 0, &uri[4], &image_data);
		webkit_network_request_set_uri(request, image_data);
		free(image_data);
	} else if(strncmp(uri, "data:", 5) == 0) {
		// pass
	} else { // assume remote resource
		if(!d->load_remote)
			webkit_network_request_set_uri(request, "about:blank");
	}
}

static void wemed_panel_init(WemedPanel* wp) {
	GET_D(wp);

	GtkPaned* paned = GTK_PANED(wp);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (paned), GTK_ORIENTATION_VERTICAL);

	// create and configure all our widgets
	d->headerview = gtk_text_view_new();
	d->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->headerview));
	d->webview = webkit_web_view_new();
	d->progress_bar = gtk_progress_bar_new();

	// connect everything
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

void wemed_panel_load_doc(WemedPanel* wp, WemedPanelDocType type, const char* headers, const char* content, const char* charset) {
	GET_D(wp);
	wemed_panel_clear(wp);

	gtk_text_buffer_set_text(d->headertext, headers, strlen(headers));
	gtk_widget_set_sensitive(d->headerview, TRUE);

	webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), FALSE);
	//WebKitWebSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(d->webview));
	//g_object_set(G_OBJECT(settings), "default-encoding", charset, NULL);

	if(type == WEMED_PANEL_DOC_TYPE_TEXT_HTML) {
		webkit_web_view_load_string(WEBKIT_WEB_VIEW(d->webview), content, "text/html", charset, NULL);
		webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
	} else if(type == WEMED_PANEL_DOC_TYPE_TEXT_PLAIN) {
		webkit_web_view_load_string(WEBKIT_WEB_VIEW(d->webview), content, "text/plain", charset, NULL);
		webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
	} else if(type == WEMED_PANEL_DOC_TYPE_IMAGE) {
		webkit_web_view_load_uri(WEBKIT_WEB_VIEW(d->webview), content);
	} else
		return; // unhandled type!
	gtk_widget_show(d->progress_bar);
}

void wemed_panel_show_source(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	webkit_web_view_set_view_source_mode(WEBKIT_WEB_VIEW(d->webview), en);
}

void wemed_panel_load_remote_resources(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	d->load_remote = en;
}

void wemed_panel_clear(WemedPanel* wp) {
	GET_D(wp);
	// hide the web view
	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(d->webview));
	gtk_widget_hide(d->webview);
	// clear the header text
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(d->headertext, &start);
	gtk_text_buffer_get_end_iter(d->headertext, &end);
	gtk_text_buffer_delete(d->headertext, &start, &end);
	gtk_widget_set_sensitive(d->headerview, FALSE);
}
