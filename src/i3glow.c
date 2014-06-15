#include <cairo.h>
#include <cairo-xcb.h>
#include <errno.h>
#include <i3/ipc.h>
#include <json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

#include "util.h"

#define X_CHECKED(code) { xcb_generic_error_t *error; xcb_void_cookie_t cookie = code; if ((error = xcb_request_check(i3g.c, cookie))) FG_FAIL("X11 request at %s:%d failed with %s", __FILE__, __LINE__, xcb_event_get_error_label(error->error_code)); }

#define I3G_BARHEIGHT 6
#define I3G_WINDOWHEIGHT 8
#define I3G_INDICATORWIDTH 20
#define I3G_INDICATORSPACE 12
#define I3G_WS_SHOW_OFFSET 1

struct {
	struct {
		bool seen;
		int mode;
		bool active;
		bool urgent;
	} desktops[64];

	xcb_connection_t *c;
	xcb_screen_t *screen;
	xcb_visualtype_t *argb_visual;

	xcb_window_t window;
	cairo_surface_t *surface;

	int i3_fd;
} i3g;

void i3g_draw() {
	int width = i3g.screen->width_in_pixels;
	cairo_t *cr = cairo_create(i3g.surface);

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle(cr, 0, 0, width, I3G_WINDOWHEIGHT);
	cairo_set_source_rgba(cr, 1, 1, 1, .8);
	cairo_fill(cr);

	for (int i = I3G_WS_SHOW_OFFSET; i < 64; i++) {
		cairo_rectangle(cr, I3G_INDICATORSPACE + (I3G_INDICATORWIDTH + I3G_INDICATORSPACE) * (i - I3G_WS_SHOW_OFFSET), 0, I3G_INDICATORWIDTH, I3G_BARHEIGHT);

		if (!i3g.desktops[i].seen) {
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
		} else if (i3g.desktops[i].active) {
			cairo_set_source_rgb(cr, .965, .362, .162);
		} else if (i3g.desktops[i].urgent) {
			cairo_set_source_rgb(cr, .551, .751, .999);
		} else {
			cairo_set_source_rgb(cr, .5, .5, .5);
		}
		cairo_fill_preserve(cr);

		if (!i3g.desktops[i].seen) {
		} else if (i3g.desktops[i].active) {
			cairo_set_source_rgba(cr, .865, .262, .062, .5);
			c_offset_quads(cr, 4, 0);
		} else if (i3g.desktops[i].urgent) {
			cairo_set_source_rgba(cr, .501, .701, .991, .5);
			c_offset_quads(cr, 8, 0);
		} else {
		}

		cairo_new_path(cr);
	}

	cairo_destroy(cr);

	cairo_surface_flush(i3g.surface);
	xcb_flush(i3g.c);
}

void i3g_handle_event(xcb_generic_event_t *event) {
	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
		case XCB_EXPOSE:
			i3g_draw();
			break;
		case XCB_VISIBILITY_NOTIFY:
			x_raise_window(i3g.c, i3g.window);
			break;
		default:
			FG_DEBUG("unhandled event %s", xcb_event_get_label(event->response_type));
			break;
	}
}

void i3g_i3_send(uint32_t type, const char *payload) {
	struct i3_ipc_header header;
	memcpy(header.magic, I3_IPC_MAGIC, 6);
	header.size = strlen(payload);
	header.type = type;

	write(i3g.i3_fd, &header, sizeof(header));
	write(i3g.i3_fd, payload, header.size);
}

void i3g_i3_init_workspaces(json_object *payload) {
	json_object *workspace;

	for (int num = 0; num < 64; num++) i3g.desktops[num].seen = false;

	for (int i = 0; (workspace = json_object_array_get_idx(payload, i)); i++) {
		int num = json_object_get_int(json_object_object_get(workspace, "num"));

		i3g.desktops[num].seen = true;
		i3g.desktops[num].active = json_object_get_boolean(json_object_object_get(workspace, "focused"));
		i3g.desktops[num].urgent = json_object_get_boolean(json_object_object_get(workspace, "urgent"));
	}
}

