/*
 * See LICENSE file for copyright and license details.
 */
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <libinput.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/backend/libinput.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <X11/Xlib.h>
#include <wlr/xwayland.h>
#endif

/* macros */
#define BARF(fmt, ...)		do { fprintf(stderr, fmt "\n", ##__VA_ARGS__); exit(EXIT_FAILURE); } while (0)
#define EBARF(fmt, ...)		BARF(fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#ifdef XWAYLAND
#define WLR_SURFACE(C)          ((C)->type != XDGShell ? (C)->surface.xwayland->surface : (C)->surface.xdg->surface)
#else
#define WLR_SURFACE(C)          ((C)->surface.xdg->surface)
#endif

/* enums */
enum { CurNormal, CurMove, CurResize }; /* cursor */
#ifdef XWAYLAND
enum { NetWMWindowTypeDialog, NetWMWindowTypeSplash, NetWMWindowTypeToolbar,
	NetWMWindowTypeUtility, NetLast }; /* EWMH atoms */
enum { XDGShell, X11Managed, X11Unmanaged }; /* client types */
#endif

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	struct wl_list link;
	struct wl_list flink;
	struct wl_list slink;
	union {
		struct wlr_xdg_surface *xdg;
#ifdef XWAYLAND
		struct wlr_xwayland_surface *xwayland;
#endif
	} surface;
#ifdef XWAYLAND
	struct wl_listener activate;
#endif
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener fullscreen;
	struct wlr_box geom;  /* layout-relative, includes border */
	Monitor *mon;
#ifdef XWAYLAND
	unsigned int type;
#endif
	int bw;
	unsigned int tags;
	int isfloating;
	uint32_t resize; /* configure serial of a pending resize */
	int prevx;
	int prevy;
	int prevwidth;
	int prevheight;
	int isfullscreen;
} Client;

typedef struct {
	struct wl_listener request_mode;
	struct wl_listener destroy;
} Decoration;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wl_list link;
	struct wlr_input_device *device;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} Keyboard;

typedef struct {
	struct wlr_layer_surface_v1 *layer_surface;
	struct wl_list link;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_commit;

	struct wlr_box geo;
	enum zwlr_layer_shell_v1_layer layer;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wlr_box m;      /* monitor area, layout-relative */
	struct wlr_box w;      /* window area, layout-relative */
	struct wl_list layers[4]; // LayerSurface::link
	const Layout *lt[2];
	int gappih;           /* horizontal gap between windows */
	int gappiv;           /* vertical gap between windows */
	int gappoh;           /* horizontal outer gaps */
	int gappov;           /* vertical outer gaps */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	double mfact;
	int nmaster;
	int position;
	Client *fullscreen;
	Client *fullscreenclient;
	int ybw; // 1: yes borders
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
} MonitorRule;

typedef struct {
	const char *id;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
	int x;
	int y;
	int w;
	int h;
} Rule;

/* Used to move all of the data necessary to render a surface from the top-level
 * frame handler to the per-surface render function. */
