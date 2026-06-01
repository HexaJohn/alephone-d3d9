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

	Font handler
	by Loren Petrich,
	December 17, 2000
	
	This is for specifying and working with text fonts

Dec 25, 2000 (Loren Petrich):
	Added OpenGL-rendering support

Dec 31, 2000 (Loren Petrich):
	Switched to a 32-bit intermediate GWorld, so that text antialiasing
	will work properly.

Jan 12, 2001 (Loren Petrich):
	Fixed MacOS version of TextWidth() -- uses current font
*/

#include "cseries.h"

#ifdef HAVE_OPENGL
#include "OGL_Headers.h"
#include "OGL_Blitter.h"
#include "OGL_Render.h"
#endif

#include <math.h>
#include <string.h>
#include "FontHandler.h"

#include "shape_descriptors.h"
#include "screen_drawing.h"
#include "screen.h"

#ifdef HAVE_OPENGL
std::set<FontSpecifier*> *FontSpecifier::m_font_registry = NULL;
#endif

#ifdef HAVE_DX9
#include <d3d9.h>
#ifdef DrawText           // <windows.h> (via d3d9.h) macro clobbers FontSpecifier::DrawText
#undef DrawText
#endif
#include "D3D9_Setup.h"
std::set<FontSpecifier*> *FontSpecifier::m_d3d_font_registry = NULL;
#endif

// MacOS-specific: stuff that gets reused
// static CTabHandle Grays = NULL;

// Font-specifier equality and assignment:

bool FontSpecifier::operator==(FontSpecifier& F)
{
	if (Size != F.Size) return false;
	if (Style != F.Style) return false;
	if (File != F.File) return false;
	return true;
}

FontSpecifier& FontSpecifier::operator=(FontSpecifier& F)
{
	Size = F.Size;
	Style = F.Style;
	File = F.File;
	return *this;
}

FontSpecifier::~FontSpecifier()
{
#ifdef HAVE_OPENGL
	OGL_Reset(false);
#endif
#ifdef HAVE_DX9
	D3D9_Reset(false);
#endif
}

// Initializer: call before using because of difficulties in setting up a proper constructor:

void FontSpecifier::Init()
{
	Info = NULL;
	Update();
#ifdef HAVE_OPENGL
	OGL_Texture = NULL;
#endif
#ifdef HAVE_DX9
	D3D9_Texture = NULL;
#endif
}

void FontSpecifier::Update()
{
	// Clear away
	if (Info) {
		unload_font(Info);
		Info = NULL;
	}
		
	TextSpec Spec;
	Spec.size = Size;
	Spec.style = Style;
	Spec.adjust_height = AdjustLineHeight;
    
	// Simply implements format "#<value>"; may want to generalize this
	if (File[0] == '#') 
	{
		short ID;
		sscanf(File.c_str() +1, "%hd", &ID);
		
		Spec.font = ID;
		if (ID == 4)
		{
			Spec.font = -1;
			Spec.normal = "Monaco";
			Spec.size = Size * 1.34f;
		}
		else if (ID == 22) 
		{
			Spec.font = -1;
			Spec.normal = "Courier Prime";
			Spec.bold = "Courier Prime Bold";
			Spec.oblique = "Courier Prime Italic";
			Spec.bold_oblique = "Courier Prime Bold Italic";
			Spec.adjust_height -= Size * 0.084f;
		}
	}
	else
	{
		Spec.font = -1; // no way to fall back :(
		Spec.normal = File;
	}

	Info = load_font(Spec);
	
	if (Info) {
		Ascent = Info->get_ascent();
		Descent = Info->get_descent();
		Leading = Info->get_leading();
		Height = Ascent + Leading;
		LineSpacing = Ascent + Descent + Leading;
		for (int k=0; k<256; k++)
			Widths[k] = char_width(k, Info, Style);
	} else
		Ascent = Descent = Leading = Height = LineSpacing = 0;
}

// Defined in screen_drawing_sdl.cpp
extern int8 char_width(uint8 c, const sdl_font_info *font, uint16 style);

