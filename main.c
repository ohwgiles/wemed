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
#include "parsemime.h"

struct DisplayWidgets {
	GtkWidget* container;
	GtkWidget* progressbar;
	GtkWidget* webview;
	GtkWidget* image;
};

void tree_selection_changed_cb(GtkTreeSelection* selection, gpointer data) {
	printf("selection changed!\n");
	GtkTreeIter iter;
	GtkTreeModel *model;
	gpointer part;
	WemedPanel* wp = (WemedPanel*) data;

	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gtk_tree_model_get(model, &iter, 1, &part, -1);
		GMimeObject* obj = (GMimeObject*) part;
		load_document_part(wp, obj);
	}

}


GtkWidget* build_menubar() {
	GtkWidget* menubar = gtk_menu_bar_new();

	{ // File
	GtkWidget* file = gtk_menu_item_new_with_mnemonic("_File");
	GtkWidget* filemenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);
		{ // File -> New
		GtkWidget* new = gtk_menu_item_new_with_mnemonic("_New");
		GtkWidget* newmenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(new), newmenu);
			{ // File -> New -> Blank Document
			GtkWidget* blank = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
			gtk_menu_item_set_label(GTK_MENU_ITEM(blank), "_Blank Document");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), blank);
			}
			{ // File -> New -> Email Template
			GtkWidget* email = gtk_menu_item_new_with_mnemonic("_Email Template");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), email);
			}
			{ // File -> New -> MHTML Template
			GtkWidget* mhtml = gtk_menu_item_new_with_mnemonic("M_HTML Template");
			gtk_menu_shell_append(GTK_MENU_SHELL(newmenu), mhtml);
			}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), new);
		}
		{ // File -> Open
		GtkWidget* open = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), open);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), gtk_separator_menu_item_new());
		{ // File -> Quit
		GtkWidget* quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
		g_signal_connect(G_OBJECT(quit), "activate", G_CALLBACK(gtk_main_quit), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), quit);
		}
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file);
	}

	return menubar;

}

int main(int argc, char** argv) {
	gtk_init(&argc, &argv);

	if(argc != 2) return printf("Present usage: %s <path_to_mime_file>\n", argv[0]), 1;

	MimeModel* m = mime_model_create_from_file(argv[1]);

	GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* menubar = build_menubar();

	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 3);


	GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);


	GtkWidget* view = gtk_tree_view_new();
	GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);


	GtkTreeViewColumn* col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Segment");
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", 0);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Icon");
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "pixbuf", 2);


	gtk_tree_view_set_model(GTK_TREE_VIEW(view), mime_model_get_gtk_model(m));

	gtk_box_pack_start(GTK_BOX(hbox), view, FALSE, FALSE, 0);
	WemedPanel* wp = wemed_panel_create(hbox, mime_model_get_cid_hash(m));
	wemed_panel_set_header_change_callback(wp, mime_model_update_header, m);
	g_signal_connect(G_OBJECT(select), "changed", G_CALLBACK(tree_selection_changed_cb), wp);
	
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);


	gtk_widget_show_all(window);

	gtk_main();
	return 0;
}
