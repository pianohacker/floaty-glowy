#include <cairo.h>
#include <cairo-xcb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

#include "util.h"

#define MB_BARHEIGHT 3
#define MB_WINDOWHEIGHT 6
#define MB_INDICATORWIDTH 20
#define MB_INDICATORSPACE 12

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
} mb;

void mb_draw() {
	int width = mb.screen->width_in_pixels;
	cairo_t *cr = cairo_create(mb.surface);

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle(cr, 0, 0, width, MB_BARHEIGHT);
	cairo_set_source_rgba(cr, 1, 1, 1, .8);
	cairo_fill(cr);

	for (int i = 0; i < 64 && mb.desktops[i].seen; i++) {
		cairo_rectangle(cr, MB_INDICATORSPACE + (MB_INDICATORWIDTH + MB_INDICATORSPACE) * i, 0, MB_INDICATORWIDTH, MB_WINDOWHEIGHT);
		if (mb.desktops[i].active) {
			cairo_set_source_rgba(cr, .815, .212, .012, .9);
		} else if (mb.desktops[i].urgent) {
			cairo_set_source_rgba(cr, .451, .651, .941, .9);
		} else if (mb.desktops[i].n_windows) {
			cairo_set_source_rgba(cr, .3, .3, .3, .8);
		} else {
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
		}
		cairo_fill(cr);
	}

	cairo_destroy(cr);

	cairo_surface_flush(mb.surface);
	xcb_flush(mb.c);
}

void mb_handle_event(xcb_generic_event_t *event) {
	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
		case XCB_EXPOSE:
			mb_draw();
			break;
		case XCB_VISIBILITY_NOTIFY:
			x_raise_window(mb.c, mb.window);
			break;
		default:
			FG_DEBUG("unhandled event %s", xcb_event_get_label(event->response_type));
			break;
	}
}

int main() {
	int screen_nbr;
	mb.c = xcb_connect(NULL, &screen_nbr);
	mb.screen = x_get_screen(mb.c, screen_nbr);
	mb.argb_visual = x_get_visual(mb.screen, 32);
	xcb_colormap_t argb_colormap = x_get_colormap(mb.c, mb.screen, mb.argb_visual->visual_id);

	x_init(mb.c);

	mb.window = xcb_generate_id(mb.c);
	uint32_t set_attrs = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
	uint32_t attrs[] = { 0, 0, 0, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_VISIBILITY_CHANGE, argb_colormap };
	X_CHECKED(xcb_create_window_checked(mb.c,
		32,
		mb.window,
		mb.screen->root,
		0, 0,
		mb.screen->width_in_pixels, 6,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		mb.argb_visual->visual_id,
		set_attrs, attrs
	));
	x_set_net_wm_window_type(mb.c, mb.window, _NET_WM_WINDOW_TYPE_DOCK);
	X_CHECKED(xcb_map_window_checked(mb.c, mb.window));

	mb.surface = cairo_xcb_surface_create(mb.c, mb.window, mb.argb_visual, mb.screen->width_in_pixels, 6);

	int xcb_fd = xcb_get_file_descriptor(mb.c);
	fd_set rfds;
	xcb_generic_event_t *event;

	while (1) {
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		FD_SET(xcb_fd, &rfds);

		while ((event = xcb_poll_for_queued_event(mb.c))) {
			mb_handle_event(event);
		}

		if (select(xcb_fd + 1, &rfds, NULL, NULL, NULL) == -1) FG_FAIL("select failed: %s", strerror(errno));

		if (FD_ISSET(0, &rfds)) {
			int i, n_windows, mode, urgent, active;

			char *line = NULL;
			size_t len;
			if (getline(&line, &len, stdin) == -1) return EXIT_SUCCESS;

			int num_read = 0;
			while (sscanf(line, "%d:%d:%d:%d:%d%n", &i, &n_windows, &mode, &active, &urgent, &num_read) == 5) {
				if (i < 0 || i >= 64) continue;

				mb.desktops[i].seen = true;
				mb.desktops[i].n_windows = n_windows;
				mb.desktops[i].mode = mode;
				mb.desktops[i].urgent = urgent;
				mb.desktops[i].active = active;

				line += num_read;
			}

			mb_draw();
		} else {
			while ((event = xcb_poll_for_event(mb.c))) {
				mb_handle_event(event);
			}

			if (xcb_connection_has_error(mb.c)) return EXIT_SUCCESS;
		}
	}

	return EXIT_SUCCESS;
}
