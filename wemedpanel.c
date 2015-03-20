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

#include <libintl.h>
#define _(str) gettext(str)

extern GtkIconTheme* system_icon_theme;
G_DEFINE_TYPE(WemedPanel, wemed_panel, GTK_TYPE_PANED);
#define GET_D(o) WemedPanelPrivate* d = (G_TYPE_INSTANCE_GET_PRIVATE ((o), wemed_panel_get_type(), WemedPanelPrivate))

// signals
enum {
	WP_SIG_GET_CID,
	WP_SIG_IMPORT,
	WP_SIG_DIRTIED,
	WP_SIG_LAST
};

static gint wemed_panel_signals[WP_SIG_LAST] = {0};

// the private elements
typedef struct {
	GtkWidget* webview;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	GtkWidget* toolbar;
	GtkWidget* progress_bar;
	gboolean load_remote;
	gint dirty_signals[2];
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

GString wemed_panel_get_headers(WemedPanel* wp) {
	GET_D(wp);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(d->headertext, &start, &end);
	GString s;
	s.len = gtk_text_iter_get_offset(&end);
	s.str = gtk_text_buffer_get_text(d->headertext, &start, &end, TRUE);
	return s;
}

GString wemed_panel_get_content(WemedPanel* wp, gboolean as_html_source) {
	GET_D(wp);
	gboolean source_view = webkit_web_view_get_view_source_mode(WEBKIT_WEB_VIEW(d->webview));
	WebKitDOMDocument* dom_doc = webkit_web_view_get_dom_document(WEBKIT_WEB_VIEW(d->webview));
	WebKitDOMHTMLElement* doc_element = WEBKIT_DOM_HTML_ELEMENT(webkit_dom_document_get_document_element(dom_doc));
	GString result;
	if(as_html_source && !source_view)
		result.str = webkit_dom_html_element_get_outer_html(doc_element);
	else
		result.str = webkit_dom_html_element_get_inner_text(doc_element);
	result.len = strlen(result.str);
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

static void edit_bold_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('bold',false,false)");
}
static void edit_italic_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('italic',false,false)");
}
static void edit_uline_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('underline',false,false)");
}
static void edit_strike_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('strikethrough',false,false)");
}
static void edit_font_cb(GtkFontButton* fb, WemedPanel* wp) {
	GET_D(wp);
	PangoFontDescription* font = pango_font_description_from_string(gtk_font_button_get_font_name(fb));
	const char* family = pango_font_description_get_family(font);
	int size = pango_font_description_get_size(font);
	char* family_cmd;
	asprintf(&family_cmd, "document.execCommand('fontname', false, '%s')", family);
	char* size_cmd;
	asprintf(&size_cmd, "window.getSelection().getRangeAt(0).commonAncestorContainer.parentNode.style.fontSize = '%dpt';", size/PANGO_SCALE);
	if(pango_font_description_get_style(font) == PANGO_STYLE_ITALIC)
		webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('italic',false,false)");
	if(pango_font_description_get_weight(font) > PANGO_WEIGHT_MEDIUM)
		webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('bold',false,false)");
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), family_cmd);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), size_cmd);
	free(family_cmd);
	free(size_cmd);
	pango_font_description_free(font);
}
static void edit_ltr_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.body.style.direction = 'ltr'");
}
static void edit_rtl_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.body.style.direction = 'rtl'");
}

