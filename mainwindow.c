/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>

#include <string.h>
#include <stdlib.h>
#include <gmime/gmime.h>
#include <webkit2/webkit2.h>
#include "mimeapp.h"
#include "wemedpanel.h"
#include "mimemodel.h"
#include "mimetree.h"
#include "mainwindow.h"
#include "openwith.h"

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
	MimeModel* model;
	GtkWidget* root_window;
	GtkWidget* view;
	GtkWidget* panel;
	char* filename;
	gboolean dirty;
	GMimeObject* current_part;
	struct Application mime_app;
	MenuWidgets* menu_widgets;
};

static void set_current_part(WemedWindow* w, GMimeObject* part) {
	GMimeObject* obj = (GMimeObject*) part;
	w->current_part = obj;
	const char* mime_type = mime_model_content_type(w->current_part);
	gtk_widget_set_sensitive(w->menu_widgets->part, TRUE);
	//wemed_panel_load_part(WEMED_PANEL(w->panel), obj, mime_type);
	char* headers = g_mime_object_get_headers(part);
	char* content = GMIME_IS_PART(part) ? mime_model_part_content(GMIME_PART(part)) : NULL;

	WemedPanelDocType type =
		(strcmp(mime_type, "text/plain") == 0)? WEMED_PANEL_DOC_TYPE_TEXT_PLAIN:
		(strcmp(mime_type, "text/html") == 0)? WEMED_PANEL_DOC_TYPE_TEXT_HTML:
		(strncmp(mime_type, "image/", 6) == 0)? WEMED_PANEL_DOC_TYPE_IMAGE: WEMED_PANEL_DOC_TYPE_OTHER;
	if(type == WEMED_PANEL_DOC_TYPE_TEXT_HTML) {
		gtk_widget_set_sensitive(w->menu_widgets->show_html_source, TRUE);
		wemed_panel_show_source(WEMED_PANEL(w->panel), gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w->menu_widgets->show_html_source)));
	} else {
		gtk_widget_set_sensitive(w->menu_widgets->show_html_source, FALSE);
		wemed_panel_show_source(WEMED_PANEL(w->panel), FALSE);
	}
	wemed_panel_load_doc(WEMED_PANEL(w->panel), type, headers, content);
	free(headers);
	free(content);

	free(w->mime_app.name);
	free(w->mime_app.exec);
	w->mime_app = get_default_mime_app(mime_type);
	if(w->mime_app.exec) {
		const char* editwith = "_Edit with %s";
		char* label = malloc(strlen(editwith) + strlen(w->mime_app.name));
		sprintf(label, editwith, w->mime_app.name);
		gtk_menu_item_set_label(GTK_MENU_ITEM(w->menu_widgets->menu_part_edit), label);
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, TRUE);
	} else {
		gtk_widget_set_sensitive(w->menu_widgets->menu_part_edit, FALSE);
	}
}

