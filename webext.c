/* Copyright 2017-2022 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <glib.h>
#include <webkit2/webkit-web-extension.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

typedef struct {
	gboolean load_remote;
} WemedExt;

static void input_cb(WebKitWebPage* web_page) {
	webkit_web_page_send_message_to_view(web_page, webkit_user_message_new("dirtied", NULL), NULL, NULL, NULL);
}

static void loaded_cb(WebKitWebPage* web_page, WemedExt* ext) {
	JSCContext* ctx = webkit_frame_get_js_context(webkit_web_page_get_main_frame(web_page));
	jsc_context_set_value(ctx, "__notifyChanged", jsc_value_new_function(ctx, NULL, G_CALLBACK(input_cb), web_page, NULL, G_TYPE_NONE, 0));
	static const char *code = "document.documentElement.addEventListener('input', __notifyChanged);";
	JSCValue *value = jsc_context_evaluate(ctx, code, strlen(code));
	g_object_unref(value);
}

static gboolean send_request_cb(WebKitWebPage* web_page, WebKitURIRequest* request, WebKitURIResponse* redirected_response, WemedExt* ext) {
	const char *request_uri =  webkit_uri_request_get_uri (request);
	// Always allow about:blank and cid: urls
	if(strcmp(request_uri, "about:blank") == 0 || strncmp(request_uri, "cid:", 4) == 0)
		return FALSE;
	// return TRUE to block the request
	return !ext->load_remote;
}

static gboolean user_message_received_cb(WebKitWebPage* web_page, WebKitUserMessage *message, gpointer user_data) {
	if(g_strcmp0(webkit_user_message_get_name(message), "load-remote-resources") == 0) {
		WemedExt* ext = (WemedExt*) user_data;
		ext->load_remote = g_variant_get_boolean(webkit_user_message_get_parameters(message));
		return TRUE;
	}
	return FALSE;
}

static void web_page_created_callback(WebKitWebExtension* extension, WebKitWebPage* web_page, WemedExt* ext) {
	g_signal_connect(web_page, "document-loaded", G_CALLBACK(loaded_cb), ext);
	g_signal_connect(web_page, "send-request", G_CALLBACK(send_request_cb), ext);
	g_signal_connect(web_page, "user-message-received", G_CALLBACK(user_message_received_cb), ext);
}

G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(WebKitWebExtension *extension, GVariant* user_data) {
	WemedExt* ext = (WemedExt*) malloc(sizeof(WemedExt)); // leaks!
	ext->load_remote = FALSE;

	g_signal_connect(extension, "page-created", G_CALLBACK(web_page_created_callback), ext);
}
