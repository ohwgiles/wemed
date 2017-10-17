/* Copyright 2013-2017 Oliver Giles
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
#include "mimeapp.h"
#include "wemedpanel.h"
#include "openwith.h"

#include <libintl.h>
#define _(str) gettext(str)

extern GtkIconTheme* system_icon_theme;
G_DEFINE_TYPE(WemedPanel, wemed_panel, GTK_TYPE_PANED)
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
	WebKitWebContext* webkit_ctx;
	gboolean webkit_dirty;
	GtkWidget* sourceview;
	GtkTextBuffer* sourcetext;
	int ipc;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	GtkWidget* toolbar;
	GtkWidget* progress_bar;
	gboolean load_remote;
	gboolean view_source;
} WemedPanelPrivate;

// called when WebKit loads part of the HTML content. Used to dynamically
// update the position of the GTK progress bar
static void progress_changed_cb(GObject* web_view, GdkEvent* e, WemedPanel* wp) {
	GET_D(wp);
	gdouble p;
	g_object_get(web_view, "estimated-load-progress", &p, NULL);
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

// In webkit1 you could synchronously get the html contents. Making this method
// work asynchronously is a bigger effort than I have time for right now, so here
// we block on the IPC socket to get our synchronicity back
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
		// trigger a dump of the DOM content in the web extension
		webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.dispatchEvent(new CustomEvent('wemed_content'));", NULL, NULL, NULL);

		// block on the IPC socket to receive the data string
		int len;
		read(d->ipc, &len, sizeof(len));
		result.str = (char*) malloc(len+1);
		read(d->ipc, result.str, len);
		result.str[len] = '\0';
		result.len = len;
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
	PangoFontDescription* font = pango_font_description_from_string(gtk_font_button_get_font_name(fb));
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
			char* exec = 0;
			asprintf(&exec, "document.execCommand('insertimage', false, 'cid:%s')", cid);
			webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), exec, NULL, NULL, NULL);
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
	if((emitter == G_OBJECT(d->sourcetext) && gtk_text_buffer_get_modified(d->sourcetext)) ||
			(emitter == G_OBJECT(d->headertext) && gtk_text_buffer_get_modified(d->headertext)))
		g_signal_emit(wp, wemed_panel_signals[WP_SIG_DIRTIED], 0);
}


static gboolean webkit_process_message(GIOChannel* source, GIOCondition condition, gpointer data) {
	WemedPanel* wp = (WemedPanel*) data;
	GET_D(wp);
	int opcode = -1;
	int cfd = g_io_channel_unix_get_fd(source);
	if(read(cfd, &opcode, sizeof(int)) != sizeof(int))
		return perror("read"), FALSE;

	if(opcode == 0) {
		// child process notified webview was dirtied
		if(!d->webkit_dirty) {
			d->webkit_dirty = TRUE;
			g_signal_emit(wp, wemed_panel_signals[WP_SIG_DIRTIED], 0);
		}
	} else {
		g_printerr("IPC sent %d\n", opcode);
		return FALSE;
	}
	return TRUE;
}

static gboolean webkit_process_connected(GIOChannel *source, GIOCondition condition, gpointer data) {
	WemedPanel* wp = (WemedPanel*) data;
	GET_D(wp);
	int cfd = accept(g_io_channel_unix_get_fd(source), NULL, NULL);
	d->ipc = cfd;
	GIOChannel* channel = g_io_channel_unix_new(cfd);
	g_io_add_watch(channel, G_IO_IN, &webkit_process_message, data);
	return FALSE;
}

static void initialize_web_extensions (WebKitWebContext *context, gpointer user_data) {
	webkit_web_context_set_web_extensions_directory (context, WEMED_WEBEXT_DIR);
	webkit_web_context_set_web_extensions_initialization_user_data(context, g_variant_new_string((gchar*) user_data));
}

void load_cid_cb(WebKitURISchemeRequest *request, gpointer user_data) {
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

	struct sockaddr_un saddr;
	// create an abstract unix socket to communicate with the web extension
	int sd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&saddr, 0, sizeof(saddr));
	saddr.sun_family = AF_UNIX;
	// generate a semi-unique name for the pipe
	sprintf(saddr.sun_path+1, "wemed-%d-%lu", getpid(), (((unsigned long)wp)>>8) & 0xFF);
	if(bind(sd, &saddr, sizeof(struct sockaddr_un)) != 0)
		perror("bind");
	if(listen(sd, 1) != 0)
		perror("listen");

	GIOChannel* channel = g_io_channel_unix_new(sd);
	g_io_add_watch(channel, G_IO_IN, &webkit_process_connected, wp);

	// create and configure all our widgets
	d->headerview = gtk_text_view_new();
	d->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->headerview));
	d->webkit_ctx = webkit_web_context_new();
	webkit_web_context_set_cache_model(d->webkit_ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	g_signal_connect(d->webkit_ctx, "initialize-web-extensions", G_CALLBACK (initialize_web_extensions), saddr.sun_path+1);
	d->webview = webkit_web_view_new_with_context(d->webkit_ctx);
	d->progress_bar = gtk_progress_bar_new();

	webkit_web_context_register_uri_scheme(d->webkit_ctx, "cid", load_cid_cb, wp, NULL);
	webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.execCommand('styleWithCSS',false,true)", NULL, NULL, NULL);

	d->sourceview = gtk_source_view_new();
	d->sourcetext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->sourceview));
	// connect everything
	g_signal_connect(G_OBJECT(d->webview), "notify::estimated-load-progress", G_CALLBACK(progress_changed_cb), wp);
	g_signal_connect(G_OBJECT(d->webview), "show", G_CALLBACK(hide_progress_bar), d->progress_bar);

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
		GBytes* bytes = g_bytes_new(doc.content.str, doc.content.len);
		if(strncmp(doc.content_type, "text/", 5) == 0) {
			if(strncmp(&doc.content_type[5], "html", 4) == 0 && !d->view_source) {
				// use webkit widget
				webkit_web_view_load_bytes(WEBKIT_WEB_VIEW(d->webview), bytes, doc.content_type, doc.charset, NULL);
				webkit_web_view_set_editable(WEBKIT_WEB_VIEW(d->webview), TRUE);
				gtk_widget_show(d->toolbar);
				// start the progress bar, it should be hidden on completion of load
				gtk_widget_show(d->progress_bar);
			} else {
				// use sourceview widget
				GString src = doc.content;
				if(doc.charset && strcmp(doc.charset, "utf8") != 0) {
					// GtkTextBuffer must be fed utf-8
					printf("converting from %s\n", doc.charset);
					gsize sz;
					char* converted = g_convert(src.str, src.len, "utf8", doc.charset, NULL, &sz, NULL);
					if(converted) {
						free(src.str);
						src.str = converted;
						src.len = sz;
					} else printf("Conversion failed\n");
				}
				gtk_text_buffer_set_text(d->sourcetext, src.str, src.len);
				gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(d->sourcetext), gtk_source_language_manager_guess_language(gtk_source_language_manager_get_default(), NULL, doc.content_type));
				gtk_text_buffer_set_modified(d->sourcetext, FALSE);
				gtk_widget_show(d->sourceview);
			}
		} else if(webkit_web_view_can_show_mime_type(WEBKIT_WEB_VIEW(d->webview), doc.content_type)) {
			// load image or other webkit-displayable read-only type
			webkit_web_view_load_bytes(WEBKIT_WEB_VIEW(d->webview), bytes, doc.content_type, doc.charset, NULL);
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
	// TODO: CustomEvent can take a 'detail' parameter which could be used for the value
	// of this property, but I can't see how to retrieve it in the web extension
	if(d->load_remote)
		webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.dispatchEvent(new CustomEvent('wemed_load_remote_true'));", NULL, NULL, NULL);
	else
		webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(d->webview), "document.dispatchEvent(new CustomEvent('wemed_load_remote_false'));", NULL, NULL, NULL);
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