int FontSpecifier::TextWidth(const char *text)
{
	int width = 0;
	char c;
	if (!text)
		return width;
	while ((c = *text++) != 0)
		width += Widths[static_cast<unsigned char>(c)];
	return width;
}

#ifdef HAVE_OPENGL
// Reset the OpenGL fonts; its arg indicates whether this is for starting an OpenGL session
// (this is to avoid texture and display-list memory leaks and other such things)
void FontSpecifier::OGL_Reset(bool IsStarting)
{
	// Don't delete these if there is no valid texture;
	// that indicates that there are no valid texture and display-list ID's.
	if (!IsStarting && OGL_Texture)
	{
		glDeleteTextures(1,&TxtrID);
		glDeleteLists(DispList,256);
		OGL_Deregister(this);
	}

	// Invalidates whatever texture had been present
	if (OGL_Texture)
	{
		delete[]OGL_Texture;
		OGL_Texture = NULL;
	}
    
    if (!IsStarting)
        return;
	
	// Put some padding around each glyph so as to avoid clipping it
	const int Pad = 1;
	int ascent_p = Ascent + Pad, descent_p = Descent + Pad;
	int widths_p[256];
	for (int i=0; i<256; i++) {
	  widths_p[i] = Widths[i] + 2*Pad;
	}
	// Now for the totals and dimensions
	int TotalWidth = 0;
	for (int k=0; k<256; k++)
		TotalWidth += widths_p[k];
	
	// For an empty font, clear out
	if (TotalWidth <= 0) return;
	
	int GlyphHeight = ascent_p + descent_p;
	
	int EstDim = int(sqrt(static_cast<float>(TotalWidth*GlyphHeight)) + 0.5);
	TxtrWidth = MAX(128, NextPowerOfTwo(EstDim));
	
	// Find the character starting points and counts
	unsigned char CharStarts[256], CharCounts[256];
	int LastLine = 0;
	CharStarts[LastLine] = 0;
	CharCounts[LastLine] = 0;
	short Pos = 0;
	for (int k=0; k<256; k++)
	{
		// Over the edge? If so, then start a new line
		short NewPos = Pos + widths_p[k];
		if (NewPos > TxtrWidth)
		{
			LastLine++;
			CharStarts[LastLine] = k;
			Pos = widths_p[k];
			CharCounts[LastLine] = 1;
		} else {
			Pos = NewPos;
			CharCounts[LastLine]++;
		}
	}
	TxtrHeight = MAX(128, NextPowerOfTwo(GlyphHeight*(LastLine+1)));
	
	// Render the font glyphs into the SDL surface
	SDL_Surface *FontSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, TxtrWidth, TxtrHeight, 32, 0xff0000, 0x00ff00, 0x0000ff, 0);
	if (FontSurface == NULL)
		return;

	// Set background to black
	SDL_FillRect(FontSurface, NULL, SDL_MapRGB(FontSurface->format, 0, 0, 0));
	Uint32 White = SDL_MapRGB(FontSurface->format, 0xFF, 0xFF, 0xFF);
	
	// Copy to surface
	for (int k = 0; k <= LastLine; k++)
	{
		char Which = CharStarts[k];
		int VPos = (k * GlyphHeight) + ascent_p;
		int HPos = Pad;
		for (int m = 0; m < CharCounts[k]; m++)
		{
		  
		  ::draw_text(FontSurface, &Which, 1, HPos, VPos, White, Info, Style);
		  HPos += widths_p[(unsigned char) (Which++)];
		}
	}
 	
 	// Non-MacOS-specific: allocate the texture buffer
 	// Its format is LA 88, where L is the luminosity and A is the alpha channel
 	// The font value will go into A.
 	OGL_Texture = new uint8[2*GetTxtrSize()];
	
	// Copy the SDL surface into the OpenGL texture
	uint8 *PixBase = (uint8 *)FontSurface->pixels;
	int Stride = FontSurface->pitch;
 	
 	for (int k=0; k<TxtrHeight; k++)
 	{
 		uint8 *SrcPxl = PixBase + k*Stride + 1;	// Use one of the middle channels (red or green or blue)
 		uint8 *DstPxl = OGL_Texture + 2*k*TxtrWidth;
 		for (int m=0; m<TxtrWidth; m++)
 		{
 			*(DstPxl++) = 0xff;	// Base color: white (will be modified with glColorxxx())
 			*(DstPxl++) = *SrcPxl;
 			SrcPxl += 4;
  		}
 	}
	
	// Clean up
	SDL_FreeSurface(FontSurface);
	
	// OpenGL stuff starts here 	
 	// Load texture
 	glGenTextures(1,&TxtrID);
 	glBindTexture(GL_TEXTURE_2D,TxtrID);
	OGL_Register(this);
 	
 	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, NearFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, TxtrWidth, TxtrHeight,
		0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, OGL_Texture);
 	
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	
 	// Allocate and create display lists of rendering commands
 	DispList = glGenLists(256);
 	GLfloat TWidNorm = GLfloat(1)/TxtrWidth;
 	GLfloat THtNorm = GLfloat(1)/TxtrHeight;
 	for (int k=0; k<=LastLine; k++)
 	{
 		unsigned char Which = CharStarts[k];
 		GLfloat Top = k*(THtNorm*GlyphHeight);
 		GLfloat Bottom = (k+1)*(THtNorm*GlyphHeight);
 		int Pos = 0;
 		for (int m=0; m<CharCounts[k]; m++)
 		{
 			short Width = widths_p[Which];
 			int NewPos = Pos + Width;
 			GLfloat Left = TWidNorm*Pos;
 			GLfloat Right = TWidNorm*NewPos;
 			
 			glNewList(DispList + Which, GL_COMPILE);
 			
 			// Move to the current glyph's (padded) position
 			glTranslatef(-Pad,0,0);
 			
 			// Draw the glyph rectangle
			OGL_RenderTexturedRect(0, -ascent_p, Width, descent_p + ascent_p,
								   Left, Top, Right, Bottom);
			
			// Move to the next glyph's position
			glTranslated(Width-Pad,0,0);
			
 			glEndList();
 			
 			// For next one
 			Pos = NewPos;
 			Which++;
 		}
 	}
}


