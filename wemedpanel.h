#ifndef WEMEDPANEL_H
#define WEMEDPANEL_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>

G_BEGIN_DECLS
#define WEMED_PANEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), wemed_panel_get_type(), WemedPanel))
//#define WEMED_PANEL_CLASS(klass) GTK_CHECK_CLASS_CAST(klass, wemed_panel_get_type(), WemedPanelClass)
//#define IS_WEMED_PANEL(obj) GTK_CHECK_TYPE(obj, wemed_panel_get_type())

typedef struct _WemedPanel WemedPanel;
typedef struct _WemedPanelClass WemedPanelClass;
struct WemedPanelPrivate;

struct _WemedPanel {
	GtkPaned root;
};

struct _WemedPanelClass {
	GtkPanedClass parent_class;
	void (*headers_changed)(WemedPanel*wp);
};

GType wemed_panel_get_type();
GtkWidget* wemed_panel_new();

//struct WemedPanel_S;
//typedef struct WemedPanel_S WemedPanel;



typedef GMimeObject* (*WemedPanelHeaderCallback)(void*, GMimeObject*, const char*);
GtkWidget* wemed_panel_get_widget(WemedPanel* wp);
void wemed_open_part(WemedPanel* wp, const char* app);
const char* wemed_panel_current_content_type(WemedPanel* wp);
void wemed_panel_set_cid_table(WemedPanel* wp, GHashTable* hash);
void wemed_panel_set_header_change_callback(WemedPanel* wp, WemedPanelHeaderCallback, void*);
void wemed_panel_clear(WemedPanel* wp);
void load_document_part(WemedPanel* wp, GMimeObject* obj);
G_END_DECLS
#endif

