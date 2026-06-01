/*

	Copyright (C) 1991-2001 and beyond by Bungie Studios, Inc.
	and the "Aleph One" developers.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html

	Direct3D 9 fixed-function render-tree rasterizer implementation.

*/

#include "cseries.h"

#ifdef HAVE_DX9

#include "RenderRasterize_D3D9.h"
#include "D3D9_Setup.h"
#include "D3D9_Textures.h"
#include "OGL_Textures.h"
#include "AnimatedTextures.h"
#include "lightsource.h"
#include "map.h"
#include "scottish_textures.h"
#include "interface.h"
#include "Logging.h"

#include <d3d9.h>
#include <cmath>

// DEPTH_TO_SHADE from scottish_textures.cpp: how fast brightness falls off with
// distance into the view.
#define DEPTH_TO_SHADE(d) (((_fixed)(d))<<(FIXED_FRACTIONAL_BITS-WORLD_FRACTIONAL_BITS-3))

// Compute the final per-vertex brightness (0xAARRGGBB gray) the way the
// software renderer's calculate_shading_table does: combine the surface's
// ambient shade with a depth-cued shade (darker farther from the viewer), so
// surfaces gradient-shade like classic Marathon instead of looking flat.
//   ambient_shade: the surface light (_fixed; negative means shadeless/fixed)
//   depth: world distance from the viewer to this vertex
static unsigned long shaded_vertex_color(view_data* view, _fixed ambient_shade, double depth)
{
	_fixed final_shade;
	if (ambient_shade < 0)
	{
		final_shade = -ambient_shade; // shadeless: use ambient magnitude directly
	}
	else
	{
		_fixed shade = view->maximum_depth_intensity - DEPTH_TO_SHADE((short)depth);
		if (shade < 0) shade = 0;
		if (shade > FIXED_ONE) shade = FIXED_ONE;
		// i0 + i1 == MAX(i0,i1) + MIN(i0,i1)/2
		final_shade = (ambient_shade > shade) ? (ambient_shade + (shade >> 1))
											  : (shade + (ambient_shade >> 1));
	}
	if (final_shade < 0) final_shade = 0;
	if (final_shade > FIXED_ONE) final_shade = FIXED_ONE;

	// Linear shade; brightness scaled by MODULATE2X in the texture stage.
	int v = (int)(((double)final_shade / (double)FIXED_ONE) * 255.0 + 0.5);
	if (v < 0) v = 0; if (v > 255) v = 255;
	return 0xff000000u |
		   (static_cast<unsigned long>(v) << 16) |
		   (static_cast<unsigned long>(v) << 8) |
		   static_cast<unsigned long>(v);
}

// Distance from the viewer to a world point (for depth cueing).
static double vertex_depth(view_data* view, double x, double y, double z)
{
	double dx = x - view->origin.x;
	double dy = y - view->origin.y;
	double dz = z - view->origin.z;
	return sqrt(dx * dx + dy * dy + dz * dz);
}

// Set up a TextureManager for a wall/floor/ceiling texture, mirroring
// RenderAsRealWall's manager configuration, and return its cached D3D9 texture.
static IDirect3DTexture9* setup_wall_texture(shape_descriptor texture, short transfer_mode,
											 TextureManager& TMgr)
{
	(void)transfer_mode; // surface animation mode; not the texture transfer mode
	shape_descriptor desc = AnimTxtr_Translate(texture);
	if (desc == UNONE)
		return nullptr;

	// Fetch the bitmap + shading tables for this shape (TextureManager::Setup
	// reads pixels from Texture).
	bitmap_definition* bitmap = nullptr;
	void* shading_tables = nullptr;
	get_shape_bitmap_and_shading_table(desc, &bitmap, &shading_tables, _shading_normal);
	if (!bitmap)
		return nullptr;

	TMgr.ShapeDesc = desc;
	TMgr.ShadingTables = shading_tables;
	TMgr.Texture = bitmap;
	// This is the rendering-transfer mode (scottish_textures.h enum), not the
	// surface's animation transfer mode. Walls/floors are plain textured;
	// passing the animation mode here was misread as _tinted/_static and forced
	// the silhouette (all-white) color table.
	TMgr.TransferMode = _textured_transfer;
	TMgr.TransferData = 0;
	TMgr.IsShadeless = false;
	TMgr.TextureType = OGL_Txtr_Wall;
	if (!TMgr.Setup())
		return nullptr;
	return D3D9_GetTexture(TMgr);
}