// Renders a C-style string in OpenGL.
// assumes screen coordinates and that the left baseline point is at (0,0).
// Alters the modelview matrix so that the next characters will be drawn at the proper place.
// One can surround it with glPushMatrix() and glPopMatrix() to remember the original.
void FontSpecifier::OGL_Render(const char *Text)
{
	// Bug out if no texture to render
	if (!OGL_Texture)
	{
        OGL_Reset(true);
        if (!OGL_Texture) return;
	}
	
	glPushAttrib(GL_ENABLE_BIT);
	
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

	glBindTexture(GL_TEXTURE_2D,TxtrID);
	
	size_t Len = MIN(strlen(Text),255);
	for (size_t k=0; k<Len; k++)
	{
		unsigned char c = Text[k];
		glCallList(DispList+c);
	}
	
	glPopAttrib();
}


// Renders text a la _draw_screen_text() (see screen_drawing.h), with
// alignment and wrapping. Modelview matrix is unaffected.
void FontSpecifier::OGL_DrawText(const char *text, const screen_rectangle &r, short flags)
{
	// Copy the text to draw
	char text_to_draw[256];
	strncpy(text_to_draw, text, 256);
	text_to_draw[255] = 0;

	// Check for wrapping, and if it occurs, be recursive
	if (flags & _wrap_text) {
		int last_non_printing_character = 0, text_width = 0;
		unsigned count = 0;
		auto len = strlen(text_to_draw);
		while (count < len && text_width < RECTANGLE_WIDTH(&r)) {
			text_width += CharWidth(text_to_draw[count]);
			if (text_to_draw[count] == ' ')
				last_non_printing_character = count;
			count++;
		}
		
		if( count != len) {
			char remaining_text_to_draw[256];
			
			// If we ever have to wrap text, we can't also center vertically. Sorry.
			flags &= ~_center_vertical;
			flags |= _top_justified;
			
			// Pass the rest of it back in, recursively, on the next line
			memcpy(remaining_text_to_draw, text_to_draw + last_non_printing_character + 1, strlen(text_to_draw + last_non_printing_character + 1) + 1);
	
			screen_rectangle new_destination = r;
			new_destination.top += LineSpacing;
			OGL_DrawText(remaining_text_to_draw, new_destination, flags);
	
			// Now truncate our text to draw
			text_to_draw[last_non_printing_character] = 0;
		}
	}

	// Truncate text if necessary
	int t_width = TextWidth(text_to_draw);
	if (t_width > RECTANGLE_WIDTH(&r)) {
		int width = 0;
		int num = 0;
		char c, *p = text_to_draw;
		while ((c = *p++) != 0) {
			width += CharWidth(c);
			if (width > RECTANGLE_WIDTH(&r))
				break;
			num++;
		}
		text_to_draw[num] = 0;
		t_width = TextWidth(text_to_draw);
	}


	// Horizontal positioning
	int x, y;
	if (flags & _center_horizontal)
		x = r.left + ((RECTANGLE_WIDTH(&r) - t_width) / 2);
	else if (flags & _right_justified)
		x = r.right - t_width;
	else
		x = r.left;

	// Vertical positioning
	if (flags & _center_vertical) {
		if (Height > RECTANGLE_HEIGHT(&r))
			y = r.top;
		else {
			y = r.bottom;
			int offset = RECTANGLE_HEIGHT(&r) - Height;
			y -= (offset / 2) + (offset & 1) + 1;
		}
	} else if (flags & _top_justified) {
		if (Height > RECTANGLE_HEIGHT(&r))
			y = r.bottom;
		else
			y = r.top + Height;
	} else
		y = r.bottom;

	// Draw text
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glTranslated(x, y, 0);
	OGL_Render(text_to_draw);
	glPopMatrix();
}

