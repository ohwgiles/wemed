/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the 
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */

#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include "openwith.h"

char* strchrnul(const char*, int);

static void populate_options(const char* content_type, GtkIconTheme* icontheme, GtkListStore* store) {
	// first get all the possible apps from the MIME cache
	int pipes[2];
	char buffer[20001] = {0};
	int n;

	pipe(pipes);
	fcntl(pipes[1], F_SETFL, fcntl(pipes[1], F_GETFL) | O_NONBLOCK);
	fcntl(pipes[0], F_SETFL, fcntl(pipes[0], F_GETFL) | O_NONBLOCK);

	char* grepsearch = malloc(1 + strlen(content_type) + 2);
	sprintf(grepsearch, "^%s=", content_type);

	pid_t pid = fork();
	if(pid == 0) { //child
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		execlp("grep", "grep", grepsearch, "/usr/share/applications/mimeinfo.cache", NULL);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	free(grepsearch);
	n = read(pipes[0], buffer, 20000);
	if(n < 0) return;
	buffer[n] = '\0';
	*strchrnul(buffer, '\n') = '\0';


	// now we have their .desktops in our buffer, get all the possible apps.
	// to save execution overhead, do it in one big grep
	char* argv[100];
	argv[0] = "grep";
	argv[1] = "-E";
	argv[2] = "-Z";
	argv[3] = "^Name=|^Exec=|^Icon=";
	int argc = 3;
	for(char* p = buffer; *p; ++p) {
		if(*p == ';') *p++ = 0, argv[++argc] = p;
	}

	pid = fork();
	if(pid == 0) { //child
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		chdir("/usr/share/applications");
		execvp("grep", argv);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	n = read(pipes[0], buffer, 20000);
	if(n < 0) return ;
	buffer[n] = '\0';
	
	// now we have all the apps, get their names, paths and icons
	char *pf = buffer, *pe = 0, *pn = 0, *pi = 0;
	for(char* p = buffer; p != &buffer[n]; ++p) {
		if(*p == '\0') {
			if(strncmp(&p[1], "Name=", 5) == 0) {
				pn = &p[6];
			} else if(strncmp(&p[1], "Exec=", 5) == 0) {
				pe = &p[6];
			} else if(strncmp(&p[1], "Icon=", 5) == 0) {
				pi = &p[6];
			} else {
				printf("Something weird happened\n");
				printf("p1 is %s\n", &p[1]);
				continue;
			}

		}
		else if(*p == '\n') {
			*p = '\0';
			if(strcmp(&p[1], pf) != 0) {
				printf("found name %s exec %s icon %s\n", pn, pe, pi);
				GdkPixbuf* icon = gtk_icon_theme_load_icon(icontheme, pi, 16, GTK_ICON_LOOKUP_USE_BUILTIN, 0);
				GtkTreeIter iter;
				gtk_list_store_append(store, &iter);
				gtk_list_store_set(store, &iter, 0, icon, 1, pn, 2, pe, -1);
				pn = pe = pi = 0;
				pf = &p[1];
			}
		}
	}
}

static GtkWidget* create_list_widget() {
	GtkWidget* view = gtk_tree_view_new();
	GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_BROWSE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

	GtkTreeViewColumn* col = gtk_tree_view_column_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
	GtkCellRenderer* renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "pixbuf", 0);

	col = gtk_tree_view_column_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", 1);

	return view;
}




char* open_with(GtkWidget* parent, const char* content_type) {
	GtkIconTheme* git = gtk_icon_theme_get_default();

	GtkListStore* store = gtk_list_store_new(3, G_TYPE_OBJECT, G_TYPE_STRING, G_TYPE_STRING);
	populate_options(content_type, git, store);

	GtkTreeModel* model = GTK_TREE_MODEL(store);
	GtkWidget* view = create_list_widget();
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);

	GtkWidget* custom = gtk_entry_new();

	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Open With:"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), view, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Custom Application:"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), custom, FALSE, FALSE, 0);

	GtkWidget* dialog = gtk_dialog_new_with_buttons("Open With", GTK_WINDOW(parent), GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_add(GTK_CONTAINER(content), vbox);
	gtk_widget_show_all(dialog);

	int response = gtk_dialog_run(GTK_DIALOG(dialog));

	char* ret = 0;

	if(response == GTK_RESPONSE_ACCEPT) {
		if(gtk_entry_get_text_length(GTK_ENTRY(custom)) > 0) {
			ret = strdup(gtk_entry_get_text(GTK_ENTRY(custom)));
		} else {
			GtkTreeIter iter;
			char* exec_entry;
			gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), &model, &iter);
			gtk_tree_model_get(model, &iter, 2, &exec_entry, -1);
			ret = strdup(exec_entry);
		}
	}

	gtk_widget_destroy(dialog);

	return ret;
}


