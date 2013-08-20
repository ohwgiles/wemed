#ifndef WEMEDPANEL_H
#define WEMEDPANEL_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>

#define WEMED_PANEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), wemed_panel_get_type(), WemedPanel))

typedef struct _WemedPanel WemedPanel;
typedef struct _WemedPanelClass WemedPanelClass;
struct WemedPanelPrivate;

struct _WemedPanel {
	GtkPaned root;
};

struct _WemedPanelClass {
	GtkPanedClass parent_class;
};

GType wemed_panel_get_type();

GtkWidget* wemed_panel_new();

void wemed_panel_set_cid_table(WemedPanel* wp, GHashTable* hash);

void wemed_panel_load_part(WemedPanel* wp, GMimeObject* obj, const char* content_type_name);

void wemed_panel_clear(WemedPanel* wp);

#endif