struct render_data {
	struct wlr_output *output;
	struct timespec *when;
	int x, y; /* layout-relative */
};

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyexclusive(struct wlr_box *usable_area, uint32_t anchor,
		int32_t exclusive, int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, bool exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void cleanup(void);
static void cleanupkeyboard(struct wl_listener *listener, void *data);
static void cleanupmon(struct wl_listener *listener, void *data);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_input_device *device);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_input_device *device);
static void createxdeco(struct wl_listener *listener, void *data);
static void cursorframe(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroyxdeco(struct wl_listener *listener, void *data);
static Monitor *dirtomon(int dir);
static void focusclient(Client *old, Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void fullscreennotify(struct wl_listener *listener, void *data);
static Client *focustop(Monitor *m);
static void getxdecomode(struct wl_listener *listener, void *data);
static void incnmaster(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static void killclient(const Arg *arg);
static void maplayersurfacenotify(struct wl_listener *listener, void *data);
static void maprequest(struct wl_listener *listener, void *data);
static void monocle(Monitor *m);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, bool test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void quit(const Arg *arg);
static void quitallfullscreen(Monitor *m);
static void render(struct wlr_surface *surface, int sx, int sy, void *data);
static void renderclients(Monitor *m, struct timespec *now);
static void renderlayer(struct wl_list *layer_surfaces, struct timespec *now);
static void rendermon(struct wl_listener *listener, void *data);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void run(char *startup_cmd);
static void scalebox(struct wlr_box *box, float scale);
static Client *selclient(void);
static void setcursor(struct wl_listener *listener, void *data);
static void setkblayout(Keyboard *kb, const struct xkb_rule_names *newrule);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setgaps(int oh, int ov, int ih, int iv);
static void incrgaps(const Arg *arg);
static void incrigaps(const Arg *arg);
static void incrogaps(const Arg *arg);
static void incrohgaps(const Arg *arg);
static void incrovgaps(const Arg *arg);
static void incrihgaps(const Arg *arg);
static void incrivgaps(const Arg *arg);
static void togglegaps(const Arg *arg);
static void defaultgaps(const Arg *arg);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, unsigned int newtags);
static void setup(void);
static void sigchld(int unused);
static void shotFocusMon();
static bool shouldfocusclients();
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void togglekblayout(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unmaplayersurface(LayerSurface *layersurface);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons();
static void view(const Arg *arg);
static Client *xytoclient(double x, double y);
static struct wlr_surface *xytolayersurface(struct wl_list *layer_surfaces,
		double x, double y, double *sx, double *sy);
static Monitor *xytomon(double x, double y);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static struct wl_display *dpy;
static struct wlr_backend *backend;
static struct wlr_renderer *drw;
static struct wlr_compositor *compositor;

static struct wlr_xdg_shell *xdg_shell;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wl_list stack;   /* stacking z-order */
static struct wl_list independents;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_xdg_decoration_manager_v1 *xdeco_mgr;
static struct wlr_output_manager_v1 *output_mgr;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_seat *seat;
static struct wl_list keyboards;
static unsigned int kblayout = 0; /* index of kblayouts */
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy; /* client-relative */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;

static int enablegaps = 1;   /* enables gaps, used by togglegaps */
static int nclients;

/* global event handlers */
static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener new_input = {.notify = inputdevice};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdeco = {.notify = createxdeco};
static struct wl_listener new_xdg_surface = {.notify = createnotify};
static struct wl_listener new_layer_shell_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static Atom getatom(xcb_connection_t *xc, const char *name);
static void renderindependents(struct wlr_output *output, struct timespec *now);
static void updatewindowtype(Client *c);
static void xwaylandready(struct wl_listener *listener, void *data);
static Client *xytoindependent(double x, double y);
static struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
static struct wlr_xwayland *xwayland;
static Atom netatom[NetLast];
#endif

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1, c->geom.width);
	c->geom.height = MAX(1, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width + 2 * c->bw <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height + 2 * c->bw <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyexclusive(struct wlr_box *usable_area,
		uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left) {
	struct {
		uint32_t singular_anchor;
		uint32_t anchor_triplet;
		int *positive_axis;
		int *negative_axis;
		int margin;
	} edges[4];

	if (exclusive <= 0)
		return;

	// Top
	edges[0].singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	edges[0].anchor_triplet =
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	edges[0].positive_axis = &usable_area->y;
	edges[0].negative_axis = &usable_area->height;
	edges[0].margin = margin_top;
	// Bottom
	edges[1].singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	edges[1].anchor_triplet =
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	edges[1].positive_axis = NULL;
	edges[1].negative_axis = &usable_area->height;
	edges[1].margin = margin_bottom;
	// Left
	edges[2].singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	edges[2].anchor_triplet =
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	edges[2].positive_axis = &usable_area->x;
	edges[2].negative_axis = &usable_area->width;
	edges[2].margin = margin_left;
	// Right
	edges[3].singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	edges[3].anchor_triplet =
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	edges[3].positive_axis = NULL;
	edges[3].negative_axis = &usable_area->width;
	edges[3].margin = margin_right;

	for (size_t i = 0; i < LENGTH(edges); ++i) {
		if ((anchor == edges[i].singular_anchor || anchor == edges[i].anchor_triplet)
				&& exclusive + edges[i].margin > 0) {
			if (edges[i].positive_axis)
				*edges[i].positive_axis += exclusive + edges[i].margin;
			if (edges[i].negative_axis)
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			break;
		}
	}
}

void
applyrules(Client *c)
{
	const char *appid, *title;
	unsigned int i, newtags = 0;
	const Rule *r;
	Monitor *mon = selmon, *m;

	/* rule matching */
	c->isfloating = 0;
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		updatewindowtype(c);
		appid = c->surface.xwayland->class;
		title = c->surface.xwayland->title;
	} else
#endif
	{
		appid = c->surface.xdg->toplevel->app_id;
		title = c->surface.xdg->toplevel->title;
	}
	if (!appid)
		appid = broken;
	if (!title)
		title = broken;

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link)
				if (r->monitor == i++)
					mon = m;
			if (c->isfloating)
				resize(c, r->x ? r->x + mon->w.x : mon->w.width / 2 - c->geom.width / 2 + mon->w.x,
						r->y ? r->y + mon->w.y : mon->w.height / 2 - c->geom.height / 2 + mon->w.y,
						r->w ? r->w : c->geom.width,
						r->h ? r->h : c->geom.height, 1);
		}
	}
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	m->fullscreenclient = NULL;

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	else {
		Client *c;
		wl_list_for_each(c, &clients, link) {
			if (c->isfullscreen && VISIBLEON(c, m)) {
				m->fullscreenclient = c;
				resize(c, c->mon->m.x, c->mon->m.y, c->mon->m.width, c->mon->m.height, 0);
				return;
			}
		}
	}
	/* XXX recheck pointer focus here... or in resize()? */

	// nclients has just been updated
	m->ybw = !((m->lt[m->sellt]->arrange && nclients <= 1) ||
			(m->lt[m->sellt]->arrange == layouts[2].arrange)); // monocle
	// not directly checking if tiling to allow compatibility with more layouts
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, bool exclusive)
{
	LayerSurface *layersurface;
	struct wlr_box full_area = m->m;

	wl_list_for_each(layersurface, list, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
		struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;
		struct wlr_box bounds;
		struct wlr_box box = {
			.width = state->desired_width,
			.height = state->desired_height
		};
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

		if (exclusive != (state->exclusive_zone > 0))
			continue;

		bounds = state->exclusive_zone == -1 ? full_area : *usable_area;

		// Horizontal axis
		if ((state->anchor & both_horiz) && box.width == 0) {
			box.x = bounds.x;
			box.width = bounds.width;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = bounds.x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = bounds.x + (bounds.width - box.width);
		} else {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}
		// Vertical axis
		if ((state->anchor & both_vert) && box.height == 0) {
			box.y = bounds.y;
			box.height = bounds.height;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = bounds.y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = bounds.y + (bounds.height - box.height);
		} else {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}
		// Margin
		if ((state->anchor & both_horiz) == both_horiz) {
			box.x += state->margin.left;
			box.width -= state->margin.left + state->margin.right;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x += state->margin.left;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x -= state->margin.right;
		}
		if ((state->anchor & both_vert) == both_vert) {
			box.y += state->margin.top;
			box.height -= state->margin.top + state->margin.bottom;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y += state->margin.top;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y -= state->margin.bottom;
		}
		if (box.width < 0 || box.height < 0) {
			wlr_layer_surface_v1_close(wlr_layer_surface);
			continue;
		}
		layersurface->geo = box;

		applyexclusive(usable_area, state->anchor, state->exclusive_zone,
				state->margin.top, state->margin.right,
				state->margin.bottom, state->margin.left);
		wlr_layer_surface_v1_configure(wlr_layer_surface, box.width, box.height);
	}
}

void
arrangelayers(Monitor *m)
{
	struct wlr_box usable_area = m->m;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	size_t nlayers = LENGTH(layers_above_shell);
	LayerSurface *layersurface;
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);

	// Arrange exclusive surfaces from top->bottom
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, true);
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, true);
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, true);
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, true);

	if (memcmp(&usable_area, &m->w, sizeof(struct wlr_box))) {
		m->w = usable_area;
		arrange(m);
	}

	// Arrange non-exlusive surfaces from top->bottom
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, false);
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, false);
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, false);
	arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, false);

	// Find topmost keyboard interactive layer, if such a layer exists
	for (size_t i = 0; i < nlayers; ++i) {
		wl_list_for_each_reverse(layersurface,
				&m->layers[layers_above_shell[i]], link) {
			if (layersurface->layer_surface->current.keyboard_interactive &&
					layersurface->layer_surface->mapped) {
				wlr_seat_keyboard_notify_enter(seat, layersurface->layer_surface->surface,
						kb->keycodes, kb->num_keycodes, &kb->modifiers);
				return;
			}
		}
	}
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_event_pointer_axis *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_event_pointer_button *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	switch (event->state) {
	case WLR_BUTTON_PRESSED:;
		/* Change focus if the button was _pressed_ over a client */
		if ((c = xytoclient(cursor->x, cursor->y)))
			focusclient(selclient(), c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = wlr_keyboard_get_modifiers(keyboard);
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WLR_BUTTON_RELEASED:
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* XXX should reset to the pointer focus's current setcursor */
		if (cursor_mode != CurNormal) {
			wlr_xcursor_manager_set_cursor_image(cursor_mgr,
					"left_ptr", cursor);
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon, 0);
			return;
		}
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
chvt(const Arg *arg)
{
	wlr_session_change_vt(wlr_backend_get_session(backend), arg->ui);
}

void
cleanup(void)
{
#ifdef XWAYLAND
	wlr_xwayland_destroy(xwayland);
#endif
	wl_display_destroy_clients(dpy);
	wl_display_destroy(dpy);

	wlr_xcursor_manager_destroy(cursor_mgr);
	wlr_cursor_destroy(cursor);
	wlr_output_layout_destroy(output_layout);
}

void
cleanupkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_input_device *device = data;
	Keyboard *kb = device->data;

	wl_list_remove(&kb->destroy.link);
	free(kb);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	struct wlr_output *wlr_output = data;
	Monitor *m = wlr_output->data, *newmon;
	Client *c;

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wlr_output_layout_remove(output_layout, m->wlr_output);

	updatemons();

	wl_list_for_each(newmon, &mons, link) {
		wl_list_for_each(c, &clients, link) {
			if (c->isfloating && c->geom.x > m->m.width) {
				resize(c, c->geom.x - m->w.width, c->geom.y,
						c->geom.width, c->geom.height, 0);
			}
			if (c->mon == m)
				setmon(c, newmon, c->tags);
		}
		break;
	}
	free(m);
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, surface_commit);
	struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
	struct wlr_output *wlr_output = wlr_layer_surface->output;
	Monitor *m;

	if (!wlr_output)
		return;

	m = wlr_output->data;
	arrangelayers(m);

	if (layersurface->layer != wlr_layer_surface->current.layer) {
		wl_list_remove(&layersurface->link);
		wl_list_insert(&m->layers[wlr_layer_surface->current.layer],
			&layersurface->link);
		layersurface->layer = wlr_layer_surface->current.layer;
	}
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->configure_serial)
		c->resize = 0;
}

