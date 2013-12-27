#include <cairo.h>
#include <cairo-xcb.h>
#include <errno.h>
#include <i3/ipc.h>
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

#define I3G_BARHEIGHT 3
#define I3G_WINDOWHEIGHT 6
#define I3G_INDICATORWIDTH 20
#define I3G_INDICATORSPACE 12

struct {
	struct {
		bool seen;
		int n_windows;
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

	cairo_rectangle(cr, 0, 0, width, I3G_BARHEIGHT);
	cairo_set_source_rgba(cr, 1, 1, 1, .8);
	cairo_fill(cr);

	for (int i = 0; i < 64 && i3g.desktops[i].seen; i++) {
		cairo_rectangle(cr, I3G_INDICATORSPACE + (I3G_INDICATORWIDTH + I3G_INDICATORSPACE) * i, 0, I3G_INDICATORWIDTH, I3G_WINDOWHEIGHT);
		if (i3g.desktops[i].active) {
			cairo_set_source_rgba(cr, .815, .212, .012, .9);
		} else if (i3g.desktops[i].urgent) {
			cairo_set_source_rgba(cr, .451, .651, .941, .9);
		} else if (i3g.desktops[i].n_windows) {
			cairo_set_source_rgba(cr, .3, .3, .3, .8);
		} else {
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
		}
		cairo_fill(cr);
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
}

void i3g_i3_recv() {
}

int i3g_i3_connect() {
	char *sockname = x_get_string_property(i3g.c, i3g.screen->root, I3_SOCKET_PATH);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) FG_FAIL_ERRNO("i3 connect failed: %s");

	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sockname);

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) FG_FAIL_ERRNO("i3 connect failed: %s");

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
		i3g.screen->width_in_pixels, 6,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		i3g.argb_visual->visual_id,
		set_attrs, attrs
	));
	x_set_net_wm_window_type(i3g.c, i3g.window, _NET_WM_WINDOW_TYPE_DOCK);
	X_CHECKED(xcb_map_window_checked(i3g.c, i3g.window));

	i3g.surface = cairo_xcb_surface_create(i3g.c, i3g.window, i3g.argb_visual, i3g.screen->width_in_pixels, 6);

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
