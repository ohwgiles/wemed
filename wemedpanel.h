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

typedef struct {
	const char* content_type;
	const char* charset;
	GString headers;
	GString content;
} WemedPanelDoc;

GType wemed_panel_get_type();

GtkWidget* wemed_panel_new();

// Loads a new MIME part into the display pane
void wemed_panel_load_doc(WemedPanel* wp, WemedPanelDoc doc);

// Toggle between showing HTML or source
void wemed_panel_show_source(WemedPanel* wp, gboolean);

// Toggle the loading of remote resources in HTML view
void wemed_panel_load_remote_resources(WemedPanel* wp, gboolean en);

// Toggle the display of images
void wemed_panel_display_images(WemedPanel* wp, gboolean en);

// Return the (possibly modified) headers
GString wemed_panel_get_headers(WemedPanel* wp);

// Return the (possibly modified) text or HTML-source content
GString wemed_panel_get_content(WemedPanel* wp);

void wemed_panel_clear(WemedPanel* wp);

#endif