void
setkblayout(Keyboard *kb, const struct xkb_rule_names *newrule)
{
	/* Prepare an XKB keymap and assign it to the keyboard. */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_map_new_from_names(context, newrule,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(kb->device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
}

void
createkeyboard(struct wlr_input_device *device)
{
	Keyboard *kb;

	kb = device->data = calloc(1, sizeof(*kb));
	kb->device = device;

	setkblayout(kb, &xkb_rules);

	wlr_keyboard_set_repeat_info(device->keyboard, repeat_rate, repeat_delay);

	/* Here we set up listeners for keyboard events. */
	kb->modifiers.notify = keypressmod;
	wl_signal_add(&device->keyboard->events.modifiers, &kb->modifiers);
	kb->key.notify = keypress;
	wl_signal_add(&device->keyboard->events.key, &kb->key);
	kb->destroy.notify = cleanupkeyboard;
	wl_signal_add(&device->events.destroy, &kb->destroy);

	wlr_seat_set_keyboard(seat, device);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&keyboards, &kb->link);
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	Monitor *m, *moni, *insertmon = NULL;
	const MonitorRule *r;
	size_t nlayers = LENGTH(m->layers);
	int x = 0;

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_set_mode(wlr_output, wlr_output_preferred_mode(wlr_output));

	/* Allocates and configures monitor state using configured rules */
	m = wlr_output->data = calloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	m->gappih = gappih;
	m->gappiv = gappiv;
	m->gappoh = gappoh;
	m->gappov = gappov;
	m->tagset[0] = m->tagset[1] = 1;
	m->position = -1;
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->mfact = r->mfact;
			m->nmaster = r->nmaster;
			wlr_output_set_scale(wlr_output, r->scale);
			wlr_xcursor_manager_load(cursor_mgr, r->scale);
			m->lt[0] = m->lt[1] = r->lt;
			wlr_output_set_transform(wlr_output, r->rr);
			m->position = r - monrules;
			break;
		}
	}
	/* Set up event listeners */
	m->frame.notify = rendermon;
	wl_signal_add(&wlr_output->events.frame, &m->frame);
	m->destroy.notify = cleanupmon;
	wl_signal_add(&wlr_output->events.destroy, &m->destroy);

	wl_list_for_each(moni, &mons, link)
		if (m->position > moni->position)
			insertmon = moni;
	if (insertmon) {
		x = insertmon->w.x + insertmon->w.width;
		wl_list_insert(&insertmon->link, &m->link);
		fprintf(stderr, "%s inserted in pos %d\n", m->wlr_output->name, m->position);
	} else {
		wl_list_insert(&mons, &m->link);
		fprintf(stderr, "%s defaulting\n", m->wlr_output->name);
	}

	wlr_output_enable(wlr_output, 1);
	if (!wlr_output_commit(wlr_output))
		return;

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	wlr_output_layout_add(output_layout, wlr_output, x, 0);
	wl_list_for_each_reverse(moni, &mons, link) {
		/* all monitors that on the right of the new one must be moved */
		if (moni == m)
			break;
		wlr_output_layout_move(output_layout, moni->wlr_output, moni->w.x + m->wlr_output->width, 0);
		fprintf(stderr, "moved %s to %d", moni->wlr_output->name, moni->w.x + m->wlr_output->width);
	}

	for (size_t i = 0; i < nlayers; ++i)
		wl_list_init(&m->layers[i]);

	/* When adding monitors, the geometries of all monitors must be updated */
	updatemons();
	wl_list_for_each(m, &mons, link) {
		/* the first monitor in the list is the most recently added */
		Client *c;
		wl_list_for_each(c, &clients, link) {
			if (c->isfloating)
				resize(c, c->geom.x + m->w.width, c->geom.y,
						c->geom.width, c->geom.height, 0);
		}
		return;
	}
}

void quitallfullscreen(Monitor *m) {
	Client *c;
	wl_list_for_each(c, &clients, link)
		if (c->isfullscreen && VISIBLEON(c, m))
			setfullscreen(c, 0);
	/* Alternaitve: faster but only disables fullscreen on the focused window.
	 * All other currently visible fullscreen windows will be left untouched */
	/* if (selmon->fullscreenclient) */
	/* 	setfullscreen(selmon->fullscreenclient, 0); */
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct wlr_xdg_surface *xdg_surface = data;
	Client *c;

	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;
	quitallfullscreen(selmon);

	/* Allocate a Client for this surface */
	c = xdg_surface->data = calloc(1, sizeof(*c));
	c->surface.xdg = xdg_surface;
	c->bw = borderpx;

	/* Tell the client not to try anything fancy */
	wlr_xdg_toplevel_set_tiled(c->surface.xdg, WLR_EDGE_TOP |
			WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);

	/* Listen to the various events it can emit */
	c->commit.notify = commitnotify;
	wl_signal_add(&xdg_surface->surface->events.commit, &c->commit);
	c->map.notify = maprequest;
	wl_signal_add(&xdg_surface->events.map, &c->map);
	c->unmap.notify = unmapnotify;
	wl_signal_add(&xdg_surface->events.unmap, &c->unmap);
	c->destroy.notify = destroynotify;
	wl_signal_add(&xdg_surface->events.destroy, &c->destroy);

	c->fullscreen.notify = fullscreennotify;
	wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &c->fullscreen);
	c->isfullscreen = 0;
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;
	LayerSurface *layersurface;
	Monitor *m;
	struct wlr_layer_surface_v1_state old_state;

	if (!wlr_layer_surface->output) {
		wlr_layer_surface->output = selmon->wlr_output;
	}

	layersurface = calloc(1, sizeof(LayerSurface));

	layersurface->surface_commit.notify = commitlayersurfacenotify;
	wl_signal_add(&wlr_layer_surface->surface->events.commit,
		&layersurface->surface_commit);
	layersurface->destroy.notify = destroylayersurfacenotify;
	wl_signal_add(&wlr_layer_surface->events.destroy, &layersurface->destroy);
	layersurface->map.notify = maplayersurfacenotify;
	wl_signal_add(&wlr_layer_surface->events.map, &layersurface->map);
	layersurface->unmap.notify = unmaplayersurfacenotify;
	wl_signal_add(&wlr_layer_surface->events.unmap, &layersurface->unmap);

	layersurface->layer_surface = wlr_layer_surface;
	wlr_layer_surface->data = layersurface;

	m = wlr_layer_surface->output->data;

	wl_list_insert(&m->layers[wlr_layer_surface->client_pending.layer],
			&layersurface->link);

	// Temporarily set the layer's current state to client_pending
	// so that we can easily arrange it
	old_state = wlr_layer_surface->current;
	wlr_layer_surface->current = wlr_layer_surface->client_pending;
	arrangelayers(m);
	wlr_layer_surface->current = old_state;
}