void FontSpecifier::OGL_ResetFonts(bool IsStarting)
{
    if (!m_font_registry)
        return;
    
	std::set<FontSpecifier*>::iterator it;
	if (IsStarting)
	{
		for (it = m_font_registry->begin();
			 it != m_font_registry->end();
			 ++it)
			(*it)->OGL_Reset(IsStarting);
	}
	else
	{
		for (it = m_font_registry->begin();
			 it != m_font_registry->end();
			 it = m_font_registry->begin())
			(*it)->OGL_Reset(IsStarting);
	}
}

void FontSpecifier::OGL_Register(FontSpecifier *F)
{
	if (!m_font_registry)
		m_font_registry = new std::set<FontSpecifier*>;
	m_font_registry->insert(F);
}

void FontSpecifier::OGL_Deregister(FontSpecifier *F)
{
	if (m_font_registry)
		m_font_registry->erase(F);
	// we could delete registry here, but why bother?
}

#endif // def HAVE_OPENGL


#ifdef HAVE_DX9
// Build a glyph-atlas texture and per-glyph UV rects for Direct3D 9 text. This
// mirrors OGL_Reset: it lays out all 256 glyphs into a power-of-two surface,
// then uploads it as a D3D9 texture (white RGB, glyph coverage in alpha) so
// D3D9_Render can draw each character as a tinted textured quad.
void FontSpecifier::D3D9_Reset(bool IsStarting)
{
	if (!IsStarting && D3D9_Texture)
	{
		D3D9_Texture->Release();
		D3D9_Texture = NULL;
		D3D9_Deregister(this);
	}
	if (!IsStarting)
		return;
	if (D3D9_Texture)   // already built
		return;

	const int Pad = 1;
	int ascent_p = Ascent + Pad, descent_p = Descent + Pad;
	int widths_p[256];
	for (int i = 0; i < 256; i++)
		widths_p[i] = Widths[i] + 2 * Pad;

	int TotalWidth = 0;
	for (int k = 0; k < 256; k++)
		TotalWidth += widths_p[k];
	if (TotalWidth <= 0)
		return;

	int GlyphHeight = ascent_p + descent_p;
	int EstDim = int(sqrt(static_cast<float>(TotalWidth * GlyphHeight)) + 0.5);
	int TW = MAX(128, NextPowerOfTwo(EstDim));

	unsigned char CharStarts[256], CharCounts[256];
	int LastLine = 0;
	CharStarts[0] = 0; CharCounts[0] = 0;
	short Pos = 0;
	for (int k = 0; k < 256; k++)
	{
		short NewPos = Pos + widths_p[k];
		if (NewPos > TW)
		{
			LastLine++;
			CharStarts[LastLine] = k;
			Pos = widths_p[k];
			CharCounts[LastLine] = 1;
		}
		else { Pos = NewPos; CharCounts[LastLine]++; }
	}
	int TH = MAX(128, NextPowerOfTwo(GlyphHeight * (LastLine + 1)));

	SDL_Surface *FontSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, TW, TH, 32,
													0xff0000, 0x00ff00, 0x0000ff, 0xff000000);
	if (!FontSurface)
		return;
	SDL_FillRect(FontSurface, NULL, SDL_MapRGBA(FontSurface->format, 0, 0, 0, 0));
	Uint32 White = SDL_MapRGBA(FontSurface->format, 0xFF, 0xFF, 0xFF, 0xFF);

	for (int k = 0; k <= LastLine; k++)
	{
		unsigned char Which = CharStarts[k];
		int VPos = (k * GlyphHeight) + ascent_p;
		int HPos = Pad;
		for (int m = 0; m < CharCounts[k]; m++)
		{
			::draw_text(FontSurface, (char*)&Which, 1, HPos, VPos, White, Info, Style);
			HPos += widths_p[Which++];
		}
	}

	// draw_text wrote opaque white glyphs on a transparent surface; force the
	// alpha to follow luminance so the atlas is a coverage mask.
	SDL_LockSurface(FontSurface);
	for (int y = 0; y < TH; y++)
	{
		uint32_t *px = (uint32_t*)((uint8_t*)FontSurface->pixels + y * FontSurface->pitch);
		for (int x = 0; x < TW; x++)
		{
			uint32_t p = px[x];
			uint8_t lum = (uint8_t)(p & 0xFF); // blue channel = coverage
			px[x] = 0x00FFFFFFu | ((uint32_t)lum << 24);
		}
	}
	SDL_UnlockSurface(FontSurface);

	// Per-glyph UV rects.
	const float TWidNorm = 1.0f / TW, THtNorm = 1.0f / TH;
	for (int k = 0; k <= LastLine; k++)
	{
		unsigned char Which = CharStarts[k];
		float Top = k * (THtNorm * GlyphHeight);
		float Bottom = (k + 1) * (THtNorm * GlyphHeight);
		int p = 0;
		for (int m = 0; m < CharCounts[k]; m++)
		{
			short Width = widths_p[Which];
			int NewPos = p + Width;
			D3D9_GlyphU0[Which] = TWidNorm * p;
			D3D9_GlyphU1[Which] = TWidNorm * NewPos;
			D3D9_GlyphV0[Which] = Top;
			D3D9_GlyphV1[Which] = Bottom;
			p = NewPos;
			Which++;
		}
	}
	D3D9_TxtrW = TW; D3D9_TxtrH = TH;
	D3D9_AscentP = ascent_p; D3D9_DescentP = descent_p;

	int tw = 0, th = 0;
	D3D9_UploadSurfaceTexture(FontSurface, &D3D9_Texture, &tw, &th, false);
	SDL_FreeSurface(FontSurface);
	if (D3D9_Texture)
		D3D9_Register(this);
}

