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

	Direct3D 9 setup implementation.

*/

#include "cseries.h"

#ifdef HAVE_DX9

#include <d3d9.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "Logging.h"
#include "D3D9_Setup.h"

// The single device and the D3D object that created it. The backend is a
// singleton (the engine renders one main view), mirroring how OGL_* keeps
// global GL state.
static IDirect3DDevice9* d3d_device = nullptr;
static IDirect3D9* d3d_object = nullptr;
static D3DPRESENT_PARAMETERS present_params = {};
static bool device_lost = false;

// Texture used to blit the engine's software framebuffer / 2D UI to the screen.
// Recreated when the source surface size changes.
static IDirect3DTexture9* blit_texture = nullptr;
static int blit_w = 0, blit_h = 0;

// Pre-transformed (screen-space) textured vertex for the fullscreen blit quad.
struct ScreenVertex
{
	float x, y, z, rhw;
	float u, v;
};
static const DWORD SCREEN_FVF = D3DFVF_XYZRHW | D3DFVF_TEX1;

static void release_blit_texture()
{
	if (blit_texture)
	{
		blit_texture->Release();
		blit_texture = nullptr;
	}
	blit_w = blit_h = 0;
}

IDirect3DDevice9* D3D9_Device()
{
	return d3d_device;
}

bool D3D9_IsActive()
{
	return d3d_device != nullptr;
}

// Pull the Win32 HWND out of the SDL window so D3D9 can render to it.
static HWND get_hwnd(SDL_Window* window)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info))
	{
		logError("D3D9: SDL_GetWindowWMInfo failed: %s", SDL_GetError());
		return nullptr;
	}
	if (info.subsystem != SDL_SYSWM_WINDOWS)
	{
		logError("D3D9: window is not a Win32 window");
		return nullptr;
	}
	return info.info.win.window;
}

static void fill_present_params(HWND hwnd, int width, int height, bool fullscreen, bool vsync)
{
	ZeroMemory(&present_params, sizeof(present_params));
	present_params.Windowed = fullscreen ? FALSE : TRUE;
	present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	present_params.hDeviceWindow = hwnd;
	present_params.BackBufferWidth = width;
	present_params.BackBufferHeight = height;
	// X8R8G8B8 is the safe FFP-friendly backbuffer format (no sRGB, no float)
	// that RTX Remix expects. UNKNOWN is only valid in windowed mode.
	present_params.BackBufferFormat = fullscreen ? D3DFMT_X8R8G8B8 : D3DFMT_UNKNOWN;
	present_params.BackBufferCount = 1;
	present_params.EnableAutoDepthStencil = TRUE;
	present_params.AutoDepthStencilFormat = D3DFMT_D24S8;
	present_params.PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE
												: D3DPRESENT_INTERVAL_IMMEDIATE;
}

bool D3D9_Startup(SDL_Window* window, int width, int height, bool fullscreen, bool vsync)
{
	if (d3d_device)
		D3D9_Shutdown();

	HWND hwnd = get_hwnd(window);
	if (!hwnd)
		return false;

	d3d_object = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d_object)
	{
		logError("D3D9: Direct3DCreate9 failed");
		return false;
	}

	fill_present_params(hwnd, width, height, fullscreen, vsync);

	// SOFTWARE_VERTEXPROCESSING keeps us firmly on the fixed-function path and
	// is the most compatible choice; HARDWARE can be tried later for perf.
	DWORD behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;

	HRESULT hr = d3d_object->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
										  hwnd, behavior, &present_params,
										  &d3d_device);
	if (FAILED(hr))
	{
		logError("D3D9: CreateDevice failed (hr=0x%08lx)", (unsigned long)hr);
		d3d_object->Release();
		d3d_object = nullptr;
		d3d_device = nullptr;
		return false;
	}

	device_lost = false;
	logNote("D3D9: device created (%dx%d, %s)", width, height,
			fullscreen ? "fullscreen" : "windowed");
	return true;
}

void D3D9_Shutdown()
{
	release_blit_texture();
	if (d3d_device)
	{
		d3d_device->Release();
		d3d_device = nullptr;
	}
	if (d3d_object)
	{
		d3d_object->Release();
		d3d_object = nullptr;
	}
	device_lost = false;
}