void
createpointer(struct wlr_input_device *device)
{
	struct libinput_device *libinput_device =  (struct libinput_device*)
			wlr_libinput_get_device_handle(device);

	if (tap_to_click && libinput_device_config_tap_get_finger_count(libinput_device))
		libinput_device_config_tap_set_enabled(libinput_device, LIBINPUT_CONFIG_TAP_ENABLED);

	if (libinput_device_config_scroll_has_natural_scroll(libinput_device))
		libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, natural_scrolling);

	/* We don't do anything special with pointers. All of our pointer handling
	 * is proxied through wlr_cursor. On another compositor, you might take this
	 * opportunity to do libinput configuration on the device to set
	 * acceleration, etc. */
	wlr_cursor_attach_input_device(cursor, device);
}

void
createxdeco(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	Decoration *d = wlr_deco->data = calloc(1, sizeof(*d));

	wl_signal_add(&wlr_deco->events.request_mode, &d->request_mode);
	d->request_mode.notify = getxdecomode;
	wl_signal_add(&wlr_deco->events.destroy, &d->destroy);
	d->destroy.notify = destroyxdeco;

	getxdecomode(&d->request_mode, wlr_deco);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, destroy);
	Monitor *m;

	if (layersurface->layer_surface->mapped)
		unmaplayersurface(layersurface);
	wl_list_remove(&layersurface->link);
	wl_list_remove(&layersurface->destroy.link);
	wl_list_remove(&layersurface->map.link);
	wl_list_remove(&layersurface->unmap.link);
	wl_list_remove(&layersurface->surface_commit.link);
	if (layersurface->layer_surface->output) {
		m = layersurface->layer_surface->output->data;
		if (m)
			arrangelayers(m);
		layersurface->layer_surface->output = NULL;
	}
	free(layersurface);
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is destroyed and should never be shown again. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type == X11Managed)
		wl_list_remove(&c->activate.link);
	else if (c->type == XDGShell)
#endif
		wl_list_remove(&c->commit.link);
	free(c);
}

void
destroyxdeco(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	Decoration *d = wlr_deco->data;

	wl_list_remove(&d->destroy.link);
	wl_list_remove(&d->request_mode.link);
	free(d);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = selclient();
	setfullscreen(sel, !sel->isfullscreen);
}

void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	c->bw = (1 - fullscreen) * borderpx;

#ifdef XWAYLAND
	if (c->type == X11Managed)
		wlr_xwayland_surface_set_fullscreen(c->surface.xwayland, fullscreen);
	else
#endif
		wlr_xdg_toplevel_set_fullscreen(c->surface.xdg, fullscreen);

	if (fullscreen) {
		c->prevx = c->geom.x;
		c->prevy = c->geom.y;
		c->prevheight = c->geom.height;
		c->prevwidth = c->geom.width;
		resize(c, c->mon->m.x, c->mon->m.y, c->mon->m.width, c->mon->m.height, 0);
		c->mon->fullscreenclient = c;
	} else {
		resize(c, c->prevx, c->prevy, c->prevwidth, c->prevheight, 0);
		c->mon->fullscreenclient = NULL;
	}
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, !c->isfullscreen);
}

Monitor *
dirtomon(int dir)
{
	Monitor *m;

	if (dir > 0) {
		if (selmon->link.next == &mons)
			return wl_container_of(mons.next, m, link);
		return wl_container_of(selmon->link.next, m, link);
	} else {
		if (selmon->link.prev == &mons)
			return wl_container_of(mons.prev, m, link);
		return wl_container_of(selmon->link.prev, m, link);
	}
}

void
focusclient(Client *old, Client *c, int lift)
{
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);

	/* Raise client in stacking order if requested */
	if (c && lift) {
		wl_list_remove(&c->slink);
		wl_list_insert(&stack, &c->slink);
	}

	/* Nothing else to do? */
	if (c == old)
		return;

	/* Deactivate old client if focus is changing */
	if (c != old && old) {
#ifdef XWAYLAND
		if (old->type != XDGShell)
			wlr_xwayland_surface_activate(old->surface.xwayland, 0);
		else
#endif
			wlr_xdg_toplevel_set_activated(old->surface.xdg, 0);
	}

	/* Update wlroots' keyboard focus */
	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Have a client, so focus its top-level wlr_surface */
	if (shouldfocusclients(c->mon))
		wlr_seat_keyboard_notify_enter(seat, WLR_SURFACE(c),
				kb->keycodes, kb->num_keycodes, &kb->modifiers);

	/* Put the new client atop the focus stack and select its monitor */
	wl_list_remove(&c->flink);
	wl_list_insert(&fstack, &c->flink);
	selmon = c->mon;

	/* Activate the new client */
#ifdef XWAYLAND
	if (c->type != XDGShell)
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
	else
#endif
		wlr_xdg_toplevel_set_activated(c->surface.xdg, 1);
}

void
focusmon(const Arg *arg)
{
	Client *sel = selclient();
	Monitor *prevm = selmon;

	selmon = dirtomon(arg->i);
	focusclient(sel, focustop(selmon), 1);
	wlr_cursor_move(cursor, NULL, selmon->m.x - prevm->m.x , 0);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = selclient();
	if (!sel)
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;  /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break;  /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;  /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break;  /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(sel, c, 1);
}

Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink)
		if (VISIBLEON(c, m))
			return c;
	return NULL;
}

void
getxdecomode(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	wlr_xdg_toplevel_decoration_v1_set_mode(wlr_deco,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(device);
		break;
	default:
		/* XXX handle other input device types */
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* XXX do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&keyboards))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	int handled = 0;
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod) &&
				sym == k->keysym && k->func) {
			k->func(&k->arg);
			handled = 1;
		}
	}
	return handled;
}

void
keypress(struct wl_listener *listener, void *data)
{
	/* This event is raised when a key is pressed or released. */
	Keyboard *kb = wl_container_of(listener, kb, key);
	struct wlr_event_keyboard_key *event = data;
	int i;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			kb->device->keyboard->xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(kb->device->keyboard);
	/* On _press_, attempt to process a compositor keybinding. */
	if (event->state == WLR_KEY_PRESSED)
		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;

	if (!handled) {
		/* Pass unhandled keycodes along to the client. */
		wlr_seat_set_keyboard(seat, kb->device);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	Keyboard *kb = wl_container_of(listener, kb, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(seat, kb->device);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
		&kb->device->keyboard->modifiers);
}

void
killclient(const Arg *arg)
{
	Client *sel = selclient();
	if (!sel)
		return;

#ifdef XWAYLAND
	if (sel->type != XDGShell)
		wlr_xwayland_surface_close(sel->surface.xwayland);
	else
#endif
		wlr_xdg_toplevel_send_close(sel->surface.xdg);
}

void
maplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, map);
	wlr_surface_send_enter(layersurface->layer_surface->surface, layersurface->layer_surface->output);
	motionnotify(0);
}

