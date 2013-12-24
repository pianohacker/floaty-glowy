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

#define MB_BARHEIGHT 3
#define MB_WINDOWHEIGHT 6
#define MB_INDICATORWIDTH 20
#define MB_INDICATORSPACE 12

#define FG_DEBUG(format, ...) fprintf(stderr, "monsterbar(%s:%d): " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define FG_FAIL(format, ...) { fprintf(stderr, "monsterbar: " format "\n", ##__VA_ARGS__); exit(EXIT_FAILURE); }
#define X_CHECKED(code) { xcb_generic_error_t *error; xcb_void_cookie_t cookie = code; if ((error = xcb_request_check(mb.c, cookie))) FG_FAIL("X11 request at %s:%d failed with %s", __FILE__, __LINE__, xcb_event_get_error_label(error->error_code)); }

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
		default:
			FG_DEBUG("unhandled event %s", xcb_event_get_label(event->response_type));
			break;
	}
}

xcb_screen_t* x_get_screen(xcb_connection_t *c, int i) {
	xcb_screen_iterator_t iter;

	/* Get the screen #screen_nbr */
	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; --i, xcb_screen_next(&iter)) {
		if (i == 0) {
			return iter.data;
			break;
		}
	}

	FG_FAIL("invalid screen %d", i);
}

xcb_visualtype_t* x_get_visual(xcb_screen_t *screen, int depth) {
	xcb_depth_iterator_t depth_iter;
	xcb_depth_t *depth_info = NULL;

	depth_iter = xcb_screen_allowed_depths_iterator(screen);
	for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
		if (depth_iter.data->depth == depth) {
			depth_info = depth_iter.data;
			break;
		}
	}

	if (!depth_info) FG_FAIL("could not find visual of depth %d", depth);

	return xcb_depth_visuals(depth_info);
}

xcb_colormap_t x_get_colormap(xcb_connection_t *c, xcb_screen_t *screen, xcb_visualid_t visual) {
	xcb_colormap_t colormap = xcb_generate_id(c);
	X_CHECKED(xcb_create_colormap_checked(mb.c, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visual));

	return colormap;
}

int main() {
	int screen_nbr;
	mb.c = xcb_connect(NULL, &screen_nbr);
	mb.screen = x_get_screen(mb.c, screen_nbr);
	mb.argb_visual = x_get_visual(mb.screen, 32);
	xcb_colormap_t argb_colormap = x_get_colormap(mb.c, mb.screen, mb.argb_visual->visual_id);

	mb.window = xcb_generate_id(mb.c);
	uint32_t set_attrs = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
	uint32_t attrs[] = { 0, 0, 1, XCB_EVENT_MASK_EXPOSURE, argb_colormap };
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
	X_CHECKED(xcb_configure_window_checked(mb.c, mb.window, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]) {XCB_STACK_MODE_ABOVE}));
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