// Render a string as a batch of glyph quads. (x,y) is the top-left baseline
// origin (matching OGL_Render, which translates to (x, y + Height*scale) and
// draws each glyph from its baseline). tint is 0xAARRGGBB.
void FontSpecifier::D3D9_Render(const char *Text, float x, float y, float scale, unsigned long tint)
{
	if (!D3D9_Texture)
	{
		D3D9_Reset(true);
		if (!D3D9_Texture)
			return;
	}
	if (!Text || !*Text)
		return;

	const int Pad = 1;
	const int GlyphH = D3D9_AscentP + D3D9_DescentP;
	// Baseline: OGL_Render draws glyphs with the rect [0,-ascent_p]..[Width,descent].
	// We position the pen at (x, y + Ascent*scale) so the top of the text is at y.
	float penX = x;
	const float baseY = y + (float)Ascent * scale;

	size_t Len = strlen(Text);
	if (Len > 255) Len = 255;

	D3D9_HUDVertex verts[255 * 6];
	int vi = 0;
	for (size_t k = 0; k < Len; k++)
	{
		unsigned char c = (unsigned char)Text[k];
		short w = Widths[c];
		float gw = (float)(w + 2 * Pad) * scale;
		float gh = (float)GlyphH * scale;
		// glyph quad top-left at (penX - Pad*scale, baseY - ascent_p*scale)
		float gx = penX - (float)Pad * scale;
		float gy = baseY - (float)D3D9_AscentP * scale;
		float u0 = D3D9_GlyphU0[c], u1 = D3D9_GlyphU1[c];
		float v0 = D3D9_GlyphV0[c], v1 = D3D9_GlyphV1[c];
		D3DCOLOR col = (D3DCOLOR)tint;
		float x0 = gx - 0.5f, y0 = gy - 0.5f, x1 = gx + gw - 0.5f, y1 = gy + gh - 0.5f;
		D3D9_HUDVertex q0 = { x0, y0, 0, 1, (unsigned long)col, u0, v0 };
		D3D9_HUDVertex q1 = { x1, y0, 0, 1, (unsigned long)col, u1, v0 };
		D3D9_HUDVertex q2 = { x1, y1, 0, 1, (unsigned long)col, u1, v1 };
		D3D9_HUDVertex q3 = { x0, y1, 0, 1, (unsigned long)col, u0, v1 };
		verts[vi++] = q0; verts[vi++] = q1; verts[vi++] = q2;
		verts[vi++] = q0; verts[vi++] = q2; verts[vi++] = q3;
		penX += (float)w * scale;
	}
	if (vi > 0)
		D3D9_DrawTexturedTris(verts, vi / 3, D3D9_Texture, tint);
}

