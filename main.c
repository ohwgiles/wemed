/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <webkit2/webkit2.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include "mimemodel.h"
#include "mainwindow.h"

int main(int argc, char** argv) {
	gtk_init(&argc, &argv);
	g_mime_init(0);

	WemedWindow* w = wemed_window_create();
	if(argc == 2) {
		MimeModel* m = mime_model_create_from_file(argv[1]);
		wemed_window_open(w, m, argv[1]);
	}

	gtk_main();
	return 0;
}