void RenderRasterize_D3D9::render_tree()
{
	// TextureManager::Setup needs the per-collection texture-state sets, which
	// are normally allocated by OGL_StartRun (OpenGL only). Initialize them once
	// for the D3D9 path too (the call itself does no GL work).
	static bool textures_started = false;
	if (!textures_started)
	{
		OGL_StartTextures();
		textures_started = true;
	}

	// Establish the 3D camera for this frame, then walk the tree as usual.
	D3D9_SetViewTransforms(view);
	RenderRasterizerClass::render_tree();
}

// Draw one polygon's floor or ceiling as a world-space textured fan. Pure map
// data in: the polygon (for its endpoints) and a horizontal_surface_data
// (height/texture/light/origin). Shared by the vis-tree node path and the
// full-level pass.
static void draw_horizontal_surface(view_data* view, polygon_data* polygon,
	horizontal_surface_data* surface, bool ceil)
{
	shape_descriptor texture = AnimTxtr_Translate(surface->texture);
	if (texture == UNONE)
		return;

	TextureManager TMgr;
	IDirect3DTexture9* tex = setup_wall_texture(surface->texture, surface->transfer_mode, TMgr);
	if (!tex)
		return; // texture setup failed; TMgr fields (TileRatio etc.) are invalid

	_fixed ambient = get_light_intensity(surface->lightsource_index);

	float scale = float(WORLD_ONE) * TMgr.TileRatio();
	if (scale == 0.0f) scale = float(WORLD_ONE);

	short vertex_count = polygon->vertex_count;
	if (vertex_count < 3 || vertex_count > 32)
		return;

	D3D9_WorldVertex verts[32];
	for (short i = 0; i < vertex_count; ++i)
	{
		// Reverse winding for ceilings so the fan faces the viewer; engine
		// supplies polygon endpoints in floor winding.
		short idx = ceil ? (vertex_count - 1 - i) : i;
		world_point2d vertex = get_endpoint_data(polygon->endpoint_indexes[idx])->vertex;
		verts[i].x = float(vertex.x);
		verts[i].y = float(vertex.y);
		verts[i].z = float(surface->height);
		verts[i].color = shaded_vertex_color(view, ambient,
			vertex_depth(view, vertex.x, vertex.y, surface->height));
		// Marathon textures are stored rotated; swap U/V vs the world axes.
		verts[i].v = (float(vertex.x) + float(surface->origin.x)) / scale;
		verts[i].u = (float(vertex.y) + float(surface->origin.y)) / scale;
	}

	bool blended = tex && TMgr.IsBlended();
	D3D9_DrawWorldPolygon(verts, vertex_count, tex, blended, blended);
}

void RenderRasterize_D3D9::render_node_floor_or_ceiling(clipping_window_data* /*window*/,
	polygon_data* polygon, horizontal_surface_data* surface, bool /*void_present*/,
	bool ceil, RenderStep renderStep)
{
	if (renderStep != kDiffuse)
		return;
	draw_horizontal_surface(view, polygon, surface, ceil);
}

