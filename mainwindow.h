#ifndef MAINWINDOW_H
#define MAINWINDOW_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */

struct WemedWindow_S;
typedef struct WemedWindow_S WemedWindow;

// the main GTK window
WemedWindow* wemed_window_create();

// open a new MIME model
gboolean wemed_window_open(WemedWindow* w, const char* filename);

void wemed_window_free(WemedWindow* w);

#endif

