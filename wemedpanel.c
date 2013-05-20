


#include <gtk/gtk.h>
#include <stdlib.h>
#include <webkit2/webkit2.h>
#include <gmime/gmime.h>
#include <string.h>
#include "mimeapp.h"
#include "wemedpanel.h"

struct WemedPanel_S {
	GtkWidget* webview;
	GtkTextBuffer* headertext;
};

WebKitWebContext* ctx;
gboolean            load_failed_cb                      (WebKitWebView  *web_view,
		WebKitLoadEvent load_event,
		gchar          *failing_uri,
		gpointer        error,
		gpointer        user_data) {

	printf("loading %s failed: %s\n", failing_uri, ((GError*)error)->message);
	return FALSE;

}

void load_document_part(WemedPanel* wp, GMimeObject* obj) {
	char* str = g_mime_object_get_headers(obj);
	gtk_text_buffer_set_text(wp->headertext, str, strlen(str));
	free(str);

	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(wp->webview));
	webkit_web_context_clear_cache(ctx);
	//webkit_web_view_load_plain_text(wp->webview, "");
	if(GMIME_IS_PART(obj)) {
		GMimeContentType* ct = g_mime_object_get_content_type(obj);
		const char* content_type_name = g_mime_content_type_to_string(ct);
		printf("selected type name: %s\n", content_type_name);
		if(strcmp(content_type_name, "text/html") == 0) {
			printf("selected html\n");
			GMimeDataWrapper* mco = g_mime_part_get_content_object((GMimePart*)obj);
			GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
			char* str = malloc(g_mime_stream_length(gms));
			g_mime_stream_read(gms, str, g_mime_stream_length(gms));
			g_mime_stream_reset(gms);
			webkit_web_view_load_html(WEBKIT_WEB_VIEW(wp->webview),  str, NULL);
			free(str);
			//gtk_widget_show(wp->webview);


		} else if(strncmp("image", content_type_name, 5) == 0) {
			struct Application a = get_default_mime_app(content_type_name);
			printf("app name %s, path %s\n", a.name, a.exec);

			const char* cid = g_mime_object_get_content_id(obj);
			int l = strlen(cid);
			char* str = malloc(4 + l + 1);
			memcpy(str, "cid:", 4);
			memcpy(&str[4], cid, l+1);

			printf("looking up %s\n",str);
			webkit_web_view_load_uri(WEBKIT_WEB_VIEW(wp->webview),  str);
			free(str);

			printf("selected a part!\n");
		}
	}
}

void cid_loading_cb(WebKitURISchemeRequest* request, gpointer user_data) {
	GHashTable* hash = (GHashTable*) user_data;
	const gchar* path = webkit_uri_scheme_request_get_path(request);
	GMimePart* part = g_hash_table_lookup(hash, path);
	if(part) {
		GMimeContentType* ct = g_mime_object_get_content_type((GMimeObject*)part);
		const char* content_type_name = g_mime_content_type_to_string(ct);
		GMimeDataWrapper* mco = g_mime_part_get_content_object(part);
		GMimeStream* gms = g_mime_data_wrapper_get_stream(mco);
		GMimeFilter* basic_filter = g_mime_filter_basic_new(g_mime_part_get_content_encoding(part), FALSE);
		GMimeStream* stream_filter = g_mime_stream_filter_new(gms);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), basic_filter);
		GMimeStream* memstream = g_mime_stream_mem_new();
		g_mime_stream_write_to_stream(stream_filter, memstream);
		g_mime_stream_reset(gms);
		GByteArray* arr = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(memstream));
		GBytes* b = g_byte_array_free_to_bytes(arr);
		gsize sz;
		gpointer* data = g_bytes_unref_to_data(b, &sz);
		g_mime_stream_mem_set_owner(memstream, FALSE);
		webkit_uri_scheme_request_finish(request, g_memory_input_stream_new_from_data(data, sz, g_free), sz, content_type_name);
		g_object_unref(memstream);
	} else {
		printf("could not find %s in hash\n", path);
	}
}

void load_changed_cb(WebKitWebView  *web_view, WebKitLoadEvent load_event, gpointer        user_data) {
		printf("load changed\n");
		}

WemedPanel* create_wemed_panel(GtkWidget* parent, GHashTable* cidhash) {
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	WemedPanel* wp = (WemedPanel*) malloc(sizeof(WemedPanel));

	ctx = webkit_web_context_get_default();
//	webkit_web_context_set_cache_model(ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	webkit_web_context_register_uri_scheme(ctx, "cid", cid_loading_cb, cidhash, NULL);

	wp->webview = webkit_web_view_new();
	webkit_web_view_load_plain_text(WEBKIT_WEB_VIEW(wp->webview), "test<b>bold</b>");
//	g_signal_connect(G_OBJECT(wp->webview), "resource-load-started", G_CALLBACK(resource_request_cb), cidhash);
//	g_signal_connect(G_OBJECT(wp->webview), "decide-policy", G_CALLBACK(policy_decision_cb), NULL);
	g_signal_connect(G_OBJECT(wp->webview), "load-failed", G_CALLBACK(load_failed_cb), NULL);
	g_signal_connect(G_OBJECT(wp->webview), "load-changed", G_CALLBACK(load_changed_cb), NULL);

	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Headers:"), FALSE, FALSE, 0);
	GtkWidget* headers = gtk_text_view_new();
	wp->headertext = gtk_text_view_get_buffer(GTK_TEXT_VIEW(headers));
	gtk_box_pack_start(GTK_BOX(vbox), headers, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Content:"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), wp->webview, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gtk_button_new_with_label("test"), FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(parent), vbox, TRUE, TRUE, 0);
	return wp;
}








