#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

#define FG_DEBUG(format, ...) _fg_debug(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define FG_FAIL(format, ...) { fprintf(stderr, "monsterbar: " format "\n", ##__VA_ARGS__); exit(EXIT_FAILURE); }
#define X_CHECKED(code) { xcb_generic_error_t *error; xcb_void_cookie_t cookie = code; if ((error = xcb_request_check(c, cookie))) FG_FAIL("X11 request at %s:%d failed with %s", __FILE__, __LINE__, xcb_event_get_error_label(error->error_code)); }

void _fg_debug(char *filename, int line, char *format, ...) {
	va_list args;
	va_start(args, format);
	va_end(args);
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

xcb_visualid_t x_get_visual(xcb_screen_t *screen, int depth) {
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

	return xcb_depth_visuals(depth_info)[0].visual_id;
}

xcb_colormap_t x_get_colormap(xcb_connection_t *c, xcb_screen_t *screen, xcb_visualid_t visual) {
	xcb_colormap_t colormap = xcb_generate_id(c);
	X_CHECKED(xcb_create_colormap_checked(c, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visual));

	return colormap;
}

int main() {
	int screen_nbr;
	xcb_connection_t *c = xcb_connect(NULL, &screen_nbr);
	xcb_screen_t *screen = x_get_screen(c, screen_nbr);
	xcb_visualid_t argb_visual = x_get_visual(screen, 32);
	xcb_colormap_t argb_colormap = x_get_colormap(c, screen, argb_visual);

	xcb_window_t window = xcb_generate_id(c);
	uint32_t set_attrs = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_COLORMAP;
	uint32_t attrs[4] = { 0, 0, 1, argb_colormap };
	X_CHECKED(xcb_create_window_checked(c,
		32,
		window,
		screen->root,
		0, 0,
		screen->width_in_pixels, 6,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		argb_visual,
		set_attrs, attrs
	));
	X_CHECKED(xcb_map_window_checked(c, window));
	xcb_flush(c);

	getc(stdin);

	return EXIT_SUCCESS;
}