// Draw one wall trapezoid in world space. Posts (p0=left, p1=right) and heights
// (zt geometric top, zb bottom) are already world-space; tex_top_world is the
// texture's vertical anchor in world-z (= the side's nominal top, which can be
// above zt when the panel is clipped, so the texture stays continuous). Shared
// by the vis-tree node path (which unprojects view-frame posts first) and the
// full-level pass (which passes raw map endpoints).
static void draw_wall_quad(view_data* view,
	float p0x, float p0y, float p1x, float p1y,
	float zt, float zb, float tex_top_world,
	world_distance length, side_texture_definition* tex_def,
	int16 transfer_mode, _fixed ambient)
{
	if (!tex_def)
		return;
	shape_descriptor texture = AnimTxtr_Translate(tex_def->texture);
	if (texture == UNONE)
		return;
	if (zt <= zb)
		return;

	TextureManager TMgr;
	IDirect3DTexture9* tex = setup_wall_texture(tex_def->texture, transfer_mode, TMgr);
	if (!tex)
		return; // texture setup failed; TMgr fields are invalid

	float div = float(WORLD_ONE) * TMgr.TileRatio();
	if (transfer_mode == _xfer_2x) div = 2 * float(WORLD_ONE) * TMgr.TileRatio();
	else if (transfer_mode == _xfer_4x) div = 4 * float(WORLD_ONE) * TMgr.TileRatio();
	if (div == 0.0f) div = float(WORLD_ONE);

	int divi = (int)div ? (int)div : WORLD_ONE;
	world_distance x0 = tex_def->x0 % divi;
	world_distance y0 = tex_def->y0 % divi;
	float tOffset = tex_top_world + float(y0);

	// vertices: 0=top-left 1=top-right 2=bottom-right 3=bottom-left.
	D3D9_WorldVertex verts[4];
	float len = float(length);

	verts[0].x = p0x; verts[0].y = p0y; verts[0].z = zt;
	verts[1].x = p1x; verts[1].y = p1y; verts[1].z = zt;
	verts[2].x = p1x; verts[2].y = p1y; verts[2].z = zb;
	verts[3].x = p0x; verts[3].y = p0y; verts[3].z = zb;

	// Marathon textures are stored rotated; swap U/V (horizontal span -> V,
	// vertical span -> U).
	float vL = float(x0) / div;
	float vR = float(x0 + len) / div;
	verts[0].v = vL; verts[0].u = (tOffset - zt) / div;
	verts[1].v = vR; verts[1].u = (tOffset - zt) / div;
	verts[2].v = vR; verts[2].u = (tOffset - zb) / div;
	verts[3].v = vL; verts[3].u = (tOffset - zb) / div;

	for (int i = 0; i < 4; ++i)
		verts[i].color = shaded_vertex_color(view, ambient,
			vertex_depth(view, verts[i].x, verts[i].y, verts[i].z));

	bool blended = tex && TMgr.IsBlended();
	D3D9_DrawWorldPolygon(verts, 4, tex, blended, blended);
}

void RenderRasterize_D3D9::render_node_side(clipping_window_data* /*window*/,
	vertical_surface_data* surface, bool /*void_present*/, RenderStep renderStep)
{
	if (renderStep != kDiffuse)
		return;
	if (!surface->texture_definition)
		return;

	world_distance h = (surface->h1 < surface->hmax) ? surface->h1 : surface->hmax;
	if (h <= surface->h0)
		return;

	// Trapezoid posts (left p0, right p1) are stored in the engine's view-centered
	// frame: rotated by the view yaw and relative to the view origin. Undo both
	// (inverse-rotate, then translate) to get world space matching the
	// floor/ceiling geometry.
	const double trc = 1.0 / double(TRIG_MAGNITUDE);
	float cy = float(trc * cosine_table[view->yaw]);
	float sy = float(trc * sine_table[view->yaw]);
	auto unproject = [&](long_vector2d p, float& wx, float& wy) {
		float px = float(p.i), py = float(p.j);
		wx = px * cy - py * sy + float(view->origin.x);
		wy = px * sy + py * cy + float(view->origin.y);
	};
	float p0x, p0y, p1x, p1y;
	unproject(surface->p0, p0x, p0y);
	unproject(surface->p1, p1x, p1y);

	// Wall heights h0/h1 are relative to the view origin's z (unlike floor/ceiling
	// absolute height), so add view->origin.z to put them in world space.
	float zt = float(h + view->origin.z);
	float zb = float(surface->h0 + view->origin.z);
	float tex_top_world = float(surface->h1 + view->origin.z);

	_fixed ambient = get_light_intensity(surface->lightsource_index) + surface->ambient_delta;

	draw_wall_quad(view, p0x, p0y, p1x, p1y, zt, zb, tex_top_world,
		surface->length, surface->texture_definition, surface->transfer_mode, ambient);
}

