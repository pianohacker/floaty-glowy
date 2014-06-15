#include <assert.h>
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
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
	"_NET_WM_STRUT",
	"_NET_WM_STRUT_PARTIAL",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DOCK",
	"I3_SOCKET_PATH",
	"UTF8_STRING",
};

xcb_atom_t X_ATOMS[X_ATOM_COUNT];

typedef struct {
	double x;
	double y;
} DPoint;

static DPoint _line_intersect(
		double x1, double y1,
		double x2, double y2,
		double x3, double y3,
		double x4, double y4
	) {
	return (DPoint) {
		((x1*y2-y1*x2)*(x3-x4)-(x1-x2)*(x3*y4-y3*x4)) / ((x1-x2)*(y3-y4)-(y1-y2)*(x3-x4)),
		((x1*y2-y1*x2)*(y3-y4)-(y1-y2)*(x3*y4-y3*x4)) / ((x1-x2)*(y3-y4)-(y1-y2)*(x3-x4))
	};
}

// Takes an existing path, and generates glow quads around that path, using its orientation.
// Each segment of the path is offset `glow_size` pixels along its normal vector, then to create the
// polygons, the intersection of each pair of offset lines is found.
void c_offset_quads(cairo_t *cr, double offset, double end_alpha) {
	cairo_path_t *path = cairo_copy_path(cr);
	cairo_pattern_t *result = cairo_pattern_create_mesh();
	cairo_path_data_t *cur_data = path->data;
	cairo_path_data_t *end = path->data + path->num_data;

	double red, green, blue, alpha;
	assert(cairo_pattern_get_rgba(cairo_get_source(cr), &red, &green, &blue, &alpha) == CAIRO_STATUS_SUCCESS);

	int path_len = path->num_data / 2;
	double norm_angles[path_len];
	//double *angle_spans = malloc(sizeof(double) * path_len);
	DPoint points[path_len];
	DPoint line_starts[path_len];
	DPoint line_ends[path_len];
	DPoint intersect_points[path_len];

	for (; cur_data < end; cur_data++) {
		if (cur_data->header.type != CAIRO_PATH_MOVE_TO) continue;

		int num_points = 1;
		points[0] = (DPoint) {cur_data[1].point.x, cur_data[1].point.y};
		
		cairo_path_data_t *path_data = cur_data + 2;
		int i = 1;
		bool in_path = true;
		for (; path_data < end && in_path; path_data++) {
			double x, y;
			if (path_data->header.type == CAIRO_PATH_LINE_TO) {
				points[i].x = x = path_data[1].point.x;
				points[i].y = y = path_data[1].point.y;
				num_points++;
				path_data++;
			} else if (path_data->header.type == CAIRO_PATH_CLOSE_PATH) {
				x = cur_data[1].point.x;
				y = cur_data[1].point.y;
				path_data++;
				in_path = false;
			} else {
				path_data += path_data->header.length;
				continue;
			}

			norm_angles[i - 1] = atan2(
				y - points[i - 1].y,
				x - points[i - 1].x
			) - M_PI / 2;
			for (int i = 0; i < path_len; i++) intersect_points[i].x = NAN;

			i++;
		}
		cur_data = path_data;

		for (i = 0; i < num_points; i++) {
			int next_i = (i + 1) % num_points;

			line_starts[i] = (DPoint) {
				points[i].x + cos(norm_angles[i]) * offset,
				points[i].y + sin(norm_angles[i]) * offset
			};

			line_ends[i] = (DPoint) {
				points[next_i].x + cos(norm_angles[i]) * offset,
				points[next_i].y + sin(norm_angles[i]) * offset
			};
		}

		for (i = 0; i < num_points; i++) {
			int last_i = (i - 1 + num_points) % num_points;
			int next_i = (i + 1) % num_points;

			if (isnan(intersect_points[i].x)) {
				intersect_points[i] = _line_intersect(
					line_starts[last_i].x,
					line_starts[last_i].y,
					line_ends[last_i].x,
					line_ends[last_i].y,
					line_starts[i].x,
					line_starts[i].y,
					line_ends[i].x,
					line_ends[i].y
				);
			}

			if (isnan(intersect_points[next_i].x)) {
				intersect_points[next_i] = _line_intersect(
					line_starts[i].x,
					line_starts[i].y,
					line_ends[i].x,
					line_ends[i].y,
					line_starts[next_i].x,
					line_starts[next_i].y,
					line_ends[next_i].x,
					line_ends[next_i].y
				);
			}

			cairo_mesh_pattern_begin_patch(result);
			cairo_mesh_pattern_move_to(result, points[next_i].x, points[next_i].y);
			cairo_mesh_pattern_line_to(result, points[i].x, points[i].y);
			cairo_mesh_pattern_line_to(result, intersect_points[i].x, intersect_points[i].y);
			cairo_mesh_pattern_line_to(result, intersect_points[next_i].x, intersect_points[next_i].y);

			cairo_mesh_pattern_set_corner_color_rgba(result, 0, red, green, blue, alpha);
			cairo_mesh_pattern_set_corner_color_rgba(result, 1, red, green, blue, alpha);
			cairo_mesh_pattern_set_corner_color_rgba(result, 2, red, green, blue, end_alpha);
			cairo_mesh_pattern_set_corner_color_rgba(result, 3, red, green, blue, end_alpha);
			cairo_mesh_pattern_end_patch(result);
		}
	}

	cairo_set_source(cr, result);
	cairo_paint(cr);
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

void x_set_net_wm_struts(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t win, int left, int right, int top, int bottom) {
	uint32_t values[] = {
		left,
		right,
		top,
		bottom,
		0,
		left ? screen->height_in_pixels : 0,
		0,
		right ? screen->height_in_pixels : 0,
		0,
		top ? screen->width_in_pixels : 0,
		0,
		bottom ? screen->width_in_pixels : 0,
	};

	X_CHECKED_API(xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, win, X_ATOMS[_NET_WM_STRUT_PARTIAL], XCB_ATOM_CARDINAL, 32, 12, values));
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
