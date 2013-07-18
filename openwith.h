
#ifndef OPEN_WITH_H
#define OPEN_WITH_H

#include <gtk/gtk.h>

// given a content type, open a dialog allowing the user to
// choose a handling application or specify a custom executable.
// Returns the path to the executable (must be free'd) or NULL
// if the request was cancelled
char* open_with(GtkWidget* parent, const char* content_type);

#endif


