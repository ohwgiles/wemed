/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>

#include <string.h>
#include <stdlib.h>
#include <gmime/gmime.h>
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
} MenuWidgets;

struct WemedWindow_S {
	// widgets/view
	GtkWidget* root_window;
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

// loads a new part into the panel. Triggered by a selection
// change in the MIME tree view widget
static void set_current_part(WemedWindow* w, GMimeObject* part) {
	w->current_part = part;

	const char* mime_type = mime_model_content_type(w->current_part);
	gtk_widget_set_sensitive(w->menu_widgets->part, TRUE);
	
	char* headers = g_mime_object_get_headers(part);
	char* content = GMIME_IS_PART(part) ? mime_model_part_content(GMIME_PART(part)) : NULL;
	WemedPanelDocType type =
		(strcmp(mime_type, "text/html") == 0)? WEMED_PANEL_DOC_TYPE_TEXT_HTML:
		(strncmp(mime_type, "text/", 5) == 0)? WEMED_PANEL_DOC_TYPE_TEXT_PLAIN:
		(strncmp(mime_type, "image/", 6) == 0)? WEMED_PANEL_DOC_TYPE_IMAGE: WEMED_PANEL_DOC_TYPE_OTHER;
	if(type == WEMED_PANEL_DOC_TYPE_TEXT_HTML) {
		gtk_widget_set_sensitive(w->menu_widgets->show_html_source, TRUE);
		wemed_panel_show_source(WEMED_PANEL(w->panel), gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w->menu_widgets->show_html_source)));
	} else {
		gtk_widget_set_sensitive(w->menu_widgets->show_html_source, FALSE);
		wemed_panel_show_source(WEMED_PANEL(w->panel), FALSE);
	}

	const char* charset = g_mime_object_get_content_type_parameter(part, "charset");
	wemed_panel_load_doc(WEMED_PANEL(w->panel), type, headers, content, charset);
	free(headers);
	free(content);

	// determine the external program for the given mime type and update the menu accordingly
	free(w->mime_app.name);
	free(w->mime_app.exec);
	w->mime_app = get_default_mime_app(mime_type);
	if(w->mime_app.exec) {
		const char* editwith = "_Edit with %s";
		char* label = malloc(strlen(editwith) + strlen(w->mime_app.name));
		sprintf(label, editwith, w->mime_app.name);
		gtk_menu_item_set_label(GTK_MENU_ITEM(w->menu_widgets->menu_part_edit), label);
		free(label); 
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, TRUE);
	} else {
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, FALSE);
	}
}

// update the internal model based on the changes the user has made in the view.
// this involves fetching header and content data from the view and needs to be
// called before the user changes to a different part or performs any model manipulations
static void register_changes(WemedWindow* w) {
	if(w->current_part == NULL) return;
	// first see if the content has been changed. Only do this for
	// content types of text/ since other types can only be edited
	// externally, at which time they get saved
	if(GMIME_IS_PART(w->current_part)) {
		const char* ct = mime_model_content_type(w->current_part);
		if(strncmp(ct, "text/", 5) == 0) {
			gboolean as_html_source = (strcmp(ct, "text/html") == 0);
			// new_content is in utf-8, so we have to convert it back if that's not
			// the character encoding of this part
			char* new_content = wemed_panel_get_content(WEMED_PANEL(w->panel), as_html_source);
			const char* charset = g_mime_object_get_content_type_parameter(w->current_part, "charset");
			if(charset && strcmp("utf8", charset) != 0) {
				gsize sz;
				char* converted = g_convert(new_content, strlen(new_content), charset, "utf8", NULL, &sz, NULL);
				if(converted) {
					free(new_content);
					new_content = converted;
				} else printf("Conversion failed\n");
			}
			char* old_content = mime_model_part_content(GMIME_PART(w->current_part));
			if(strcmp(new_content, old_content) != 0) {
				w->dirty = TRUE;
				gtk_widget_set_sensitive(w->menu_widgets->revert, TRUE);
				gtk_widget_set_sensitive(w->menu_widgets->save, TRUE);
				mime_model_update_content(w->model, GMIME_PART(w->current_part), new_content, strlen(new_content));
			}
			free(new_content);
			free(old_content);
		}
	}

	// now do the headers
	char* new_headers = wemed_panel_get_headers(WEMED_PANEL(w->panel));
	if(strcmp(new_headers,g_mime_object_get_headers(w->current_part)) != 0) {
		w->dirty = TRUE;
		gtk_widget_set_sensitive(w->menu_widgets->revert, TRUE);
		gtk_widget_set_sensitive(w->menu_widgets->save, TRUE);
		GMimeObject* new_part = mime_model_update_header(w->model, w->current_part, new_headers);
		// a header change can cause a display change in the panel,
		// for example changing mime type or encoding. This causes the
		// display to be updated immediately.
		if(new_part) {
			set_current_part(w, new_part);
		} else {
			printf("error: new_part is NULL\n");
		}
	}
	free(new_headers);
}

