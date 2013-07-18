
#ifndef WEMEDPANEL_H
#define WEMEDPANEL_H

struct WemedPanel_S;
typedef struct WemedPanel_S WemedPanel;


typedef gboolean (*WemedPanelHeaderCallback)(void*, GMimeObject*, const char*);
WemedPanel* wemed_panel_create(GtkWidget* parent, GHashTable* cidhash);
void wemed_panel_set_header_change_callback(WemedPanel*, WemedPanelHeaderCallback, void*);
void load_document_part(WemedPanel* wp, GMimeObject* obj);

#endif