void i3g_i3_recv() {
	struct i3_ipc_header header;
	ssize_t header_read = read(i3g.i3_fd, &header, sizeof(header));
	if (header_read < 0) {
		FG_FAIL_ERRNO("could not read from I3: %s");
	} else if (header_read == 0) {
		FG_FAIL("I3 closed socket");
	}

	if (strncmp(header.magic, I3_IPC_MAGIC, 6) != 0) FG_FAIL("invalid message from I3");

	size_t bytes_read = 0;
	char *payload = calloc(header.size + 1, 1);

	while (bytes_read < header.size) {
		ssize_t chunk_read = read(i3g.i3_fd, payload + bytes_read, header.size - bytes_read);

		if (chunk_read < 0) {
			FG_FAIL_ERRNO("could not read from I3: %s");
		} else if (chunk_read == 0) {
			FG_FAIL("I3 closed socket");
		}

		bytes_read += chunk_read;
	}

	json_object *payload_obj = json_tokener_parse(payload);

	if (header.type & I3_IPC_EVENT_MASK) {
		switch (header.type) {
			case I3_IPC_EVENT_WORKSPACE:
				i3g_i3_send(I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, "");
				i3g_i3_recv();
				break;
		}
	} else {
		switch (header.type) {
			case I3_IPC_REPLY_TYPE_WORKSPACES:
				i3g_i3_init_workspaces(payload_obj);
				break;
			case I3_IPC_REPLY_TYPE_SUBSCRIBE:
				if (!json_object_get_boolean(json_object_object_get(payload_obj, "success"))) FG_FAIL("subscribe failed");
				break;
		}
	}

	json_object_put(payload_obj);
}

void i3g_i3_connect() {
	char *sockname = x_get_string_property(i3g.c, i3g.screen->root, I3_SOCKET_PATH);

	i3g.i3_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (i3g.i3_fd < 0) FG_FAIL_ERRNO("i3 connect failed: %s");

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sockname);

	if (connect(i3g.i3_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) FG_FAIL_ERRNO("i3 connect failed: %s");

	i3g_i3_send(I3_IPC_MESSAGE_TYPE_SUBSCRIBE, "[\"workspace\"]");
	i3g_i3_recv();

	i3g_i3_send(I3_IPC_MESSAGE_TYPE_GET_WORKSPACES, "");
	i3g_i3_recv();
}

int main() {
	int screen_nbr;
	i3g.c = xcb_connect(NULL, &screen_nbr);
	i3g.screen = x_get_screen(i3g.c, screen_nbr);
	i3g.argb_visual = x_get_visual(i3g.screen, 32);
	xcb_colormap_t argb_colormap = x_get_colormap(i3g.c, i3g.screen, i3g.argb_visual->visual_id);

	x_init(i3g.c);

	i3g.window = xcb_generate_id(i3g.c);
	uint32_t set_attrs = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
	uint32_t attrs[] = { 0, 0, 0, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_VISIBILITY_CHANGE, argb_colormap };
	X_CHECKED(xcb_create_window_checked(i3g.c,
		32,
		i3g.window,
		i3g.screen->root,
		0, 0,
		i3g.screen->width_in_pixels, I3G_WINDOWHEIGHT,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		i3g.argb_visual->visual_id,
		set_attrs, attrs
	));
	x_set_net_wm_struts(i3g.c, i3g.screen, i3g.window, 0, 0, I3G_WINDOWHEIGHT, 0);
	x_set_net_wm_window_type(i3g.c, i3g.window, _NET_WM_WINDOW_TYPE_DOCK);
	X_CHECKED(xcb_map_window_checked(i3g.c, i3g.window));

	i3g.surface = cairo_xcb_surface_create(i3g.c, i3g.window, i3g.argb_visual, i3g.screen->width_in_pixels, I3G_WINDOWHEIGHT);

	int xcb_fd = xcb_get_file_descriptor(i3g.c);
	i3g_i3_connect();
	fd_set rfds;
	xcb_generic_event_t *event;

	while (1) {
		FD_ZERO(&rfds);
		FD_SET(i3g.i3_fd, &rfds);
		FD_SET(xcb_fd, &rfds);

		while ((event = xcb_poll_for_queued_event(i3g.c))) {
			i3g_handle_event(event);
		}

		if (select(MAX(xcb_fd, i3g.i3_fd) + 1, &rfds, NULL, NULL, NULL) == -1) FG_FAIL("select failed: %s", strerror(errno));

		if (FD_ISSET(i3g.i3_fd, &rfds)) {
			i3g_i3_recv();

			i3g_draw();
		} else {
			while ((event = xcb_poll_for_event(i3g.c))) {
				i3g_handle_event(event);
			}

			if (xcb_connection_has_error(i3g.c)) return EXIT_SUCCESS;
		}
	}

	return EXIT_SUCCESS;
}
