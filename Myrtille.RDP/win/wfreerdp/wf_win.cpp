/*
   FreeRDP: A Remote Desktop Protocol client.

   Copyright (c) 2009-2011 Jay Sorg
   Copyright (c) 2010-2011 Vic Lee

   Myrtille: A native HTML4/5 Remote Desktop Protocol client.

   Copyright (c) 2014-2016 Cedric Coste

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "gdi_color.h"
#include "wf_color.h"
#include "wf_win.h"
#include "wf_event.h"

#pragma region Myrtille
#include <sstream>
#include <string>
#include <vector>

#include <GdiPlus.h>
#pragma comment(lib, "gdiplus")
using namespace Gdiplus;

#include "webp/encode.h"
#pragma comment(lib, "libwebp.lib")
//const char *webpFilename;	// debug

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#pragma comment(lib, "libeay32.lib")
#pragma comment(lib, "ssleay32.lib")

// consolidated region update
int imageIdx = 0;
int	imageLeft = -1;
int	imageTop = -1;
int	imageRight = -1;
int	imageBottom = -1;

DWORD imageCountLastCheck = GetTickCount();
int imageCountPerSec = 0;
#define MAX_IMAGE_COUNT_PER_SECOND 0	// ips throttling; pros: less images to generate = lower server cpu / faster; cons: skipping some images may result in display inconsistencies. 0 to disable

#define INPUTS_PIPE_BUFFER_SIZE 131072	// 128 KB
#define IMAGES_PIPE_BUFFER_SIZE 1048576	// 1024 KB

std::string getCurrentTime();
std::wstring s2ws(const std::string& s);
HANDLE createRemoteSessionPipe(wfInfo * wfi, std::string pipeName);
std::string createRemoteSessionDirectory(wfInfo * wfi);
void SendMouseInput(wfInfo * wfi, std::string input, int positionIdx, uint16 flags);
void processImage(wfInfo* wfi, Gdiplus::Bitmap* bmp, bool fullscreen);
void saveImage(wfInfo * wfi, Gdiplus::Bitmap* bmp, int idx, int format, int quality, bool fullscreen);
void sendImage(wfInfo * wfi, Gdiplus::Bitmap* bmp, int idx, int posX, int posY, int width, int height, int format, int quality, IStream* stream, int size, bool fullscreen);
char *unbase64(char *b64buffer, int length);

void WebPEncoder(wfInfo* wfi, Gdiplus::Bitmap* bmp, IStream* stream, float quality, bool fullscreen);
static int WebPWriter(const uint8_t* data, size_t data_size, const WebPPicture* const pic);

DWORD WINAPI ProcessInputsPipe(LPVOID lpParameter);
DWORD WINAPI SendFullscreen(LPVOID lpParameter);
#pragma endregion

extern LPCTSTR g_wnd_class_name;
extern HINSTANCE g_hInstance;
extern HCURSOR g_default_cursor;

/* See http://msdn.microsoft.com/en-us/library/dd145130/ and @msdn{cc241583} */
static const DWORD rop3_code_table[] =
{
	0x00000042, // 0
	0x00010289, // DPSoon
	0x00020C89, // DPSona
	0x000300AA, // PSon
	0x00040C88, // SDPona
	0x000500A9, // DPon
	0x00060865, // PDSxnon
	0x000702C5, // PDSaon
	0x00080F08, // SDPnaa
	0x00090245, // PDSxon
	0x000A0329, // DPna
	0x000B0B2A, // PSDnaon
	0x000C0324, // SPna
	0x000D0B25, // PDSnaon
	0x000E08A5, // PDSonon
	0x000F0001, // Pn
	0x00100C85, // PDSona
	0x001100A6, // DSon
	0x00120868, // SDPxnon
	0x001302C8, // SDPaon
	0x00140869, // DPSxnon
	0x001502C9, // DPSaon
	0x00165CCA, // PSDPSanaxx
	0x00171D54, // SSPxDSxaxn
	0x00180D59, // SPxPDxa
	0x00191CC8, // SDPSanaxn
	0x001A06C5, // PDSPaox
	0x001B0768, // SDPSxaxn
	0x001C06CA, // PSDPaox
	0x001D0766, // DSPDxaxn
	0x001E01A5, // PDSox
	0x001F0385, // PDSoan
	0x00200F09, // DPSnaa
	0x00210248, // SDPxon
	0x00220326, // DSna
	0x00230B24, // SPDnaon
	0x00240D55, // SPxDSxa
	0x00251CC5, // PDSPanaxn
	0x002606C8, // SDPSaox
	0x00271868, // SDPSxnox
	0x00280369, // DPSxa
	0x002916CA, // PSDPSaoxxn
	0x002A0CC9, // DPSana
	0x002B1D58, // SSPxPDxaxn
	0x002C0784, // SPDSoax
	0x002D060A, // PSDnox
	0x002E064A, // PSDPxox
	0x002F0E2A, // PSDnoan
	0x0030032A, // PSna
	0x00310B28, // SDPnaon
	0x00320688, // SDPSoox
	0x00330008, // Sn
	0x003406C4, // SPDSaox
	0x00351864, // SPDSxnox
	0x003601A8, // SDPox
	0x00370388, // SDPoan
	0x0038078A, // PSDPoax
	0x00390604, // SPDnox
	0x003A0644, // SPDSxox
	0x003B0E24, // SPDnoan
	0x003C004A, // PSx
	0x003D18A4, // SPDSonox
	0x003E1B24, // SPDSnaox
	0x003F00EA, // PSan
	0x00400F0A, // PSDnaa
	0x00410249, // DPSxon
	0x00420D5D, // SDxPDxa
	0x00431CC4, // SPDSanaxn
	0x00440328, // SDna
	0x00450B29, // DPSnaon
	0x004606C6, // DSPDaox
	0x0047076A, // PSDPxaxn
	0x00480368, // SDPxa
	0x004916C5, // PDSPDaoxxn
	0x004A0789, // DPSDoax
	0x004B0605, // PDSnox
	0x004C0CC8, // SDPana
	0x004D1954, // SSPxDSxoxn
	0x004E0645, // PDSPxox
	0x004F0E25, // PDSnoan
	0x00500325, // PDna
	0x00510B26, // DSPnaon
	0x005206C9, // DPSDaox
	0x00530764, // SPDSxaxn
	0x005408A9, // DPSonon
	0x00550009, // Dn
	0x005601A9, // DPSox
	0x00570389, // DPSoan
	0x00580785, // PDSPoax
	0x00590609, // DPSnox
	0x005A0049, // DPx
	0x005B18A9, // DPSDonox
	0x005C0649, // DPSDxox
	0x005D0E29, // DPSnoan
	0x005E1B29, // DPSDnaox
	0x005F00E9, // DPan
	0x00600365, // PDSxa
	0x006116C6, // DSPDSaoxxn
	0x00620786, // DSPDoax
	0x00630608, // SDPnox
	0x00640788, // SDPSoax
	0x00650606, // DSPnox
	0x00660046, // DSx
	0x006718A8, // SDPSonox
	0x006858A6, // DSPDSonoxxn
	0x00690145, // PDSxxn
	0x006A01E9, // DPSax
	0x006B178A, // PSDPSoaxxn
	0x006C01E8, // SDPax
	0x006D1785, // PDSPDoaxxn
	0x006E1E28, // SDPSnoax
	0x006F0C65, // PDSxnan
	0x00700CC5, // PDSana
	0x00711D5C, // SSDxPDxaxn
	0x00720648, // SDPSxox
	0x00730E28, // SDPnoan
	0x00740646, // DSPDxox
	0x00750E26, // DSPnoan
	0x00761B28, // SDPSnaox
	0x007700E6, // DSan
	0x007801E5, // PDSax
	0x00791786, // DSPDSoaxxn
	0x007A1E29, // DPSDnoax
	0x007B0C68, // SDPxnan
	0x007C1E24, // SPDSnoax
	0x007D0C69, // DPSxnan
	0x007E0955, // SPxDSxo
	0x007F03C9, // DPSaan
	0x008003E9, // DPSaa
	0x00810975, // SPxDSxon
	0x00820C49, // DPSxna
	0x00831E04, // SPDSnoaxn
	0x00840C48, // SDPxna
	0x00851E05, // PDSPnoaxn
	0x008617A6, // DSPDSoaxx
	0x008701C5, // PDSaxn
	0x008800C6, // DSa
	0x00891B08, // SDPSnaoxn
	0x008A0E06, // DSPnoa
	0x008B0666, // DSPDxoxn
	0x008C0E08, // SDPnoa
	0x008D0668, // SDPSxoxn
	0x008E1D7C, // SSDxPDxax
	0x008F0CE5, // PDSanan
	0x00900C45, // PDSxna
	0x00911E08, // SDPSnoaxn
	0x009217A9, // DPSDPoaxx
	0x009301C4, // SPDaxn
	0x009417AA, // PSDPSoaxx
	0x009501C9, // DPSaxn
	0x00960169, // DPSxx
	0x0097588A, // PSDPSonoxx
	0x00981888, // SDPSonoxn
	0x00990066, // DSxn
	0x009A0709, // DPSnax
	0x009B07A8, // SDPSoaxn
	0x009C0704, // SPDnax
	0x009D07A6, // DSPDoaxn
	0x009E16E6, // DSPDSaoxx
	0x009F0345, // PDSxan
	0x00A000C9, // DPa
	0x00A11B05, // PDSPnaoxn
	0x00A20E09, // DPSnoa
	0x00A30669, // DPSDxoxn
	0x00A41885, // PDSPonoxn
	0x00A50065, // PDxn
	0x00A60706, // DSPnax
	0x00A707A5, // PDSPoaxn
	0x00A803A9, // DPSoa
	0x00A90189, // DPSoxn
	0x00AA0029, // D
	0x00AB0889, // DPSono
	0x00AC0744, // SPDSxax
	0x00AD06E9, // DPSDaoxn
	0x00AE0B06, // DSPnao
	0x00AF0229, // DPno
	0x00B00E05, // PDSnoa
	0x00B10665, // PDSPxoxn
	0x00B21974, // SSPxDSxox
	0x00B30CE8, // SDPanan
	0x00B4070A, // PSDnax
	0x00B507A9, // DPSDoaxn
	0x00B616E9, // DPSDPaoxx
	0x00B70348, // SDPxan
	0x00B8074A, // PSDPxax
	0x00B906E6, // DSPDaoxn
	0x00BA0B09, // DPSnao
	0x00BB0226, // DSno
	0x00BC1CE4, // SPDSanax
	0x00BD0D7D, // SDxPDxan
	0x00BE0269, // DPSxo
	0x00BF08C9, // DPSano
	0x00C000CA, // PSa
	0x00C11B04, // SPDSnaoxn
	0x00C21884, // SPDSonoxn
	0x00C3006A, // PSxn
	0x00C40E04, // SPDnoa
	0x00C50664, // SPDSxoxn
	0x00C60708, // SDPnax
	0x00C707AA, // PSDPoaxn
	0x00C803A8, // SDPoa
	0x00C90184, // SPDoxn
	0x00CA0749, // DPSDxax
	0x00CB06E4, // SPDSaoxn
	0x00CC0020, // S (SRCCOPY)
	0x00CD0888, // SDPono
	0x00CE0B08, // SDPnao
	0x00CF0224, // SPno
	0x00D00E0A, // PSDnoa
	0x00D1066A, // PSDPxoxn
	0x00D20705, // PDSnax
	0x00D307A4, // SPDSoaxn
	0x00D41D78, // SSPxPDxax
	0x00D50CE9, // DPSanan
	0x00D616EA, // PSDPSaoxx
	0x00D70349, // DPSxan
	0x00D80745, // PDSPxax
	0x00D906E8, // SDPSaoxn
	0x00DA1CE9, // DPSDanax
	0x00DB0D75, // SPxDSxan
	0x00DC0B04, // SPDnao
	0x00DD0228, // SDno
	0x00DE0268, // SDPxo
	0x00DF08C8, // SDPano
	0x00E003A5, // PDSoa
	0x00E10185, // PDSoxn
	0x00E20746, // DSPDxax
	0x00E306EA, // PSDPaoxn
	0x00E40748, // SDPSxax
	0x00E506E5, // PDSPaoxn
	0x00E61CE8, // SDPSanax
	0x00E70D79, // SPxPDxan
	0x00E81D74, // SSPxDSxax
	0x00E95CE6, // DSPDSanaxxn
	0x00EA02E9, // DPSao
	0x00EB0849, // DPSxno
	0x00EC02E8, // SDPao
	0x00ED0848, // SDPxno
	0x00EE0086, // DSo
	0x00EF0A08, // SDPnoo
	0x00F00021, // P
	0x00F10885, // PDSono
	0x00F20B05, // PDSnao
	0x00F3022A, // PSno
	0x00F40B0A, // PSDnao
	0x00F50225, // PDno
	0x00F60265, // PDSxo
	0x00F708C5, // PDSano
	0x00F802E5, // PDSao
	0x00F90845, // PDSxno
	0x00FA0089, // DPo
	0x00FB0A09, // DPSnoo
	0x00FC008A, // PSo
	0x00FD0A0A, // PSDnoo
	0x00FE02A9, // DPSoo
	0x00FF0062  // 1
};