// Attempt to recover a lost device. Returns true if the device is ready.
static bool handle_device_lost()
{
	HRESULT coop = d3d_device->TestCooperativeLevel();
	if (coop == D3D_OK)
	{
		device_lost = false;
		return true;
	}
	if (coop == D3DERR_DEVICELOST)
	{
		// Not ready to reset yet (e.g. still minimized). Skip this frame.
		return false;
	}
	if (coop == D3DERR_DEVICENOTRESET)
	{
		// Release DEFAULT-pool resources before Reset.
		release_blit_texture();
		HRESULT hr = d3d_device->Reset(&present_params);
		if (SUCCEEDED(hr))
		{
			device_lost = false;
			return true;
		}
		logWarning("D3D9: device Reset failed (hr=0x%08lx)", (unsigned long)hr);
		return false;
	}
	logWarning("D3D9: unexpected TestCooperativeLevel (hr=0x%08lx)", (unsigned long)coop);
	return false;
}

bool D3D9_BeginFrame(unsigned long clear_rgb)
{
	if (!d3d_device)
		return false;

	if (device_lost && !handle_device_lost())
		return false;

	D3DCOLOR color = D3DCOLOR_XRGB((clear_rgb >> 16) & 0xff,
								   (clear_rgb >> 8) & 0xff,
								   clear_rgb & 0xff);

	d3d_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
					  color, 1.0f, 0);

	if (FAILED(d3d_device->BeginScene()))
		return false;

	return true;
}

void D3D9_EndFrame()
{
	if (!d3d_device)
		return;

	d3d_device->EndScene();

	HRESULT hr = d3d_device->Present(nullptr, nullptr, nullptr, nullptr);
	if (hr == D3DERR_DEVICELOST)
		device_lost = true;
}

#include <SDL2/SDL_surface.h>

bool D3D9_Present2D(SDL_Surface* surface)
{
	if (!d3d_device || !surface)
		return false;

	if (device_lost && !handle_device_lost())
		return false;

	const int w = surface->w;
	const int h = surface->h;

	// (Re)create the blit texture if the source size changed. DEFAULT pool +
	// DYNAMIC so we can lock it cheaply every frame; must be released on reset.
	if (!blit_texture || blit_w != w || blit_h != h)
	{
		release_blit_texture();
		HRESULT hr = d3d_device->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC,
											   D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
											   &blit_texture, nullptr);
		if (FAILED(hr))
		{
			logError("D3D9: blit texture creation failed (hr=0x%08lx)", (unsigned long)hr);
			return false;
		}
		blit_w = w;
		blit_h = h;
	}

	// Upload the surface pixels into the texture.
	D3DLOCKED_RECT locked;
	if (FAILED(blit_texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD)))
		return false;

	SDL_LockSurface(surface);
	const uint8_t* src = static_cast<const uint8_t*>(surface->pixels);
	uint8_t* dst = static_cast<uint8_t*>(locked.pBits);
	const int row_bytes = w * 4; // both sides are 32-bit BGRA / A8R8G8B8
	for (int y = 0; y < h; ++y)
		memcpy(dst + y * locked.Pitch, src + y * surface->pitch, row_bytes);
	SDL_UnlockSurface(surface);
	blit_texture->UnlockRect(0);

	// Draw the texture as a fullscreen quad sized to the backbuffer.
	const float bw = static_cast<float>(present_params.BackBufferWidth);
	const float bh = static_cast<float>(present_params.BackBufferHeight);
	// -0.5 texel offset aligns texels to pixels in D3D9.
	const ScreenVertex quad[4] = {
		{ -0.5f,      -0.5f,      0.0f, 1.0f, 0.0f, 0.0f },
		{ bw - 0.5f,  -0.5f,      0.0f, 1.0f, 1.0f, 0.0f },
		{ -0.5f,      bh - 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
		{ bw - 0.5f,  bh - 0.5f,  0.0f, 1.0f, 1.0f, 1.0f },
	};

	d3d_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

	if (FAILED(d3d_device->BeginScene()))
		return false;

	d3d_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	d3d_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	d3d_device->SetRenderState(D3DRS_ZENABLE, FALSE);
	d3d_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	d3d_device->SetTexture(0, blit_texture);
	d3d_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	d3d_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	d3d_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	d3d_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	d3d_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	d3d_device->SetFVF(SCREEN_FVF);
	d3d_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ScreenVertex));

	d3d_device->EndScene();

	HRESULT hr = d3d_device->Present(nullptr, nullptr, nullptr, nullptr);
	if (hr == D3DERR_DEVICELOST)
		device_lost = true;

	return true;
}

#endif // HAVE_DX9
