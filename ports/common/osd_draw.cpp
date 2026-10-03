/*
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdio.h>

#include "osd.h"
#include "dosbox.h"
#include "mem.h"
#include "platform.h"
#include "osd_font.h"
#include "dosbox_splash.h"

void OSD_DecodeGimpImage(void *image_buf, const unsigned char *rle_data,
                         unsigned int size, unsigned int bpp) {
	GIMP_IMAGE_RUN_LENGTH_DECODE((unsigned char *)image_buf, rle_data, size, bpp);
}

static OSD_BeginFrameFn osd_begin_frame = NULL;
static OSD_EndFrameFn osd_end_frame = NULL;
static OSD_PadStateFn osd_pad_state = NULL;
static OSD_FrameLimitFn osd_frame_limit = NULL;
static enum OSDPixelFormat osd_format = OSD_PIXFMT_RGBA;
static int osd_width = 0;
static int osd_height = 0;

void OSD_Init(OSD_BeginFrameFn begin, OSD_EndFrameFn end, enum OSDPixelFormat format) {
	osd_begin_frame = begin;
	osd_end_frame = end;
	osd_format = format;

	OSDCanvas canvas;
	if (OSD_BeginFrame(&canvas)) {
		osd_width = canvas.width;
		osd_height = canvas.height;
		OSD_EndFrame();
	}
}

void OSD_SetInputCallbacks(OSD_PadStateFn pad, OSD_FrameLimitFn limit) {
	osd_pad_state = pad;
	osd_frame_limit = limit;
}

bool OSD_ReadPad(struct OSDPad *pad) {
	if (!osd_pad_state || !pad) return false;
	return osd_pad_state(pad);
}

void OSD_FrameLimit(void) {
	if (osd_frame_limit) osd_frame_limit();
}

static const char *const osd_shader_names[OSD_SHADER_COUNT] = {
	"none", "smooth", "scanlines", "aperture", "crt"
};
static int osd_shader_selected = OSD_SHADER_NONE;

const char *OSD_ShaderName(int shader) {
	if (shader < 0 || shader >= OSD_SHADER_COUNT) return osd_shader_names[0];
	return osd_shader_names[shader];
}

void OSD_ShaderSelect(int shader) {
	if (shader < 0 || shader >= OSD_SHADER_COUNT) return;
	osd_shader_selected = shader;
}

int OSD_ShaderSelected(void) {
	return osd_shader_selected;
}

void OSD_Shutdown(void) {
	osd_begin_frame = NULL;
	osd_end_frame = NULL;
	osd_pad_state = NULL;
	osd_frame_limit = NULL;
}

void OSD_ScreenSize(int *width, int *height) {
	if (width) *width = osd_width;
	if (height) *height = osd_height;
}

bool OSD_BeginFrame(struct OSDCanvas *canvas) {
	struct OSDCanvas local;
	if (!osd_begin_frame) return false;
	if (!osd_begin_frame(&local)) return false;
	*canvas = local;
	if (local.width > osd_width) osd_width = local.width;
	if (local.height > osd_height) osd_height = local.height;
	return true;
}

void OSD_EndFrame(void) {
	if (osd_end_frame) osd_end_frame();
}

uint32_t OSD_Colour(int r, int g, int b) {
	return OSD_pack(r, g, b, osd_format);
}

void OSD_FillRect(struct OSDCanvas *c, int x, int y, int w, int h, uint32_t colour) {
	if (w <= 0 || h <= 0) return;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > c->width) w = c->width - x;
	if (y + h > c->height) h = c->height - y;
	if (w <= 0 || h <= 0) return;

	for (int row = 0; row < h; row++) {
		uint32_t *dst = c->pixels + (y + row) * c->pitch + x;
		for (int col = 0; col < w; col++) dst[col] = colour;
	}
}

void OSD_DrawRect(struct OSDCanvas *c, int x, int y, int w, int h, uint32_t colour) {
	if (w <= 0 || h <= 0) return;
	OSD_FillRect(c, x, y, w, 1, colour);
	OSD_FillRect(c, x, y + h - 1, w, 1, colour);
	OSD_FillRect(c, x, y, 1, h, colour);
	OSD_FillRect(c, x + w - 1, y, 1, h, colour);
}

static uint32_t OSD_Lerp(uint32_t a, uint32_t b, int t, int tmax) {
	if (tmax <= 0) return a;
	int ca[4], cb[4];
	ca[0] = (int)(a & 0xff);         cb[0] = (int)(b & 0xff);
	ca[1] = (int)((a >> 8) & 0xff);  cb[1] = (int)((b >> 8) & 0xff);
	ca[2] = (int)((a >> 16) & 0xff); cb[2] = (int)((b >> 16) & 0xff);
	ca[3] = (int)((a >> 24) & 0xff); cb[3] = (int)((b >> 24) & 0xff);
	unsigned cr[4];
	for (int i = 0; i < 4; i++) {
		int v = ca[i] + ((cb[i] - ca[i]) * t) / tmax;
		cr[i] = (unsigned)(v < 0 ? 0 : (v > 255 ? 255 : v));
	}
	return cr[0] | (cr[1] << 8) | (cr[2] << 16) | (cr[3] << 24);
}

void OSD_GradientV(struct OSDCanvas *c, int x, int y, int w, int h,
                   uint32_t top, uint32_t bottom) {
	if (w <= 0 || h <= 0) return;
	for (int row = 0; row < h; row++) {
		int sy = y + row;
		if (sy < 0 || sy >= c->height) continue;
		OSD_FillRect(c, x, sy, w, 1, OSD_Lerp(top, bottom, row, h - 1));
	}
}

void OSD_GradientH(struct OSDCanvas *c, int x, int y, int w, int h,
                   uint32_t left, uint32_t right) {
	if (w <= 0 || h <= 0) return;
	for (int col = 0; col < w; col++) {
		OSD_FillRect(c, x + col, y, 1, h, OSD_Lerp(left, right, col, w - 1));
	}
}

void OSD_BlendRect(struct OSDCanvas *c, int x, int y, int w, int h,
                   uint32_t colour, int alpha) {
	if (w <= 0 || h <= 0) return;
	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;
	if (alpha == 0) return;
	if (alpha == 255) { OSD_FillRect(c, x, y, w, h, colour); return; }

	unsigned sr = colour & 0xff, sg = (colour >> 8) & 0xff, sb = (colour >> 16) & 0xff;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > c->width) w = c->width - x;
	if (y + h > c->height) h = c->height - y;
	if (w <= 0 || h <= 0) return;

	for (int row = 0; row < h; row++) {
		uint32_t *dst = c->pixels + (y + row) * c->pitch + x;
		for (int col = 0; col < w; col++) {
			uint32_t d = dst[col];
			unsigned dr = d & 0xff, dg = (d >> 8) & 0xff, db = (d >> 16) & 0xff;
			dr = (dr * (255 - alpha) + sr * alpha) / 255;
			dg = (dg * (255 - alpha) + sg * alpha) / 255;
			db = (db * (255 - alpha) + sb * alpha) / 255;
			dst[col] = 0xff000000u | (db << 16) | (dg << 8) | dr;
		}
	}
}

void OSD_Panel(struct OSDCanvas *c, int x, int y, int w, int h,
               uint32_t fill, uint32_t border, int alpha) {
	OSD_BlendRect(c, x, y, w, h, fill, alpha);
	OSD_BlendRect(c, x, y, w, 1, border, alpha < 200 ? alpha + 40 : 255);
	OSD_BlendRect(c, x, y + h - 1, w, 1, OSD_Lerp(fill, 0x000000, 1, 3), alpha);
	OSD_BlendRect(c, x, y, 1, h, border, alpha < 200 ? alpha + 40 : 255);
	OSD_BlendRect(c, x + w - 1, y, 1, h, OSD_Lerp(fill, 0x000000, 1, 3), alpha);
}

static int OSD_FontIndex(unsigned char ch) {
	if (ch < OSD_FONT_FIRST || ch >= OSD_FONT_FIRST + OSD_FONT_COUNT) return -1;
	return (int)(ch - OSD_FONT_FIRST);
}

static const struct OSDFontGlyph *OSD_FontGlyph(int rig, unsigned char ch) {
	const int i = OSD_FontIndex(ch);
	if (i < 0) return NULL;
	return &osd_font_glyphs[rig][i];
}

static int OSD_FontRig(int scale, int *magnify) {
	int rig = (scale < 1) ? 1 : scale;
	if (rig > OSD_FONT_RIGS) rig = OSD_FONT_RIGS;
	*magnify = (scale > OSD_FONT_RIGS)
		? (scale + OSD_FONT_RIGS - 1) / OSD_FONT_RIGS
		: 1;
	return rig - 1;
}

static inline uint32_t OSD_BlendPixel(uint32_t dst, uint32_t colour, unsigned a) {
	if (a == 0) return dst;
	if (a >= 255) return colour;
	const unsigned ia = 255 - a;
	const unsigned d0 = dst & 0xff, d1 = (dst >> 8) & 0xff, d2 = (dst >> 16) & 0xff;
	const unsigned s0 = colour & 0xff, s1 = (colour >> 8) & 0xff, s2 = (colour >> 16) & 0xff;
	const unsigned r0 = (d0 * ia + s0 * a + 127) / 255;
	const unsigned r1 = (d1 * ia + s1 * a + 127) / 255;
	const unsigned r2 = (d2 * ia + s2 * a + 127) / 255;
	return 0xff000000u | (r2 << 16) | (r1 << 8) | r0;
}

int OSD_TextWidth(const char *text, int scale) {
	int magnify;
	const int rig = OSD_FontRig(scale, &magnify);
	int total = 0;
	for (const char *p = text; *p; p++) {
		if (*p == '\n' || *p == '\r') continue;
		const struct OSDFontGlyph *g = OSD_FontGlyph(rig, (unsigned char)*p);
		if (!g) continue;
		total += (int)g->advance * magnify;
	}
	return total;
}

static int OSD_Glyph(struct OSDCanvas *c, int x, int y, unsigned char ch,
                     uint32_t colour, int rig, int magnify) {
	const struct OSDFontGlyph *g = OSD_FontGlyph(rig, ch);
	if (!g) return 0;
	if (g->width == 0 || g->height == 0) return (int)g->advance * magnify;

	const unsigned char *box = osd_font_alpha + g->offset;
	const int left = x + (int)g->bearing * magnify;

	for (int row = 0; row < g->height; row++) {
		const int sy = y + ((int)g->top + row) * magnify;
		if (sy >= c->height) break;
		const unsigned char *src = box + (size_t)row * g->width;
		for (int col = 0; col < g->width; col++) {
			const unsigned a = src[col];
			if (!a) continue;
			const int sx = left + col * magnify;
			for (int dy = 0; dy < magnify; dy++) {
				const int py = sy + dy;
				if (py < 0 || py >= c->height) continue;
				uint32_t *dst = c->pixels + (size_t)py * c->pitch;
				for (int dx = 0; dx < magnify; dx++) {
					const int px = sx + dx;
					if (px < 0 || px >= c->width) continue;
					dst[px] = OSD_BlendPixel(dst[px], colour, a);
				}
			}
		}
	}
	return (int)g->advance * magnify;
}

void OSD_Text(struct OSDCanvas *c, int x, int y, const char *text,
              uint32_t colour, int scale, bool shadow) {
	if (!text) return;
	int magnify;
	const int rig = OSD_FontRig(scale, &magnify);
	(void)shadow;
	int cx = x;
	for (const char *p = text; *p; p++) {
		const unsigned char ch = (unsigned char)*p;
		if (ch == '\n' || ch == '\r') continue;
		cx += OSD_Glyph(c, cx, y, ch, colour, rig, magnify);
	}
}

void OSD_TextCentre(struct OSDCanvas *c, int cx, int y, const char *text,
                    uint32_t colour, int scale, bool shadow) {
	OSD_Text(c, cx - OSD_TextWidth(text, scale) / 2, y, text, colour, scale, shadow);
}

void OSD_TextRight(struct OSDCanvas *c, int rx, int y, const char *text,
                   uint32_t colour, int scale, bool shadow) {
	OSD_Text(c, rx - OSD_TextWidth(text, scale), y, text, colour, scale, shadow);
}

static OSD_StatusFn osd_status = NULL;

void OSD_SetStatusCallbacks(OSD_StatusFn status) {
	osd_status = status;
}

#define OSD_STATUS_REFRESH_MS 5000

static const struct OSDStatus *OSD_Status(void) {
	static struct OSDStatus cached;
	static Bit32u next_read = 0;
	static bool read_once = false;
	if (!osd_status) return NULL;
	const Bit32u now = (Bit32u)Platform_GetTicks();
	if (read_once && (Bit32s)(now - next_read) < 0) return &cached;
	read_once = true;
	next_read = now + OSD_STATUS_REFRESH_MS;
	memset(&cached, 0, sizeof(cached));
	if (!osd_status(&cached)) memset(&cached, 0, sizeof(cached));
	return &cached;
}

static void OSD_SizeText(char *out, size_t out_size, unsigned long long bytes) {
	const unsigned long long mb = bytes / (1024ull * 1024ull);
	if (mb >= 1024ull) {
		const unsigned long long tenths =
			(bytes / (1024ull * 1024ull * 1024ull / 10ull)) % 10ull;
		snprintf(out, out_size, "%llu.%llu GB", mb / 1024ull, tenths);
	} else if (mb >= 1ull) {
		snprintf(out, out_size, "%llu MB", mb);
	} else {
		snprintf(out, out_size, "%llu KB", bytes / 1024ull);
	}
}

static int OSD_DrawBattery(struct OSDCanvas *c, int x, int y, const struct OSDStatus *st) {
	const int case_w = 40, case_h = 20;
	const int nub_w = 3;
	const uint32_t case_colour = OSD_Colour(0x80, 0x90, 0xad);
	const uint32_t inner = OSD_Colour(0x0d, 0x13, 0x26);

	int level = st->battery_percent;
	if (level < 0) level = 0;
	if (level > 100) level = 100;

	uint32_t fill = OSD_Colour(0x5c, 0xd6, 0x8b);
	if (level <= 10) fill = OSD_Colour(0xff, 0x6b, 0x6b);
	else if (level <= 20) fill = OSD_Colour(0xff, 0xc0, 0x60);
	if (st->battery_charging) fill = OSD_Colour(0x9e, 0xc4, 0xff);

	OSD_DrawRoundedRect(c, x, y, case_w, case_h, 4, case_colour);
	OSD_DrawRoundedRect(c, x + 2, y + 2, case_w - 4, case_h - 4, 3, inner);

	const int room = case_w - 8;
	int w = room * level / 100;
	if (level > 0 && w < 2) w = 2;
	if (w > 0) OSD_FillRect(c, x + 4, y + 4, w, case_h - 8, fill);

	OSD_FillRect(c, x + case_w, y + 6, nub_w, case_h - 12, case_colour);
	if (st->battery_charging) {
		const int cx = x + case_w / 2;
		const uint32_t bolt = OSD_Colour(0xff, 0xff, 0xff);
		OSD_FillRect(c, cx - 1, y + 3, 3, 6, bolt);
		OSD_FillRect(c, cx - 4, y + 8, 9, 3, bolt);
		OSD_FillRect(c, cx - 2, y + 11, 3, 6, bolt);
	}
	return case_w + nub_w;
}

void OSD_DrawStatusReadout(struct OSDCanvas *c, int right, int y) {
	const struct OSDStatus *st = OSD_Status();
	if (!st) return;

	const int scale = 2;
	const int gap = 28;
	int x = right;

	if (st->card_valid) {
		char size_text[32];
		char line[48];
		OSD_SizeText(size_text, sizeof(size_text), st->card_free_bytes);
		snprintf(line, sizeof(line), "SD %s", size_text);
		const uint32_t colour = (st->card_free_bytes < 1024ull * 1024ull * 1024ull)
			? OSD_Colour(0xff, 0xc0, 0x60) : OSD_Colour(0x80, 0x90, 0xad);
		const int w = OSD_TextWidth(line, scale);
		OSD_Text(c, x - w, y, line, colour, scale, false);
		x -= w + gap;
	}

	if (st->battery_valid) {
		char line[16];
		snprintf(line, sizeof(line), "%d%%", st->battery_percent);
		const int w = OSD_TextWidth(line, scale);
		OSD_Text(c, x - w, y, line, OSD_Colour(0xe8, 0xee, 0xf8), scale, false);
		x -= w + 10;
		x -= OSD_DrawBattery(c, x - 43, y + (OSD_FONT_HEIGHT * scale - 20) / 2, st);
	}
}

void OSD_DrawSplashLogo(struct OSDCanvas *c, int x, int y, int w, int h) {
	static unsigned char splash_rgb[640 * 400 * 3];
	static bool splash_decoded = false;
	if (!splash_decoded) {
		extern void OSD_DecodeGimpImage(void *image_buf, const unsigned char *rle_data,
		                                unsigned int size, unsigned int bpp);
		OSD_DecodeGimpImage(splash_rgb, gimp_image.rle_pixel_data,
		                    sizeof(splash_rgb) / 3, 3);
		splash_decoded = true;
	}

	const uint32_t step_x = (uint32_t)(((uint64_t)640 << 16) / (uint32_t)w);
	const uint32_t step_y = (uint32_t)(((uint64_t)400 << 16) / (uint32_t)h);
	const bool swap = (osd_format == OSD_PIXFMT_RGBA);

	for (uint32_t row = 0; row < (uint32_t)h; row++) {
		const unsigned char *src = splash_rgb + (uint64_t)(step_y * row >> 16) * 640 * 3;
		int32_t sy = y + (int32_t)row;
		if (sy < 0 || sy >= c->height) continue;
		uint32_t sx = 0;
		for (uint32_t col = 0; col < (uint32_t)w; col++) {
			int32_t dx = x + (int32_t)col;
			if (dx >= 0 && dx < c->width) {
				uint32_t off = (sx >> 16) * 3;
				unsigned r = src[off + 0];
				unsigned g = src[off + 1];
				unsigned b = src[off + 2];
				c->pixels[sy * c->pitch + dx] = swap
					? (0xffu << 24 | b << 16 | g << 8 | r)
					: (0xffu << 24 | r << 16 | g << 8 | b);
			}
			sx += step_x;
		}
	}
}

void OSD_DrawRoundedRect(struct OSDCanvas *c, int x, int y, int w, int h, int r, uint32_t colour) {
	if (w <= 0 || h <= 0) return;
	int min_half = (w < h ? w : h) / 2;
	if (r <= 0) {
		OSD_FillRect(c, x, y, w, h, colour);
		return;
	}
	if (r > min_half) r = min_half;

	int mid_y = y + r;
	int mid_h = h - 2 * r;
	if (mid_h > 0) {
		OSD_FillRect(c, x, mid_y, w, mid_h, colour);
	}

	float radius_sq = (float)r * (float)r;
	for (int row = 0; row < r; row++) {
		float dist_y = (float)r - (float)row - 0.5f;
		float chord = sqrtf(radius_sq - dist_y * dist_y);
		int inset = r - (int)(chord + 0.5f);
		if (inset < 0) inset = 0;
		int row_w = w - 2 * inset;
		if (row_w <= 0) continue;

		OSD_FillRect(c, x + inset, y + row, row_w, 1, colour);

		int bot_y = y + h - 1 - row;
		if (bot_y > y + row) {
			OSD_FillRect(c, x + inset, bot_y, row_w, 1, colour);
		}
	}
}

void OSD_DrawRoundedPill(struct OSDCanvas *c, int x, int y, int w, int h, int r, uint32_t fill, uint32_t border) {
	if (border != fill) {
		OSD_DrawRoundedRect(c, x, y, w, h, r, border);
		OSD_DrawRoundedRect(c, x + 1, y + 1, w - 2, h - 2, (r > 1) ? (r - 1) : 1, fill);
	} else {
		OSD_DrawRoundedRect(c, x, y, w, h, r, fill);
	}
}

int OSD_DrawButtonGlyph(struct OSDCanvas *c, int x, int y, enum OSDButtonGlyph glyph) {
	const int gh = 26;
	const int text_y = y + (gh - OSD_FONT_HEIGHT) / 2;
	const uint32_t text_white = OSD_Colour(255, 255, 255);

	switch (glyph) {
	case OSD_GLYPH_A: {
		const int gw = 26;
		OSD_DrawRoundedRect(c, x, y, gw, gh, 13, OSD_Colour(229, 37, 33));
		int tw = OSD_TextWidth("A", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "A", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_B: {
		const int gw = 26;
		OSD_DrawRoundedRect(c, x, y, gw, gh, 13, OSD_Colour(60, 68, 82));
		int tw = OSD_TextWidth("B", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "B", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_X: {
		const int gw = 26;
		OSD_DrawRoundedRect(c, x, y, gw, gh, 13, OSD_Colour(0, 136, 204));
		int tw = OSD_TextWidth("X", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "X", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_Y: {
		const int gw = 26;
		OSD_DrawRoundedRect(c, x, y, gw, gh, 13, OSD_Colour(245, 158, 11));
		int tw = OSD_TextWidth("Y", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "Y", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_PLUS: {
		const int gw = 26;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 13, OSD_Colour(45, 55, 75), OSD_Colour(80, 95, 125));
		int cx = x + 13, cy = y + 13;
		OSD_FillRect(c, cx - 5, cy - 1, 11, 2, text_white);
		OSD_FillRect(c, cx - 1, cy - 5, 2, 11, text_white);
		return gw;
	}
	case OSD_GLYPH_MINUS: {
		const int gw = 26;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 13, OSD_Colour(45, 55, 75), OSD_Colour(80, 95, 125));
		int cx = x + 13, cy = y + 13;
		OSD_FillRect(c, cx - 5, cy - 1, 11, 2, text_white);
		return gw;
	}
	case OSD_GLYPH_L: {
		const int gw = 32;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(38, 48, 68), OSD_Colour(75, 90, 120));
		int tw = OSD_TextWidth("L", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "L", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_R: {
		const int gw = 32;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(38, 48, 68), OSD_Colour(75, 90, 120));
		int tw = OSD_TextWidth("R", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "R", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_ZL: {
		const int gw = 38;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(38, 48, 68), OSD_Colour(75, 90, 120));
		int tw = OSD_TextWidth("ZL", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "ZL", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_ZR: {
		const int gw = 38;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(38, 48, 68), OSD_Colour(75, 90, 120));
		int tw = OSD_TextWidth("ZR", 1);
		OSD_Text(c, x + (gw - tw) / 2, text_y, "ZR", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_LR: {
		int tw = OSD_TextWidth("L/R", 1);
		const int gw = tw + 16;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(38, 48, 68), OSD_Colour(75, 90, 120));
		OSD_Text(c, x + (gw - tw) / 2, text_y, "L/R", text_white, 1, false);
		return gw;
	}
	case OSD_GLYPH_DPAD: {
		const int gw = 32;
		OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(45, 55, 75), OSD_Colour(80, 95, 125));
		int cx = x + 16, cy = y + 13;
		OSD_FillRect(c, cx - 6, cy - 2, 13, 5, text_white);
		OSD_FillRect(c, cx - 2, cy - 6, 5, 13, text_white);
		OSD_FillRect(c, cx - 1, cy - 1, 3, 3, OSD_Colour(45, 55, 75));
		return gw;
	}
	default:
		return 0;
	}
}

int OSD_DrawButtonGlyphByName(struct OSDCanvas *c, int x, int y, const char *name) {
	if (!name || !name[0]) return 0;
	if (!strcasecmp(name, "A")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_A);
	if (!strcasecmp(name, "B")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_B);
	if (!strcasecmp(name, "X")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_X);
	if (!strcasecmp(name, "Y")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_Y);
	if (!strcmp(name, "+") || !strcasecmp(name, "PLUS")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_PLUS);
	if (!strcmp(name, "-") || !strcasecmp(name, "MINUS")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_MINUS);
	if (!strcasecmp(name, "L")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_L);
	if (!strcasecmp(name, "R")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_R);
	if (!strcasecmp(name, "ZL")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_ZL);
	if (!strcasecmp(name, "ZR")) return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_ZR);
	if (!strcasecmp(name, "L/R") || !strcasecmp(name, "LR") || !strcasecmp(name, "L / R"))
		return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_LR);
	if (!strcasecmp(name, "DPAD") || !strcasecmp(name, "D-PAD") || !strcasecmp(name, "D-pad"))
		return OSD_DrawButtonGlyph(c, x, y, OSD_GLYPH_DPAD);

	int tw = OSD_TextWidth(name, 1);
	int gw = tw + 14;
	if (gw < 26) gw = 26;
	const int gh = 26;
	OSD_DrawRoundedPill(c, x, y, gw, gh, 6, OSD_Colour(45, 55, 75), OSD_Colour(80, 95, 125));
	OSD_Text(c, x + (gw - tw) / 2, y + (gh - OSD_FONT_HEIGHT) / 2, name,
	         OSD_Colour(255, 255, 255), 1, false);
	return gw;
}

int OSD_DrawHintItem(struct OSDCanvas *c, int x, int y, const char *button, const char *label) {
	int gw = OSD_DrawButtonGlyphByName(c, x, y, button);
	int total_w = gw;
	if (label && label[0]) {
		int lx = x + gw + 8;
		int ly = y + (26 - OSD_FONT_HEIGHT) / 2;
		OSD_Text(c, lx, ly, label, OSD_Colour(220, 230, 245), 1, false);
		total_w += 8 + OSD_TextWidth(label, 1);
	}
	return total_w;
}

