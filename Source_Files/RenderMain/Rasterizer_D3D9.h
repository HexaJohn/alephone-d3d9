#ifndef _RASTERIZER_D3D9_CLASS_
#define _RASTERIZER_D3D9_CLASS_
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

	Rasterizer Direct3D 9 fixed-function implementation.

	Draws the engine's already-projected, screen-space render polygons as
	Direct3D 9 fixed-function geometry (pre-transformed XYZRHW vertices), so
	the world is rendered natively by the GPU instead of the software
	rasterizer. This is the basis for the RTX Remix path.

	The engine's render tree hands us 2D screen-space convex polygons plus the
	texture/shading info in polygon_definition / rectangle_definition; we turn
	each into a triangle fan and submit it via the D3D9 device.

*/

#ifdef HAVE_DX9

#include "Rasterizer.h"

class Rasterizer_D3D9_Class : public RasterizerClass
{
public:
	view_data* view = nullptr;

	void SetView(view_data& View) override { view = &View; }

	void Begin() override;
	void End() override;

	void texture_horizontal_polygon(polygon_definition& textured_polygon) override;
	void texture_vertical_polygon(polygon_definition& textured_polygon) override;
	void texture_rectangle(rectangle_definition& textured_rectangle) override;
};

#endif // HAVE_DX9

#endif
