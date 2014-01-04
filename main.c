/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include <webkit/webkitglobals.h>
#include "mimemodel.h"
#include "mainwindow.h"

#include <locale.h>
#include <libintl.h>

GtkIconTheme* system_icon_theme = 0;

int main(int argc, char** argv) {
	setlocale(LC_ALL, "");
	textdomain("wemed");
	gtk_init(&argc, &argv);
	system_icon_theme = gtk_icon_theme_get_default();
	webkit_set_cache_model(WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	g_mime_init(0);

	WemedWindow* w = wemed_window_create();
	if(argc == 2) {
		wemed_window_open(w, argv[1]);
	}

	gtk_main();
	wemed_window_free(w);
	return 0;
}

