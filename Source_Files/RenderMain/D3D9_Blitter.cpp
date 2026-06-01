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

	Direct3D 9 image blitter implementation.

*/

#include "cseries.h"

#ifdef HAVE_DX9

#include <d3d9.h>
#include "D3D9_Blitter.h"
#include "D3D9_Setup.h"

D3D9_Blitter::D3D9_Blitter()
	: m_texture(nullptr), m_tex_w(0), m_tex_h(0)
{
}

void D3D9_Blitter::Unload()
{
	if (m_texture) { m_texture->Release(); m_texture = nullptr; }
	m_tex_w = m_tex_h = 0;
	Image_Blitter::Unload();
}

D3D9_Blitter::~D3D9_Blitter()
{
	if (m_texture) { m_texture->Release(); m_texture = nullptr; }
}

void D3D9_Blitter::Draw(SDL_Surface* /*dst_surface*/, const Image_Rect& dst, const Image_Rect& src)
{
	if (!Loaded() || !m_surface)
		return;

	// Upload the loaded image to a managed texture once (refreshed if the source
	// changes size). m_surface is RGBA (alpha-capable), keep its alpha.
	if (!D3D9_UploadSurfaceTexture(m_surface, &m_texture, &m_tex_w, &m_tex_h, false))
		return;

	// The source rect (crop_rect) is in *scaled* coordinates (Image_Blitter::
	// Rescale scales crop_rect into m_scaled_src space), so normalize UVs by
	// m_scaled_src, not the unscaled image size.
	const float iw = (m_scaled_src.w > 0) ? m_scaled_src.w : (float)m_tex_w;
	const float ih = (m_scaled_src.h > 0) ? m_scaled_src.h : (float)m_tex_h;
	const float u0 = src.x / iw;
	const float v0 = src.y / ih;
	const float u1 = (src.x + src.w) / iw;
	const float v1 = (src.y + src.h) / ih;

	const unsigned long tint =
		((unsigned long)(tint_color_a * 255.0f) << 24) |
		((unsigned long)(tint_color_r * 255.0f) << 16) |
		((unsigned long)(tint_color_g * 255.0f) << 8) |
		((unsigned long)(tint_color_b * 255.0f));

	D3D9_DrawTexturedQuad(dst.x, dst.y, dst.w, dst.h, u0, v0, u1, v1, m_texture, tint);
}

#endif // HAVE_DX9
