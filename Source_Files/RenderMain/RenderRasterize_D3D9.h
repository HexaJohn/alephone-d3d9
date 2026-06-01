#ifndef _RENDER_RASTERIZE_D3D9_CLASS_
#define _RENDER_RASTERIZE_D3D9_CLASS_
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

	Direct3D 9 fixed-function render-tree rasterizer.

	Mirrors RenderRasterize_Shader but emits native fixed-function 3D geometry
	(world-space vertices + SetTransform matrices) so the scene is real 3D the
	GPU transforms, which is what RTX Remix needs to ray-trace.

*/

#ifdef HAVE_DX9

#include "RenderRasterize.h"

class RenderRasterize_D3D9 : public RenderRasterizerClass
{
public:
	void render_tree() override;

	// Submit the entire static level (floors/ceilings/walls) in world space,
	// in addition to the vis-tree render, so RTX Remix has off-screen geometry
	// for real-time lights and reflections. Call after render_tree().
	void render_full_level();

protected:
	void render_node_floor_or_ceiling(clipping_window_data* window,
		polygon_data* polygon, horizontal_surface_data* surface,
		bool void_present, bool ceil, RenderStep renderStep) override;

	void render_node_side(clipping_window_data* window,
		vertical_surface_data* surface, bool void_present, RenderStep renderStep) override;

	void render_node_object(render_object_data* object,
		bool other_side_of_media, RenderStep renderStep) override;
};

#endif // HAVE_DX9

#endif
