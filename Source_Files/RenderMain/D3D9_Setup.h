#ifndef _D3D9_SETUP_
#define _D3D9_SETUP_
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

	Direct3D 9 setup interface.

	Mirrors the role of OGL_Setup for the Direct3D 9 fixed-function backend
	used for the RTX Remix path. Windows-only; guarded by HAVE_DX9.

	This file only manages the device/present lifecycle. Actual rendering
	lives in Rasterizer_D3D9 / RenderRasterize_D3D9.

*/

#ifdef HAVE_DX9

struct SDL_Window;

// Returns whether the Direct3D 9 device is currently created and usable.
bool D3D9_IsActive();

// Create the IDirect3DDevice9 against the given SDL window's native HWND.
// width/height are the backbuffer size; fullscreen selects the present mode.
// Returns true on success. On failure D3D9_IsActive() stays false and the
// caller should fall back to another renderer.
bool D3D9_Startup(SDL_Window* window, int width, int height, bool fullscreen, bool vsync);

// Destroy the device and release the Direct3D object.
void D3D9_Shutdown();

// Begin a frame: handle device-lost/reset, clear the backbuffer to the given
// color (0xRRGGBB), and BeginScene. Returns false if the device is lost and
// could not be reset this frame (caller should skip drawing).
bool D3D9_BeginFrame(unsigned long clear_rgb);

// EndScene and Present the backbuffer.
void D3D9_EndFrame();

// Present a 32-bit SDL surface (the engine's software framebuffer / 2D UI) to
// the D3D9 backbuffer as a fullscreen textured quad, then Present. This lets
// menus and the software-rendered view display through the D3D9 device while
// the native FFP geometry path is built out. Returns false on failure.
struct SDL_Surface;
bool D3D9_Present2D(struct SDL_Surface* surface);

// --- Native fixed-function world rendering (Rasterizer_D3D9) ---

// Begin/end a native world frame: clear the backbuffer + depth, BeginScene /
// EndScene + Present. Distinct from D3D9_Present2D (which blits a surface).
bool D3D9_WorldBegin();
void D3D9_WorldEnd();

// Draw a convex polygon given as screen-space points, as a flat-shaded
// triangle fan in the given color (0xRRGGBB). Used while the world geometry
// is rendered untextured. Points are screen pixel coordinates.
struct D3D9_ScreenPoint { float x, y; };
void D3D9_DrawScreenPolygon(const D3D9_ScreenPoint* points, int count, unsigned long rgb);

// --- True 3D world rendering (RenderRasterize_D3D9) ---

struct view_data;
struct IDirect3DTexture9;

// Build and set the D3D9 world/view/projection transforms from the engine
// view (origin, yaw, pitch, FOV). Call once per frame after D3D9_WorldBegin.
void D3D9_SetViewTransforms(view_data* view);

// A world-space textured/colored vertex.
struct D3D9_WorldVertex
{
	float x, y, z;     // world position
	unsigned long color; // 0xAARRGGBB diffuse (lighting baked per-vertex)
	float u, v;        // texture coordinates
};

// Draw a convex polygon (triangle fan) of world-space vertices with the given
// texture (may be null for untextured). blend selects alpha blending.
void D3D9_DrawWorldPolygon(const D3D9_WorldVertex* verts, int count,
						   IDirect3DTexture9* texture, bool blend, bool alpha_test);

// Forward declaration so render code can reach the device without pulling in
// <d3d9.h> everywhere. Defined in D3D9_Setup.cpp.
struct IDirect3DDevice9* D3D9_Device();

#endif // HAVE_DX9

#endif
