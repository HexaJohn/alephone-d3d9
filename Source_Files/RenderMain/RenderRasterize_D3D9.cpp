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

void RenderRasterize_D3D9::render_node_floor_or_ceiling(clipping_window_data* /*window*/,
	polygon_data* polygon, horizontal_surface_data* surface, bool /*void_present*/,
	bool ceil, RenderStep renderStep)
{
	if (renderStep != kDiffuse)
		return;

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

void RenderRasterize_D3D9::render_node_side(clipping_window_data* /*window*/,
	vertical_surface_data* surface, bool /*void_present*/, RenderStep renderStep)
{
	if (renderStep != kDiffuse)
		return;
	if (!surface->texture_definition)
		return;

	shape_descriptor texture = AnimTxtr_Translate(surface->texture_definition->texture);
	if (texture == UNONE)
		return;

	world_distance h = (surface->h1 < surface->hmax) ? surface->h1 : surface->hmax;
	if (h <= surface->h0)
		return;

	TextureManager TMgr;
	IDirect3DTexture9* tex = setup_wall_texture(surface->texture_definition->texture,
												surface->transfer_mode, TMgr);
	if (!tex)
		return; // texture setup failed; TMgr fields are invalid

	_fixed ambient = get_light_intensity(surface->lightsource_index) + surface->ambient_delta;

	float div = float(WORLD_ONE) * TMgr.TileRatio();
	if (surface->transfer_mode == _xfer_2x) div = 2 * float(WORLD_ONE) * TMgr.TileRatio();
	else if (surface->transfer_mode == _xfer_4x) div = 4 * float(WORLD_ONE) * TMgr.TileRatio();
	if (div == 0.0f) div = float(WORLD_ONE);

	// Trapezoid posts (left p0, right p1), top h, bottom h0. p0/p1 are stored in
	// the engine's view-centered frame: rotated by the view yaw and relative to
	// the view origin. Undo both (inverse-rotate, then translate) to get world
	// space that matches the floor/ceiling geometry.
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

	int divi = (int)div ? (int)div : WORLD_ONE;
	world_distance x0 = surface->texture_definition->x0 % divi;
	world_distance y0 = surface->texture_definition->y0 % divi;
	float tOffset = float(surface->h1 + view->origin.z + y0);

	// vertices: 0=top-left 1=top-right 2=bottom-right 3=bottom-left. Wall heights
	// h0/h1 are relative to the view origin's z (unlike floor/ceiling absolute
	// height), so add view->origin.z to put them in world space.
	D3D9_WorldVertex verts[4];
	float zt = float(h + view->origin.z);
	float zb = float(surface->h0 + view->origin.z);
	float len = float(surface->length);

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

void RenderRasterize_D3D9::render_node_object(render_object_data* object,
	bool /*other_side_of_media*/, RenderStep renderStep)
{
	if (renderStep != kDiffuse)
		return;

	// Sprites/objects: not yet ported to native 3D billboards. Skip for now so
	// the world (walls/floors/ceilings) renders cleanly; objects come next.
	(void)object;
}

#endif // HAVE_DX9
