#ifndef _D3D9_TEXTURES_
#define _D3D9_TEXTURES_
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

	Direct3D 9 texture cache. Converts the engine's already-decoded RGBA
	textures (from TextureManager) into IDirect3DTexture9 objects and caches
	them, keyed by collection/table/frame.

*/

#ifdef HAVE_DX9

class TextureManager;
struct IDirect3DTexture9;

// Get (creating + caching on first use) a D3D9 texture for the texture set up
// in TMgr. TMgr.Setup() must already have succeeded. Returns nullptr on
// failure. The returned texture is owned by the cache; do not Release it.
IDirect3DTexture9* D3D9_GetTexture(TextureManager& TMgr);

// Drop all cached textures (call on device loss / shutdown / texture reset).
void D3D9_FlushTextureCache();

#endif // HAVE_DX9

#endif
