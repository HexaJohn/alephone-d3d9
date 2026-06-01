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

	Direct3D 9 texture cache implementation.

*/

#include "cseries.h"

#ifdef HAVE_DX9

#include <d3d9.h>
#include <map>
#include <cstdio>

#include "D3D9_Textures.h"
#include "D3D9_Setup.h"
#include "OGL_Textures.h"
#include "ImageLoader.h"
#include "Logging.h"

// Cache key: identifies a unique decoded texture image. Collection/CTable/
// Frame/Bitmap come from the TextureManager and uniquely name the source art;
// shadeless is included because it can change the decoded pixels.
struct TexKey
{
	short collection, ctable, frame, bitmap;
	bool shadeless;

	bool operator<(const TexKey& o) const
	{
		if (collection != o.collection) return collection < o.collection;
		if (ctable != o.ctable) return ctable < o.ctable;
		if (frame != o.frame) return frame < o.frame;
		if (bitmap != o.bitmap) return bitmap < o.bitmap;
		return shadeless < o.shadeless;
	}
};

static std::map<TexKey, IDirect3DTexture9*> texture_cache;

void D3D9_FlushTextureCache()
{
	for (auto& kv : texture_cache)
	{
		if (kv.second)
			kv.second->Release();
	}
	texture_cache.clear();
}

// Convert the engine's RGBA (byte order R,G,B,A; little-endian uint32
// 0xAABBGGRR) to D3D9 A8R8G8B8 (0xAARRGGBB) by swapping R and B.
static inline uint32 rgba_to_argb(uint32 p)
{
	return (p & 0xff00ff00u) |
		   ((p & 0x00ff0000u) >> 16) |
		   ((p & 0x000000ffu) << 16);
}

static IDirect3DTexture9* create_texture(const ImageDescriptor* img)
{
	IDirect3DDevice9* dev = D3D9_Device();
	if (!dev || !img || !img->IsPresent())
		return nullptr;

	const int w = img->GetWidth();
	const int h = img->GetHeight();
	if (w <= 0 || h <= 0)
		return nullptr;

	IDirect3DTexture9* tex = nullptr;
	// MANAGED pool survives device loss (driver keeps a system-memory copy), so
	// the cache does not need rebuilding on reset.
	HRESULT hr = dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8,
									D3DPOOL_MANAGED, &tex, nullptr);
	if (FAILED(hr) || !tex)
	{
		logError("D3D9: CreateTexture %dx%d failed (hr=0x%08lx)", w, h, (unsigned long)hr);
		return nullptr;
	}

	D3DLOCKED_RECT locked;
	if (FAILED(tex->LockRect(0, &locked, nullptr, 0)))
	{
		tex->Release();
		return nullptr;
	}

	const uint32* src = img->GetBuffer();
	if (!src)
	{
		tex->UnlockRect(0);
		tex->Release();
		return nullptr;
	}
	uint8* dst_rows = static_cast<uint8*>(locked.pBits);
	for (int y = 0; y < h; ++y)
	{
		uint32* dst = reinterpret_cast<uint32*>(dst_rows + y * locked.Pitch);
		const uint32* srow = src + y * w;
		for (int x = 0; x < w; ++x)
			dst[x] = rgba_to_argb(srow[x]);
	}
	tex->UnlockRect(0);

	return tex;
}

IDirect3DTexture9* D3D9_GetTexture(TextureManager& TMgr)
{
	TexKey key;
	key.collection = TMgr.GetCollection();
	key.ctable = TMgr.GetCTable();
	key.frame = TMgr.GetFrame();
	key.bitmap = TMgr.GetBitmap();
	key.shadeless = TMgr.IsShadeless;

	auto it = texture_cache.find(key);
	if (it != texture_cache.end())
		return it->second;

	IDirect3DTexture9* tex = create_texture(TMgr.GetNormalImage());
	texture_cache[key] = tex; // cache even nullptr to avoid retrying every frame
	return tex;
}

#endif // HAVE_DX9
