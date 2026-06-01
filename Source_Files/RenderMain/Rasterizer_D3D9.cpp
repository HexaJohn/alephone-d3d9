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

*/

#include "cseries.h"

#ifdef HAVE_DX9

#include "Rasterizer_D3D9.h"
#include "D3D9_Setup.h"
#include "scottish_textures.h"

// Map the engine's ambient shade (a _fixed, 0..FIXED_ONE) to a 0..255 gray so
// the untextured world geometry shows depth/lighting variation. This is a
// stand-in until real textures arrive (Step 5).
static unsigned long shade_to_gray(_fixed ambient_shade)
{
	int v = ambient_shade >> 8; // FIXED_ONE (1<<16) -> 256
	if (v < 16) v = 16;          // floor so nothing is pure black
	if (v > 255) v = 255;
	return (static_cast<unsigned long>(v) << 16) |
		   (static_cast<unsigned long>(v) << 8) |
		   static_cast<unsigned long>(v);
}

// Give walls vs floors/ceilings a slight tint so structure is readable while
// untextured: horizontal surfaces (floors/ceilings) lean blue, walls stay gray.
static unsigned long tint(unsigned long gray, bool horizontal)
{
	if (!horizontal)
		return gray;
	int v = gray & 0xff;
	int r = (v * 3) / 4;
	return (static_cast<unsigned long>(r) << 16) |
		   (static_cast<unsigned long>(r) << 8) |
		   static_cast<unsigned long>(v);
}

static void draw_polygon(polygon_definition& poly, bool horizontal)
{
	const int n = poly.vertex_count;
	if (n < 3 || n > 16)
		return;

	D3D9_ScreenPoint pts[16];
	for (int i = 0; i < n; ++i)
	{
		pts[i].x = static_cast<float>(poly.vertices[i].x);
		pts[i].y = static_cast<float>(poly.vertices[i].y);
	}

	const unsigned long color = tint(shade_to_gray(poly.ambient_shade), horizontal);
	D3D9_DrawScreenPolygon(pts, n, color);
}

void Rasterizer_D3D9_Class::Begin()
{
	D3D9_WorldBegin();
}

void Rasterizer_D3D9_Class::End()
{
	D3D9_WorldEnd();
}

void Rasterizer_D3D9_Class::texture_horizontal_polygon(polygon_definition& textured_polygon)
{
	// Floors and ceilings.
	draw_polygon(textured_polygon, true);
}

void Rasterizer_D3D9_Class::texture_vertical_polygon(polygon_definition& textured_polygon)
{
	// Walls.
	draw_polygon(textured_polygon, false);
}

void Rasterizer_D3D9_Class::texture_rectangle(rectangle_definition& textured_rectangle)
{
	// Sprites / objects: draw the screen-space bounding rect as a flat quad for
	// now (textured sprites come in a later step).
	rectangle_definition& r = textured_rectangle;
	D3D9_ScreenPoint pts[4] = {
		{ static_cast<float>(r.x0), static_cast<float>(r.y0) },
		{ static_cast<float>(r.x1), static_cast<float>(r.y0) },
		{ static_cast<float>(r.x1), static_cast<float>(r.y1) },
		{ static_cast<float>(r.x0), static_cast<float>(r.y1) },
	};
	D3D9_DrawScreenPolygon(pts, 4, shade_to_gray(r.ambient_shade));
}

#endif // HAVE_DX9