void RenderRasterize_D3D9::render_node_object(render_object_data* object,
	bool /*other_side_of_media*/, RenderStep renderStep)
{
	if (renderStep != kDiffuse)
		return;

	rectangle_definition& rect = object->rectangle;
	double depth = (double)rect.depth;
	if (depth <= 0)
		return; // weapons-in-hand handled by the HUD/2D pass

	TextureManager TMgr;
	TMgr.ShapeDesc = rect.ShapeDesc;
	TMgr.LowLevelShape = rect.LowLevelShape;
	TMgr.ShadingTables = rect.shading_tables;
	TMgr.Texture = rect.texture;
	TMgr.TransferMode = rect.transfer_mode;
	TMgr.TransferData = rect.transfer_data;
	TMgr.IsShadeless = (rect.flags & _SHADELESS_BIT) != 0;
	TMgr.TextureType = OGL_Txtr_Inhabitant;
	if (!TMgr.Setup())
		return;
	IDirect3DTexture9* tex = D3D9_GetTexture(TMgr);
	if (!tex)
		return;

	if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
		return;

	// Use the object's native world-space billboard data (set by RenderPlaceObjs
	// for the "new rendering pipeline"): Position is the world location, and
	// WorldLeft/Right/Top/Bottom are the sprite's extents around it. No screen
	// projection inversion needed.
	//
	// Standard Marathon sprite = Y-axis billboard: it stays vertical (world z up)
	// and rotates about z to face the viewer. Horizontal axis = direction from
	// the object to the viewer, projected onto the horizontal plane, rotated 90.
	// Horizontal billboard axis = the camera's right vector projected onto the
	// horizontal plane (matches the engine, which lays sprites out along screen-x
	// = the view yaw's right, not per-object). Keeps sprites parallel to the
	// view plane and vertical (z up).
	float camR[3], camU[3], camF[3];
	D3D9_GetCameraBasis(camR, camU, camF);
	double hrx = camR[0], hry = camR[1];
	double hlen = sqrt(hrx * hrx + hry * hry);
	if (hlen < 1e-6) { hrx = 1; hry = 0; hlen = 1; }
	hrx /= hlen; hry /= hlen;

	double cx = rect.Position.x, cy = rect.Position.y, cz = rect.Position.z;
	double wl = rect.WorldLeft, wr = rect.WorldRight;   // horizontal extents
	double wt = rect.WorldTop, wb = rect.WorldBottom;   // vertical extents (z), engine-exact

	// The decoded sprite occupies a sub-rectangle of a power-of-two-padded
	// texture: texcoord = U_Offset + U_Scale*frac (OpenGL convention). Map the
	// quad to that sub-rect so the sprite fills the billboard. Marathon sprites
	// are stored rotated, so screen-vertical = texture U, screen-horizontal = V.
	float uLo = (float)TMgr.U_Offset, uHi = (float)(TMgr.U_Offset + TMgr.U_Scale);
	float vLo = (float)TMgr.V_Offset, vHi = (float)(TMgr.V_Offset + TMgr.V_Scale);
	float uMin = rect.flip_vertical   ? uHi : uLo;
	float uMax = rect.flip_vertical   ? uLo : uHi;
	float vMin = rect.flip_horizontal ? vHi : vLo;
	float vMax = rect.flip_horizontal ? vLo : vHi;

	unsigned long color = shaded_vertex_color(view, rect.ambient_shade, depth);

	// Corners: top-left, top-right, bottom-right, bottom-left.
	// Horizontal offset = hr * (wl or wr); vertical offset = world z (wt or wb).
	struct C { double h; double z; float u, v; };
	C cc[4] = {
		{ wl, wt, uMin, vMin }, // top-left
		{ wr, wt, uMin, vMax }, // top-right
		{ wr, wb, uMax, vMax }, // bottom-right
		{ wl, wb, uMax, vMin }, // bottom-left
	};
	D3D9_WorldVertex verts[4];
	for (int i = 0; i < 4; ++i)
	{
		verts[i].x = (float)(cx + hrx * cc[i].h);
		verts[i].y = (float)(cy + hry * cc[i].h);
		verts[i].z = (float)(cz + cc[i].z);
		verts[i].u = cc[i].u;
		verts[i].v = cc[i].v;
		verts[i].color = color;
	}

	bool blended = TMgr.IsBlended();
	// Sprites render after the floor in tree order and the engine intends their
	// base to dip into the floor; draw without depth test (the render tree is
	// already back-to-front) so the floor doesn't clip the lower part.
	D3D9_DrawWorldSprite(verts, 4, tex, blended);
}

// ---- full-level geometry pass (for RTX Remix) -------------------------------
//
// Submit the entire static level (floors, ceilings, walls) every frame in
// world space, in addition to the normal vis-tree render, so Remix's real-time
// lights and reflections have geometry that is off-screen / around corners /
// behind the camera (which the camera-visibility walk never emits). The
// rasterized image is unchanged: this draws the same surfaces with the same
// depth, additively.
//
// The wall height/texture logic mirrors the engine's own surface construction
// in RenderRasterize.cpp (render_node walls, ~lines 199-285) but uses absolute
// world heights directly instead of view-relative (h - view->origin.z).

