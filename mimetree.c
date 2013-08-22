/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <gmime/gmime.h>
#include "mimemodel.h"
#include "mimetree.h"

G_DEFINE_TYPE(MimeTree, mime_tree, GTK_TYPE_TREE_VIEW);
#define GET_D(o) MimeTreePrivate* d = (G_TYPE_INSTANCE_GET_PRIVATE ((o), mime_tree_get_type(), MimeTreePrivate))

// signals
enum {
	MT_SELECTION_CHANGED,
	MT_SIG_LAST
};

static gint mime_tree_signals[MT_SIG_LAST] = {0};

// the private elements
typedef struct {
	GtkWidget* webview;
	GtkWidget* headerview;
	GtkTextBuffer* headertext;
	GMimeObject* last_part;
	//WebKitWebContext* ctx;
	GtkWidget* progress_bar;
	gboolean html;
} MimeTreePrivate;

static void mime_tree_class_init(MimeTreeClass* class) {
	g_type_class_add_private(class, sizeof(MimeTreePrivate));
	mime_tree_signals[MT_SELECTION_CHANGED] = g_signal_new(
			"selection-changed",
			G_TYPE_FROM_CLASS ((GObjectClass*)class),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			0, // v-table offset
			NULL,
			NULL,
			NULL, //marshaller
			G_TYPE_NONE, // return tye
			1, // num args
			G_TYPE_POINTER); // arg types
}

GtkWidget* mime_tree_new() {
	return g_object_new(mime_tree_get_type(), NULL);
}

static void selection_changed(GtkTreeSelection* selection, MimeTree* mt) {
	GtkTreeIter iter;
	GtkTreeModel *model;

	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gpointer part;
		gtk_tree_model_get(model, &iter, MIME_MODEL_COL_OBJECT, &part, -1);
		g_signal_emit(mt, mime_tree_signals[MT_SELECTION_CHANGED], 0, part);
	}
}

void mime_tree_init(MimeTree* mt) {
	GET_D(mt);
	GtkTreeView* tv = GTK_TREE_VIEW(mt);

	GtkTreeViewColumn* col = gtk_tree_view_column_new();
	gtk_tree_view_append_column(tv, col);
	gtk_tree_view_set_headers_visible(tv, FALSE);

	GtkCellRenderer* renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(col, renderer, FALSE);
	gtk_tree_view_column_add_attribute(col, renderer, "pixbuf", MIME_MODEL_COL_ICON);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", MIME_MODEL_COL_NAME);

	GtkTreeSelection *select = gtk_tree_view_get_selection (tv);
	gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
	
	g_signal_connect(G_OBJECT(select), "changed", G_CALLBACK(selection_changed), mt);

}