static void close_document(WemedWindow* w) {
	wemed_panel_clear(WEMED_PANEL(w->panel));
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->mime_tree), NULL);
	mime_model_free(w->model);
	w->model = 0;
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
	char* tmpfile = strdup("wemed-tmpfile-XXXXXX");
	int fd = mkstemp(tmpfile);
	FILE* fp = fdopen(fd, "wb");
	mime_model_write_part(part, fp);
	char* buffer = malloc(strlen(app) + strlen(tmpfile) + 5);
	char* p = 0;
	// this is not done very robustly.
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
	fp = fopen(tmpfile, "rb");
	fseek(fp, 0, SEEK_END);
	int len = ftell(fp);
	rewind(fp);
	char* new_content = malloc(len);
	fread(new_content, 1, len, fp);
	w->dirty = TRUE;
	gtk_widget_set_sensitive(w->menu_widgets->revert, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->save, TRUE);
	mime_model_update_content(w->model, part, new_content, len);
	set_current_part(w, GMIME_OBJECT(part));
	free(new_content);
	
	unlink(tmpfile); // be a tidy kiwi
	free(tmpfile);
}


//>>>>>>>>>> BEGIN MENU BAR CALLBACK SECTION

static gboolean menu_file_save_as(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);

	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Save File", GTK_WINDOW(w->root_window), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), w->filename?:"untitled.eml");

	gboolean ret = FALSE;
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
		FILE* fp = fopen(filename, "wb");
		if(fp && mime_model_write_to_file(w->model, fp)) {
			w->dirty = FALSE;
			gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
			gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
			free(w->filename);
			w->filename = filename;
			char* title = g_strdup_printf("%s - wemed", w->filename);
			gtk_window_set_title(GTK_WINDOW(w->root_window), title);
			g_free(title);
			ret = TRUE;
		} else
			free(filename);
	}

	gtk_widget_destroy(dialog);
	return ret;
}

static gboolean menu_file_save(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	if(w->filename == NULL) // run 'Save As' instead
		return menu_file_save_as(NULL, w);
	else {
		FILE* fp = fopen(w->filename, "wb");
		if(!fp) return FALSE;
		gboolean ret = mime_model_write_to_file(w->model, fp);
		if(ret) {
			w->dirty = FALSE;
			gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
			gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
		} 
		return ret;
	}
}

// returns a boolean of whether the user closed or not so the function
// can be reused when creating or opening a new document
static gboolean menu_file_close(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	if(w->dirty) {
		GtkWidget* dialog = gtk_message_dialog_new(
				GTK_WINDOW(w->root_window),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_NONE,
				"File has been modified. Would you like to save it?");
		gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_YES, GTK_RESPONSE_YES);
		gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_NO, GTK_RESPONSE_NO);
		gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
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
	close_document(w);
	return TRUE;
}