void
maprequest(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *c = wl_container_of(listener, c, map);

#ifdef XWAYLAND
	if (c->type == X11Unmanaged) {
		/* Insert this independent into independents lists. */
		wl_list_insert(&independents, &c->link);
		return;
	}
#endif

	/* Insert this client into client lists. */
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);
	wl_list_insert(&stack, &c->slink);

#ifdef XWAYLAND
	if (c->type != XDGShell) {
		c->geom.x = c->surface.xwayland->x;
		c->geom.y = c->surface.xwayland->y;
		c->geom.width = c->surface.xwayland->width + 2 * c->bw;
		c->geom.height = c->surface.xwayland->height + 2 * c->bw;
	} else
#endif
	{
		wlr_xdg_surface_get_geometry(c->surface.xdg, &c->geom);
		c->geom.width += 2 * c->bw;
		c->geom.height += 2 * c->bw;
	}

	/* Set initial monitor, tags, floating status, and focus */
	applyrules(c);
}

void
monocle(Monitor *m)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m))
			continue;
		if (c->isfullscreen) {
			m->fullscreenclient = c;
			resize(c, c->mon->m.x, c->mon->m.y, c->mon->m.width, c->mon->m.height, 0);
			return;
		}
		if (!c->isfloating)
			resize(c, m->w.x, m->w.y, m->w.width, m->w.height, 0);
	}
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(cursor, event->device, event->x, event->y);
	motionnotify(event->time_msec);
}

void
motionnotify(uint32_t time)
{
	double sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	struct timespec now;
	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Update selmon (even while dragging a window) */
	if (sloppyfocus)
		selmon = xytomon(cursor->x, cursor->y);

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		resize(grabc, cursor->x - grabcx, cursor->y - grabcy,
				grabc->geom.width, grabc->geom.height, 1);
		return;
	} else if (cursor_mode == CurResize) {
		resize(grabc, grabc->geom.x, grabc->geom.y,
				cursor->x - grabc->geom.x,
				cursor->y - grabc->geom.y, 1);
		return;
	}

	if ((surface = xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
					cursor->x, cursor->y, &sx, &sy)))
		;
	else if ((surface = xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
					cursor->x, cursor->y, &sx, &sy)))
		;
#ifdef XWAYLAND
	/* Find an independent under the pointer and send the event along. */
	else if ((c = xytoindependent(cursor->x, cursor->y))) {
		surface = wlr_surface_surface_at(c->surface.xwayland->surface,
				cursor->x - c->surface.xwayland->x - c->bw,
				cursor->y - c->surface.xwayland->y - c->bw, &sx, &sy);

	/* Otherwise, find the client under the pointer and send the event along. */
	}
#endif
	else if ((c = xytoclient(cursor->x, cursor->y))) {
#ifdef XWAYLAND
		if (c->type != XDGShell)
			surface = wlr_surface_surface_at(c->surface.xwayland->surface,
					cursor->x - c->geom.x - c->bw,
					cursor->y - c->geom.y - c->bw, &sx, &sy);
		else
#endif
			surface = wlr_xdg_surface_surface_at(c->surface.xdg,
					cursor->x - c->geom.x - c->bw,
					cursor->y - c->geom.y - c->bw, &sx, &sy);
	}
	else if ((surface = xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
					cursor->x, cursor->y, &sx, &sy)))
		;
	else
		surface = xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
					cursor->x, cursor->y, &sx, &sy);

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface)
		wlr_xcursor_manager_set_cursor_image(cursor_mgr,
				"left_ptr", cursor);

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_event_pointer_motion *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(cursor, event->device,
			event->delta_x, event->delta_y);
	motionnotify(event->time_msec);
}

void
moveresize(const Arg *arg)
{
	grabc = xytoclient(cursor->x, cursor->y);
	if (!grabc)
		return;

	/* Float the window and tell motionnotify to grab it */
	setfloating(grabc, 1);
	switch (cursor_mode = arg->ui) {
	case CurMove:
		grabcx = cursor->x - grabc->geom.x;
		grabcy = cursor->y - grabc->geom.y;
		wlr_xcursor_manager_set_cursor_image(cursor_mgr, "fleur", cursor);
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started */
		wlr_cursor_warp_closest(cursor, NULL,
				grabc->geom.x + grabc->geom.width,
				grabc->geom.y + grabc->geom.height);
		wlr_xcursor_manager_set_cursor_image(cursor_mgr,
				"bottom_right_corner", cursor);
		break;
	}
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, false);
}

// apply_output_config
void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, bool test)
{
	struct wlr_output_configuration_head_v1 *config_head;
	bool ok = true;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;

		wlr_output_enable(wlr_output, config_head->state.enabled);

		if (config_head->state.enabled) {
			if (config_head->state.mode)
				wlr_output_set_mode(wlr_output, config_head->state.mode);
			else
				wlr_output_set_custom_mode(wlr_output,
						config_head->state.custom_mode.width,
						config_head->state.custom_mode.height,
						config_head->state.custom_mode.refresh);

			wlr_output_layout_move(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);
			wlr_output_set_transform(wlr_output, config_head->state.transform);
			wlr_output_set_scale(wlr_output, config_head->state.scale);
		}

		if (test) {
			ok &= wlr_output_test(wlr_output);
			wlr_output_rollback(wlr_output);
		} else
			ok &= wlr_output_commit(wlr_output);
	}
	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
		if (!test)
			updatemons();
	}
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, true);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	/* Use top level surface if nothing more specific given */
	if (c && !surface)
		surface = WLR_SURFACE(c);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	/* If surface is already focused, only notify of motion */
	if (surface == seat->pointer_state.focused_surface) {
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		return;
	}

	/* Otherwise, let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired. */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);

	if (!c)
		return;

#if XWAYLAND
	if (c->type == X11Unmanaged)
		return;
#endif

	if (sloppyfocus)
		focusclient(selclient(), c, 0);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
render(struct wlr_surface *surface, int sx, int sy, void *data)
{
	/* This function is called for every surface that needs to be rendered. */
	struct render_data *rdata = data;
	struct wlr_output *output = rdata->output;
	double ox = 0, oy = 0;
	struct wlr_box obox;
	float matrix[9];
	enum wl_output_transform transform;

	/* We first obtain a wlr_texture, which is a GPU resource. wlroots
	 * automatically handles negotiating these with the client. The underlying
	 * resource could be an opaque handle passed from the client, or the client
	 * could have sent a pixel buffer which we copied to the GPU, or a few other
	 * means. You don't have to worry about this, wlroots takes care of it. */
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (!texture)
		return;

	/* The client has a position in layout coordinates. If you have two displays,
	 * one next to the other, both 1080p, a client on the rightmost display might
	 * have layout coordinates of 2000,100. We need to translate that to
	 * output-local coordinates, or (2000 - 1920). */
	wlr_output_layout_output_coords(output_layout, output, &ox, &oy);

	/* We also have to apply the scale factor for HiDPI outputs. This is only
	 * part of the puzzle, dwl does not fully support HiDPI. */
	obox.x = ox + rdata->x + sx;
	obox.y = oy + rdata->y + sy;
	obox.width = surface->current.width;
	obox.height = surface->current.height;
	scalebox(&obox, output->scale);

	/*
	 * Those familiar with OpenGL are also familiar with the role of matrices
	 * in graphics programming. We need to prepare a matrix to render the
	 * client with. wlr_matrix_project_box is a helper which takes a box with
	 * a desired x, y coordinates, width and height, and an output geometry,
	 * then prepares an orthographic projection and multiplies the necessary
	 * transforms to produce a model-view-projection matrix.
	 *
	 * Naturally you can do this any way you like, for example to make a 3D
	 * compositor.
	 */
	transform = wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &obox, transform, 0,
		output->transform_matrix);

	/* This takes our matrix, the texture, and an alpha, and performs the actual
	 * rendering on the GPU. */
	wlr_render_texture_with_matrix(drw, texture, matrix, 1);

	/* This lets the client know that we've displayed that frame and it can
	 * prepare another one now if it likes. */
	wlr_surface_send_frame_done(surface, rdata->when);
}