/* @msdn{cc241582} vs @msdn{dd145088} */
static const uint8 g_rop2_map[] = {
	R2_BLACK,       /* 0 */
	R2_NOTMERGEPEN, /* DPon */
	R2_MASKNOTPEN,  /* DPna */
	R2_NOTCOPYPEN,  /* Pn */
	R2_MASKPENNOT,  /* PDna */
	R2_NOT,         /* Dn */
	R2_XORPEN,      /* DPx */
	R2_NOTMASKPEN,  /* DPan */
	R2_MASKPEN,     /* DPa */
	R2_NOTXORPEN,   /* DPxn */
	R2_NOP,         /* D */
	R2_MERGENOTPEN, /* DPno */
	R2_COPYPEN,     /* P */
	R2_MERGEPENNOT, /* PDno */
	R2_MERGEPEN,    /* PDo */
	R2_WHITE,       /* 1 */
};

static int
wf_set_rop2(HDC hdc, int rop2)
{
	if ((rop2 < 0x01) || (rop2 > 0x10))
	{
		//printf("wf_set_rop2: unknown rop2 %x\n", rop2);
		return 0;
	}
	return SetROP2(hdc, g_rop2_map[rop2 - 1]);
}

static void
wf_invalidate_region(wfInfo * wfi, int x1, int y1, int x2, int y2)
{
	#pragma region Myrtille
	// check the region to invalidate; for some reason, the given coordinates are sometimes outside the screen... ensure consistency
	if (x1 < 0 || x1 > wfi->settings->width || y1 < 0 || y1 > wfi->settings->height ||
		x2 < 0 || x2 > wfi->settings->width || y2 < 0 || y2 > wfi->settings->height ||
		x1 > x2 || y1 > y2)
		return;
	#pragma endregion

	if (wfi->hwnd)
	{
		RECT update_rect;
		update_rect.left = x1;
		update_rect.top = y1;
		update_rect.right = x2;
		update_rect.bottom = y2;
		InvalidateRect(wfi->hwnd, &update_rect, FALSE);
	}

	#pragma region Myrtille
	// set the consolidated region min/max coordinates
	if (imageLeft == -1 || imageLeft > x1)
		imageLeft = x1;

	if (imageTop == -1 || imageTop > y1)
		imageTop = y1;

	if (imageRight == -1 || imageRight < x2)
		imageRight = x2;

	if (imageBottom == -1 || imageBottom < y2)
		imageBottom = y2;
	#pragma endregion
}

static HBITMAP
wf_create_dib(wfInfo * wfi, int width, int height, int bpp, int reverse, uint8 * data)
{
	HDC hdc;
	HBITMAP bitmap;
	BITMAPINFO bmi;
	uint8* cdata = NULL;

	hdc = GetDC(NULL);
	bmi.bmiHeader.biSize = sizeof(BITMAPINFO);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	bmi.bmiHeader.biCompression = BI_RGB;
	bitmap = CreateDIBSection (hdc, &bmi, DIB_RGB_COLORS, (void**)&cdata, NULL, 0);

	if (data != NULL)
		wf_image_convert(wfi, width, height, bpp, reverse, data, cdata);

	ReleaseDC(NULL, hdc);
	GdiFlush();
	return bitmap;
}

static HBRUSH
wf_create_brush(wfInfo * wfi, RD_BRUSH * brush, int color, int bpp)
{
	HBRUSH br;
	LOGBRUSH lbr;
	HBITMAP pattern = NULL;
	uint8 * cdata;
	int i;
	uint8 ipattern[8];

	// Style
	lbr.lbStyle = brush->style;
	// Color
	if (lbr.lbStyle == BS_DIBPATTERN || lbr.lbStyle == BS_DIBPATTERN8X8 || lbr.lbStyle == BS_DIBPATTERNPT)
	{
		lbr.lbColor = DIB_RGB_COLORS;
	}
	else
	{
		lbr.lbColor = color;
	}
	// Hatch
	if (lbr.lbStyle == BS_PATTERN || lbr.lbStyle == BS_PATTERN8X8)
	{
		if (brush->bd == 0)	/* rdp4 brush */
		{
			for (i = 0; i != 8; i++)
			{
				ipattern[7 - i] = brush->pattern[i];
			}
			cdata = wf_glyph_convert(wfi, 8, 8, ipattern);
			pattern = CreateBitmap(8, 8, 1, 1, cdata);
			lbr.lbHatch = (ULONG_PTR)pattern;
			free(cdata);
		}
		else if (brush->bd->color_code > 1)	/* > 1 bpp */
		{
			pattern = wf_create_dib(wfi, 8, 8, bpp, 1, brush->bd->data);
			lbr.lbHatch = (ULONG_PTR)pattern;
		}
		else
		{
			cdata = wf_glyph_convert(wfi, 8, 8, brush->bd->data);
			pattern = CreateBitmap(8, 8, 1, 1, cdata);
			lbr.lbHatch = (ULONG_PTR)pattern;
			free(cdata);
		}
	}
	else if (lbr.lbStyle == BS_HATCHED)
	{
		lbr.lbHatch = brush->pattern[0];
	}
	else
	{
		lbr.lbHatch = 0;
	}

	br = CreateBrushIndirect(&lbr);
	SetBrushOrgEx(wfi->drw->hdc, brush->xorigin, brush->yorigin, NULL);
	if (pattern != NULL)
	{
		DeleteObject(pattern);
	}
	return br;
}

static struct wf_bitmap *
wf_bitmap_new(wfInfo * wfi, int width, int height, int bpp, int reverse, uint8 * data)
{
	struct wf_bitmap *bm;
	HDC hdc;

	hdc = GetDC(NULL);
	bm = (struct wf_bitmap *) malloc(sizeof(struct wf_bitmap));
	bm->hdc = CreateCompatibleDC(hdc);
	if (data == NULL)
	{
		bm->bitmap = CreateCompatibleBitmap(hdc, width, height);
	}
	else
	{
		bm->bitmap = wf_create_dib(wfi, width, height, bpp, reverse, data);
	}
	bm->org_bitmap = (HBITMAP)SelectObject(bm->hdc, bm->bitmap);
	ReleaseDC(NULL, hdc);
	return bm;
}

static void
wf_bitmap_free(struct wf_bitmap * bm)
{
	if (bm != 0)
	{
		SelectObject(bm->hdc, bm->org_bitmap);
		DeleteObject(bm->bitmap);
		DeleteDC(bm->hdc);
		free(bm);
	}
}

static void
l_ui_error(struct rdp_inst * inst, const char * text)
{
	printf("ui_error: %s", text);
}