static void update_preview_cb(GtkFileChooser *file_chooser, gpointer data) {
	GtkWidget *preview;
	char *filename;
	GdkPixbuf *pixbuf;
	gboolean have_preview;

	preview = GTK_WIDGET (data);
	filename = gtk_file_chooser_get_preview_filename (file_chooser);

	pixbuf = gdk_pixbuf_new_from_file_at_size (filename, 128, 128, NULL);
	have_preview = (pixbuf != NULL);
	g_free (filename);

	gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pixbuf);
	if (pixbuf)
		g_object_unref (pixbuf);

	gtk_file_chooser_set_preview_widget_active (file_chooser, have_preview);
}
static void edit_image_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Select images"),
			NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN,
			_("Cancel"), GTK_RESPONSE_CANCEL,
			_("Open"), GTK_RESPONSE_ACCEPT,
			NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
	GtkWidget *preview = gtk_image_new ();
	gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(dialog), preview);
	g_signal_connect(dialog, "update-preview", G_CALLBACK (update_preview_cb), preview);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		GSList* files = gtk_file_chooser_get_files(GTK_FILE_CHOOSER(dialog));
		GSList* p = files;
		while(p) {
			GFile* f = p->data;
			char* path = g_file_get_path(f);
			char* cid = 0;
			g_signal_emit(wp, wemed_panel_signals[WP_SIG_IMPORT], 0, path, &cid);
			char* exec = 0;
			asprintf(&exec, "document.execCommand('insertimage', false, 'cid:%s')", cid);
			webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), exec);
			free(cid);
			free(exec);
			g_free(path);
			g_object_unref(f);
			p = p->next;
		}
		g_slist_free(files);
	}

	g_object_unref(preview);
	gtk_widget_destroy (dialog);
}

static gboolean filter_variant_fonts(const PangoFontFamily* family, const PangoFontFace* face, gpointer data) {
	gboolean include = FALSE;
	PangoFontDescription* font = pango_font_face_describe((PangoFontFace*) face);
	if(pango_font_description_get_style(font) == PANGO_STYLE_NORMAL && pango_font_description_get_weight(font) <= PANGO_WEIGHT_NORMAL)
		include = TRUE;
	pango_font_description_free(font);
	return include;
}