static void register_changes(WemedWindow* w) {
	if(w->current_part == NULL) return;
	// first see if the content has been changed. Only do this for
	// content types of text/* since other types are edited externally,
	// and they are saved then
	if(GMIME_IS_PART(w->current_part)) {
		const char* ct = mime_model_content_type(w->current_part);
		if(strncmp(ct, "text/", 5) == 0) {
			gboolean is_html = (strcmp(&ct[5], "html") == 0);
			char* new_content = wemed_panel_get_text_content(WEMED_PANEL(w->panel), is_html);
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
	if( (p = strstr(app, "%f")) || (p = strstr(app, "%U")) || (p = strstr(app, "%s"))) {
		p[1] = 's';
		sprintf(buffer, app, tmpfile);
	} else {
		sprintf(buffer, "%s %s", app, tmpfile);
	}
	close(fd);
	system(buffer);
	free(buffer);

	fp = fopen(tmpfile, "rb");
	fseek(fp, 0, SEEK_END);
	int len = ftell(fp);
	rewind(fp);
	char* new_content = malloc(len);
	fread(new_content, 1, len, fp);
	mime_model_update_content(w->model, part, new_content, len);
	set_current_part(w, GMIME_OBJECT(part));
	free(new_content);
	
	unlink(tmpfile); // be a tidy kiwi
}

static gboolean menu_file_save_as(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	(void) item; // unused
	gboolean ret = TRUE;
	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Save File", GTK_WINDOW(w->root_window), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), w->filename?:"untitled.eml");

	ret = (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT);
	if(ret) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		ret = mime_model_write_to_file(w->model, filename);
		if(ret) {
			w->dirty = FALSE;
			gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
			gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
			w->filename = strdup(filename);
		}
		g_free(filename);
	}
	gtk_widget_destroy (dialog);
	return ret;
}

static gboolean menu_file_save(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	gboolean ret = TRUE;
	if(w->filename == NULL)
		ret = menu_file_save_as(NULL, w);
	else if(w->dirty) {
		ret = mime_model_write_to_file(w->model, w->filename);
		if(ret) {
			w->dirty = FALSE;
			gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
			gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
		}
	}
	return ret;
}
static gboolean menu_file_close(GtkMenuItem* item, WemedWindow* w) {
	register_changes(w);
	(void) item; // unused
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
			if(menu_file_save(NULL, w) != TRUE) return FALSE;
		} else if(ret == GTK_RESPONSE_NO) { // passthrough
		} else return FALSE;
	}
	wemed_window_close(w);
	return TRUE;
}

static void menu_file_reload(GtkMenuItem* item, WemedWindow* w) {
	(void) item;
	char* f = strdup(w->filename);
	if(menu_file_close(NULL, w) == FALSE) return;
	wemed_window_open(w, w->model, f);
	free(f);
}

static void menu_file_open(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(menu_file_close(NULL, w) == FALSE) return;

	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
			GTK_WINDOW(w->root_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		MimeModel* m = mime_model_create_from_file(filename);
		if(!m) return;
		wemed_window_open(w, m, filename);
		g_free(filename);
	}

	gtk_widget_destroy (dialog);
}

void menu_file_new_blank(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(menu_file_close(NULL, w) == FALSE) return;

	MimeModel* m = mime_model_create_blank();
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->view), mime_model_get_gtk_model(m));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->view));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
}
void menu_file_new_email(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(menu_file_close(NULL, w) == FALSE) return;

	MimeModel* m = mime_model_create_email();
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->view), mime_model_get_gtk_model(m));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->view));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
}
void menu_file_new_mhtml(GtkMenuItem* item, WemedWindow* w) {
	(void) item; // unused
	if(menu_file_close(NULL, w) == FALSE) return;
}

static void menu_view_html_source(GtkCheckMenuItem* item, WemedWindow* w) {
	register_changes(w);
	gboolean source = gtk_check_menu_item_get_active(item);
	wemed_panel_show_source(WEMED_PANEL(w->panel), source);
	set_current_part(w, w->current_part);
}

static void menu_part_new_node(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget* combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "multipart/related");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "multipart/alternative");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	GtkWidget* dialog = gtk_dialog_new_with_buttons("Select Node Type", NULL/*GTK_WINDOW(w->window)*/, GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, NULL);
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_add(GTK_CONTAINER(content), combo);
	gtk_widget_show_all(dialog);
	int response = gtk_dialog_run(GTK_DIALOG(dialog));
	if(response == GTK_RESPONSE_ACCEPT) {
		printf("selected %s\n", gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo)));
		mime_model_new_node(w->model, w->current_part, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo)), NULL);
	}
	gtk_widget_destroy(dialog);
}

static void menu_part_new_empty(GtkMenuItem* item, WemedWindow* w) {
	mime_model_new_node(w->model, w->current_part, "text/plain", NULL);
}


static void menu_part_new_from_file(GtkMenuItem* item, WemedWindow* w) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
			GTK_WINDOW(w->root_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		mime_model_new_node(w->model, w->current_part, NULL, filename);
		// determine likely mime type
		g_free(filename);
	}

	gtk_widget_destroy (dialog);
}