static void menu_file_quit(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	if(menu_file_close(item, w))
		gtk_main_quit();
}

static void menu_file_reload(GtkMenuItem* item, WemedWindow* w) {
	// just close the current document and reopen it
	char* f = strdup(w->filename); // save the filename for reopening
	GtkWidget* dialog = gtk_message_dialog_new(
			GTK_WINDOW(w->root_window),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_QUESTION,
			GTK_BUTTONS_YES_NO,
			"Are you sure you want to reload the file from disk?");
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
		close_document(w);
		FILE* fp = fopen(f, "rb");
		MimeModel* m = mime_model_create_from_file(fp);
		wemed_window_open(w, m, f);
	}
	gtk_widget_destroy(dialog);
	free(f);
}

static void menu_file_open(GtkMenuItem* item, WemedWindow* w) {
	if(menu_file_close(NULL, w) == FALSE) return;

	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
			GTK_WINDOW(w->root_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		FILE* fp = fopen(filename, "rb");
		MimeModel* m = mime_model_create_from_file(fp);
		if(!m) return;
		wemed_window_open(w, m, filename);
		free(filename);
	}

	gtk_widget_destroy (dialog);
}

static void menu_file_new_blank(GtkMenuItem* item, WemedWindow* w) {
	if(menu_file_close(NULL, w) == FALSE) return;

	MimeModel* m = mime_model_create_blank();
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->mime_tree), mime_model_get_gtk_model(m));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->mime_tree));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
}

static void menu_file_new_email(GtkMenuItem* item, WemedWindow* w) {
	if(menu_file_close(NULL, w) == FALSE) return;

	MimeModel* m = mime_model_create_email();
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->mime_tree), mime_model_get_gtk_model(m));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->mime_tree));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
}

static void menu_view_html_source(GtkCheckMenuItem* item, WemedWindow* w) {
	register_changes(w);
	gboolean source = gtk_check_menu_item_get_active(item);
	wemed_panel_show_source(WEMED_PANEL(w->panel), source);
	set_current_part(w, w->current_part);
}

static void menu_view_remote_resources(GtkCheckMenuItem* item, WemedWindow* w) {
	gboolean remote = gtk_check_menu_item_get_active(item);
	wemed_panel_load_remote_resources(WEMED_PANEL(w->panel), remote);
}

static void menu_view_display_images(GtkCheckMenuItem* item, WemedWindow* w) {
	gboolean images = gtk_check_menu_item_get_active(item);
	wemed_panel_display_images(WEMED_PANEL(w->panel), images);
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
			"Select Node Type",
			GTK_WINDOW(w->root_window),
			GTK_DIALOG_MODAL,
			GTK_STOCK_OK,
			GTK_RESPONSE_ACCEPT,
			GTK_STOCK_CANCEL,
			GTK_RESPONSE_REJECT,
			NULL);
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_add(GTK_CONTAINER(content), combo);
	gtk_widget_show_all(dialog);
	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		w->dirty = TRUE;
		gtk_widget_set_sensitive(w->menu_widgets->revert, TRUE);
		gtk_widget_set_sensitive(w->menu_widgets->save, TRUE);
		mime_model_new_node(w->model, w->current_part, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo)));
	}
	gtk_widget_destroy(dialog);
}

static void menu_part_new_empty(GtkMenuItem* item, WemedWindow* w) {
	mime_model_new_node(w->model, w->current_part, "text/plain");
}

static void menu_part_new_from_file(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
			GTK_WINDOW(w->root_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		char* mime_type = get_file_mime_type(filename);
		GMimePart* part = GMIME_PART(mime_model_new_node(w->model, w->current_part, mime_type));
		{
			FILE* fp = fopen(filename, "rb");
			if(fp) {
				fseek(fp, 0, SEEK_END);
				int len = ftell(fp);
				rewind(fp);
				char* new_content = malloc(len);
				fread(new_content, 1, len, fp);
				fclose(fp);
				g_mime_part_set_filename(part, &strrchr(filename,'/')[1]);
				mime_model_update_content(w->model, part, new_content, len);
				free(new_content);
			}
		}
		free(mime_type);
		free(filename);
	}

	gtk_widget_destroy (dialog);
}