static void
l_ui_warning(struct rdp_inst * inst, const char * text)
{
	printf("ui_warning: %s\n", text);
}

static void
l_ui_unimpl(struct rdp_inst * inst, const char * text)
{
	printf("ui_unimpl: %s\n", text);
}

static void
l_ui_begin_update(struct rdp_inst * inst)
{
	//printf("ui_begin_update\n");

	#pragma region Myrtille
	imageLeft = -1;
	imageTop = -1;
	imageRight = -1;
	imageBottom = -1;
	#pragma endregion
}

static void
l_ui_end_update(struct rdp_inst * inst)
{
	wfInfo * wfi = GET_WFI(inst);

	//printf("ui_end_update\n");

	#pragma region Myrtille
	// --------------------------- consistency check ----------------------------------------------

	if (imageLeft == -1 || imageTop == -1 || imageRight == -1 || imageBottom == -1)
		return;

	// --------------------------- ips regulator --------------------------------------------------

	if (MAX_IMAGE_COUNT_PER_SECOND > 0)
	{
		DWORD now = GetTickCount();

		int diff = now - imageCountLastCheck;

		if (diff >= 1000)
		{
			imageCountLastCheck = now;
			imageCountPerSec = 0;
		}
		else
			imageCountPerSec++;

		if (imageCountPerSec >= MAX_IMAGE_COUNT_PER_SECOND)
			return;
	}

	// --------------------------- pipe check -----------------------------------------------------

	if (wfi->settings->remotesession_id == 0)
		return;

	// --------------------------- extract the consolidated region --------------------------------

	HDC hdc = CreateCompatibleDC(wfi->backstore->hdc);
	HBITMAP hbmp = CreateCompatibleBitmap(wfi->backstore->hdc, imageRight - imageLeft, imageBottom - imageTop);
	SelectObject(hdc, hbmp);

	//printf("l_ui_end_update left:%i top:%i right:%i bottom:%i\n", imageLeft, imageTop, imageRight, imageBottom);

	BitBlt(
		hdc,
		0,
		0,
		imageRight - imageLeft,
		imageBottom - imageTop,
		wfi->backstore->hdc,
		imageLeft,
		imageTop,
		SRCCOPY);

	Gdiplus::Bitmap *bmpRegion = Gdiplus::Bitmap::FromHBITMAP(hbmp, (HPALETTE)0);

	// ---------------------------  process it ----------------------------------------------------

	processImage(wfi, bmpRegion, false);

	// ---------------------------  cleanup -------------------------------------------------------

	delete bmpRegion;
	bmpRegion = NULL;

	DeleteObject(hbmp);
	hbmp = NULL;

	DeleteDC(hdc);
	hdc = NULL;
	#pragma endregion
}

static void
l_ui_desktop_save(struct rdp_inst * inst, int offset, int x, int y,
	int cx, int cy)
{
	printf("ui_desktop_save:\n");
}

static void
l_ui_desktop_restore(struct rdp_inst * inst, int offset, int x, int y,
	int cx, int cy)
{
	printf("ui_desktop_restore:\n");
}

static RD_HGLYPH
l_ui_create_glyph(struct rdp_inst * inst, int width, int height, uint8 * data)
{
	wfInfo * wfi;
	uint8 * cdata;
	HDC hdc;
	struct wf_bitmap * bm;

	wfi = GET_WFI(inst);
	hdc = GetDC(NULL);
	bm = (struct wf_bitmap *) malloc(sizeof(struct wf_bitmap));
	bm->hdc = CreateCompatibleDC(hdc);
	cdata = wf_glyph_convert(wfi, width, height, data);
	bm->bitmap = CreateBitmap(width, height, 1, 1, cdata);
	bm->org_bitmap = (HBITMAP)SelectObject(bm->hdc, bm->bitmap);
	free(cdata);
	ReleaseDC(NULL, hdc);
	return (RD_HGLYPH)bm;
}

static void
l_ui_destroy_glyph(struct rdp_inst * inst, RD_HGLYPH glyph)
{
	wf_bitmap_free((struct wf_bitmap *) glyph);
}

static RD_HBITMAP
l_ui_create_bitmap(struct rdp_inst * inst, int width, int height, uint8 * data)
{
	wfInfo * wfi;
	struct wf_bitmap * bm;

	wfi = GET_WFI(inst);
	bm = wf_bitmap_new(wfi, width, height, inst->settings->server_depth, 1, data);
	return (RD_HBITMAP) bm;
}

static void
l_ui_paint_bitmap(struct rdp_inst * inst, int x, int y, int cx, int cy, int width,
	int height, uint8 * data)
{
	wfInfo * wfi;
	struct wf_bitmap * bm;

	wfi = GET_WFI(inst);
	//printf("ui_paint_bitmap x %d y %d cx %d cy %d width %d height %d\n", x, y, cx, cy, width, height);
	bm = (struct wf_bitmap *) l_ui_create_bitmap(inst, width, height, data);
	BitBlt(wfi->backstore->hdc, x, y, cx, cy, bm->hdc, 0, 0, SRCCOPY);
	wf_bitmap_free(bm);
	/* We painted directly on backstore, so we should _always_ invalidate */
	wf_invalidate_region(wfi, x, y, x + cx, y + cy);
}

static void
l_ui_destroy_bitmap(struct rdp_inst * inst, RD_HBITMAP bmp)
{
	wf_bitmap_free((struct wf_bitmap *) bmp);
}

static void
l_ui_line(struct rdp_inst * inst, uint8 opcode, int startx, int starty, int endx, int endy, RD_PEN * pen)
{
	HPEN hpen;
	HPEN org_hpen;
	int color;
	int org_rop2;
	wfInfo * wfi = GET_WFI(inst);
	
	//printf("ui_line opcode %d startx %d starty %d endx %d endy %d\n", opcode, startx, starty, endx, endy);
	color = wf_color_convert(wfi, pen->color, inst->settings->server_depth);
	hpen = CreatePen(pen->style, pen->width, color);
	org_rop2 = wf_set_rop2(wfi->drw->hdc, opcode);
	org_hpen = (HPEN)SelectObject(wfi->drw->hdc, hpen);
	MoveToEx(wfi->drw->hdc, startx, starty, NULL);
	LineTo(wfi->drw->hdc, endx, endy);
	SelectObject(wfi->drw->hdc, org_hpen);
	wf_set_rop2(wfi->drw->hdc, org_rop2);
	DeleteObject(hpen);
	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, min(startx, endx), min(starty, endy), max(startx, endx) + 1, max(starty, endy) + 1);
	}
}

static void
l_ui_rect(struct rdp_inst * inst, int x, int y, int cx, int cy, int color)
{
	RECT rect;
	HBRUSH brush;
	wfInfo * wfi = GET_WFI(inst);

	color = wf_color_convert(wfi, color, inst->settings->server_depth);
	//printf("ui_rect %i %i %i %i %i\n", x, y, cx, cy, color);
	rect.left = x;
	rect.top = y;
	rect.right = x + cx;
	rect.bottom = y + cy;
	brush = CreateSolidBrush(color);
	FillRect(wfi->drw->hdc, &rect, brush);
	DeleteObject(brush);

	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, rect.left, rect.top, rect.right, rect.bottom);
	}
}

static void
l_ui_polygon(struct rdp_inst * inst, uint8 opcode, uint8 fillmode, RD_POINT * point,
	int npoints, RD_BRUSH * brush, int bgcolor, int fgcolor)
{
	printf("ui_polygon:\n");
}

static void
l_ui_polyline(struct rdp_inst * inst, uint8 opcode, RD_POINT * points, int npoints, RD_PEN * pen)
{
	int i;
	int color;
	int org_rop2;
	HPEN hpen;
	HPEN org_hpen;
	POINT * ps;
	wfInfo * wfi = GET_WFI(inst);

	//printf("ui_polyline opcode %d npoints %d\n", opcode, npoints);
	color = wf_color_convert(wfi, pen->color, inst->settings->server_depth);
	hpen = CreatePen(pen->style, pen->width, color);
	org_rop2 = wf_set_rop2(wfi->drw->hdc, opcode);
	org_hpen = (HPEN)SelectObject(wfi->drw->hdc, hpen);
	if (npoints > 0)
	{
		ps = (POINT *) malloc(sizeof(POINT) * npoints);
		for (i = 0; i < npoints; i++)
		{
			//printf("ui_polyline point %d %d %d\n", i, points[i].x, points[i].y);
			if (i == 0)
			{
				ps[i].x = points[i].x;
				ps[i].y = points[i].y;
			}
			else
			{
				ps[i].x = ps[i - 1].x + points[i].x;
				ps[i].y = ps[i - 1].y + points[i].y;
			}
			if (wfi->drw == wfi->backstore)
			{
				wf_invalidate_region(wfi, ps[i].x, ps[i].y, ps[i].x + 1, ps[i].y + 1);
			}
		}
		Polyline(wfi->drw->hdc, ps, npoints);
	}
	SelectObject(wfi->drw->hdc, org_hpen);
	wf_set_rop2(wfi->drw->hdc, org_rop2);
	DeleteObject(hpen);
}

static void
l_ui_ellipse(struct rdp_inst * inst, uint8 opcode, uint8 fillmode, int x, int y,
	int cx, int cy, RD_BRUSH * brush, int bgcolor, int fgcolor)
{
	printf("ui_ellipse:\n");
}

static void
l_ui_start_draw_glyphs(struct rdp_inst * inst, int bgcolor, int fgcolor)
{
	wfInfo * wfi = GET_WFI(inst);
	fgcolor = wf_color_convert(wfi, fgcolor, inst->settings->server_depth);
	wfi->brush = CreateSolidBrush(fgcolor);
	wfi->org_brush = (HBRUSH)SelectObject(wfi->drw->hdc, wfi->brush);
}