// Draw the sized panels for one side, exactly as the engine's switch on
// side->type does (full / high / low / split), plus the transparent texture.
static void emit_full_level_side(view_data* view, polygon_data* polygon, short edge)
{
	short side_index = polygon->side_indexes[edge];
	if (side_index == NONE)
		return;

	line_data* line = get_line_data(polygon->line_indexes[edge]);
	side_data* side = get_side_data(side_index);

	world_point2d e0 = get_endpoint_data(polygon->endpoint_indexes[edge])->vertex;
	short next = (edge + 1) % polygon->vertex_count;
	world_point2d e1 = get_endpoint_data(polygon->endpoint_indexes[next])->vertex;
	if (e0.x == e1.x && e0.y == e1.y)
		return; // degenerate

	float p0x = float(e0.x), p0y = float(e0.y);
	float p1x = float(e1.x), p1y = float(e1.y);
	world_distance length = line->length;
	_fixed ambient = get_light_intensity(side->primary_lightsource_index) + side->ambient_delta;
	_fixed ambient2 = get_light_intensity(side->secondary_lightsource_index) + side->ambient_delta;

	const world_distance floor_h = polygon->floor_height;
	const world_distance ceil_h = polygon->ceiling_height;

	// Draws one panel clamped to [bottom, top] (absolute world z); tex anchor at
	// tex_top so textures stay continuous when geometric top is clamped.
	auto panel = [&](world_distance top, world_distance bottom, world_distance tex_top,
					 side_texture_definition* td, int16 xfer, _fixed amb) {
		world_distance geo_top = (top < ceil_h) ? top : ceil_h; // hmax clamp
		draw_wall_quad(view, p0x, p0y, p1x, p1y,
			float(geo_top), float(bottom), float(tex_top), length, td, xfer, amb);
	};

	switch (side->type)
	{
		case _full_side:
			panel(ceil_h, floor_h, ceil_h, &side->primary_texture,
				  side->primary_transfer_mode, ambient);
			break;
		case _split_side: /* low side (secondary) first */
			panel(MAX(line->highest_adjacent_floor, floor_h), floor_h,
				  MAX(line->highest_adjacent_floor, floor_h),
				  &side->secondary_texture, side->secondary_transfer_mode, ambient2);
			/* fall through to high side */
		case _high_side:
			panel(ceil_h, MIN(line->lowest_adjacent_ceiling, ceil_h), ceil_h,
				  &side->primary_texture, side->primary_transfer_mode, ambient);
			break;
		case _low_side:
			panel(MAX(line->highest_adjacent_floor, floor_h), floor_h,
				  MAX(line->highest_adjacent_floor, floor_h),
				  &side->primary_texture, side->primary_transfer_mode, ambient);
			break;
		default:
			break;
	}

	if (side->transparent_texture.texture != UNONE)
	{
		_fixed amb_t = get_light_intensity(side->transparent_lightsource_index) + side->ambient_delta;
		panel(line->lowest_adjacent_ceiling,
			  MAX(line->highest_adjacent_floor, floor_h),
			  line->lowest_adjacent_ceiling,
			  &side->transparent_texture, side->transparent_transfer_mode, amb_t);
	}
}

void RenderRasterize_D3D9::render_full_level()
{
	if (!view || !map_polygons)
		return;

	short count = dynamic_world->polygon_count;
	for (short pi = 0; pi < count; ++pi)
	{
		polygon_data* polygon = get_polygon_data(pi);
		if (POLYGON_IS_DETACHED(polygon))
			continue;

		// Floor + ceiling from the polygon's own surfaces.
		horizontal_surface_data fl;
		fl.height = polygon->floor_height;
		fl.lightsource_index = polygon->floor_lightsource_index;
		fl.texture = polygon->floor_texture;
		fl.transfer_mode = polygon->floor_transfer_mode;
		fl.transfer_mode_data = 0;
		fl.origin = polygon->floor_origin;
		draw_horizontal_surface(view, polygon, &fl, false);

		horizontal_surface_data ce;
		ce.height = polygon->ceiling_height;
		ce.lightsource_index = polygon->ceiling_lightsource_index;
		ce.texture = polygon->ceiling_texture;
		ce.transfer_mode = polygon->ceiling_transfer_mode;
		ce.transfer_mode_data = 0;
		ce.origin = polygon->ceiling_origin;
		draw_horizontal_surface(view, polygon, &ce, true);

		// Walls: every edge that has a side (textured), regardless of visibility.
		for (short edge = 0; edge < polygon->vertex_count; ++edge)
			emit_full_level_side(view, polygon, edge);
	}
}

#endif // HAVE_DX9