static void menu_part_edit(GtkMenuItem* item, WemedWindow* w) {
	(void) item; //unused
	//wemed_open_part(WEMED_PANEL(w->panel), w->mime_app.exec);
	register_changes(w);
	open_part_with_external_app(w, GMIME_PART(w->current_part), w->mime_app.exec);
}
static void menu_part_edit_with(GtkMenuItem* item, WemedWindow* w) {
	(void) item; //unused
	register_changes(w);
	const char* content_type_name = mime_model_content_type(w->current_part);
	printf("calling open_with on type %s\n", content_type_name);
	char* exec = open_with(w->root_window, content_type_name);
	if(!exec) return;
	//wemed_open_part(WEMED_PANEL(w->panel), exec);
	open_part_with_external_app(w, GMIME_PART(w->current_part), exec);
}
static void menu_part_export(GtkMenuItem* item, WemedWindow* w) {
	(void) item;
	register_changes(w);
	GtkWidget *dialog = gtk_file_chooser_dialog_new ("Save File", GTK_WINDOW(w->root_window), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), g_mime_part_get_filename(GMIME_PART(w->current_part)));

	if(gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		FILE* fp = fopen(filename, "wb");
		mime_model_write_part(GMIME_PART(w->current_part), fp);
		g_free(filename);
	}
	gtk_widget_destroy (dialog);
}


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
		{ // File -> New MHTML Template
			GtkWidget* mhtml = gtk_menu_item_new_with_mnemonic("New M_HTML Template");
			g_signal_connect(G_OBJECT(mhtml), "activate", G_CALLBACK(menu_file_new_mhtml), w);
			gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), mhtml);
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
			g_signal_connect(G_OBJECT(quit), "activate", G_CALLBACK(gtk_main_quit), NULL);
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
			//g_signal_connect(G_OBJECT(export), "activate", G_CALLBACK(menu_part_export), w);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menubar), m->part);
	}
	w->menu_widgets = m;

	return menubar;

}


gboolean wemed_window_open(WemedWindow* w, MimeModel* m, const char* filename) {
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->view), mime_model_get_gtk_model(m));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(w->view));

	w->model = m;
	g_signal_connect(G_OBJECT(w->panel), "cid-requested", G_CALLBACK(mime_model_object_from_cid), w->model);
	w->filename = filename ? strdup(filename): strdup("untitled.eml");
	gtk_widget_set_sensitive(w->menu_widgets->saveas, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->close, TRUE);
	gtk_widget_set_sensitive(w->menu_widgets->part, FALSE);
	return TRUE;
}



WemedWindow* wemed_window_create() {
	WemedWindow* w = calloc(1, sizeof(WemedWindow));

	w->root_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(w->root_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* menubar = build_menubar(w);

	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 3);
	GtkWidget* hpanel = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

	w->view = mime_tree_new();

	GtkWidget* treeviewwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(treeviewwin), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(treeviewwin), w->view);
	gtk_paned_add1(GTK_PANED(hpanel), treeviewwin);
	w->panel = wemed_panel_new();
	gtk_paned_add2(GTK_PANED(hpanel), w->panel);

	g_signal_connect(G_OBJECT(w->view), "selection-changed", G_CALLBACK(tree_selection_changed), w);

	gtk_box_pack_start(GTK_BOX(vbox), hpanel, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(w->root_window), vbox);

	wemed_window_close(w);

	gtk_widget_show_all(w->root_window);

	return w;
}
void wemed_window_close(WemedWindow* w) {
	wemed_panel_clear(WEMED_PANEL(w->panel));
	gtk_tree_view_set_model(GTK_TREE_VIEW(w->view), NULL);
	mime_model_free(w->model);
	w->model = 0;
	gtk_widget_set_sensitive(w->menu_widgets->revert, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->save, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->saveas, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->close, FALSE);
	gtk_widget_set_sensitive(w->menu_widgets->part, FALSE);
}