static void
l_ui_draw_glyph(struct rdp_inst * inst, int x, int y, int cx, int cy,
	RD_HGLYPH glyph)
{
	wfInfo * wfi;
	struct wf_bitmap * bm;

	wfi = GET_WFI(inst);
	bm = (struct wf_bitmap *) glyph;
	BitBlt(wfi->drw->hdc, x, y, cx, cy, bm->hdc, 0, 0, 0x00E20746); // DSPDxax

	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, x, y, x + cx, y + cy);
	}
}

static void
l_ui_end_draw_glyphs(struct rdp_inst * inst, int x, int y, int cx, int cy)
{
	wfInfo * wfi;

	wfi = GET_WFI(inst);
	SelectObject(wfi->drw->hdc, wfi->org_brush);
	DeleteObject(wfi->brush);
	wfi->brush = NULL;
	wfi->org_brush = NULL;
}

static uint32
l_ui_get_toggle_keys_state(struct rdp_inst * inst)
{
	uint32 state = 0;

	if (GetKeyState(VK_SCROLL))
	{
		state |= KBD_SYNC_SCROLL_LOCK;
	}
	if (GetKeyState(VK_NUMLOCK))
	{
		state |= KBD_SYNC_NUM_LOCK;
	}
	if (GetKeyState(VK_CAPITAL))
	{
		state |= KBD_SYNC_CAPS_LOCK;
	}
	if (GetKeyState(VK_KANA))
	{
		state |= KBD_SYNC_KANA_LOCK;
	}
	return state;
}

static void
l_ui_bell(struct rdp_inst * inst)
{
	wfInfo * wfi = GET_WFI(inst);

	#pragma region Myrtille
	if (wfi->settings->show_window)
	{
	#pragma endregion
		MessageBeep(MB_OK);
	#pragma region Myrtille
	}
	#pragma endregion
}

static void
l_ui_destblt(struct rdp_inst * inst, uint8 opcode, int x, int y, int cx, int cy)
{
	wfInfo * wfi = GET_WFI(inst);

	BitBlt(wfi->drw->hdc, x, y, cx, cy, NULL, 0, 0, rop3_code_table[opcode]);
	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, x, y, x + cx, y + cy);
	}
}

static void
l_ui_patblt(struct rdp_inst * inst, uint8 opcode, int x, int y, int cx, int cy,
	RD_BRUSH * brush, int bgcolor, int fgcolor)
{
	HBRUSH br;
	HBRUSH org_br;
	int org_bkmode;
	COLORREF org_bkcolor;
	COLORREF org_textcolor;
	wfInfo * wfi = GET_WFI(inst);
	
	//printf("ui_patblt: style %d x %d y %d cx %d cy %d\n", brush->style, x, y, cx, cy);

	fgcolor = wf_color_convert(wfi, fgcolor, inst->settings->server_depth);
	bgcolor = wf_color_convert(wfi, bgcolor, inst->settings->server_depth);

	br = wf_create_brush(wfi, brush, fgcolor, inst->settings->server_depth);
	org_bkmode = SetBkMode(wfi->drw->hdc, OPAQUE);
	org_bkcolor = SetBkColor(wfi->drw->hdc, bgcolor);
	org_textcolor = SetTextColor(wfi->drw->hdc, fgcolor);
	org_br = (HBRUSH)SelectObject(wfi->drw->hdc, br);
	PatBlt(wfi->drw->hdc, x, y, cx, cy, rop3_code_table[opcode]);
	SelectObject(wfi->drw->hdc, org_br);
	DeleteObject(br);
	SetBkMode(wfi->drw->hdc, org_bkmode);
	SetBkColor(wfi->drw->hdc, org_bkcolor);
	SetTextColor(wfi->drw->hdc, org_textcolor);
	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, x, y, x + cx, y + cy);
	}
}

static void
l_ui_screenblt(struct rdp_inst * inst, uint8 opcode, int x, int y, int cx, int cy,
	int srcx, int srcy)
{
	wfInfo * wfi = GET_WFI(inst);
	//printf("ui_screenblt: opcode %d x %d y %d cx %d cy %d srcx %d srcy %d \n", opcode, x, y, cx, cy, srcx, srcy);
	BitBlt(wfi->drw->hdc, x, y, cx, cy, wfi->backstore->hdc, srcx, srcy, rop3_code_table[opcode]); /* The source surface is always the primary drawing surface */
	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, x, y, x + cx, y + cy);
	}
}

static void
l_ui_memblt(struct rdp_inst * inst, uint8 opcode, int x, int y, int cx, int cy,
	RD_HBITMAP src, int srcx, int srcy)
{
	wfInfo * wfi;
	struct wf_bitmap * bm;

	wfi = GET_WFI(inst);
	bm = (struct wf_bitmap *) src;
	//printf("ui_memblt %i %i %i %i %i %i %i\n", x, y, cx, cy, srcx, srcy, opcode);
	BitBlt(wfi->drw->hdc, x, y, cx, cy, bm->hdc, srcx, srcy, rop3_code_table[opcode]);
	if (wfi->drw == wfi->backstore)
	{
		wf_invalidate_region(wfi, x, y, x + cx, y + cy);
	}
}

static void
l_ui_triblt(struct rdp_inst * inst, uint8 opcode, int x, int y, int cx, int cy,
	RD_HBITMAP src, int srcx, int srcy, RD_BRUSH * brush, int bgcolor, int fgcolor)
{
	printf("ui_triblt\n");
	/* TODO */
}

static int
l_ui_select(struct rdp_inst * inst, int rdp_socket)
{
	/* printf("ui_select: inst %p\n", inst); */
	return 1;
}

static void
l_ui_set_clip(struct rdp_inst * inst, int x, int y, int cx, int cy)
{
	wfInfo * wfi;
	HRGN hrgn;

	wfi = GET_WFI(inst);
	/* printf("ui_set_clip %i %i %i %i\n", x, y, cx, cy); */
	hrgn = CreateRectRgn(x, y, x + cx, y + cy);
	SelectClipRgn(wfi->drw->hdc, hrgn);
	DeleteObject(hrgn);
}

static void
l_ui_reset_clip(struct rdp_inst * inst)
{
	wfInfo * wfi = GET_WFI(inst);
	/* printf("ui_reset_clip\n"); */
	SelectClipRgn(wfi->drw->hdc, NULL);
}

static void
l_ui_resize_window(struct rdp_inst * inst)
{
	printf("ui_resize_window:\n");
}

static void
l_ui_set_cursor(struct rdp_inst * inst, RD_HCURSOR cursor)
{
	wfInfo * wfi = GET_WFI(inst);

	wfi->cursor = (HCURSOR)cursor;

	if (wfi->hwnd)
	{
		PostMessage(wfi->hwnd, WM_SETCURSOR, 0, 0);
	}

	#pragma region Myrtille
	// --------------------------- pipe check -----------------------------------------------------

	if (wfi->settings->remotesession_id == 0)
		return;

	// --------------------------- set cursor -----------------------------------------------------

	// set a display context and a bitmap to store the mouse cursor

	HDC hdc = CreateCompatibleDC(wfi->backstore->hdc);
	HBITMAP hbmp = CreateCompatibleBitmap(wfi->backstore->hdc, GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR));
	SelectObject(hdc, hbmp);

	// set a colored background, so that it will be possible to identify parts of the cursor that should be made transparent

	HBRUSH hbrush = CreateSolidBrush(RGB(0, 0, 255));
		
	// draw the cursor on the display context
		
	DrawIconEx(hdc, 0, 0, (HICON)wfi->cursor, 0, 0, 0, hbrush, DI_NORMAL);

	// cursor bitmap

	Gdiplus::Bitmap *bmpCursor = Gdiplus::Bitmap::FromHBITMAP(hbmp, (HPALETTE)0);

	// extract the relevant cursor image. also, transparency requires ARGB format

	Gdiplus::Bitmap *bmpTransparentCursor = bmpCursor->Clone(
		0,
		0,
		GetSystemMetrics(SM_CXCURSOR),
		GetSystemMetrics(SM_CYCURSOR),
		PixelFormat32bppARGB);

	// make the background transparent

	for (int x = 0; x < bmpTransparentCursor->GetWidth(); x++)
	{
		for (int y = 0; y < bmpTransparentCursor->GetHeight(); y++)
		{
			Gdiplus::Color color;
			bmpTransparentCursor->GetPixel(x, y, &color);
				
			if (color.GetValue() == Gdiplus::Color::Blue)
			{
				bmpTransparentCursor->SetPixel(x, y, Gdiplus::Color::Transparent);
			}

			// for some reason, some cursors (like the text one) are yellow instead of black ?! switching color...

			else if (color.GetValue() == Gdiplus::Color::Yellow)
			{
				bmpTransparentCursor->SetPixel(x, y, Gdiplus::Color::Black);
			}
		}
	}

	// convert into PNG

	IStream* pngStream;
	CreateStreamOnHGlobal(NULL, TRUE, &pngStream);
	bmpTransparentCursor->Save(pngStream, &wfi->pngClsid);
	
	STATSTG statstg;
	pngStream->Stat(&statstg, STATFLAG_DEFAULT);
	ULONG pngSize = (ULONG)statstg.cbSize.LowPart;

	// retrieve cursor info

	ICONINFO cursorInfo;
	GetIconInfo((HICON)wfi->cursor, &cursorInfo);

	// send

	if (pngStream != NULL && pngSize > 0 && pngSize <= IMAGES_PIPE_BUFFER_SIZE)
	{
		sendImage(
			wfi,
			bmpTransparentCursor,
			++imageIdx,
			cursorInfo.xHotspot,
			cursorInfo.yHotspot,
			bmpTransparentCursor->GetWidth(),
			bmpTransparentCursor->GetHeight(),
			IMAGE_FORMAT_CUR,
			IMAGE_QUALITY_HIGHEST,
			pngStream,
			pngSize,
			false);
	}

	// cleanup

	DeleteObject(cursorInfo.hbmMask);
	DeleteObject(cursorInfo.hbmColor);

	if (pngStream != NULL)
	{
		pngStream->Release();
		pngStream = NULL;
	}

	delete bmpTransparentCursor;
	bmpTransparentCursor = NULL;

	delete bmpCursor;
	bmpCursor = NULL;

	DeleteObject(hbrush);
	hbrush = NULL;

	DeleteObject(hbmp);
	hbmp = NULL;

	DeleteDC(hdc);
	hdc = NULL;
	#pragma endregion
}

