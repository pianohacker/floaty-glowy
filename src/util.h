#ifndef __UTIL_H__
#define __UTIL_H__

#include <errno.h>
#include <string.h>

#define FG_DEBUG(format, ...) fprintf(stderr, "monsterbar(%s:%d): " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define FG_FAIL(format, ...) { fprintf(stderr, "monsterbar: " format "\n", ##__VA_ARGS__); abort(); }
#define FG_FAIL_ERRNO(format, ...) FG_FAIL(format, strerror(errno), ##__VA_ARGS__)
#define MAX(x, y) ((x) > (y) ? (x) : (y))

typedef enum {
	_NET_WM_STATE,
	_NET_WM_STATE_ABOVE,
	_NET_WM_STRUT,
	_NET_WM_STRUT_PARTIAL,
	_NET_WM_WINDOW_TYPE,
	_NET_WM_WINDOW_TYPE_DOCK,
	I3_SOCKET_PATH,
	UTF8_STRING,
	X_ATOM_COUNT
} XAtom;

void c_offset_quads(cairo_t *cr, double offset, double end_alpha);
void x_init(xcb_connection_t *c);
xcb_screen_t* x_get_screen(xcb_connection_t *c, int i);
xcb_visualtype_t* x_get_visual(xcb_screen_t *screen, int depth);
xcb_colormap_t x_get_colormap(xcb_connection_t *c, xcb_screen_t *screen, xcb_visualid_t visual);
void x_set_net_wm_window_type(xcb_connection_t *c, xcb_window_t win, XAtom state);
void x_set_net_wm_struts(xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t win, int left, int right, int top, int bottom);
void x_raise_window(xcb_connection_t *c, xcb_window_t win);
char* x_get_string_property(xcb_connection_t *c, xcb_window_t win, XAtom property);

#endif
