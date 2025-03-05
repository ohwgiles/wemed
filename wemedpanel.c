/* Copyright 2013-2022 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <webkit2/webkit2.h>
#include <gmime/gmime.h>
#include <gtksourceview/gtksource.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "wemedpanel.h"

#include <libintl.h>
#define _(str) gettext(str)

extern GtkIconTheme* system_icon_theme;

// the private elements
typedef struct {
	GtkWidget* webview;
	WebKitWebContext* webkit_ctx;
	GtkWidget* sourceview;
	GtkTextBuffer* sourcetext;
	gboolean webkit_dirty;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	GtkWidget* toolbar;
	GtkWidget* progress_bar;
	gboolean load_remote;
	gboolean view_source;
	GtkWidget* open_ext_box;
	GtkWidget* open_ext_btn;
	GtkWidget* open_with_ext_btn;
} WemedPanelPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(WemedPanel, wemed_panel, GTK_TYPE_PANED)
#define GET_D(o) WemedPanelPrivate* d = wemed_panel_get_instance_private(o)

// signals
enum {
	WP_SIG_GET_CID,
	        WP_SIG_IMPORT,
	        WP_SIG_DIRTIED,
	        WP_SIG_OPEN_EXTERNAL,
	        WP_SIG_LAST
};

static guint wemed_panel_signals[WP_SIG_LAST] = {0};

// called when WebKit loads part of the HTML content. Used to dynamically
// update the position of the GTK progress bar
static void progress_changed_cb(GObject* web_view, GdkEvent* e, WemedPanel* wp) {
	GET_D(wp);
	gdouble p;
	g_object_get(web_view, "estimated-load-progress", &p, NULL);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), p);
	if(p == 1.0) { // loading is complete
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

static void get_content_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	WebKitJavascriptResult *js_res;
	JSCValue* val;
	GString* out_str = (GString*) user_data;

	js_res = webkit_web_view_run_javascript_finish (WEBKIT_WEB_VIEW (source_object), res, NULL);
	g_assert(js_res);
	val = webkit_javascript_result_get_js_value (js_res);
	g_assert(jsc_value_is_string(val));
	out_str->str = jsc_value_to_string(val);
	out_str->len = strlen(out_str->str);
	webkit_javascript_result_unref (js_res);
}

// In webkit1 you could synchronously get the html contents. Making this method
// work asynchronously is a bigger effort than I have time for right now, so here
// we manually iterate the gtk mainloop until our callback is dealt with
GString wemed_panel_get_content(WemedPanel* wp) {
	GET_D(wp);
	GString result = {0};

	if(gtk_widget_is_visible(d->sourceview)) {
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(d->sourcetext, &start);
		gtk_text_buffer_get_end_iter(d->sourcetext, &end);
		result.str = gtk_text_buffer_get_text(d->sourcetext, &start, &end, TRUE);
		result.len = gtk_text_iter_get_offset(&end);
	} else {
		GMainContext* ctx = g_main_context_default();
		webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.documentElement.outerHTML", NULL, get_content_callback, &result);
		while(result.str == NULL)
			g_main_context_iteration(ctx, TRUE);
	}

	return result;
}

static void edit_bold_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('bold',false,false)", NULL, NULL, NULL);
}
static void edit_italic_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('italic',false,false)", NULL, NULL, NULL);
}
static void edit_uline_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('underline',false,false)", NULL, NULL, NULL);
}
static void edit_strike_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('strikethrough',false,false)", NULL, NULL, NULL);
}
static void edit_font_cb(GtkFontButton* fb, WemedPanel* wp) {
	GET_D(wp);
	PangoFontDescription* font = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(fb));
	const char* family = pango_font_description_get_family(font);
	int size = pango_font_description_get_size(font);
	char* family_cmd;
	asprintf(&family_cmd, "document.execCommand('fontname', false, '%s')", family);
	char* size_cmd;
	asprintf(&size_cmd, "window.getSelection().getRangeAt(0).commonAncestorContainer.parentNode.style.fontSize = '%dpt';", size/PANGO_SCALE);
	if(pango_font_description_get_style(font) == PANGO_STYLE_ITALIC)
		webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('italic',false,false)", NULL, NULL, NULL);
	if(pango_font_description_get_weight(font) > PANGO_WEIGHT_MEDIUM)
		webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('bold',false,false)", NULL, NULL, NULL);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), family_cmd, NULL, NULL, NULL);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), size_cmd, NULL, NULL, NULL);
	free(family_cmd);
	free(size_cmd);
	pango_font_description_free(font);
}
static void edit_ltr_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.body.style.direction = 'ltr'", NULL, NULL, NULL);
}
static void edit_rtl_cb(GtkToolItem* item, WemedPanel* wp) {
	GET_D(wp);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.body.style.direction = 'rtl'", NULL, NULL, NULL);
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
			if(cid) {
				char* exec = 0;
				asprintf(&exec, "document.execCommand('insertimage', false, 'cid:%s')", cid);
				webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), exec, NULL, NULL, NULL);
				free(cid);
				free(exec);
			}
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
	if((emitter == G_OBJECT(d->sourcetext) && gtk_text_buffer_get_modified(d->sourcetext)) ||
	        (emitter == G_OBJECT(d->headertext) && gtk_text_buffer_get_modified(d->headertext)))
		g_signal_emit(wp, wemed_panel_signals[WP_SIG_DIRTIED], 0);
}

gboolean webext_msg_received(WebKitWebView* web_view, WebKitUserMessage *message, gpointer user_data) {
	WemedPanel* wp = (WemedPanel*) user_data;
	GET_D(wp);

	if(g_strcmp0(webkit_user_message_get_name(message), "dirtied") == 0) {
		// child process notified webview was dirtied
		if(!d->webkit_dirty) {
			d->webkit_dirty = TRUE;
			g_signal_emit(wp, wemed_panel_signals[WP_SIG_DIRTIED], 0);
		}
		return TRUE;
	}

	return FALSE;
}

static void initialize_web_extensions(WebKitWebContext *context, gpointer user_data) {
	webkit_web_context_set_web_extensions_directory(context, WEMED_WEBEXT_DIR);
}

static void openext_cb(GtkButton* btn, WemedPanel* wp) {
	g_signal_emit(wp, wemed_panel_signals[WP_SIG_OPEN_EXTERNAL], 0, FALSE);
}

static void openwith_cb(GtkButton* btn, WemedPanel* wp) {
	g_signal_emit(wp, wemed_panel_signals[WP_SIG_OPEN_EXTERNAL], 0, TRUE);
}

static void load_cid_cb(WebKitURISchemeRequest *request, gpointer user_data) {
	WemedPanel* wp = (WemedPanel*) user_data;
	const char* path = webkit_uri_scheme_request_get_path(request);
	GByteArray* s = NULL;
	// this is a synchronous signal which should populate a GByteArray
	g_signal_emit(wp, wemed_panel_signals[WP_SIG_GET_CID], 0, path, &s);

	if(s && s->data) {
		GInputStream* stream;
		stream = g_memory_input_stream_new_from_data(s->data, s->len, g_free);
		// In theory we could also give the MIME type from the mimemodel, but webkit
		// does just fine with NULL...
		webkit_uri_scheme_request_finish (request, stream,  s->len, NULL);
		g_object_unref(stream);
	} else {
		GError *error;
		error = g_error_new(g_quark_from_string("wemed"), 0, "Invalid cid:%s link.", path);
		webkit_uri_scheme_request_finish_error (request, error);
		g_error_free (error);
	}
}

static void wemed_panel_init(WemedPanel* wp) {
	GET_D(wp);

	GtkPaned* paned = GTK_PANED(wp);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (paned), GTK_ORIENTATION_VERTICAL);

	// create and configure all our widgets
	d->headerview = gtk_text_view_new();
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(d->headerview), TRUE);
	d->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->headerview));
	d->webkit_ctx = webkit_web_context_new();
	webkit_web_context_set_cache_model(d->webkit_ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	g_signal_connect(d->webkit_ctx, "initialize-web-extensions", G_CALLBACK(initialize_web_extensions), NULL);
	d->webview = webkit_web_view_new_with_context(d->webkit_ctx);
	d->progress_bar = gtk_progress_bar_new();
	d->open_ext_btn = gtk_button_new();
	g_signal_connect(d->open_ext_btn, "pressed", G_CALLBACK(openext_cb), wp);
	d->open_with_ext_btn = gtk_button_new_with_mnemonic(_("Edit _With..."));
	g_signal_connect(d->open_with_ext_btn, "pressed", G_CALLBACK(openwith_cb), wp);

	webkit_web_context_register_uri_scheme(d->webkit_ctx, "cid", load_cid_cb, wp, NULL);
	webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('styleWithCSS',false,true)", NULL, NULL, NULL);

	d->sourceview = gtk_source_view_new();
	gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(d->sourceview), TRUE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(d->sourceview), TRUE);
	d->sourcetext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->sourceview));
	// connect everything
	g_signal_connect(G_OBJECT(d->webview), "notify::estimated-load-progress", G_CALLBACK(progress_changed_cb), wp);
	g_signal_connect(G_OBJECT(d->webview), "show", G_CALLBACK(hide_progress_bar), d->progress_bar);
	g_signal_connect(G_OBJECT(d->webview), "user-message-received", G_CALLBACK(webext_msg_received), wp);

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
	d->toolbar = toolbar;

	// layout
	GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), d->headerview);
	gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, FALSE);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), d->webview, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), d->sourceview, TRUE, TRUE, 0);

	d->open_ext_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget* owbb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_box_pack_start(GTK_BOX(owbb), d->open_ext_btn, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(owbb), d->open_with_ext_btn, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(d->open_ext_box), owbb, TRUE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), d->open_ext_box, TRUE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(box), d->progress_bar, FALSE, FALSE, 3);
	
	gtk_paned_pack2(GTK_PANED(paned), box, TRUE, TRUE);

	gtk_paned_set_position(paned, 100);
}

static void wemed_panel_class_init(WemedPanelClass* class) {
	wemed_panel_signals[WP_SIG_GET_CID] = g_signal_new(
	    "cid-requested",
	    G_TYPE_FROM_CLASS ((GObjectClass*)class),
	    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	    0, // v-table offset
	    NULL,
	    NULL,
	    NULL, //marshaller
	    G_TYPE_BYTE_ARRAY, // return tye
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
	wemed_panel_signals[WP_SIG_OPEN_EXTERNAL] = g_signal_new(
	    "open-external",
	    G_TYPE_FROM_CLASS ((GObjectClass*)class),
	    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    G_TYPE_NONE,
	    1,
	    G_TYPE_BOOLEAN);
}

GtkWidget* wemed_panel_new() {
	return g_object_new(wemed_panel_get_type(), NULL);
}

void wemed_panel_load_doc(WemedPanel* wp, WemedPanelDoc doc) {
	GET_D(wp);
	wemed_panel_clear(wp);

	gtk_text_buffer_set_text(d->headertext, doc.headers.str, doc.headers.len);
	gtk_text_buffer_set_modified(d->headertext, FALSE);

	gtk_widget_set_sensitive(d->headerview, TRUE);
	if(doc.content.str && doc.content_type) {
		if(strncmp(doc.content_type, "text/", 5) == 0) {
			if(strncmp(&doc.content_type[5], "html", 4) == 0 && !d->view_source) {
				// use webkit widget
				// Since the content is a string, include the NULL terminator in the GBytes size.
				// webkit will refuse to load a GBytes with zero length.
				// webkit_web_view_load_html is not used since there is no way to set the charset
				GBytes* bytes = g_bytes_new(doc.content.str, doc.content.len + 1);
				webkit_web_view_load_bytes(WEBKIT_WEB_VIEW(d->webview), bytes, doc.content_type, doc.charset, NULL);
				g_bytes_unref(bytes);
				webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
				gtk_widget_show(d->toolbar);
				// start the progress bar, it should be hidden on completion of load
				gtk_widget_show(d->progress_bar);
			} else {
				// use sourceview widget
				GString src = doc.content;
				if(doc.charset && strcasecmp(doc.charset, "utf-8") != 0) {
					// GtkTextBuffer must be fed utf-8
					gsize sz;
					char* converted = g_convert(src.str, src.len, "utf-8", doc.charset, NULL, &sz, NULL);
					if(converted) {
						gtk_text_buffer_set_text(d->sourcetext, converted, sz);
						free(converted);
					} else
						fprintf(stderr, "Conversion from %s to utf8 failed\n", doc.charset);
				} else {
					// already utf-8
					gtk_text_buffer_set_text(d->sourcetext, src.str, src.len);
				}
				gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(d->sourcetext), gtk_source_language_manager_guess_language(gtk_source_language_manager_get_default(), NULL, doc.content_type));
				gtk_text_buffer_set_modified(d->sourcetext, FALSE);
				gtk_widget_show(d->sourceview);
			}
		} else if(webkit_web_view_can_show_mime_type(WEBKIT_WEB_VIEW(d->webview), doc.content_type)) {
			// load image or other webkit-displayable read-only type
			GBytes* bytes = g_bytes_new(doc.content.str, doc.content.len);
			webkit_web_view_load_bytes(WEBKIT_WEB_VIEW(d->webview), bytes, doc.content_type, doc.charset, NULL);
			g_bytes_unref(bytes);
		} else {
			// unhandled type - offer to open with external app
			char* label = NULL;
			asprintf(&label, _("Edit with %s"), doc.mimeapp_name);
			gtk_button_set_label(GTK_BUTTON(d->open_ext_btn), label);
			free(label);
			gtk_widget_show(d->open_ext_box);
		}
	}

	// set up callbacks to notify when the content is dirtied
	// for the webview, this is handled by an IPC message
	g_signal_connect(G_OBJECT(d->headertext), "modified-changed", G_CALLBACK(dirtied_cb), wp);
	g_signal_connect(G_OBJECT(d->sourcetext), "modified-changed", G_CALLBACK(dirtied_cb), wp);
}

void wemed_panel_show_source(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	d->view_source = en;
}

void wemed_panel_load_remote_resources(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	d->load_remote = en;
	WebKitUserMessage* msg = webkit_user_message_new("load-remote-resources", g_variant_new_boolean(en));
	webkit_web_view_send_message_to_page(WEBKIT_WEB_VIEW(d->webview), msg, NULL, NULL, NULL);
}

void wemed_panel_display_images(WemedPanel* wp, gboolean en) {
	GET_D(wp);
	WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(d->webview));
	g_object_set(G_OBJECT(settings), "auto-load-images", en, NULL);
}

void wemed_panel_clear(WemedPanel* wp) {
	GET_D(wp);
	// hide the web view
	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(d->webview));
	webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), FALSE);
	gtk_widget_hide(d->webview);
	gtk_widget_hide(d->toolbar);
	gtk_widget_hide(d->progress_bar);
	// hide source view
	gtk_widget_hide(d->sourceview);
	// hide open with
	gtk_widget_hide(d->open_ext_box);
	// disconnect modify handlers so a dirty signal won't be triggered
	g_signal_handlers_disconnect_by_func(d->headertext, G_CALLBACK(dirtied_cb), wp);
	g_signal_handlers_disconnect_by_func(d->sourcetext, G_CALLBACK(dirtied_cb), wp);
	// clear the header text
	gtk_text_buffer_set_text(d->headertext, "", 0);
	gtk_widget_set_sensitive(d->headerview, FALSE);
}

void wemed_panel_set_clean(WemedPanel *wp) {
	GET_D(wp);
	d->webkit_dirty = FALSE;
	// this will trigger modified-changed callbacks but they should do nothing
	gtk_text_buffer_set_modified(d->sourcetext, FALSE);
	gtk_text_buffer_set_modified(d->headertext, FALSE);
}