static void
l_ui_destroy_cursor(struct rdp_inst * inst, RD_HCURSOR cursor)
{
	wfInfo * wfi = GET_WFI(inst);

	if (wfi->hwnd)
	{
		if (wfi->cursor == (HCURSOR)cursor)
		{
			wfi->cursor = g_default_cursor;
			PostMessage(wfi->hwnd, WM_SETCURSOR, 0, 0);
		}
		DestroyCursor((HCURSOR)cursor);
	}
}

static RD_HCURSOR
l_ui_create_cursor(struct rdp_inst * inst, uint32 x, uint32 y,
	int width, int height, uint8 * andmask, uint8 * xormask, int bpp)
{
	wfInfo * wfi;
	HCURSOR cursor;
	ICONINFO iconinfo;
	uint8 * cdata;

	wfi = GET_WFI(inst);
	if (bpp == 1)
	{
		cursor = CreateCursor(g_hInstance, x, y, width, height, andmask, xormask);
	}
	else
	{
		iconinfo.fIcon = FALSE;
		iconinfo.xHotspot = x;
		iconinfo.yHotspot = y;
		cdata = wf_cursor_mask_convert(wfi, width, height, andmask);
		iconinfo.hbmMask = CreateBitmap(width, height, 1, 1, cdata);
		iconinfo.hbmColor = wf_create_dib(wfi, width, height, bpp, 0, xormask);
		cursor = CreateIconIndirect(&iconinfo);
		DeleteObject(iconinfo.hbmMask);
		DeleteObject(iconinfo.hbmColor);
		free(cdata);
	}
	return (RD_HCURSOR)cursor;
}

static void
l_ui_set_null_cursor(struct rdp_inst * inst)
{
	wfInfo * wfi = GET_WFI(inst);

	if (wfi->hwnd)
	{
		wfi->cursor = NULL;
		PostMessage(wfi->hwnd, WM_SETCURSOR, 0, 0);
	}
}

static void
l_ui_set_default_cursor(struct rdp_inst * inst)
{
	wfInfo * wfi = GET_WFI(inst);

	if (wfi->hwnd)
	{
		wfi->cursor = g_default_cursor;
		PostMessage(wfi->hwnd, WM_SETCURSOR, 0, 0);
	}
}

static RD_HPALETTE
l_ui_create_palette(struct rdp_inst * inst, RD_HPALETTE palette)
{
	RD_PALETTE* opal = (RD_PALETTE*) palette;
	RD_PALETTE* npal = (RD_PALETTE*) malloc(sizeof(RD_PALETTE));

	npal->count = opal->count;
	npal->entries = (RD_PALETTEENTRY*) malloc(sizeof(RD_PALETTEENTRY) * npal->count);
	memcpy(npal->entries, opal->entries, sizeof(RD_PALETTEENTRY) * npal->count);

	return (RD_HPALETTE) npal;
}

static void
l_ui_set_palette(struct rdp_inst * inst, RD_HPALETTE palette)
{
	wfInfo * wfi = GET_WFI(inst);

	if (wfi->palette != NULL)
		free(wfi->palette);

	wfi->palette = (RD_PALETTE*) palette;
}

static void
l_ui_move_pointer(struct rdp_inst * inst, int x, int y)
{
	wfInfo * wfi = GET_WFI(inst);
	/* TODO */
}

static RD_HBITMAP
l_ui_create_surface(struct rdp_inst * inst, int width, int height, RD_HBITMAP old_surface)
{
	wfInfo * wfi;
	struct wf_bitmap * bm;
	struct wf_bitmap * old_bm;

	wfi = GET_WFI(inst);
	bm = wf_bitmap_new(wfi, width, height, 0, 0, NULL);
	old_bm = (struct wf_bitmap *) old_surface;
	if (old_bm != 0)
	{
		BitBlt(bm->hdc, 0, 0, width, height, old_bm->hdc, 0, 0, SRCCOPY);
		wf_bitmap_free(old_bm);
	}
	if (wfi->drw == old_bm)
	{
		wfi->drw = bm;
	}
	return (RD_HBITMAP)bm;
}

static void
l_ui_set_surface(struct rdp_inst * inst, RD_HBITMAP surface)
{
	wfInfo * wfi;

	wfi = GET_WFI(inst);
	if (surface != 0)
	{
		wfi->drw = (struct wf_bitmap *) surface;
	}
	else
	{
		wfi->drw = wfi->backstore;
	}
}

static void
l_ui_destroy_surface(struct rdp_inst * inst, RD_HBITMAP surface)
{
	wfInfo * wfi = GET_WFI(inst);

	if (wfi->drw == surface)
	{
		l_ui_warning(inst, "ui_destroy_surface: freeing active surface!\n");
		wfi->drw = wfi->backstore;
	}
	if (surface != 0)
	{
		wf_bitmap_free((struct wf_bitmap *)surface);
	}
}

static void
l_ui_channel_data(struct rdp_inst * inst, int chan_id, char * data, int data_size,
	int flags, int total_size)
{
	/* TODO */
}

/* fgets from stdin do for some reason not work and there is no getpass ... */
static void
getsn(char *s, int size, char bullet)
{
	int i;
	for (i=0; i < size; i++)
	{
		if ((s[i] = _getch()) == '\r')
			break;
		_putch(bullet ? bullet : s[i]);
	}
	s[i] = 0;
}

static RD_BOOL
l_ui_authenticate(struct rdp_inst * inst)
{
	if (!AllocConsole())
		_cputs("\n");	/* reusing debug console */

	_cputs("Please enter NLA login credential.\n");

	_cputs("User name:");
	if (inst->settings->username[0] == 0)
		getsn(inst->settings->username, sizeof(inst->settings->username), 0);
	else
		_cputs(inst->settings->username);
	_putch('\n');

	_cputs("Domain:");
	if (inst->settings->domain[0] == 0)
		getsn(inst->settings->domain, sizeof(inst->settings->domain), 0);
	else
		_cputs(inst->settings->domain);
	_putch('\n');

	_cputs("Password:");
	if (inst->settings->password[0] == 0)
		getsn(inst->settings->password, sizeof(inst->settings->password), '*');
	else
		_cputs("***");
	_putch('\n');

	return 1;
}

static int
l_ui_decode(struct rdp_inst * inst, uint8 * data, int data_size)
{
	printf("l_ui_decode: size %d\n", data_size);
	return 0;
}

RD_BOOL
l_ui_check_certificate(rdpInst * inst, const char * fingerprint,
	const char * subject, const char * issuer, RD_BOOL verified)
{
	printf("certificate details:\n");
	printf("  Subject:\n    %s\n", subject);
	printf("  Issued by:\n    %s\n", issuer);
	printf("  Fingerprint:\n    %s\n",  fingerprint);

	if (!verified)
		printf("The server could not be authenticated. Connection security may be compromised!\n");

	return TRUE;
}

static int
wf_assign_callbacks(rdpInst * inst)
{
	inst->ui_error = l_ui_error;
	inst->ui_warning = l_ui_warning;
	inst->ui_unimpl = l_ui_unimpl;
	inst->ui_begin_update = l_ui_begin_update;
	inst->ui_end_update = l_ui_end_update;
	inst->ui_desktop_save = l_ui_desktop_save;
	inst->ui_desktop_restore = l_ui_desktop_restore;
	inst->ui_create_bitmap = l_ui_create_bitmap;
	inst->ui_paint_bitmap = l_ui_paint_bitmap;
	inst->ui_destroy_bitmap = l_ui_destroy_bitmap;
	inst->ui_line = l_ui_line;
	inst->ui_rect = l_ui_rect;
	inst->ui_polygon = l_ui_polygon;
	inst->ui_polyline = l_ui_polyline;
	inst->ui_ellipse = l_ui_ellipse;
	inst->ui_start_draw_glyphs = l_ui_start_draw_glyphs;
	inst->ui_draw_glyph = l_ui_draw_glyph;
	inst->ui_end_draw_glyphs = l_ui_end_draw_glyphs;
	inst->ui_get_toggle_keys_state = l_ui_get_toggle_keys_state;
	inst->ui_bell = l_ui_bell;
	inst->ui_destblt = l_ui_destblt;
	inst->ui_patblt = l_ui_patblt;
	inst->ui_screenblt = l_ui_screenblt;
	inst->ui_memblt = l_ui_memblt;
	inst->ui_triblt = l_ui_triblt;
	inst->ui_create_glyph = l_ui_create_glyph;
	inst->ui_destroy_glyph = l_ui_destroy_glyph;
	inst->ui_select = l_ui_select;
	inst->ui_set_clip = l_ui_set_clip;
	inst->ui_reset_clip = l_ui_reset_clip;
	inst->ui_resize_window = l_ui_resize_window;
	inst->ui_set_cursor = l_ui_set_cursor;
	inst->ui_destroy_cursor = l_ui_destroy_cursor;
	inst->ui_create_cursor = l_ui_create_cursor;
	inst->ui_set_null_cursor = l_ui_set_null_cursor;
	inst->ui_set_default_cursor = l_ui_set_default_cursor;
	inst->ui_create_palette = l_ui_create_palette;
	inst->ui_set_palette = l_ui_set_palette;
	inst->ui_move_pointer = l_ui_move_pointer;
	inst->ui_create_surface = l_ui_create_surface;
	inst->ui_set_surface = l_ui_set_surface;
	inst->ui_destroy_surface = l_ui_destroy_surface;
	inst->ui_channel_data = l_ui_channel_data;
	inst->ui_authenticate = l_ui_authenticate;
	inst->ui_decode = l_ui_decode;
	inst->ui_check_certificate = l_ui_check_certificate;
	return 0;
}

