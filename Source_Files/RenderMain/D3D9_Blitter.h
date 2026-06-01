#ifndef _D3D9_BLITTER_
#define _D3D9_BLITTER_
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

	Direct3D 9 image blitter for the 2D UI / Lua HUD. Parallel to OGL_Blitter:
	uploads the loaded image to a D3D9 texture once and draws it as a screen-space
	textured quad, so the HUD runs on the GPU instead of software-blitting.

*/

#include "cseries.h"
#include "Image_Blitter.h"

#ifdef HAVE_DX9

struct IDirect3DTexture9;

class D3D9_Blitter : public Image_Blitter
{
public:
	D3D9_Blitter();

	void Unload() override;

	// Draw to the open D3D9 backbuffer (dst_surface is ignored). The 2-arg form
	// uses crop_rect as the source sub-rectangle.
	void Draw(SDL_Surface* dst_surface, const Image_Rect& dst, const Image_Rect& src) override;

	~D3D9_Blitter() override;

private:
	IDirect3DTexture9* m_texture;
	int m_tex_w, m_tex_h;
};

#endif // HAVE_DX9

#endif