static void menu_part_edit(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	open_part_with_external_app(w, GMIME_PART(w->current_part), w->mime_app.exec);
}

static void menu_part_edit_with(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	const char* content_type_name = mime_model_content_type(w->current_part);
	char* exec = open_with(w->root_window, content_type_name);
	if(exec)
		open_part_with_external_app(w, GMIME_PART(w->current_part), exec);
}

static void menu_part_export(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
			"Save File",
			GTK_WINDOW(w->root_window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL,
			GTK_RESPONSE_CANCEL,
			GTK_STOCK_SAVE,
			GTK_RESPONSE_ACCEPT,
			NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), g_mime_part_get_filename(GMIME_PART(w->current_part)));

	if(gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		FILE* fp = fopen(filename, "wb");
		mime_model_write_part(GMIME_PART(w->current_part), fp);
		free(filename);
	}
	gtk_widget_destroy (dialog);
}

static void menu_part_delete(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	mime_model_part_remove(w->model, w->current_part);
}

static void menu_help_website(GtkMenuItem* item, WemedWindow* w) {
	gtk_show_uri(NULL, "http://wemed.ohwg.net", 0, NULL);
}

static void menu_help_headers(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget* dialog = gtk_dialog_new_with_buttons(
			"MIME Headers",
			GTK_WINDOW(w->root_window),
			GTK_DIALOG_MODAL,
			GTK_STOCK_CLOSE,
			GTK_RESPONSE_ACCEPT,
			NULL);
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget* text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	static const char HEADER_INFO[] =
		"The following are the most useful MIME headers:\n\n"
		"To: <Name> <<email@address>>, <Name> <<email@address>>\n"
		"cc: <Name> <<email@address>>, <Name> <<email@address>>\n"
		"bcc: <Name> <<email@address>>, <Name> <<email@address>>\n"
		"From: <Name> <<email@address>>\n"
		"Subject: <subject>\n"
		"Reply-To: <Name> <<email@address>>\n"
		"Content-Type: <mime-type>; [charset=<charset>]\n"
		"Content-Disposition: (attachment|inline); [filename=<filename>;]\n"
		"Content-Transfer-Encoding: (7bit|quoted-printable|base64)\n"
		"Content-ID: <cid>\n";
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), HEADER_INFO, strlen(HEADER_INFO));
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
			"copyright", "2013 Oliver Giles",
			"website", "http://wemed.ohwg.net",
			NULL);
}

//<<<<<<<<<<<<<<<<<<< END MENU BAR CALLBACK SECTION

