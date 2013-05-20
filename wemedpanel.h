
#ifndef WEMEDPANEL_H
#define WEMEDPANEL_H

struct WemedPanel_S;
typedef struct WemedPanel_S WemedPanel;
WemedPanel* create_wemed_panel(GtkWidget* parent, GHashTable* cidhash);
void load_document_part(WemedPanel* wp, GMimeObject* obj);

#endif