void
renderclients(Monitor *m, struct timespec *now)
{
	Client *c, *sel = selclient();
	const float *color;
	double ox, oy;
	int i, w, h;
	struct render_data rdata;
	struct wlr_box *borders;
	struct wlr_surface *surface;
	/* Each subsequent window we render is rendered on top of the last. Because
	 * our stacking list is ordered front-to-back, we iterate over it backwards. */

	wl_list_for_each_reverse(c, &stack, slink) {
		/* Only render visible clients which show on this monitor */
		if (!VISIBLEON(c, c->mon) || !wlr_output_layout_intersects(
					output_layout, m->wlr_output, &c->geom) ||
				(m->fullscreenclient && m->fullscreenclient != c))
				/* Alternative: render at least all fullscreen windows*/
				/* (m->fullscreenclient && !c->isfullscreen)) */
			continue;

		surface = WLR_SURFACE(c);
		ox = c->geom.x, oy = c->geom.y;
		wlr_output_layout_output_coords(output_layout, m->wlr_output,
				&ox, &oy);

		c->bw = ((c->isfloating || c->mon->ybw) && !c->isfullscreen) * borderpx;
		resize(c, c->geom.x, c->geom.y, c->geom.width, c->geom.height, 0);
		if (c->bw == 0)
			goto render;

		w = surface->current.width;
		h = surface->current.height;
		borders = (struct wlr_box[4]) {
			{ox, oy, w + 2 * c->bw, c->bw},             /* top */
			{ox, oy + c->bw, c->bw, h},                 /* left */
			{ox + c->bw + w, oy + c->bw, c->bw, h},     /* right */
			{ox, oy + c->bw + h, w + 2 * c->bw, c->bw}, /* bottom */
		};

		/* Draw window borders */
		color = (c == sel) ? focuscolor : bordercolor;
		for (i = 0; i < 4; i++) {
			scalebox(&borders[i], m->wlr_output->scale);
			wlr_render_rect(drw, &borders[i], color,
					m->wlr_output->transform_matrix);
		}

render:
		/* This calls our render function for each surface among the
		 * xdg_surface's toplevel and popups. */
		rdata.output = m->wlr_output;
		rdata.when = now;
		rdata.x = c->geom.x + c->bw;
		rdata.y = c->geom.y + c->bw;
#ifdef XWAYLAND
		if (c->type != XDGShell)
			wlr_surface_for_each_surface(c->surface.xwayland->surface, render, &rdata);
		else
#endif
			wlr_xdg_surface_for_each_surface(c->surface.xdg, render, &rdata);
	}
}

void
renderlayer(struct wl_list *layer_surfaces, struct timespec *now)
{
	struct render_data rdata;
	LayerSurface *layersurface;
	wl_list_for_each(layersurface, layer_surfaces, link) {
		rdata.output = layersurface->layer_surface->output;
		rdata.when = now;
		rdata.x = layersurface->geo.x;
		rdata.y = layersurface->geo.y;

		wlr_surface_for_each_surface(layersurface->layer_surface->surface,
				render, &rdata);
	}
}

void
rendermon(struct wl_listener *listener, void *data)
{
	Client *c;
	int render = 1;

	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Do not render if any XDG clients have an outstanding resize. */
	wl_list_for_each(c, &stack, slink) {
		if (c->resize) {
			wlr_surface_send_frame_done(WLR_SURFACE(c), &now);
			render = 0;
		}
	}

	/* wlr_output_attach_render makes the OpenGL context current. */
	if (!wlr_output_attach_render(m->wlr_output, NULL))
		return;

	if (render) {
		/* Begin the renderer (calls glViewport and some other GL sanity checks) */
		wlr_renderer_begin(drw, m->wlr_output->width, m->wlr_output->height);
		wlr_renderer_clear(drw, rootcolor);

		renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND], &now);
		if (!m->fullscreenclient)
			renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], &now);
		renderclients(m, &now);
#ifdef XWAYLAND
		renderindependents(m->wlr_output, &now);
#endif
		renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &now);
		renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &now);

		/* Hardware cursors are rendered by the GPU on a separate plane, and can be
		 * moved around without re-rendering what's beneath them - which is more
		 * efficient. However, not all hardware supports hardware cursors. For this
		 * reason, wlroots provides a software fallback, which we ask it to render
		 * here. wlr_cursor handles configuring hardware vs software cursors for you,
		 * and this function is a no-op when hardware cursors are in use. */
		wlr_output_render_software_cursors(m->wlr_output, NULL);

		/* Conclude rendering and swap the buffers, showing the final frame
		 * on-screen. */
		wlr_renderer_end(drw);
	}

	wlr_output_commit(m->wlr_output);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	/*
	 * Note that I took some shortcuts here. In a more fleshed-out
	 * compositor, you'd wait for the client to prepare a buffer at
	 * the new size, then commit any movement that was prepared.
	 */
	struct wlr_box *bbox = interact ? &sgeom : &c->mon->w;
	c->geom.x = x;
	c->geom.y = y;
	c->geom.width = w;
	c->geom.height = h;
	applybounds(c, bbox);
	/* wlroots makes this a no-op if size hasn't changed */
#ifdef XWAYLAND
	if (c->type != XDGShell)
		wlr_xwayland_surface_configure(c->surface.xwayland,
				c->geom.x, c->geom.y,
				c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
	else
#endif
		c->resize = wlr_xdg_toplevel_set_size(c->surface.xdg,
				c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
}

void
run(char *startup_cmd)
{
	pid_t startup_pid = -1;

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		BARF("startup: display_add_socket_auto");

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		BARF("startup: backend_start");

	/* Now that outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* XXX hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping.  still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);

	/* Set the WAYLAND_DISPLAY environment variable to our socket and run the
	 * startup command if requested. */
	setenv("WAYLAND_DISPLAY", socket, 1);
	if (startup_cmd) {
		startup_pid = fork();
		if (startup_pid < 0)
			EBARF("startup: fork");
		if (startup_pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			EBARF("startup: execl");
		}
	}
	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(dpy);

	if (startup_cmd) {
		kill(startup_pid, SIGTERM);
		waitpid(startup_pid, NULL, 0);
	}
}

void
scalebox(struct wlr_box *box, float scale)
{
	box->x *= scale;
	box->y *= scale;
	box->width *= scale;
	box->height *= scale;
}

Client *
selclient(void)
{
	Client *c = wl_container_of(fstack.next, c, flink);
	if (wl_list_empty(&fstack) || !VISIBLEON(c, selmon))
		return NULL;
	return c;
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image */
	/* XXX still need to save the provided surface to restore later */
	if (cursor_mode != CurNormal)
		return;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setfloating(Client *c, int floating)
{
	if (c->isfloating == floating)
		return;
	c->isfloating = floating;
	arrange(c->mon);
}

void
setgaps(int oh, int ov, int ih, int iv)
{
	if (oh < 0) oh = 0;
	if (ov < 0) ov = 0;
	if (ih < 0) ih = 0;
	if (iv < 0) iv = 0;

	selmon->gappoh = oh;
	selmon->gappov = ov;
	selmon->gappih = ih;
	selmon->gappiv = iv;
	arrange(selmon);
}

void
togglegaps(const Arg *arg)
{
	enablegaps = !enablegaps;
	arrange(selmon);
}

void
defaultgaps(const Arg *arg)
{
	setgaps(gappoh, gappov, gappih, gappiv);
}

void
incrgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov + arg->i,
		selmon->gappih + arg->i,
		selmon->gappiv + arg->i
	);
}

