/* Copyright 2013-2017 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include <webkit2/webkit2.h>
#include <locale.h>
#include <libintl.h>

#include "mimemodel.h"
#include "mainwindow.h"

int main(int argc, char** argv) {

	setlocale(LC_ALL, "");
	textdomain("wemed");

	gtk_init(&argc, &argv);
	g_mime_init(0);

	WemedWindow* w = wemed_window_create();

	// open a document if it was given on the command line
	if(argc == 2) {
		wemed_window_open(w, argv[1]);
	}

	gtk_main();
	wemed_window_free(w);

	return 0;
}

