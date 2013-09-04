/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include "mimemodel.h"
#include "mainwindow.h"

GtkIconTheme* system_icon_theme = 0;

int main(int argc, char** argv) {
	gtk_init(&argc, &argv);
	g_mime_init(0);
	system_icon_theme = gtk_icon_theme_get_default();

	WemedWindow* w = wemed_window_create();
	if(argc == 2) {
		FILE* fp = fopen(argv[1], "rb");
		if(fp) {
			MimeModel* m = mime_model_create_from_file(fp);
			wemed_window_open(w, m, argv[1]);
		}
	}

	gtk_main();
	return 0;
}