void FontSpecifier::D3D9_ResetFonts(bool IsStarting)
{
	if (!m_d3d_font_registry)
		return;
	std::set<FontSpecifier*>::iterator it;
	if (IsStarting)
		for (it = m_d3d_font_registry->begin(); it != m_d3d_font_registry->end(); ++it)
			(*it)->D3D9_Reset(IsStarting);
	else
		for (it = m_d3d_font_registry->begin(); it != m_d3d_font_registry->end(); it = m_d3d_font_registry->begin())
			(*it)->D3D9_Reset(IsStarting);
}

void FontSpecifier::D3D9_Register(FontSpecifier *F)
{
	if (!m_d3d_font_registry)
		m_d3d_font_registry = new std::set<FontSpecifier*>;
	m_d3d_font_registry->insert(F);
}

void FontSpecifier::D3D9_Deregister(FontSpecifier *F)
{
	if (m_d3d_font_registry)
		m_d3d_font_registry->erase(F);
}
#endif // def HAVE_DX9


// Draw text without worrying about OpenGL vs. SDL mode.
int FontSpecifier::DrawText(SDL_Surface *s, const char *text, int x, int y, uint32 pixel, bool utf8)
{
	if (!s)
		return 0;
	if (s == MainScreenSurface() && MainScreenIsOpenGL())
		return draw_text(s, text, x, y, pixel, this->Info, this->Style, utf8);

#ifdef HAVE_OPENGL
		
	uint8 r, g, b;
	SDL_GetRGB(pixel, s->format, &r, &g, &b);
	glColor4ub(r, g, b, 255);
	
	// draw into both buffers
	for (int i = 0; i < 2; i++)
	{
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glTranslatef(x, y, 0);
		this->OGL_Render(text);
		glPopMatrix();
		MainScreenSwap();
	}
	return 1;
#else
	return 0;
#endif
}
