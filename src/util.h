#ifndef __UTIL_H__
#define __UTIL_H__

#define FG_DEBUG(format, ...) fprintf(stderr, "monsterbar(%s:%d): " format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define FG_FAIL(format, ...) { fprintf(stderr, "monsterbar: " format "\n", ##__VA_ARGS__); exit(EXIT_FAILURE); }
#define X_CHECKED(code) { xcb_generic_error_t *error; xcb_void_cookie_t cookie = code; if ((error = xcb_request_check(mb.c, cookie))) FG_FAIL("X11 request at %s:%d failed with %s", __FILE__, __LINE__, xcb_event_get_error_label(error->error_code)); }

typedef enum {
	_NET_WM_STATE,
	_NET_WM_STATE_ABOVE,
	_NET_WM_WINDOW_TYPE,
	_NET_WM_WINDOW_TYPE_DOCK,
	X_ATOM_COUNT
} XAtom;

void x_init(xcb_connection_t *c);
xcb_screen_t* x_get_screen(xcb_connection_t *c, int i);
xcb_visualtype_t* x_get_visual(xcb_screen_t *screen, int depth);
xcb_colormap_t x_get_colormap(xcb_connection_t *c, xcb_screen_t *screen, xcb_visualid_t visual);
void x_set_net_wm_window_type(xcb_connection_t *c, xcb_window_t win, XAtom state);
void x_raise_window(xcb_connection_t *c, xcb_window_t win);

#endif
