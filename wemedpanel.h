#ifndef WEMEDPANEL_H
#define WEMEDPANEL_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */

struct WemedPanel_S;
typedef struct WemedPanel_S WemedPanel;


typedef GMimeObject* (*WemedPanelHeaderCallback)(void*, GMimeObject*, const char*);
WemedPanel* wemed_panel_create();
GtkWidget* wemed_panel_get_widget(WemedPanel* wp);
void wemed_open_part(WemedPanel* wp, const char* app);
const char* wemed_panel_current_content_type(WemedPanel* wp);
void wemed_panel_set_cid_table(WemedPanel* wp, GHashTable* hash);
void wemed_panel_set_header_change_callback(WemedPanel* wp, WemedPanelHeaderCallback, void*);
void wemed_panel_clear(WemedPanel* wp);
void load_document_part(WemedPanel* wp, GMimeObject* obj);

#endif

