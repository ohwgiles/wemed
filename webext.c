#include <glib.h>
#include <webkit2/webkit-web-extension.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

typedef struct {
	int ipc_fd;
	gboolean load_remote;
} WemedExt;

static gboolean input_cb(WebKitDOMDOMWindow* view, WebKitDOMEvent* event, WemedExt* ext) {
	// send int(0) to the UI process. This asynchronous message notifies it
	// that the DOM has been dirtied
	int zero = 0;
	write(ext->ipc_fd, &zero, sizeof(int));
	return TRUE;
}

static gboolean content_cb(GObject* doc, WebKitDOMEvent* event, WemedExt* ext) {
	// send DOM contents prefixed by int(len) to the UI process. The UI
	// process triggers this event and will be waiting synchronously for
	// this response
	gchar* content = webkit_dom_element_get_outer_html(webkit_dom_document_get_document_element(WEBKIT_DOM_DOCUMENT(doc)));
	int len = strlen(content);
	write(ext->ipc_fd, &len, sizeof(int));
	write(ext->ipc_fd, content, len);
	g_free(content);
	return TRUE;
}

static gboolean load_remote_true_cb(GObject* doc, WebKitDOMEvent* event, WemedExt* ext) {
	ext->load_remote = TRUE;
	return TRUE;
}

static gboolean load_remote_false_cb(GObject* doc, WebKitDOMEvent* event, WemedExt* ext) {
	ext->load_remote = FALSE;
	return TRUE;
}

static void loaded_cb(WebKitWebPage* web_page, WemedExt* ext) {
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(webkit_web_page_get_dom_document(web_page)), "input", G_CALLBACK(input_cb), TRUE, ext);
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(webkit_web_page_get_dom_document(web_page)), "wemed_content", G_CALLBACK(content_cb), TRUE, ext);
	// TODO: Figure out how to access the CustomEvent 'detail' property so a single event handler can be used
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(webkit_web_page_get_dom_document(web_page)), "wemed_load_remote_true", G_CALLBACK(load_remote_true_cb), TRUE, ext);
	webkit_dom_event_target_add_event_listener(WEBKIT_DOM_EVENT_TARGET(webkit_web_page_get_dom_document(web_page)), "wemed_load_remote_false", G_CALLBACK(load_remote_false_cb), TRUE, ext);
}

static gboolean send_request_cb(WebKitWebPage* web_page, WebKitURIRequest* request, WebKitURIResponse* redirected_response, WemedExt* ext) {
	const char *request_uri =  webkit_uri_request_get_uri (request);
	// Always allow about:blank and cid: urls
	if(strcmp(request_uri, "about:blank") == 0 || strncmp(request_uri, "cid:", 4) == 0)
		return FALSE;
	// return TRUE to block the request
	return !ext->load_remote;
}

static void web_page_created_callback(WebKitWebExtension* extension, WebKitWebPage* web_page, WemedExt* ext) {
	g_signal_connect(web_page, "document-loaded", G_CALLBACK(loaded_cb), ext);
	g_signal_connect(web_page, "send-request", G_CALLBACK(send_request_cb), ext);
}

G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(WebKitWebExtension *extension, GVariant* user_data) {
	WemedExt* ext = (WemedExt*) malloc(sizeof(WemedExt)); // leaks!
	ext->load_remote = FALSE;

	// set up the IPC
	struct sockaddr_un saddr;
	ext->ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&saddr, 0, sizeof(saddr));
	saddr.sun_family = AF_UNIX;
	gsize pathlen;
	const gchar* str = g_variant_get_string(user_data, &pathlen);
	memcpy(saddr.sun_path+1, str, pathlen);
	connect(ext->ipc_fd, (struct sockaddr*) &saddr, sizeof(saddr));

	g_signal_connect(extension, "page-created", G_CALLBACK(web_page_created_callback), ext);
}