void
incrigaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih + arg->i,
		selmon->gappiv + arg->i
	);
}

void
incrogaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov + arg->i,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incrohgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incrovgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov + arg->i,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incrihgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih + arg->i,
		selmon->gappiv
	);
}

void
incrivgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih,
		selmon->gappiv + arg->i
	);
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v) {
		if (arg->v) { // setting a layout != floating
			Client *c;
			wl_list_for_each(c, &clients, link) {
				setfloating(c, 0);
			}
		}
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	}
	/* XXX change layout symbol? */
	arrange(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setmon(Client *c, Monitor *m, unsigned int newtags)
{
	Monitor *oldmon = c->mon;
	Client *oldsel = selclient();

	if (oldmon == m)
		return;
	c->mon = m;

	/* XXX leave/enter is not optimal but works */
	if (oldmon) {
		wlr_surface_send_leave(WLR_SURFACE(c), oldmon->wlr_output);
		arrange(oldmon);
	}
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		applybounds(c, &m->m);
		wlr_surface_send_enter(WLR_SURFACE(c), m->wlr_output);
		c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
		arrange(m);
	}
	focusclient(oldsel, focustop(selmon), 1);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setup(void)
{
	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	dpy = wl_display_create();

	/* clean up child processes immediately */
	sigchld(0);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. The NULL argument here optionally allows you
	 * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
	 * backend uses the renderer, for example, to fall back to software cursors
	 * if the backend does not support hardware cursors (some older GPUs
	 * don't). */
	if (!(backend = wlr_backend_autocreate(dpy, NULL)))
		BARF("couldn't create backend");

	/* If we don't provide a renderer, autocreate makes a GLES2 renderer for us.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	drw = wlr_backend_get_renderer(backend);
	wlr_renderer_init_wl_display(drw, dpy);

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, drw);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_gamma_control_manager_v1_create(dpy);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create();
	wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our client lists and the xdg-shell. The xdg-shell is a
	 * Wayland protocol which is used for application windows. For more
	 * detail on shells, refer to the article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&stack);
	wl_list_init(&independents);

	layer_shell = wlr_layer_shell_v1_create(dpy);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_shell_surface);

	xdg_shell = wlr_xdg_shell_create(dpy);
	wl_signal_add(&xdg_shell->events.new_surface, &new_xdg_surface);

	/* Use xdg_decoration protocol to negotiate server-side decorations */
	xdeco_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdeco_mgr->events.new_toplevel_decoration, &new_xdeco);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in my
	 * input handling blog post:
	 *
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute,
			&cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&keyboards);
	wl_signal_add(&backend->events.new_input, &new_input);
	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor,
			&request_cursor);
	wl_signal_add(&seat->events.request_set_selection,
			&request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection,
			&request_set_psel);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

#ifdef XWAYLAND
	/*
	 * Initialise the XWayland X server.
	 * It will be started when the first X client is started.
	 */
	xwayland = wlr_xwayland_create(dpy, compositor, true);
	if (xwayland) {
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

		setenv("DISPLAY", xwayland->display_name, true);
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
#endif
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		EBARF("can't install SIGCHLD handler");
	while (0 < waitpid(-1, NULL, WNOHANG))
		;
}

void
shotFocusMon(Arg *arg) {
	const char *shotFocusedCmd[] = { "shot.sh", "focused", selmon->wlr_output->name, NULL };
	Arg argo = {.v = shotFocusedCmd};
	spawn(&argo);
}

bool
shouldfocusclients(Monitor *m)
{
	LayerSurface *layersurface;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	for (size_t i = 0; i < LENGTH(layers_above_shell); ++i)
		wl_list_for_each(layersurface, &m->layers[layers_above_shell[i]], link)
			if (layersurface->layer_surface->current.keyboard_interactive &&
					layersurface->layer_surface->mapped)
				return false;
	return true;
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		EBARF("dwl: execvp %s failed", ((char **)arg->v)[0]);
	}
}

void
tag(const Arg *arg)
{
	Client *sel = selclient();
	if (sel && arg->ui & TAGMASK) {
		sel->tags = arg->ui & TAGMASK;
		focusclient(sel, focustop(selmon), 1);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	Client *sel = selclient();
	if (!sel)
		return;
	setmon(sel, dirtomon(arg->i), 0);
}

void
tile(Monitor *m)
{
	unsigned int i, h, r, oe = enablegaps, ie = enablegaps, mw, my, ty;
	Client *c;
	nclients = 0;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating)
			nclients++;
	if (nclients == 0)
		return;

	if (smartgaps == nclients) {
		oe = 0; // outer gaps disabled
	}

	if (nclients > m->nmaster)
		mw = m->nmaster ? (m->w.width + m->gappiv*ie) * m->mfact : 0;
	else
		mw = m->w.width - 2*m->gappov*oe + m->gappiv*ie;
	i = 0;
	my = ty = m->gappoh*oe;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m))
			continue;
		if (c->isfullscreen) {
			m->fullscreenclient = c;
			resize(c, c->mon->m.x, c->mon->m.y, c->mon->m.width, c->mon->m.height, 0);
			return;
		}
		if (c->isfloating)
			continue;
		if (i < m->nmaster) {
			r = MIN(nclients, m->nmaster) - i;
			h = (m->w.height - my - m->gappoh*oe - m->gappih*ie * (r - 1)) / r;
			resize(c, m->w.x + m->gappov*oe, m->w.y + my, mw - m->gappiv*ie, h, 0);
			my += c->geom.height + m->gappih*ie;
		} else {
			r = nclients - i;
			h = (m->w.height - ty - m->gappoh*oe - m->gappih*ie * (r - 1)) / r;
			resize(c, m->w.x + mw + m->gappov*oe, m->w.y + ty, m->w.width - mw - 2*m->gappov*oe, h, 0);
			ty += c->geom.height + m->gappih*ie;
		}
		i++;
	}
}

void
togglefloating(const Arg *arg)
{
	Client *sel = selclient();
	if (!sel)
		return;
	/* return if fullscreen */
	setfloating(sel, !sel->isfloating /* || sel->isfixed */);
}

