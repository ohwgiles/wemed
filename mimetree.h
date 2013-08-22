#ifndef MIMETREE_H
#define MIMETREE_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <gtk/gtk.h>

#define MIME_TREE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), mime_tree_get_type(), MimeTree))

typedef struct _MimeTree MimeTree;
typedef struct _MimeTreeClass MimeTreeClass;
struct MimeTreePrivate;

struct _MimeTree {
	GtkTreeView root;
};

struct _MimeTreeClass {
	GtkTreeViewClass parent_class;
};

GType mime_tree_get_type();

GtkWidget* mime_tree_new();

#endif

