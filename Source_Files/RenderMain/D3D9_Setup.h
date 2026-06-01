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

// Reset the backbuffer to match the window client size if it has changed (the
// device is created at menu resolution and the window grows for gameplay).
void D3D9_EnsureBackbufferSize(int width, int height);

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

// Composite a sub-rectangle of a 32-bit SDL surface onto the already-open D3D9
// scene as a screen-space quad (no Clear/Present). Used to draw the 2D overlays
// (HUD, overhead map, terminal) over the native FFP world before finishing the
// frame. dst* in backbuffer pixels, src* in surface pixels.
// color_key >= 0 (0xRRGGBB) marks transparent pixels (for the sparse Lua HUD
// drawn over a cleared key color); < 0 = fully opaque (classic panel/map).
bool D3D9_BlitSurfaceRegion(struct SDL_Surface* surface,
							int src_x, int src_y, int src_w, int src_h,
							int dst_x, int dst_y, int dst_w, int dst_h,
							bool alpha_blend, long color_key = -1);

// EndScene + Present after the world and its 2D overlays have been drawn into
// the open scene (counterpart to the world Begin in Rasterizer_D3D9::Begin()).
void D3D9_FinishFrame();

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

// Camera right/up/forward in world space (valid after D3D9_SetViewTransforms);
// used to build camera-facing billboards consistent with the view matrix.
void D3D9_GetCameraBasis(float* right, float* up, float* fwd);

// Backbuffer dimensions (for scaling screen-space coordinates).
int D3D9_BackbufferWidth();
int D3D9_BackbufferHeight();

// Set the sub-rectangle of the backbuffer the 3D world renders into (the engine
// view rect; scaled/offset when the HUD is active). Call before render_view.
// (0,0,0,0) restores full-backbuffer rendering.
void D3D9_SetWorldViewport(int x, int y, int w, int h);

// Current world viewport dimensions (= view rect, or backbuffer if unset). Used
// to place screen-space draws (weapon-in-hand) consistently inside the viewport.
int D3D9_WorldViewportWidth();
int D3D9_WorldViewportHeight();
int D3D9_WorldViewportX();
int D3D9_WorldViewportY();

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

// Draw a world-space sprite billboard (alpha-tested cutout, no depth test so
// the floor it stands in does not clip it; tree order handles layering).
void D3D9_DrawWorldSprite(const D3D9_WorldVertex* verts, int count,
						  IDirect3DTexture9* texture, bool blend);

// Draw a textured, alpha-tested screen-space quad (sprites/objects). Screen
// pixel coords; z in [0,1] for depth testing against the world. uv0=top-left,
// uv1=bottom-right corner texture coords. color = 0xAARRGGBB diffuse.
void D3D9_DrawScreenSprite(float x0, float y0, float x1, float y1, float z,
						   float u0, float v0, float u1, float v1,
						   IDirect3DTexture9* texture, unsigned long color, bool blend);

// Like D3D9_DrawScreenSprite but with explicit per-corner texture coords
// (8 floats: TL.u,TL.v, TR.u,TR.v, BR.u,BR.v, BL.u,BL.v) so the texture can be
// rotated/transposed (sprite atlases store frames rotated).
void D3D9_DrawScreenSpriteUV(float x0, float y0, float x1, float y1, float z,
							 const float* uv, IDirect3DTexture9* texture,
							 unsigned long color, bool blend);

// --- GPU HUD primitives (Lua HUD drawn straight to the open backbuffer) ---
// Screen-space XYZRHW, straight alpha blend, no depth, full viewport. Textured
// draws MODULATE the texture by the tint color (0xAARRGGBB). Call HUDBegin once
// before the HUD draws and HUDEnd after.
void D3D9_HUDBegin();
void D3D9_HUDEnd();
void D3D9_DrawColorQuad(float x, float y, float w, float h, unsigned long argb);
void D3D9_DrawTexturedQuad(float x, float y, float w, float h,
						   float u0, float v0, float u1, float v1,
						   struct IDirect3DTexture9* tex, unsigned long argb_tint);
// Pre-built SpriteVertex (XYZRHW|DIFFUSE|TEX1) triangle list, e.g. glyph quads.
void D3D9_DrawTexturedTris(const void* verts, int tri_count,
						   struct IDirect3DTexture9* tex, unsigned long argb_tint);
void D3D9_SetScissor(int x, int y, int w, int h);
void D3D9_DisableScissor();
// Create/refresh a MANAGED ARGB texture from a 32-bit SDL surface (cache held by
// the caller). force_opaque ignores the surface alpha (fonts have no alpha).
struct IDirect3DTexture9* D3D9_UploadSurfaceTexture(struct SDL_Surface* surface,
													struct IDirect3DTexture9** cache,
													int* cache_w, int* cache_h,
													bool force_opaque);
// The SpriteVertex layout for D3D9_DrawTexturedTris callers (XYZRHW|DIFFUSE|TEX1).
struct D3D9_HUDVertex { float x, y, z, rhw; unsigned long color; float u, v; };

// Forward declaration so render code can reach the device without pulling in
// <d3d9.h> everywhere. Defined in D3D9_Setup.cpp.
struct IDirect3DDevice9* D3D9_Device();

#endif // HAVE_DX9

#endif