int
wf_pre_connect(wfInfo * wfi)
{
	int i1;

	wf_assign_callbacks(wfi->inst);
	wfi->cursor = g_default_cursor;

	if (wfi->percentscreen > 0)
	{
		i1 = (GetSystemMetrics(SM_CXSCREEN) * wfi->percentscreen) / 100;
		wfi->settings->width = i1;
		i1 = (GetSystemMetrics(SM_CYSCREEN) * wfi->percentscreen) / 100;
		wfi->settings->height = i1;
	}

	if (wfi->fs_toggle)
	{
		wfi->inst->settings->width = GetSystemMetrics(SM_CXSCREEN);
		wfi->inst->settings->height = GetSystemMetrics(SM_CYSCREEN);
	}

	i1 = wfi->settings->width;
	i1 = (i1 + 3) & (~3);
	wfi->settings->width = i1;
	if ((wfi->settings->width < 64) || (wfi->settings->height < 64) ||
		(wfi->settings->width > 4096) || (wfi->settings->height > 4096))
	{
		printf("wf_init: invalid dimensions %d %d\n", wfi->settings->width, wfi->settings->height);
		return 1;
	}

	return 0;
}

int
wf_post_connect(wfInfo * wfi)
{
	int width;
	int height;
	wchar_t win_title[64];

	width = wfi->inst->settings->width;
	height = wfi->inst->settings->height;
	if (wfi->settings->tcp_port_rdp == 3389)
		_snwprintf(win_title, sizeof(win_title) / sizeof(win_title[0]), L"%S - freerdp", wfi->settings->server);
	else
		_snwprintf(win_title, sizeof(win_title) / sizeof(win_title[0]), L"%S:%d - freerdp", wfi->settings->server, wfi->settings->tcp_port_rdp);

	if (!wfi->backstore)
	{
		wfi->backstore = wf_bitmap_new(wfi, width, height, 0, 0, NULL);
		BitBlt(wfi->backstore->hdc, 0, 0, width, height, NULL, 0, 0, BLACKNESS);
	}
	wfi->drw = wfi->backstore;

	#pragma region Myrtille
	if (wfi->settings->show_window)
	{
	#pragma endregion
		if (!wfi->hwnd)
		{
			wfi->hwnd = CreateWindowEx(NULL, g_wnd_class_name, win_title,
					0, 0, 0, 0, 0,
					NULL, NULL, g_hInstance, NULL);
			SetWindowLongPtr(wfi->hwnd, GWLP_USERDATA, (LONG_PTR)wfi);
		}
		if (wfi->fullscreen)
		{
			SetWindowLongPtr(wfi->hwnd, GWL_STYLE, WS_POPUP);
			SetWindowPos(wfi->hwnd, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED);
		}
		else
		{
			RECT rc_client, rc_wnd;
			POINT diff;

			SetWindowLongPtr(wfi->hwnd, GWL_STYLE, WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX);
			/* Now resize to get full canvas size and room for caption and borders */
			SetWindowPos(wfi->hwnd, HWND_TOP, 10, 10, width, height, SWP_FRAMECHANGED);
			GetClientRect(wfi->hwnd, &rc_client);
			GetWindowRect(wfi->hwnd, &rc_wnd);
			diff.x = (rc_wnd.right - rc_wnd.left) - rc_client.right;
			diff.y = (rc_wnd.bottom - rc_wnd.top) - rc_client.bottom;
			SetWindowPos(wfi->hwnd, HWND_TOP, -1, -1, width + diff.x, height + diff.y, SWP_NOMOVE | SWP_FRAMECHANGED);
		}

		ShowWindow(wfi->hwnd, SW_SHOWNORMAL);
		UpdateWindow(wfi->hwnd);
	#pragma region Myrtille
	}
	#pragma endregion

	return 0;
}

void
wf_uninit(wfInfo * wfi)
{
	if (wfi->hwnd)
		CloseWindow(wfi->hwnd);

	wf_bitmap_free(wfi->backstore);

	if (wfi->palette != 0)
		free(wfi->palette);
}

void
wf_toggle_fullscreen(wfInfo * wfi)
{
	if (wfi->hwnd)
	{
		ShowWindow(wfi->hwnd, SW_HIDE);
		wfi->fullscreen = !wfi->fullscreen;
		wf_post_connect(wfi);
		SetForegroundWindow(wfi->hwnd);
	}
}

#pragma region Myrtille

std::string getCurrentTime()
{
	SYSTEMTIME time;
	GetLocalTime(&time);

	WORD year = time.wYear;
	WORD month = time.wMonth;
	WORD day = time.wDay;
	WORD hour = time.wHour;
	WORD minute = time.wMinute;
	WORD second = time.wSecond;
	WORD millisecond = time.wMilliseconds;

	// YYYY-MM-DD hh:mm:ss,fff
	std::stringstream ss;
	ss << year << "-" <<
		(month < 10 ? "0" : "") << month << "-" <<
		(day < 10 ? "0" : "") << day << " " <<
		(hour < 10 ? "0" : "") << hour << ":" <<
		(minute < 10 ? "0" : "") << minute << ":" <<
		(second < 10 ? "0" : "") << second << "," <<
		(millisecond < 100 ? (millisecond < 10 ? "00" : "0") : "") << millisecond;

	return ss.str();
}

