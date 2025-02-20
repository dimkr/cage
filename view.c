/*
 * Cage: A Wayland kiosk.
 *
 * Copyright (C) 2018-2020 Jente Hidskes
 *
 * See the LICENSE file accompanying this file.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_surface.h>

#include "output.h"
#include "seat.h"
#include "server.h"
#include "view.h"
#if CAGE_HAS_XWAYLAND
#include "xwayland.h"
#endif

char *
view_get_title(struct cg_view *view)
{
	const char *title = view->impl->get_title(view);
	if (!title) {
		return NULL;
	}
	return strndup(title, strlen(title));
}

bool
view_is_primary(struct cg_view *view)
{
	return view->impl->is_primary(view);
}

bool
view_is_transient_for(struct cg_view *child, struct cg_view *parent)
{
	return child->impl->is_transient_for(child, parent);
}

void
view_activate(struct cg_view *view, bool activate)
{
	view->impl->activate(view, activate);
}

static bool
view_extends_output_layout(struct cg_view *view, struct wlr_box *layout_box)
{
	int width, height;
	view->impl->get_geometry(view, &width, &height);

	return (layout_box->height < height || layout_box->width < width);
}

static void
view_maximize(struct cg_view *view, struct wlr_box *layout_box)
{
	view->lx = layout_box->x;
	view->ly = layout_box->y;

	wlr_scene_node_set_position(view->scene_node, view->lx, view->ly);

	view->impl->maximize(view, layout_box->width, layout_box->height);
}

static void
view_center(struct cg_view *view, struct wlr_box *layout_box)
{
	int width, height;
	view->impl->get_geometry(view, &width, &height);

	view->lx = (layout_box->width - width) / 2;
	view->ly = (layout_box->height - height) / 2;

	wlr_scene_node_set_position(view->scene_node, view->lx, view->ly);
}

void
view_position(struct cg_view *view)
{
	struct wlr_box *layout_box = wlr_output_layout_get_box(view->server->output_layout, NULL);

	if (view_is_primary(view) || view_extends_output_layout(view, layout_box)) {
		view_maximize(view, layout_box);
	} else {
		view_center(view, layout_box);
	}
}

void
view_unmap(struct cg_view *view)
{
	wl_list_remove(&view->link);

	wlr_scene_node_destroy(view->scene_node);

	view->wlr_surface = NULL;
}

void
view_map(struct cg_view *view, struct wlr_surface *surface)
{
	view->wlr_surface = surface;

	view->scene_node = wlr_scene_subsurface_tree_create(&view->server->scene->node, surface);
	if (!view->scene_node) {
		wl_resource_post_no_memory(surface->resource);
		return;
	}
	view->scene_node->data = view;

#if CAGE_HAS_XWAYLAND
	/* We shouldn't position override-redirect windows. They set
	   their own (x,y) coordinates in handle_wayland_surface_map. */
	if (view->type != CAGE_XWAYLAND_VIEW || xwayland_view_should_manage(view))
#endif
	{
		view_position(view);
	}

	wl_list_insert(&view->server->views, &view->link);
	seat_set_focus(view->server->seat, view);
}

void
view_destroy(struct cg_view *view)
{
	struct cg_server *server = view->server;

	if (view->wlr_surface != NULL) {
		view_unmap(view);
	}

	view->impl->destroy(view);

	/* If there is a previous view in the list, focus that. */
	bool empty = wl_list_empty(&server->views);
	if (!empty) {
		struct cg_view *prev = wl_container_of(server->views.next, prev, link);
		seat_set_focus(server->seat, prev);
	}
}

void
view_init(struct cg_view *view, struct cg_server *server, enum cg_view_type type, const struct cg_view_impl *impl)
{
	view->server = server;
	view->type = type;
	view->impl = impl;
}

struct cg_view *
view_from_wlr_surface(struct cg_server *server, struct wlr_surface *surface)
{
	struct cg_view *view;
	wl_list_for_each (view, &server->views, link) {
		if (view->wlr_surface == surface) {
			return view;
		}
	}
	return NULL;
}
