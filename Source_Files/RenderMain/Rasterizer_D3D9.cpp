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
#include "D3D9_Textures.h"
#include "OGL_Textures.h"
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

void Rasterizer_D3D9_Class::texture_rectangle(rectangle_definition& r)
{
	// Foreground sprite (weapon-in-hand): the engine supplies a screen-space
	// rect (depth 0). Draw it as a textured screen-space quad on top of the
	// world, no depth test.
	TextureManager TMgr;
	TMgr.ShapeDesc = r.ShapeDesc;
	TMgr.LowLevelShape = r.LowLevelShape;
	TMgr.ShadingTables = r.shading_tables;
	TMgr.Texture = r.texture;
	TMgr.TransferMode = r.transfer_mode;
	TMgr.TransferData = r.transfer_data;
	TMgr.IsShadeless = (r.flags & _SHADELESS_BIT) != 0;
	TMgr.TextureType = OGL_Txtr_WeaponsInHand;
	if (!TMgr.Setup())
		return;
	IDirect3DTexture9* tex = D3D9_GetTexture(TMgr);
	if (!tex)
		return;

	// The weapon rect is in the engine view's coordinate space (view->screen_*),
	// which can differ from the backbuffer size; scale to backbuffer pixels.
	float sx = 1.0f, sy = 1.0f;
	if (view && view->screen_width > 0 && view->screen_height > 0)
	{
		sx = (float)D3D9_BackbufferWidth() / (float)view->screen_width;
		sy = (float)D3D9_BackbufferHeight() / (float)view->screen_height;
	}
	float L = r.x0 * sx, T = r.y0 * sy, R = r.x1 * sx, B = r.y1 * sy;
	if (R <= L || B <= T)
		return;

	// Texture occupies a sub-rect of a power-of-two atlas; sprites are stored
	// rotated (screen-vertical = texture U, screen-horizontal = texture V).
	float uLo = (float)TMgr.U_Offset, uHi = (float)(TMgr.U_Offset + TMgr.U_Scale);
	float vLo = (float)TMgr.V_Offset, vHi = (float)(TMgr.V_Offset + TMgr.V_Scale);
	float uMin = r.flip_vertical   ? uHi : uLo;
	float uMax = r.flip_vertical   ? uLo : uHi;
	float vMin = r.flip_horizontal ? vHi : vLo;
	float vMax = r.flip_horizontal ? vLo : vHi;

	// Brightness from ambient shade (no depth cue for the foreground weapon).
	int sv = r.ambient_shade >> 8;
	if (sv < 0) sv = 0; if (sv > 255) sv = 255;
	unsigned long color = 0xff000000u | (sv << 16) | (sv << 8) | sv;

	bool blended = TMgr.IsBlended();
	// Per-corner UVs matching the (working) world-sprite mapping: screen-vertical
	// follows texture U, screen-horizontal follows texture V. TL,TR,BR,BL.
	const float uv[8] = {
		uMin, vMin,  // TL
		uMin, vMax,  // TR
		uMax, vMax,  // BR
		uMax, vMin,  // BL
	};
	D3D9_DrawScreenSpriteUV(L, T, R, B, 0.0f, uv, tex, color, blended);
}

#endif // HAVE_DX9