std::string createLogDirectory()
{
	std::string path = "";

	// retrieve the module file name
	wchar_t* buffer = new wchar_t[MAX_PATH];
	if (GetModuleFileName(NULL, buffer, MAX_PATH))
	{
		// extract the parent folder
		char moduleFilename[MAX_PATH];
		wcstombs(moduleFilename, buffer, MAX_PATH);
		std::string::size_type pos = std::string(moduleFilename).find_last_of("\\/");
		std::string currentdir = std::string(moduleFilename).substr(0, pos);
		pos = currentdir.find_last_of("\\/");
		std::string parentdir = currentdir.substr(0, pos);

		// log folder
		std::stringstream ss;
		ss << parentdir << "\\log";
		path = ss.str();
		std::wstring ws = s2ws(path);
		LPCWSTR logDir = ws.c_str();

		// create the log folder if not already exists
		if (!CreateDirectory(logDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
		{
			printf("%s createLogDirectory: create directory failed with error %d\n", getCurrentTime().c_str(), GetLastError());
			path = "";
		}
	}
	else
	{
		printf("%s createLogDirectory: can't retrieve the module filename %d\n", getCurrentTime().c_str(), GetLastError());
	}

	// cleanup
	delete[] buffer;

	return path;
}

std::wstring s2ws(const std::string& s)
{
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0); 
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;
    return r;
}

DWORD wf_connect_pipes(wfInfo * wfi)
{
	// create inputs pipe (user and system commands)
	if ((wfi->inputsPipe = createRemoteSessionPipe(wfi, "inputs")) == INVALID_HANDLE_VALUE)
	{
		printf("%s wf_connect_pipes: CreateFile failed for inputs pipe with error %d\n", getCurrentTime().c_str(), GetLastError());
		return GetLastError();
	}
	
	// create images pipe (region and fullscreen updates)
	if ((wfi->imagesPipe = createRemoteSessionPipe(wfi, "outputs")) == INVALID_HANDLE_VALUE)
	{
		printf("%s wf_connect_pipes: CreateFile failed for images pipe with error %d\n", getCurrentTime().c_str(), GetLastError());
		return GetLastError();
	}

	// read inputs pipe
	DWORD threadId;
	if (CreateThread(NULL, 0, ProcessInputsPipe, wfi, 0, &threadId) == NULL)
	{
		printf("%s wf_connect_pipes: CreateThread failed for inputs pipe with error %d\n", getCurrentTime().c_str(), GetLastError());
		return GetLastError();
	}

	// simple handshaking
	CHAR hello[] = "Hello server";
	DWORD bytesToWrite = 12;
	DWORD bytesWritten;
	if (WriteFile(wfi->imagesPipe, hello, bytesToWrite, &bytesWritten, NULL) == 0)
	{
		printf("%s wf_connect_pipes: WriteFile failed for handshaking with error %d\n", getCurrentTime().c_str(), GetLastError());
		return GetLastError();
	}

	return 0;
}

HANDLE createRemoteSessionPipe(wfInfo * wfi, std::string pipeName)
{
	std::stringstream ss;
	ss << "\\\\.\\pipe\\remotesession_" << wfi->settings->remotesession_id << "_" << pipeName;
	std::string s = ss.str();
	std::wstring ws = s2ws(s);
	LPCWSTR pipeFileName = ws.c_str();

	return CreateFile(pipeFileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

std::string createRemoteSessionDirectory(wfInfo * wfi)
{
	std::string path = "";

	std::string logDirectoryPath = createLogDirectory();
	if (logDirectoryPath != "")
	{
		std::stringstream ss;
		ss << logDirectoryPath << "\\remotesession_" << wfi->settings->remotesession_id << "." << GetCurrentProcessId();
		path = ss.str();
		std::wstring ws = s2ws(path);
		LPCWSTR remoteSessionDir = ws.c_str();

		if (!CreateDirectory(remoteSessionDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
		{
			printf("%s createRemoteSessionDirectory: CreateDirectory failed with error %d\n", getCurrentTime().c_str(), GetLastError());
			path = "";
		}
	}

	return path;
}

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems)
{
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string &s, char delim)
{
    std::vector<std::string> elems;
    return split(s, delim, elems);
}

DWORD WINAPI ProcessInputsPipe(LPVOID lpParameter)
{
	wfInfo * wfi;
	wfi = (wfInfo *)lpParameter;

	// main loop
	bool processInputs = true;
	while (processInputs)
	{
		CHAR buffer[INPUTS_PIPE_BUFFER_SIZE];
		DWORD bytesRead;

		// wait for inputs pipe event
		if (ReadFile(wfi->inputsPipe, buffer, INPUTS_PIPE_BUFFER_SIZE, &bytesRead, NULL) == 0)
		{
			switch (GetLastError())
			{
				case ERROR_INVALID_HANDLE:
					printf("%s InputsPipe: ReadFile failed with error ERROR_INVALID_HANDLE\n", getCurrentTime().c_str());
					break;

				case ERROR_PIPE_NOT_CONNECTED:
					printf("%s InputsPipe: ReadFile failed with error ERROR_PIPE_NOT_CONNECTED\n", getCurrentTime().c_str());
					break;

				case ERROR_PIPE_BUSY:
					printf("%s InputsPipe: ReadFile failed with error ERROR_PIPE_BUSY\n", getCurrentTime().c_str());
					break;

				case ERROR_BAD_PIPE:
					printf("%s InputsPipe: ReadFile failed with error ERROR_BAD_PIPE\n", getCurrentTime().c_str());
					break;

				case ERROR_BROKEN_PIPE:
					printf("%s InputsPipe: ReadFile failed with error ERROR_BROKEN_PIPE\n", getCurrentTime().c_str());
					break;

				default:
					printf("%s InputsPipe: ReadFile failed with error %d\n", getCurrentTime().c_str(), GetLastError());
					break;
			}

			// pipe problem; exit
			CloseHandle(wfi->inputsPipe);
			CloseHandle(wfi->imagesPipe);
			Gdiplus::GdiplusShutdown(wfi->gdiplusToken);
			exit(EXIT_SUCCESS);
		}
		else
		{
			std::string message(buffer, bytesRead);
			
			printf("%s InputsPipe: ReadFile succeeded: %s\n", getCurrentTime().c_str(), message.c_str());

			if (bytesRead > 0)
			{
				std::vector<std::string> inputs = split(message, ',');
					
				for (int i = 0; i < inputs.size(); i++)
				{
					std::string input = inputs[i];
					std::string inputType = input.substr(0, 1);
					
					// command
					if (inputType == "C")
					{
						int separatorIdx = input.find("-");
						if (separatorIdx != std::string::npos)
						{
							std::string cmdCode = input.substr(1, separatorIdx - 1);
							std::string cmdArgs = input.substr(separatorIdx + 1, input.length() - separatorIdx - 1);

							switch (stoi(cmdCode))
							{
								// fullscreen update request
								case RDP_COMMAND_SEND_FULLSCREEN_UPDATE:
									if (QueueUserWorkItem(SendFullscreen, lpParameter, WT_EXECUTEDEFAULT) == 0)
										printf("%s InputsPipe: QueueUserWorkItem failed for SendFullscreen with error %d\n", getCurrentTime().c_str(), GetLastError());
									break;

								// the standard way to close an rdp session is to logoff the user; an alternate way is to simply close the rdp client
								// this disconnect the session, which is then subsequently closed (1 sec later if "MaxDisconnectionTime" = 1000 ms)
								case RDP_COMMAND_CLOSE_RDP_CLIENT:
									CloseHandle(wfi->inputsPipe);
									CloseHandle(wfi->imagesPipe);
									Gdiplus::GdiplusShutdown(wfi->gdiplusToken);
									exit(EXIT_SUCCESS);
									break;

								// image encoding
								case RDP_COMMAND_SET_IMAGE_ENCODING:
									wfi->image_encoding = stoi(cmdArgs);
									wfi->image_quality = IMAGE_QUALITY_HIGH;
									break;

								// image quality is tweaked depending on client bandwidth
								case RDP_COMMAND_SET_IMAGE_QUALITY:
									wfi->image_quality = stoi(cmdArgs);
									break;
							}
						}
					}

					// keyboard scancode (non character key)
					else if (inputType == "K")
					{
						int separatorIdx = input.find("-");
						if (separatorIdx != std::string::npos)
						{
							std::string keyCode = input.substr(1, separatorIdx - 1);
							std::string pressed = input.substr(separatorIdx + 1, 1);
							wfi->inst->rdp_send_input_scancode(wfi->inst, pressed == "0", 0, atoi(keyCode.c_str()));
						}
					}

					// keyboard unicode (character key)
					else if (inputType == "U")
					{
						int separatorIdx = input.find("-");
						if (separatorIdx != std::string::npos)
						{
							std::string keyCode = input.substr(1, separatorIdx - 1);
							std::string pressed = input.substr(separatorIdx + 1, 1);
							wfi->inst->rdp_send_input_unicode(wfi->inst, pressed == "0", atoi(keyCode.c_str()));
						}
					}

					// mouse
					else if (inputType == "M")
					{
						if (input.length() >= 3)
						{
							if (input.substr(0, 3) == "MMO")
							{
								SendMouseInput(wfi, input, 3, PTRFLAGS_MOVE);
							}
							else if (input.substr(0, 3) == "MLB")
							{
								uint16 flags = PTRFLAGS_BUTTON1;
								if (input.substr(3, 1) == "1")
								{
									flags = PTRFLAGS_DOWN | PTRFLAGS_BUTTON1;
								}
								SendMouseInput(wfi, input, 4, flags);
							}
							else if (input.substr(0, 3) == "MMB")
							{
								uint16 flags = PTRFLAGS_BUTTON3;
								if (input.substr(3, 1) == "1")
								{
									flags = PTRFLAGS_DOWN | PTRFLAGS_BUTTON3;
								}
								SendMouseInput(wfi, input, 4, flags);
							}
							else if (input.substr(0, 3) == "MRB")
							{
								uint16 flags = PTRFLAGS_BUTTON2;
								if (input.substr(3, 1) == "1")
								{
									flags = PTRFLAGS_DOWN | PTRFLAGS_BUTTON2;
								}
								SendMouseInput(wfi, input, 4, flags);
							}
							else if (input.substr(0, 3) == "MWU")
							{
								SendMouseInput(wfi, input, 3, PTRFLAGS_WHEEL | 0x0078);
							}
							else if (input.substr(0, 3) == "MWD")
							{
								SendMouseInput(wfi, input, 3, PTRFLAGS_WHEEL | PTRFLAGS_WHEEL_NEGATIVE | 0x0088);
							}
						}
					}
				}
			}
		}
	}

	CloseHandle(wfi->inputsPipe);
	CloseHandle(wfi->imagesPipe);
	return 0;
}

void SendMouseInput(wfInfo * wfi, std::string input, int positionIdx, uint16 flags)
{
	int separatorIdx = input.find("-");
	if (separatorIdx != std::string::npos)
	{
		std::string mX = input.substr(positionIdx, separatorIdx - positionIdx);
		std::string mY = input.substr(separatorIdx + 1, input.length() - separatorIdx - 1);
		if (!mX.empty() && stoi(mX) >= 0 && !mY.empty() && stoi(mY) >= 0)
		{
			wfi->inst->rdp_send_input_mouse(wfi->inst, flags, stoi(mX), stoi(mY));
		}
	}
}

DWORD WINAPI SendFullscreen(LPVOID lpParameter)
{
	wfInfo * wfi = (wfInfo *)lpParameter;

	// --------------------------- pipe check -----------------------------------------------------

	if (wfi->settings->remotesession_id == 0)
		return 0;

	// --------------------------- retrieve the fullscreen bitmap ---------------------------------

	Gdiplus::Bitmap *bmpScreen = Gdiplus::Bitmap::FromHBITMAP(wfi->backstore->bitmap, (HPALETTE)0);

	// ---------------------------  process it ----------------------------------------------------

	processImage(wfi, bmpScreen, true);

	// ---------------------------  cleanup -------------------------------------------------------

	delete bmpScreen;
	bmpScreen = NULL;

	return 0;
}

void processImage(wfInfo* wfi, Gdiplus::Bitmap* bmp, bool fullscreen)
{
	IStream* pngStream = NULL;
	IStream* jpgStream = NULL;
	IStream* webpStream = NULL;

	STATSTG statstg;

	int format;
	int quality = (fullscreen ? IMAGE_QUALITY_HIGHER : wfi->image_quality);	// use higher quality for fullscreen updates; otherwise current
	IStream* stream = NULL;
	ULONG size = 0;

	// normally, the PNG format is best suited (lower size and better quality) for office applications (with text) and JPG for graphic ones (with images)
	// PNG is lossless as opposite to JPG
	// WEBP can either be lossy or lossless

	if (wfi->image_encoding == IMAGE_ENCODING_PNG || wfi->image_encoding == IMAGE_ENCODING_JPEG || wfi->image_encoding == IMAGE_ENCODING_PNG_JPEG)
	{
		ULONG pngSize;
		ULONG jpgSize;

		// --------------------------- convert the bitmap into PNG --------------------------------

		if (wfi->image_encoding == IMAGE_ENCODING_PNG || wfi->image_encoding == IMAGE_ENCODING_PNG_JPEG)
		{
			CreateStreamOnHGlobal(NULL, TRUE, &pngStream);
			bmp->Save(pngStream, &wfi->pngClsid);

			pngStream->Stat(&statstg, STATFLAG_DEFAULT);
			pngSize = (ULONG)statstg.cbSize.LowPart;
		}

		// --------------------------- convert the bitmap into JPEG -------------------------------

		if (wfi->image_encoding == IMAGE_ENCODING_JPEG || wfi->image_encoding == IMAGE_ENCODING_PNG_JPEG)
		{
			CreateStreamOnHGlobal(NULL, TRUE, &jpgStream);
			wfi->encoderParameters.Parameter[0].Value = &quality;
			bmp->Save(jpgStream, &wfi->jpgClsid, &wfi->encoderParameters);

			jpgStream->Stat(&statstg, STATFLAG_DEFAULT);
			jpgSize = (ULONG)statstg.cbSize.LowPart;
		}

		// ---------------------------  use the lowest sized format -------------------------------

		// text, buttons, menus, etc... (simple image structure and low color palette) are more likely to be lower sized in PNG than JPG
		// on the opposite, a complex image (photo or graphical) is more likely to be lower sized in JPG

		if (wfi->image_encoding == IMAGE_ENCODING_PNG || (wfi->image_encoding == IMAGE_ENCODING_PNG_JPEG && pngSize <= jpgSize))
		{
			stream = pngStream;
			format = IMAGE_FORMAT_PNG;
			quality = IMAGE_QUALITY_HIGHEST;	// lossless
			size = pngSize;
		}
		else
		{
			stream = jpgStream;
			format = IMAGE_FORMAT_JPEG;
			size = jpgSize;
		}
	}
	else if (wfi->image_encoding == IMAGE_ENCODING_WEBP)
	{
		// --------------------------- convert the bitmap into WEBP -------------------------------

		CreateStreamOnHGlobal(NULL, TRUE, &webpStream);
		WebPEncoder(wfi, bmp, webpStream, quality, fullscreen);

		webpStream->Stat(&statstg, STATFLAG_DEFAULT);
		ULONG webpSize = (ULONG)statstg.cbSize.LowPart;

		stream = webpStream;
		format = IMAGE_FORMAT_WEBP;
		size = webpSize;
	}

	// ---------------------------  send the image ------------------------------------------------

	// in order to avoid overloading both the bandwidth and the browser, images are limited to 1024 KB each

	if (stream != NULL && size > 0 && size <= IMAGES_PIPE_BUFFER_SIZE)
	{
		sendImage(
			wfi,
			bmp,
			++imageIdx,
			(fullscreen ? 0 : imageLeft),
			(fullscreen ? 0 : imageTop),
			(fullscreen ? bmp->GetWidth() : imageRight - imageLeft),
			(fullscreen ? bmp->GetHeight() : imageBottom - imageTop),
			format,
			quality,
			stream,
			size,
			fullscreen);
	}

	// ---------------------------  cleanup -------------------------------------------------------

	if (pngStream != NULL)
	{
		pngStream->Release();
		pngStream = NULL;
	}

	if (jpgStream != NULL)
	{
		jpgStream->Release();
		jpgStream = NULL;
	}

	if (webpStream != NULL)
	{
		webpStream->Release();
		webpStream = NULL;
	}
}

void saveImage(wfInfo * wfi, Gdiplus::Bitmap* bmp, int idx, int format, int quality, bool fullscreen)
{
	std::string imgDirectoryPath = createRemoteSessionDirectory(wfi);
	if (imgDirectoryPath != "")
	{
		std::stringstream ss;
		ss << imgDirectoryPath;

		switch (format)
		{
			case IMAGE_FORMAT_PNG:
				ss << (fullscreen ? "\\screen_" : "\\region_") << idx << ".png";
				break;

			case IMAGE_FORMAT_JPEG:
				ss << (fullscreen ? "\\screen_" : "\\region_") << idx << "_" << quality << ".jpg";
				break;

			case IMAGE_FORMAT_CUR:
				ss << "\\cursor_" << idx << ".png";
				break;
		}

		std::string s = ss.str();
		std::wstring ws = s2ws(s);
		const wchar_t *filename = ws.c_str();

		switch (format)
		{
			case IMAGE_FORMAT_PNG:
			case IMAGE_FORMAT_CUR:
				bmp->Save(filename, &wfi->pngClsid);
				break;

			case IMAGE_FORMAT_JPEG:
				wfi->encoderParameters.Parameter[0].Value = &quality;
				bmp->Save(filename, &wfi->jpgClsid, &wfi->encoderParameters);
				break;
		}
	}
}

void sendImage(wfInfo * wfi, Gdiplus::Bitmap* bmp, int idx, int posX, int posY, int width, int height, int format, int quality, IStream* stream, int size, bool fullscreen)
{
	// seek to the beginning of the stream
	LARGE_INTEGER li = {0};
	stream->Seek(li, STREAM_SEEK_SET, NULL);
			
	// read the stream into a buffer
	char *buffer = new char[size];
	ULONG bytesRead;
	stream->Read(buffer, size, &bytesRead);

	// encode the buffer to base64
	BIO *bmem, *b64;
	BUF_MEM *bptr;

	b64 = BIO_new(BIO_f_base64());
	bmem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, bmem);
	BIO_write(b64, buffer, size);
	BIO_flush(b64);
	BIO_get_mem_ptr(b64, &bptr);

	char *b64buffer = (char *)malloc(bptr->length);
	memcpy(b64buffer, bptr->data, bptr->length-1);
	b64buffer[bptr->length-1] = 0;

	// serialize the image
	std::stringstream ss;
	ss << idx << "," << posX << "," << posY << "," << width << "," << height << "," << format << "," << quality << "," << b64buffer << "," << (fullscreen ? "1" : "0");
	std::string s = ss.str();
	const char *imgData = s.c_str();

	DWORD bytesToWrite = s.length();
	DWORD bytesWritten;

	// enqueue it
	if (WriteFile(wfi->imagesPipe, imgData, bytesToWrite, &bytesWritten, NULL) == 0)
	{
		switch (GetLastError())
		{
			case ERROR_INVALID_HANDLE:
				printf("%s ImagesPipe: WriteFile failed with error ERROR_INVALID_HANDLE\n", getCurrentTime().c_str());
				break;

			case ERROR_PIPE_NOT_CONNECTED:
				printf("%s ImagesPipe: WriteFile failed with error ERROR_PIPE_NOT_CONNECTED\n", getCurrentTime().c_str());
				break;

			case ERROR_PIPE_BUSY:
				printf("%s ImagesPipe: WriteFile failed with error ERROR_PIPE_BUSY\n", getCurrentTime().c_str());
				break;

			case ERROR_BAD_PIPE:
				printf("%s ImagesPipe: WriteFile failed with error ERROR_BAD_PIPE\n", getCurrentTime().c_str());
				break;

			case ERROR_BROKEN_PIPE:
				printf("%s ImagesPipe: WriteFile failed with error ERROR_BROKEN_PIPE\n", getCurrentTime().c_str());
				break;

			default:
				printf("%s ImagesPipe: WriteFile failed with error %d\n", getCurrentTime().c_str(), GetLastError());
				break;
		}

		// pipe problem; exit
		CloseHandle(wfi->inputsPipe);
		CloseHandle(wfi->imagesPipe);
		Gdiplus::GdiplusShutdown(wfi->gdiplusToken);
		exit(EXIT_SUCCESS);
	}

	//printf("%s ImagesPipe: WriteFile succeeded for image: %i (%s)\n", getCurrentTime().c_str(), idx, (fullscreen ? "screen" : "region"));

	// images are saved under parent "log\remotesession_#ID.#PID" folder
	//saveImage(wfi, bmp, idx, format, quality, fullscreen);	// debug. enable with caution as it will flood the disk and hinder performance!!!

	// cleanup
	delete[] b64buffer;
	b64buffer = NULL;

	BIO_free_all(b64);

	delete[] buffer;
	buffer = NULL;
}

char *unbase64(char *b64buffer, int length)
{
	char *msg = (char *)malloc(length);
    memset(msg, 0x00, length);

    //printf("b64buffer: %s\n", b64buffer);

    BIO *b64, *mem = NULL;

    b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    
	mem = BIO_new_mem_buf(b64buffer, length);
    mem = BIO_push(b64, mem);

    int readbytes = -1;
    while ((readbytes = BIO_read(mem, msg, length)) > 0)
    {
		//printf("readbytes: %d\n", readbytes);
    }

    BIO_flush(mem);
    //printf("msg: %s\n", msg);

    BIO_free_all(mem);
    //BIO_free_all(b64);

    return msg;
}

void WebPEncoder(wfInfo* wfi, Gdiplus::Bitmap* bmp, IStream* stream, float quality, bool fullscreen)
{
	WebPPicture webpPic;

	if (WebPPictureInit(&webpPic))
	{
		// debug

		//if (!createRemoteSessionDirectory(wfi))
		//	return;

		//std::stringstream ss;
		//ss << ".\\remotesession_" << wfi->settings->remotesession_id << (fullscreen ? "\\screen_" : "\\region_") << imageIdx << "_" << quality << ".webp";
		//std::string s = ss.str();
		//webpFilename = s.c_str();

		webpPic.custom_ptr = (void*)stream;
		webpPic.writer = WebPWriter;

		webpPic.width = bmp->GetWidth();
		webpPic.height = bmp->GetHeight();

		Gdiplus::BitmapData* bmpData = new Gdiplus::BitmapData();
		Gdiplus::Rect* rect = new Gdiplus::Rect(0, 0, bmp->GetWidth(), bmp->GetHeight());
		bmp->LockBits(rect, ImageLockModeRead, PixelFormat32bppARGB, bmpData);

		const uint8_t* bmpBits = (uint8_t*)bmpData->Scan0;

		if (WebPPictureImportBGRA(&webpPic, bmpBits, bmpData->Stride))
		{
			wfi->webpConfig.quality = quality;

			if (!WebPEncode(&wfi->webpConfig, &webpPic))
				printf("%s WebpEncode: WebP encoding failed\n", getCurrentTime().c_str());
		}

		// cleanup

		bmp->UnlockBits(bmpData);

		delete rect;
		rect = NULL;

		delete bmpData;
		bmpData = NULL;

		WebPPictureFree(&webpPic);
	}
}

static int WebPWriter(const uint8_t* data, size_t data_size, const WebPPicture* const pic)
{
	IStream* stream = (IStream*)pic->custom_ptr;

	ULONG bytesWritten;
	stream->Write(data, data_size, &bytesWritten);

	// debug

	//FILE* file = fopen(webpFilename, "ab");
	//if (file != NULL)
	//{
	//	fwrite(data, 1, data_size, file);
	//	fclose(file);
	//}

	return bytesWritten == data_size ? 1 : 0;
}

#pragma endregion