static void dirtied_cb(GObject* emitter, WemedPanel* wp) {
	GET_D(wp);
	if(emitter == G_OBJECT(d->webview) || 
			(emitter == G_OBJECT(d->headertext) && gtk_text_buffer_get_modified(d->headertext)))
		g_signal_emit(wp, wemed_panel_signals[WP_SIG_DIRTIED], 0);
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

	GtkWidget* toolbar = gtk_toolbar_new();
	{
		GtkToolItem* bold = gtk_tool_button_new(0, _("Bold"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(bold), "format-text-bold");
		g_signal_connect(G_OBJECT(bold), "clicked", G_CALLBACK(edit_bold_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), bold, -1);
		GtkToolItem* italic = gtk_tool_button_new(0, _("Italic"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(italic), "format-text-italic");
		g_signal_connect(G_OBJECT(italic), "clicked", G_CALLBACK(edit_italic_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), italic, -1);
		GtkToolItem* uline = gtk_tool_button_new(0, _("Underline"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(uline), "format-text-underline");
		g_signal_connect(G_OBJECT(uline), "clicked", G_CALLBACK(edit_uline_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), uline, -1);
		GtkToolItem* strike = gtk_tool_button_new(0, _("Strikethrough"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(strike), "format-text-strikethrough");
		g_signal_connect(G_OBJECT(strike), "clicked", G_CALLBACK(edit_strike_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), strike, -1);
		GtkToolItem* font = gtk_tool_item_new();
		GtkWidget* fb = gtk_font_button_new();
		gtk_font_chooser_set_filter_func(GTK_FONT_CHOOSER(fb), filter_variant_fonts, NULL, NULL);
		gtk_font_button_set_show_style(GTK_FONT_BUTTON(fb), FALSE);
		gtk_font_button_set_use_font(GTK_FONT_BUTTON(fb), TRUE);
		gtk_container_add(GTK_CONTAINER(font), fb);
		g_signal_connect(G_OBJECT(fb), "font-set", G_CALLBACK(edit_font_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), font, -1);
		GtkToolItem* img = gtk_tool_button_new(0, _("Insert image"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(img), "insert-image");
		g_signal_connect(G_OBJECT(img), "clicked", G_CALLBACK(edit_image_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), img, -1);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);
		GtkToolItem* ltr = gtk_tool_button_new(0, _("Align left"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(ltr), "format-justify-left");
		g_signal_connect(G_OBJECT(ltr), "clicked", G_CALLBACK(edit_ltr_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ltr, -1);
		GtkToolItem* rtl = gtk_tool_button_new(0, _("Align right"));
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(rtl), "format-justify-right");
		g_signal_connect(G_OBJECT(rtl), "clicked", G_CALLBACK(edit_rtl_cb), wp);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), rtl, -1);
	}
	gtk_widget_set_sensitive(toolbar, false);
	d->toolbar = toolbar;

	// layout
	GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->headerview);
	gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, FALSE);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->webview);

	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(box), d->progress_bar, FALSE, FALSE, 3);
	
	gtk_paned_pack2(GTK_PANED(paned), box, TRUE, TRUE);

	gtk_paned_set_position(paned, 100);
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
	wemed_panel_signals[WP_SIG_IMPORT] = g_signal_new(
			"import-file",
			G_TYPE_FROM_CLASS ((GObjectClass*)class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			0, // v-table offset
			NULL,
			NULL,
			NULL, //marshaller
			G_TYPE_STRING, // return tye
			1, // num args
			G_TYPE_STRING); // arg types
	wemed_panel_signals[WP_SIG_DIRTIED] = g_signal_new(
			"dirtied",
			G_TYPE_FROM_CLASS ((GObjectClass*)class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			0,
			NULL,
			NULL,
			NULL,
			G_TYPE_NONE,
			0);
}

GtkWidget* wemed_panel_new() {
	return g_object_new(wemed_panel_get_type(), NULL);
}

void wemed_panel_load_doc(WemedPanel* wp, WemedPanelDoc doc) {
	GET_D(wp);
	if(d->dirty_signals[0])
		g_signal_handler_disconnect(G_OBJECT(d->webview), d->dirty_signals[0]);
	if(d->dirty_signals[1])
		g_signal_handler_disconnect(G_OBJECT(d->headertext), d->dirty_signals[1]);
	wemed_panel_clear(wp);

	gtk_text_buffer_set_text(d->headertext, doc.headers.str, doc.headers.len);
	gtk_text_buffer_set_modified(d->headertext, FALSE);
	gtk_widget_set_sensitive(d->headerview, TRUE);

	webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), FALSE);
	gtk_widget_set_sensitive(d->toolbar, FALSE);
	if(strncmp(doc.content_type, "text/", 5) == 0) {
		webkit_web_view_load_string(WEBKIT_WEB_VIEW(d->webview), doc.content.str, doc.content_type, doc.charset, NULL);
		webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
		webkit_web_view_execute_script(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('styleWithCSS',false,true)");
		if(strncmp(&doc.content_type[5], "html", 4) == 0)
			gtk_widget_set_sensitive(d->toolbar, TRUE);
	} else if(doc.content.str) {
		webkit_web_view_load_uri(WEBKIT_WEB_VIEW(d->webview), doc.content.str);
	}
	
	// start the progress bar, it should be hidden on completion of load
	gtk_widget_show(d->progress_bar);

	d->dirty_signals[0] = g_signal_connect(G_OBJECT(d->webview), "user-changed-contents", G_CALLBACK(dirtied_cb), wp);
	d->dirty_signals[1] = g_signal_connect(G_OBJECT(d->headertext), "modified-changed", G_CALLBACK(dirtied_cb), wp);
}

void wemed_panel_show_source(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	webkit_web_view_set_view_source_mode(WEBKIT_WEB_VIEW(d->webview), en);
}

void wemed_panel_load_remote_resources(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	d->load_remote = en;
}

void wemed_panel_display_images(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	WebKitWebSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(d->webview));
	g_object_set(G_OBJECT(settings), "auto-load-images", en, NULL);
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
gboolean wemed_panel_supported_type(WemedPanel* wp, const char* mime_type) {
	GET_D(wp);
	return webkit_web_view_can_show_mime_type(WEBKIT_WEB_VIEW(d->webview), mime_type);
}
