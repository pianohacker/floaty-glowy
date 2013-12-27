#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

#include "util.h"

#define X_CHECKED_API(code) { xcb_generic_error_t *error; xcb_void_cookie_t cookie = code; if ((error = xcb_request_check(c, cookie))) FG_FAIL("X11 request at %s:%d failed with %s", __FILE__, __LINE__, xcb_event_get_error_label(error->error_code)); }

char *X_ATOM_NAMES[] = {
	"_NET_WM_STATE",
	"_NET_WM_STATE_ABOVE",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DOCK",
	"I3_SOCKET_PATH",
	"UTF8_STRING",
};

xcb_atom_t X_ATOMS[X_ATOM_COUNT];

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
	X_CHECKED_API(xcb_create_colormap_checked(c, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visual));

	return colormap;
}

static void x_get_atoms(xcb_connection_t *c, char **names, xcb_atom_t *atoms, unsigned int count) {
    xcb_intern_atom_cookie_t cookies[count];
    xcb_intern_atom_reply_t  *reply;

    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_intern_atom(c, 0, strlen(names[i]), names[i]);
    for (unsigned int i = 0; i < count; i++) {
        reply = xcb_intern_atom_reply(c, cookies[i], NULL); /* TODO: Handle error */
        if (reply) {
            atoms[i] = reply->atom; free(reply);
        } else {
			FG_FAIL("Failed to register atom %s", names[i]);
		}
    }
}

void x_set_net_wm_window_type(xcb_connection_t *c, xcb_window_t win, xcb_atom_t state) {
	X_CHECKED_API(xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, win, X_ATOMS[_NET_WM_WINDOW_TYPE], XCB_ATOM_ATOM, 32, 1, &X_ATOMS[state]));
}

void x_raise_window(xcb_connection_t *c, xcb_window_t win) {
	X_CHECKED_API(xcb_configure_window_checked(c, win, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]) {XCB_STACK_MODE_ABOVE}));
	xcb_flush(c);
}

void x_init(xcb_connection_t *c) {
	x_get_atoms(c, X_ATOM_NAMES, X_ATOMS, X_ATOM_COUNT);
}

char* x_get_string_property(xcb_connection_t *c, xcb_window_t win, XAtom property) {
	xcb_get_property_reply_t *reply = xcb_get_property_reply(c, xcb_get_property_unchecked(c, 0, win, X_ATOMS[property], X_ATOMS[UTF8_STRING], 0, PATH_MAX), NULL);

	if (!reply) FG_FAIL("could not fetch property %d of window 0x%x", property, win);

	char *result = strndup(xcb_get_property_value(reply), xcb_get_property_value_length(reply));

	free(reply);
	return result;
}