static GtkWidget* build_menubar(WemedWindow* w) {
	GtkWidget* menubar = gtk_menu_bar_new();
	MenuWidgets* m = (MenuWidgets*) malloc(sizeof(MenuWidgets));

	{ // File
		GtkWidget* file = gtk_menu_item_new_with_mnemonic("_File");
		GtkWidget* filemenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);
		{ // File -> New Blank Document
			GtkWidget* blank = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
			g_signal_connect(G_OBJECT(blank), "activate", G_CALLBACK(menu_file_new_blank), w);
			gtk_menu_item_set_label(GTK_MENU_ITEM(blank), "New _Blank Document");
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), blank);
		}
		{ // File -> New Email Template
			GtkWidget* email = gtk_menu_item_new_with_mnemonic("New _Email Template");
			g_signal_connect(G_OBJECT(email), "activate", G_CALLBACK(menu_file_new_email), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), email);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Open
			GtkWidget* open = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
			g_signal_connect(G_OBJECT(open), "activate", G_CALLBACK(menu_file_open), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), open);
		}
		{ // File -> Revert To Saved
			m->revert = gtk_image_menu_item_new_from_stock(GTK_STOCK_REVERT_TO_SAVED, NULL);
			g_signal_connect(G_OBJECT(m->revert), "activate", G_CALLBACK(menu_file_reload), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->revert);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Save
			m->save = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE, NULL);
			g_signal_connect(G_OBJECT(m->save), "activate", G_CALLBACK(menu_file_save), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->save);
		}
		{ // File -> Save As
			m->saveas = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE_AS, NULL);
			g_signal_connect(G_OBJECT(m->saveas), "activate", G_CALLBACK(menu_file_save_as), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->saveas);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Close
			m->close = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLOSE, NULL);
			g_signal_connect(G_OBJECT(m->close), "activate", G_CALLBACK(menu_file_close), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), m->close);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Quit
			GtkWidget* quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
			g_signal_connect(G_OBJECT(quit), "activate", G_CALLBACK(menu_file_quit), NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), quit);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file);
	}
	{ // View
		GtkWidget* view = gtk_menu_item_new_with_mnemonic("_View");
		GtkWidget* viewmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(view), viewmenu);
		{ // View -> HTML Source
			m->show_html_source = gtk_check_menu_item_new_with_mnemonic("_Show HTML Source");
			g_signal_connect(G_OBJECT(m->show_html_source), "toggled", G_CALLBACK(menu_view_html_source), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), m->show_html_source);
			gtk_widget_set_sensitive(m->show_html_source, FALSE);
		}
		{ // View -> Remote resources
			GtkWidget* remote = gtk_check_menu_item_new_with_mnemonic("_Load Remote Resources");
			g_signal_connect(G_OBJECT(remote), "toggled", G_CALLBACK(menu_view_remote_resources), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), remote);
		}
		{ // View -> Display Images
			GtkWidget* images = gtk_check_menu_item_new_with_mnemonic("_Display Images");
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(images), TRUE);
			g_signal_connect(G_OBJECT(images), "toggled", G_CALLBACK(menu_view_display_images), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), images);
		}
		{ // View -> Hide Inline Images
			GtkWidget* inline_parts = gtk_check_menu_item_new_with_mnemonic("_Inline Parts in Tree");
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(inline_parts), TRUE);
			g_signal_connect(G_OBJECT(inline_parts), "toggled", G_CALLBACK(menu_view_inline_parts), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), inline_parts);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view);
	}
	{ // Part
		m->part = gtk_menu_item_new_with_mnemonic("_Part");
		GtkWidget* partmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(m->part), partmenu);
		{ // Part -> New Multipart Node
			GtkWidget* node = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
			gtk_menu_item_set_label(GTK_MENU_ITEM(node), "New _Multipart Node");
			g_signal_connect(G_OBJECT(node), "activate", G_CALLBACK(menu_part_new_node), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), node);
		}
		{ // Part -> New Empty Part
			GtkWidget* empty = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
			g_signal_connect(G_OBJECT(empty), "activate", G_CALLBACK(menu_part_new_empty), w);
			gtk_menu_item_set_label(GTK_MENU_ITEM(empty), "New _Empty Part");
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), empty);
		}
		{ // Part -> New From File
			GtkWidget* fromfile = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
			g_signal_connect(G_OBJECT(fromfile), "activate", G_CALLBACK(menu_part_new_from_file), w);
			gtk_menu_item_set_label(GTK_MENU_ITEM(fromfile), "New Part From _File");
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), fromfile);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), gtk_separator_menu_item_new());
		{ // Part -> Edit
			m->menu_part_edit = gtk_image_menu_item_new_from_stock(GTK_STOCK_EDIT, NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), m->menu_part_edit);
			g_signal_connect(G_OBJECT(m->menu_part_edit), "activate", G_CALLBACK(menu_part_edit), w);
		}
		{ // Part -> Edit with
			GtkWidget* editwith = gtk_image_menu_item_new_from_stock(GTK_STOCK_EDIT, NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), editwith);
			gtk_menu_item_set_label(GTK_MENU_ITEM(editwith), "Edit _With...");
			g_signal_connect(G_OBJECT(editwith), "activate", G_CALLBACK(menu_part_edit_with), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), gtk_separator_menu_item_new());
		{ // Part -> Export
			GtkWidget* export = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE, NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), export);
			gtk_menu_item_set_label(GTK_MENU_ITEM(export), "Export...");
			g_signal_connect(G_OBJECT(export), "activate", G_CALLBACK(menu_part_export), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), gtk_separator_menu_item_new());
		{ // Part -> Delete
			GtkWidget* delete = gtk_image_menu_item_new_from_stock(GTK_STOCK_DELETE, NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(partmenu), delete);
			gtk_menu_item_set_label(GTK_MENU_ITEM(delete), "Delete");
			g_signal_connect(G_OBJECT(delete), "activate", G_CALLBACK(menu_part_delete), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), m->part);
	}
	{ // Help
		GtkWidget* help = gtk_menu_item_new_with_mnemonic("_Help");
		GtkWidget* helpmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), helpmenu);
		{ // Help -> Visit Website
			GtkWidget* web = gtk_menu_item_new_with_mnemonic("Visit _Website");
			gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), web);
			g_signal_connect(G_OBJECT(web), "activate", G_CALLBACK(menu_help_website), w);
		}
		{ // Help -> MIME Headers
			GtkWidget* headers = gtk_menu_item_new_with_mnemonic("MIME Headers");
			gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), headers);
			g_signal_connect(G_OBJECT(headers), "activate", G_CALLBACK(menu_help_headers), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), gtk_separator_menu_item_new());
		{ // Help -> About
			GtkWidget* about = gtk_menu_item_new_with_mnemonic("_About Wemed");
			gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), about);
			g_signal_connect(G_OBJECT(about), "activate", G_CALLBACK(menu_help_about), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help);
	}
	w->menu_widgets = m;

	return menubar;
}


