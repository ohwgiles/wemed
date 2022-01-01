/* Copyright 2013-2022 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <gmime/gmime.h>
#include <libintl.h>
#define _(str) gettext(str)
#include "mimeapp.h"
#include "wemedpanel.h"
#include "mimemodel.h"
#include "mimetree.h"
#include "mainwindow.h"
#include "openwith.h"

// these menu widgets are dynamically modified throughout
// the program lifecycle, so references are saved here. Other
// GtkWidgets are handled by GTK.
typedef struct {
	GtkWidget* revert;
	GtkWidget* save;
	GtkWidget* saveas;
	GtkWidget* close;
	GtkWidget* part;
	GtkWidget* show_html_source;
	GtkWidget* menu_part_edit;
	GtkWidget* menu_part_edit_with;
	GtkWidget* menu_part_export;
	GtkWidget* menu_part_delete;
} MenuWidgets;

struct _WemedWindow {
	// widgets/view
	GtkWidget* root_window;
	GtkWidget* paned;
	GdkPixbuf* icon;
	GtkWidget* mime_tree;
	GtkWidget* panel;
	MenuWidgets* menu_widgets;
	// model
	MimeModel* model;
	GMimeObject* current_part;
	char* filename;
	gboolean dirty;
	struct Application mime_app;
};

static void update_title(WemedWindow* w) {
	char* slashpos = strrchr(w->filename, '/');
	char* basename = slashpos ? &slashpos[1] : w->filename;
	char* title = g_strdup_printf("%s - wemed", basename);
	gtk_window_set_title(GTK_WINDOW(w->root_window), title);
	g_free(title);
}

static void expand_mime_tree_view(WemedWindow* w) {
	gint p;
	gtk_widget_get_preferred_width(w->mime_tree, NULL, &p);
	gtk_paned_set_position(GTK_PANED(w->paned), p);
}

static GString slurp_and_close(FILE* fp) {
	GString ret = {0, 0, 0};
	if(fseek(fp, 0, SEEK_END) < 0)
		return ret;

	long n = ftell(fp);
	if(n < 0) {
		perror("ftell");
		return ret;
	}
	ret.len = (gsize) n;
	rewind(fp);
	ret.str = malloc(ret.len);
	fread(ret.str, 1, ret.len, fp);
	fclose(fp);
	return ret;
}

static void set_model(WemedWindow* w, MimeModel* m) {
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->mime_tree), mime_model_get_gtk_model(m));
	g_signal_connect_swapped(m, "node-inserted", G_CALLBACK(mime_tree_node_inserted), w->mime_tree);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->mime_tree));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
	gtk_widget_set_sensitive(w->mime_tree, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->saveas, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->close, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->part, FALSE);
	expand_mime_tree_view(w);
}

// loads a new part into the panel. Triggered by a selection
// change in the MIME tree view widget
static void set_current_part(WemedWindow* w, GMimeObject* part) {
	w->current_part = part;
	if(part == NULL)
		return;

	GString headers = mime_model_part_headers(part);
	const char* charset = g_mime_object_get_content_type_parameter(part, "charset");
	char* mime_type = mime_model_content_type(w->current_part);
	GString content = {0, 0, 0};

	gtk_widget_set_sensitive(w->menu_widgets->menu_part_delete, (part != mime_model_root(w->model)));

	// enable the 'Part' menu
	gtk_widget_set_sensitive(w->menu_widgets->part, TRUE);
	if(GMIME_IS_MULTIPART(part)) {
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, FALSE);
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit_with, FALSE);
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_export, FALSE);
	} else {
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, TRUE);
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit_with, TRUE);
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_export, TRUE);
		// if we're displaying html, respect the "view source" menu option
		if(strcmp(mime_type, "text/html") == 0) {
			gtk_widget_set_sensitive(w->menu_widgets->show_html_source, TRUE);
			gboolean show_source = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w->menu_widgets->show_html_source));
			wemed_panel_show_source(WEMED_PANEL(w->panel), show_source);
		} else {
			gtk_widget_set_sensitive(w->menu_widgets->show_html_source, FALSE);
			wemed_panel_show_source(WEMED_PANEL(w->panel), FALSE);
		}
		
		// fetch the part content
		content = mime_model_part_content(part);

		// determine the external program for the given mime type and update the menu accordingly
		free(w->mime_app.name); // clean up the last one
		free(w->mime_app.exec);
		w->mime_app = get_default_mime_app(mime_type);
		if(w->mime_app.exec) {
			char* label = NULL;
			asprintf(&label, _("Edit with %s"), w->mime_app.name);
			gtk_menu_item_set_label(GTK_MENU_ITEM(w->menu_widgets->menu_part_edit), label);
			free(label);
			gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, TRUE);
		} else {
			gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, FALSE);
		}
	}

	WemedPanelDoc doc = { mime_type, charset, headers, content, w->mime_app.name };
	wemed_panel_load_doc(WEMED_PANEL(w->panel), doc);
	g_free(content.str);
	g_free(headers.str);
	free(mime_type);
}

static void set_dirtied(GObject* caller, WemedWindow* w) {
	w->dirty = TRUE;
	if(w->filename)
		gtk_widget_set_sensitive(w->menu_widgets->revert, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->save, TRUE);
}

// update the internal model based on the changes the user has made in the view.
// this involves fetching header and content data from the view and needs to be
// called before the user changes to a different part or performs any model manipulations
static void register_changes(WemedWindow* w) {
	if(w->current_part == NULL || w->dirty == FALSE) return;
	// first see if the content has been changed. Only do this for
	// content types of text/ since other types can only be edited
	// externally, at which time they get saved
	if(GMIME_IS_PART(w->current_part)) {
		char* ct = mime_model_content_type(w->current_part);
		if(strncmp(ct, "text/", 5) == 0) {
			// webkit returns content in utf-8, so we have to convert it back
			// if the desired encoding is different
			GString new_content = wemed_panel_get_content(WEMED_PANEL(w->panel));
			const char* charset = g_mime_object_get_content_type_parameter(w->current_part, "charset");
			if(charset && strcasecmp("utf-8", charset) != 0) {
				gsize sz;
				char* converted = g_convert(new_content.str, new_content.len, charset, "utf-8", NULL, &sz, NULL);
				if(converted) {
					free(new_content.str);
					new_content.str = converted;
					new_content.len = sz;
				} else
					fprintf(stderr, "Conversion failed\n");
			}
			mime_model_update_content(w->model, GMIME_PART(w->current_part), new_content);
			free(new_content.str);
		}
		free(ct);
	}

	// now do the headers
	GString new_headers = wemed_panel_get_headers(WEMED_PANEL(w->panel));
	if(strcmp(new_headers.str,g_mime_object_get_headers(w->current_part, g_mime_format_options_get_default())) != 0) {
		set_dirtied(NULL, w);
		GMimeObject* new_part = mime_model_update_header(w->model, w->current_part, new_headers);
		if(new_part == NULL)
			fprintf(stderr, "new_part is NULL\n");
	}
	free(new_headers.str);
}

static void close_document(WemedWindow* w) {
	wemed_panel_clear(WEMED_PANEL(w->panel));
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->mime_tree), NULL);
	mime_model_free(w->model);
	w->model = 0;
	w->current_part = NULL;
	w->dirty = FALSE;
	g_free(w->filename);
	w->filename = 0;
	gtk_widget_set_sensitive(w->mime_tree, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->saveas, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->close, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->part, FALSE);
}

static void tree_selection_changed(MimeTree* tree, GMimeObject* obj, WemedWindow* w) {
	register_changes(w);
	set_current_part(w, obj);
}

static void open_part_with_external_app(WemedWindow* w, GMimePart* part, const char* app) {
	char* tmpfile = strdup("/tmp/wemed-tmpfile-XXXXXX");
	int fd = mkstemp(tmpfile);
	FILE* fp = fdopen(fd, "wb");
	mime_model_write_part(part, fp);
	char* buffer = malloc(strlen(app) + strlen(tmpfile) + 5);
	char* p = 0;
	// TODO: improve this hack
	// The external application to execute is taken from FreeDesktop Exec key:
	// https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#exec-variables
	if((p = strstr(app, "%f")) || (p = strstr(app, "%U")) || (p = strstr(app, "%s"))) {
		p[1] = 's';
		sprintf(buffer, app, tmpfile);
	} else {
		sprintf(buffer, "%s %s", app, tmpfile);
	}
	close(fd);
	system(buffer);
	free(buffer);
	// now we've come back from the external program, read the data back in and save it
	// TODO: only if it has changed
	GString new_content = slurp_and_close(fopen(tmpfile, "rb"));
	unlink(tmpfile); // be a tidy kiwi
	free(tmpfile);

	if(new_content.str) {
		set_dirtied(NULL, w);
		mime_model_update_content(w->model, part, new_content);
		set_current_part(w, GMIME_OBJECT(part));
		free(new_content.str);
	} else {
		fprintf(stderr, "failed to slurp %s\n", tmpfile);
	}
}

static void set_clean(WemedWindow* w) {
	w->dirty = FALSE;
	gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
	wemed_panel_set_clean(WEMED_PANEL(w->panel));
}


//>>>>>>>>>> BEGIN MENU BAR CALLBACK SECTION

static gboolean menu_file_save_as(GtkMenuItem* item, WemedWindow* w) {

	GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Save File"), GTK_WINDOW(w->root_window), GTK_FILE_CHOOSER_ACTION_SAVE, _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Save"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
	if(w->filename)
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), w->filename);

	gboolean ret = FALSE;
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
		FILE* fp = fopen(filename, "wb");
		register_changes(w);
		if(fp && mime_model_write_to_file(w->model, fp)) {
			set_clean(w);
			free(w->filename);
			w->filename = filename;
			update_title(w);
			ret = TRUE;
		} else
			free(filename);
	}

	gtk_widget_destroy(dialog);
	return ret;
}

static gboolean menu_file_save(GtkMenuItem* item, WemedWindow* w) {
	if(w->filename == NULL) // run 'Save As' instead
		return menu_file_save_as(NULL, w);
	else {
		FILE* fp = fopen(w->filename, "wb");
		if(!fp) return FALSE;
		register_changes(w);
		gboolean ret = mime_model_write_to_file(w->model, fp);
		if(ret)
			set_clean(w);
		return ret;
	}
}

// returns a boolean of whether the user accepts a close or not so the function
// can be reused when creating or opening a new document
static gboolean confirm_close(WemedWindow* w) {
	if(w->dirty) {
		GtkWidget* dialog = gtk_message_dialog_new(
		                        GTK_WINDOW(w->root_window),
		                        GTK_DIALOG_DESTROY_WITH_PARENT,
		                        GTK_MESSAGE_QUESTION,
		                        GTK_BUTTONS_NONE,
		                        _("File has been modified. Would you like to save it?"));
		gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Yes"), GTK_RESPONSE_YES);
		gtk_dialog_add_button(GTK_DIALOG(dialog), _("_No"), GTK_RESPONSE_NO);
		gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
		int ret = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		if(ret == GTK_RESPONSE_YES) {
			// if saving fails, DON'T close
			if(menu_file_save(NULL, w) != TRUE)
				return FALSE;
		} else if(ret != GTK_RESPONSE_NO)
			// GTK_RESPONSE_NO falls through to close
			return FALSE;
	}
	return TRUE;
}

static gboolean menu_file_close(GtkMenuItem* item, WemedWindow* w) {
	if(confirm_close(w)) {
		close_document(w);
		return TRUE;
	} else
		return FALSE;
}

static gboolean menu_file_quit(GtkMenuItem* item, WemedWindow* w) {
	if(confirm_close(w)) {// register_changes called in confirm_close
		close_document(w);
		gtk_main_quit();
		return TRUE;
	}
	return FALSE;
}

static gboolean delete_event_handler(GtkWidget* window, GdkEvent* ev, WemedWindow* w) {
	return !menu_file_quit(NULL, w);
}

static void menu_file_reload(GtkMenuItem* item, WemedWindow* w) {
	// just close the current document and reopen it
	char* f = strdup(w->filename); // save the filename for reopening
	GtkWidget* dialog = gtk_message_dialog_new(
	                        GTK_WINDOW(w->root_window),
	                        GTK_DIALOG_DESTROY_WITH_PARENT,
	                        GTK_MESSAGE_QUESTION,
	                        GTK_BUTTONS_YES_NO,
	                        _("Are you sure you want to reload the file from disk?"));
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
		close_document(w);
		wemed_window_open(w, f);
	}
	gtk_widget_destroy(dialog);
	free(f);
}

static void menu_file_open(GtkMenuItem* item, WemedWindow* w) {
	if(confirm_close(w) == FALSE)
		return;

	GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Open File"),
	                         GTK_WINDOW(w->root_window),
	                         GTK_FILE_CHOOSER_ACTION_OPEN,
	                         _("_Cancel"), GTK_RESPONSE_CANCEL,
	                         _("_Open"), GTK_RESPONSE_ACCEPT,
	                         NULL);

	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		wemed_window_open(w, filename);
		free(filename);
	}

	gtk_widget_destroy (dialog);
}

static void menu_file_new(GtkMenuItem* item, WemedWindow* w) {
	if(confirm_close(w) == FALSE)
		return;

	close_document(w);
	GString s = {0};
	set_model(w, mime_model_new(s));
}


static void menu_file_new_email(GtkMenuItem* item, WemedWindow* w) {
	if(confirm_close(w) == FALSE)
		return;

	close_document(w);
	GString s = {0};
	MimeModel* m = mime_model_new(s);
	mime_model_create_blank_email(m);
	set_model(w, m);
}

static void menu_view_html_source(GtkCheckMenuItem* item, WemedWindow* w) {
	register_changes(w);
	gboolean source = gtk_check_menu_item_get_active(item);
	wemed_panel_show_source(WEMED_PANEL(w->panel), source);
	set_current_part(w, w->current_part);
}

static void menu_view_remote_resources(GtkCheckMenuItem* item, WemedWindow* w) {
	register_changes(w);
	gboolean remote = gtk_check_menu_item_get_active(item);
	wemed_panel_load_remote_resources(WEMED_PANEL(w->panel), remote);
	set_current_part(w, w->current_part);
}

static void menu_view_display_images(GtkCheckMenuItem* item, WemedWindow* w) {
	register_changes(w);
	gboolean images = gtk_check_menu_item_get_active(item);
	wemed_panel_display_images(WEMED_PANEL(w->panel), images);
	set_current_part(w, w->current_part);
}

static void menu_view_inline_parts(GtkCheckMenuItem* item, WemedWindow* w) {
	gboolean inl = gtk_check_menu_item_get_active(item);
	mime_model_filter_inline(w->model, !inl);
}

static void menu_part_new_node(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget* combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "multipart/related");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "multipart/alternative");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "multipart/mixed");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	GtkWidget* dialog = gtk_dialog_new_with_buttons(
	                        _("Select Node Type"),
	                        GTK_WINDOW(w->root_window),
	                        GTK_DIALOG_MODAL,
	                        _("_OK"),
	                        GTK_RESPONSE_ACCEPT,
	                        _("_Cancel"),
	                        GTK_RESPONSE_REJECT,
	                        NULL);
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_add(GTK_CONTAINER(content), combo);
	gtk_widget_show_all(dialog);
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		set_dirtied(NULL, w);
		mime_model_new_node(w->model, w->current_part, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo)));
		expand_mime_tree_view(w);
	}
	gtk_widget_destroy(dialog);
}

static void menu_part_new_empty(GtkMenuItem* item, WemedWindow* w) {
	mime_model_new_node(w->model, w->current_part, "text/plain");
	set_dirtied(NULL, w);
	expand_mime_tree_view(w);
}

static char* import_file_into_tree(WemedWindow* w,  GMimeObject* parent_or_sibling, const char* filename, const char* disposition) {
	static int partnum = 0;
	char* mime_type = get_file_mime_type(filename);
	GMimePart* part = GMIME_PART(mime_model_new_node(w->model, parent_or_sibling, mime_type));
	g_mime_part_set_content_encoding(part, GMIME_CONTENT_ENCODING_BASE64);
	GString content = slurp_and_close(fopen(filename, "rb"));
	if(!content.str)
		return NULL;
	mime_model_update_content(w->model, part, content);
	char* cid;
	asprintf(&cid, "part%d_%u", partnum++, (unsigned int)time(0));
	g_mime_part_set_content_id(part, cid);
	char* slashpos = strrchr(filename, '/');
	g_mime_part_set_filename(part, slashpos? &slashpos[1] : filename);
	if(disposition)
		g_mime_object_set_disposition((GMimeObject*) part, disposition);
	GString s = {0};
	s.str = g_mime_object_get_headers((GMimeObject*) part, g_mime_format_options_get_default());
	s.len = strlen(s.str);
	mime_model_update_header(w->model, (GMimeObject*) part, s);
	free(mime_type);
	expand_mime_tree_view(w);
	return cid;
}

static char* import_file_cb(WemedPanel* p, const char* filename, WemedWindow* w) {
	GMimeObject* parent = mime_model_find_mixed_parent(w->model, w->current_part);
	return import_file_into_tree(w, parent?:w->current_part, filename, GMIME_DISPOSITION_INLINE);
}

static void menu_part_new_from_file(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Open File"),
	                         GTK_WINDOW(w->root_window),
	                         GTK_FILE_CHOOSER_ACTION_OPEN,
	                         _("_Cancel"), GTK_RESPONSE_CANCEL,
	                         _("_Open"), GTK_RESPONSE_ACCEPT,
	                         NULL);

	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		import_file_into_tree(w, w->current_part, filename, GMIME_DISPOSITION_ATTACHMENT);
		free(filename);
	}

	gtk_widget_destroy (dialog);
}

static void menu_part_edit(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	open_part_with_external_app(w, GMIME_PART(w->current_part), w->mime_app.exec);
}

static void menu_part_edit_with(GtkMenuItem* item, WemedWindow* w) {
	char* content_type_name = mime_model_content_type(w->current_part);
	char* exec = open_with(w->root_window, content_type_name);
	free(content_type_name);
	if(exec) {
		register_changes(w);
		open_part_with_external_app(w, GMIME_PART(w->current_part), exec);
	}

}

static void panel_edit_external(WemedPanel* panel, gboolean open_with, WemedWindow* w) {
	if(open_with) {
		menu_part_edit_with(NULL, w);
	} else {
		menu_part_edit(NULL, w);
	}
}

static void menu_part_export(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
	                        _("Save File"),
	                        GTK_WINDOW(w->root_window),
	                        GTK_FILE_CHOOSER_ACTION_SAVE,
	                        _("_Cancel"),
	                        GTK_RESPONSE_CANCEL,
	                        _("_Save"),
	                        GTK_RESPONSE_ACCEPT,
	                        NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), g_mime_part_get_filename(GMIME_PART(w->current_part)));

	if(gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		register_changes(w);
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		FILE* fp = fopen(filename, "wb");
		mime_model_write_part(GMIME_PART(w->current_part), fp);
		free(filename);
	}
	gtk_widget_destroy (dialog);
}

static void menu_part_delete(GtkMenuItem* item, WemedWindow* w) {
	mime_model_part_remove(w->model, w->current_part);
	set_dirtied(NULL, w);
}

static void menu_help_website(GtkMenuItem* item, WemedWindow* w) {
	gtk_show_uri_on_window(GTK_WINDOW(w->root_window), "http://wemed.ohwg.net", gtk_get_current_event_time(), NULL);
}

static void menu_help_headers(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget* dialog = gtk_dialog_new_with_buttons(
	                        "MIME Headers",
	                        GTK_WINDOW(w->root_window),
	                        GTK_DIALOG_MODAL,
	                        _("_Close"),
	                        GTK_RESPONSE_ACCEPT,
	                        NULL);
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget* text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	char* header_info = NULL;
	int len = asprintf(&header_info, "%s\n\n"
	                       "To: <Name> <<email@address>>, <Name> <<email@address>>\n"
	                       "cc: <Name> <<email@address>>, <Name> <<email@address>>\n"
	                       "bcc: <Name> <<email@address>>, <Name> <<email@address>>\n"
	                       "From: <Name> <<email@address>>\n"
	                       "Subject: <subject>\n"
	                       "Reply-To: <Name> <<email@address>>\n"
	                       "Content-Type: <mime-type>; [charset=<charset>]\n"
	                       "Content-Disposition: (attachment|inline); [filename=<filename>;]\n"
	                       "Content-Transfer-Encoding: (7bit|quoted-printable|base64)\n"
	                       "Content-ID: <cid>\n",
	         _("The following are the most useful MIME headers:"));
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), header_info, len);
	free(header_info);
	gtk_container_add(GTK_CONTAINER(content), text);
	gtk_widget_show_all(dialog);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void menu_help_about(GtkMenuItem* item, WemedWindow* w) {
	gtk_show_about_dialog(
	    GTK_WINDOW(w->root_window),
	    "program-name", "Wemed",
	    "version", "0.1",
	    "logo", w->icon,
	    "license-type", GTK_LICENSE_GPL_3_0,
	    "copyright", "2013-2017 Oliver Giles",
	    "website", "http://wemed.ohwg.net",
	    NULL);
}

//<<<<<<<<<<<<<<<<<<< END MENU BAR CALLBACK SECTION

static GtkWidget* build_menubar(WemedWindow* w) {
	GtkWidget* menubar = gtk_menu_bar_new();
	MenuWidgets* m = g_new(MenuWidgets, 1);
	GtkAccelGroup* acc = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(w->root_window), acc);

	{ // File
		GtkWidget* file = gtk_menu_item_new_with_mnemonic(_("_File"));
		GtkWidget* filemenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);
		{ // File -> New Document
			GtkWidget* newdoc = gtk_menu_item_new_with_mnemonic(_("New _Document"));
			g_signal_connect(G_OBJECT(newdoc), "activate", G_CALLBACK(menu_file_new), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), newdoc);
			gtk_widget_add_accelerator(newdoc, "activate", acc, GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // File -> New Email Template
			GtkWidget* email = gtk_menu_item_new_with_mnemonic(_("New _Email Template"));
			g_signal_connect(G_OBJECT(email), "activate", G_CALLBACK(menu_file_new_email), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), email);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Open
			GtkWidget* open = gtk_menu_item_new_with_mnemonic(_("_Open..."));
			g_signal_connect(G_OBJECT(open), "activate", G_CALLBACK(menu_file_open), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), open);
			gtk_widget_add_accelerator(open, "activate", acc, GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // File -> Revert To Saved
			m->revert = gtk_menu_item_new_with_mnemonic(_("_Revert to Saved"));
			g_signal_connect(G_OBJECT(m->revert), "activate", G_CALLBACK(menu_file_reload), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->revert);
			gtk_widget_add_accelerator(m->revert, "activate", acc, GDK_KEY_r, GDK_SHIFT_MASK | GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Save
			m->save = gtk_menu_item_new_with_mnemonic(_("_Save"));
			g_signal_connect(G_OBJECT(m->save), "activate", G_CALLBACK(menu_file_save), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->save);
			gtk_widget_add_accelerator(m->save, "activate", acc, GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // File -> Save As
			m->saveas = gtk_menu_item_new_with_mnemonic(_("Save _As..."));
			g_signal_connect(G_OBJECT(m->saveas), "activate", G_CALLBACK(menu_file_save_as), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->saveas);
			gtk_widget_add_accelerator(m->saveas, "activate", acc, GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Close
			m->close = gtk_menu_item_new_with_mnemonic(_("_Close"));
			g_signal_connect(G_OBJECT(m->close), "activate", G_CALLBACK(menu_file_close), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->close);
			gtk_widget_add_accelerator(m->close, "activate", acc, GDK_KEY_w, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Quit
			GtkWidget* quit = gtk_menu_item_new_with_mnemonic(_("_Quit"));
			g_signal_connect(G_OBJECT(quit), "activate", G_CALLBACK(menu_file_quit), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), quit);
			gtk_widget_add_accelerator(quit, "activate", acc, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file);
	}
	{ // View
		GtkWidget* view = gtk_menu_item_new_with_mnemonic(_("_View"));
		GtkWidget* viewmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(view), viewmenu);
		{ // View -> HTML Source
			m->show_html_source = gtk_check_menu_item_new_with_mnemonic(_("_Show HTML Source"));
			g_signal_connect(G_OBJECT(m->show_html_source), "toggled", G_CALLBACK(menu_view_html_source), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), m->show_html_source);
			gtk_widget_set_sensitive(m->show_html_source, FALSE);
			gtk_widget_add_accelerator(m->show_html_source, "activate", acc, GDK_KEY_h, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // View -> Remote resources
			GtkWidget* remote = gtk_check_menu_item_new_with_mnemonic(_("_Load Remote Resources"));
			g_signal_connect(G_OBJECT(remote), "toggled", G_CALLBACK(menu_view_remote_resources), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), remote);
			gtk_widget_add_accelerator(remote, "activate", acc, GDK_KEY_l, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // View -> Display Images
			GtkWidget* images = gtk_check_menu_item_new_with_mnemonic(_("Display _Images"));
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(images), TRUE);
			g_signal_connect(G_OBJECT(images), "toggled", G_CALLBACK(menu_view_display_images), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), images);
			gtk_widget_add_accelerator(images, "activate", acc, GDK_KEY_i, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // View -> Hide Inline Images
			GtkWidget* inline_parts = gtk_check_menu_item_new_with_mnemonic(_("Inline Parts in _Tree"));
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(inline_parts), TRUE);
			g_signal_connect(G_OBJECT(inline_parts), "toggled", G_CALLBACK(menu_view_inline_parts), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), inline_parts);
			gtk_widget_add_accelerator(inline_parts, "activate", acc, GDK_KEY_t, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view);
	}
	{ // Part
		m->part = gtk_menu_item_new_with_mnemonic(_("_Part"));
		GtkWidget* partmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(m->part), partmenu);
		{ // Part -> New Multipart Node
			GtkWidget* node = gtk_menu_item_new_with_mnemonic(_("New _Multipart Node"));
			g_signal_connect(G_OBJECT(node), "activate", G_CALLBACK(menu_part_new_node), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), node);
			gtk_widget_add_accelerator(node, "activate", acc, GDK_KEY_m, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // Part -> New Empty Part
			GtkWidget* empty = gtk_menu_item_new_with_mnemonic(_("New _Empty Part"));
			g_signal_connect(G_OBJECT(empty), "activate", G_CALLBACK(menu_part_new_empty), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), empty);
			gtk_widget_add_accelerator(empty, "activate", acc, GDK_KEY_e, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // Part -> New From File
			GtkWidget* fromfile = gtk_menu_item_new_with_mnemonic(_("New Part From _File"));
			g_signal_connect(G_OBJECT(fromfile), "activate", G_CALLBACK(menu_part_new_from_file), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), fromfile);
			gtk_widget_add_accelerator(fromfile, "activate", acc, GDK_KEY_f, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), gtk_separator_menu_item_new());
		{ // Part -> Edit
			m->menu_part_edit = gtk_menu_item_new_with_label(_("Edit...")); // this label will be changed
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), m->menu_part_edit);
			g_signal_connect(G_OBJECT(m->menu_part_edit), "activate", G_CALLBACK(menu_part_edit), w);
			gtk_widget_add_accelerator(m->menu_part_edit, "activate", acc, GDK_KEY_d, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		{ // Part -> Edit with
			m->menu_part_edit_with = gtk_menu_item_new_with_mnemonic(_("Edit _With..."));
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), m->menu_part_edit_with);
			g_signal_connect(G_OBJECT(m->menu_part_edit_with), "activate", G_CALLBACK(menu_part_edit_with), w);
			gtk_widget_add_accelerator(m->menu_part_edit_with, "activate", acc, GDK_KEY_d, GDK_SHIFT_MASK | GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), gtk_separator_menu_item_new());
		{ // Part -> Export
			m->menu_part_export = gtk_menu_item_new_with_mnemonic(_("E_xport..."));
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), m->menu_part_export);
			g_signal_connect(G_OBJECT(m->menu_part_export), "activate", G_CALLBACK(menu_part_export), w);
			gtk_widget_add_accelerator(m->menu_part_export, "activate", acc, GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), gtk_separator_menu_item_new());
		{ // Part -> Delete
			m->menu_part_delete = gtk_menu_item_new_with_mnemonic(_("_Delete"));
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), m->menu_part_delete);
			g_signal_connect(G_OBJECT(m->menu_part_delete), "activate", G_CALLBACK(menu_part_delete), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), m->part);
	}
	{ // Help
		GtkWidget* help = gtk_menu_item_new_with_mnemonic(_("_Help"));
		GtkWidget* helpmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), helpmenu);
		{ // Help -> Visit Website
			GtkWidget* web = gtk_menu_item_new_with_mnemonic(_("Visit _Website"));
			gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), web);
			g_signal_connect(G_OBJECT(web), "activate", G_CALLBACK(menu_help_website), w);
		}
		{ // Help -> MIME Headers
			GtkWidget* headers = gtk_menu_item_new_with_mnemonic(_("MIME Headers"));
			gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), headers);
			g_signal_connect(G_OBJECT(headers), "activate", G_CALLBACK(menu_help_headers), w);
			gtk_widget_add_accelerator(headers, "activate", acc, GDK_KEY_F1, 0, GTK_ACCEL_VISIBLE);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), gtk_separator_menu_item_new());
		{ // Help -> About
			GtkWidget* about = gtk_menu_item_new_with_mnemonic(_("_About Wemed"));
			gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), about);
			g_signal_connect(G_OBJECT(about), "activate", G_CALLBACK(menu_help_about), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help);
	}
	w->menu_widgets = m;

	return menubar;
}

gboolean wemed_window_open(WemedWindow* w, const char* filename) {
	MimeModel* m = mime_model_new(slurp_and_close(fopen(filename, "rb")));
	if(m) {
		set_model(w, m);
		w->filename = strdup(filename);
		update_title(w);
		set_clean(w);
		return TRUE;
	} else
		return FALSE;
}

WemedWindow* wemed_window_create() {
	WemedWindow* w = g_new0(WemedWindow, 1);

	w->root_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	w->icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "wemed", 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
	gtk_window_set_icon(GTK_WINDOW(w->root_window), w->icon);
	gtk_window_set_position(GTK_WINDOW(w->root_window), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(w->root_window), 720, 576);
	g_signal_connect(w->root_window, "delete-event", G_CALLBACK(delete_event_handler), w);
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* menubar = build_menubar(w);

	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 3);
	w->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

	w->mime_tree = mime_tree_new();

	GtkWidget* treeviewwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(treeviewwin), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(treeviewwin), w->mime_tree);
	gtk_paned_add1(GTK_PANED(w->paned), treeviewwin);

	w->panel = wemed_panel_new();
	g_signal_connect(w->panel, "import-file", G_CALLBACK(import_file_cb), w);
	g_signal_connect(w->panel, "dirtied", G_CALLBACK(set_dirtied), w);
	g_signal_connect(w->panel, "open-external", G_CALLBACK(panel_edit_external), w);
	gtk_paned_add2(GTK_PANED(w->paned), w->panel);

	g_signal_connect(G_OBJECT(w->mime_tree), "selection-changed", G_CALLBACK(tree_selection_changed), w);

	gtk_box_pack_start(GTK_BOX(vbox), w->paned, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(w->root_window), vbox);

	gtk_widget_show_all(w->root_window);

	close_document(w); // initially, all widgets should be disabled etc.

	gtk_paned_set_position(GTK_PANED(w->paned), 160);

	return w;
}

void wemed_window_free(WemedWindow* w) {
	g_free(w->menu_widgets);
	free(w->mime_app.name);
	free(w->mime_app.exec);
	g_free(w);
}