void
togglekblayout(const Arg *arg)
{
	Keyboard *kb;
	struct xkb_rule_names newrule = xkb_rules;

	kblayout = (kblayout + 1) % LENGTH(kblayouts);
	newrule.layout = kblayouts[kblayout];
	wl_list_for_each(kb, &keyboards, link) {
		setkblayout(kb, &newrule);
	}
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;
	Client *sel = selclient();
	if (!sel)
		return;
	newtags = sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		sel->tags = newtags;
		focusclient(sel, focustop(selmon), 1);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	Client *sel = selclient();
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focusclient(sel, focustop(selmon), 1);
		arrange(selmon);
	}
}

void
unmaplayersurface(LayerSurface *layersurface)
{
	layersurface->layer_surface->mapped = false;
	if (layersurface->layer_surface->surface ==
			seat->keyboard_state.focused_surface)
		focusclient(NULL, selclient(), 1);
	motionnotify(0);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, unmap);
	unmaplayersurface(layersurface);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	wl_list_remove(&c->link);
#ifdef XWAYLAND
	if (c->type == X11Unmanaged)
		return;
#endif
	setmon(c, NULL, 0);
	wl_list_remove(&c->flink);
	wl_list_remove(&c->slink);
}

void
updatemons()
{
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();
	Monitor *m;
	sgeom = *wlr_output_layout_get_box(output_layout, NULL);

	wl_list_for_each(m, &mons, link) {
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		m->m = m->w = *wlr_output_layout_get_box(output_layout, m->wlr_output);
		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);

		config_head->state.enabled = m->wlr_output->enabled;
		config_head->state.mode = m->wlr_output->current_mode;
		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;
	}

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
view(const Arg *arg)
{
	Client *sel = selclient();
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focusclient(sel, focustop(selmon), 1);
	arrange(selmon);
}

Client *
xytoclient(double x, double y)
{
	/* Find the topmost visible client (if any) at point (x, y), including
	 * borders. This relies on stack being ordered from top to bottom. */
	Client *c;
	wl_list_for_each(c, &stack, slink)
		if (VISIBLEON(c, c->mon) && wlr_box_contains_point(&c->geom, x, y))
			return c;
	return NULL;
}

struct wlr_surface *
xytolayersurface(struct wl_list *layer_surfaces, double x, double y,
		double *sx, double *sy)
{
	LayerSurface *layersurface;
	wl_list_for_each_reverse(layersurface, layer_surfaces, link) {
		struct wlr_surface *sub;
		if (!layersurface->layer_surface->mapped)
			continue;
		sub = wlr_layer_surface_v1_surface_at(
				layersurface->layer_surface,
				x - layersurface->geo.x,
				y - layersurface->geo.y,
				sx, sy);
		if (sub)
			return sub;

	}
	return NULL;
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = selclient(), *oldsel = sel;

	if (!sel || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(oldsel, sel, 1);
	arrange(selmon);
}

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
       Client *c = wl_container_of(listener, c, activate);

       /* Only "managed" windows can be activated */
       if (c->type == X11Managed)
               wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
createnotifyx11(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xwayland_surface = data;
	Client *c;
	quitallfullscreen(selmon);

	/* Allocate a Client for this surface */
	c = xwayland_surface->data = calloc(1, sizeof(*c));
	c->surface.xwayland = xwayland_surface;
	c->type = xwayland_surface->override_redirect ? X11Unmanaged : X11Managed;
	c->bw = borderpx;

	/* Listen to the various events it can emit */
	c->map.notify = maprequest;
	wl_signal_add(&xwayland_surface->events.map, &c->map);
	c->unmap.notify = unmapnotify;
	wl_signal_add(&xwayland_surface->events.unmap, &c->unmap);
	c->activate.notify = activatex11;
	wl_signal_add(&xwayland_surface->events.request_activate, &c->activate);
	c->destroy.notify = destroynotify;
	wl_signal_add(&xwayland_surface->events.destroy, &c->destroy);

	c->fullscreen.notify = fullscreennotify;
	wl_signal_add(&xwayland_surface->events.request_fullscreen, &c->fullscreen);
	c->isfullscreen = 0;
}

Atom
getatom(xcb_connection_t *xc, const char *name)
{
	Atom atom = 0;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	cookie = xcb_intern_atom(xc, 0, strlen(name), name);
	if ((reply = xcb_intern_atom_reply(xc, cookie, NULL)))
		atom = reply->atom;
	free(reply);

	return atom;
}

void
renderindependents(struct wlr_output *output, struct timespec *now)
{
	Client *c;
	struct render_data rdata;
	struct wlr_box geom;

	wl_list_for_each_reverse(c, &independents, link) {
		geom.x = c->surface.xwayland->x;
		geom.y = c->surface.xwayland->y;
		geom.width = c->surface.xwayland->width;
		geom.height = c->surface.xwayland->height;

		/* Only render visible clients which show on this output */
		if (!wlr_output_layout_intersects(output_layout, output, &geom))
			continue;

		rdata.output = output;
		rdata.when = now;
		rdata.x = c->surface.xwayland->x;
		rdata.y = c->surface.xwayland->y;

		wlr_surface_for_each_surface(c->surface.xwayland->surface, render, &rdata);
	}
}

void
updatewindowtype(Client *c)
{
	size_t i;
	for (i = 0; i < c->surface.xwayland->window_type_len; i++)
		if (c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeDialog] ||
				c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeSplash] ||
				c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeToolbar] ||
				c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeUtility])
			c->isfloating = 1;
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
	xcb_connection_t *xc = xcb_connect(xwayland->display_name, NULL);
	int err = xcb_connection_has_error(xc);
	if (err) {
		fprintf(stderr, "xcb_connect to X server failed with code %d\n. Continuing with degraded functionality.\n", err);
		return;
	}

	/* Collect atoms we are interested in.  If getatom returns 0, we will
	 * not detect that window type. */
	netatom[NetWMWindowTypeDialog] = getatom(xc, "_NET_WM_WINDOW_TYPE_DIALOG");
	netatom[NetWMWindowTypeSplash] = getatom(xc, "_NET_WM_WINDOW_TYPE_SPLASH");
	netatom[NetWMWindowTypeUtility] = getatom(xc, "_NET_WM_WINDOW_TYPE_TOOLBAR");
	netatom[NetWMWindowTypeToolbar] = getatom(xc, "_NET_WM_WINDOW_TYPE_UTILITY");

	/* assign the one and only seat */
	wlr_xwayland_set_seat(xwayland, seat);

	xcb_disconnect(xc);
}

Client *
xytoindependent(double x, double y)
{
	/* Find the topmost visible independent at point (x, y).
	 * For independents, the most recently created can be used as the "top".
	 * We rely on the X11 convention of unmapping unmanaged when the "owning"
	 * client loses focus, which ensures that unmanaged are only visible on
	 * the current tag. */
	Client *c;
	struct wlr_box geom;
	wl_list_for_each_reverse(c, &independents, link) {
		geom.x = c->surface.xwayland->x;
		geom.y = c->surface.xwayland->y;
		geom.width = c->surface.xwayland->width;
		geom.height = c->surface.xwayland->height;
		if (wlr_box_contains_point(&geom, x, y))
			return c;
	}
	return NULL;
}
#endif

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:h")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	// Wayland requires XDG_RUNTIME_DIR for creating its communications
	// socket
	if (!getenv("XDG_RUNTIME_DIR"))
		BARF("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	BARF("Usage: %s [-s startup command]", argv[0]);
}