gboolean wemed_window_open(WemedWindow* w, MimeModel* m, const char* filename) {
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->mime_tree), mime_model_get_gtk_model(m));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->mime_tree));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
	w->filename = filename ? strdup(filename): strdup("untitled.eml");
	char* title = g_strdup_printf("%s - wemed", w->filename);
	gtk_window_set_title(GTK_WINDOW(w->root_window), title);
	g_free(title);
	gtk_widget_set_sensitive(w->menu_widgets->saveas, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->close, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->part, FALSE);
	return TRUE;
}

WemedWindow* wemed_window_create() {
	WemedWindow* w = calloc(1, sizeof(WemedWindow));

	w->root_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
	w->icon = gtk_icon_theme_load_icon(icon_theme, "wemed", 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
	gtk_window_set_icon(GTK_WINDOW(w->root_window), w->icon);
	gtk_window_set_position(GTK_WINDOW(w->root_window), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(w->root_window), 640, 480);
	g_signal_connect(w->root_window, "destroy", G_CALLBACK(menu_file_quit), NULL);
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* menubar = build_menubar(w);

	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 3);
	GtkWidget* hpanel = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

	w->mime_tree = mime_tree_new();

	GtkWidget* treeviewwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(treeviewwin), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(treeviewwin), w->mime_tree);
	gtk_paned_add1(GTK_PANED(hpanel), treeviewwin);
	w->panel = wemed_panel_new();
	gtk_paned_add2(GTK_PANED(hpanel), w->panel);

	g_signal_connect(G_OBJECT(w->mime_tree), "selection-changed", G_CALLBACK(tree_selection_changed), w);

	gtk_box_pack_start(GTK_BOX(vbox), hpanel, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(w->root_window), vbox);

	close_document(w); // initially, all widgets should be disabled etc.

	gtk_widget_show_all(w->root_window);

	return w;
}

