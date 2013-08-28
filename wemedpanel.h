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

typedef enum {
	WEMED_PANEL_DOC_TYPE_TEXT_HTML,
	WEMED_PANEL_DOC_TYPE_TEXT_PLAIN,
	WEMED_PANEL_DOC_TYPE_IMAGE,
	WEMED_PANEL_DOC_TYPE_OTHER
} WemedPanelDocType;

GType wemed_panel_get_type();

GtkWidget* wemed_panel_new();

// Loads a new MIME part into the display pane
void wemed_panel_load_doc(WemedPanel* wp, WemedPanelDocType type, const char* headers, const char* content, const char* charset);

// Toggle between showing HTML or source
void wemed_panel_show_source(WemedPanel* wp, gboolean);

// Toggle the loading of remote resources in HTML view
void wemed_panel_load_remote_resources(WemedPanel* wp, gboolean en);

// Return the (possibly modified) headers
char* wemed_panel_get_headers(WemedPanel* wp);

// Return the (possibly modified) text or HTML-source content
char* wemed_panel_get_content(WemedPanel* wp, gboolean as_source);

void wemed_panel_clear(WemedPanel* wp);

#endif

