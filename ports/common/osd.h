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

#ifndef DOSBOX_CONSOLE_OSD_H
#define DOSBOX_CONSOLE_OSD_H

#include "config.h"

#include <stddef.h>
#include <stdint.h>

struct OSDCanvas {
	uint32_t *pixels;
	int width;
	int height;
	int pitch;
};

struct OSDPad {
	bool up, down, left, right;
	bool a, b, x, y;
	bool l, r, zl, zr;
	bool plus, minus;
	bool stick_l, stick_r;
	float lx, ly;
};

enum OSDPixelFormat {
	OSD_PIXFMT_RGBA,
	OSD_PIXFMT_BGRA
};

static inline uint32_t OSD_pack(int r, int g, int b, enum OSDPixelFormat f) {
	return (f == OSD_PIXFMT_RGBA) ? (0xffu << 24 | (unsigned)b << 16 |
	                                (unsigned)g << 8 | (unsigned)r)
	                              : (0xffu << 24 | (unsigned)r << 16 |
	                                 (unsigned)g << 8 | (unsigned)b);
}

typedef bool (*OSD_BeginFrameFn)(OSDCanvas *canvas);
typedef void (*OSD_EndFrameFn)(void);
typedef bool (*OSD_PadStateFn)(struct OSDPad *pad);
typedef void (*OSD_FrameLimitFn)(void);

void OSD_Init(OSD_BeginFrameFn begin, OSD_EndFrameFn end, enum OSDPixelFormat format);
void OSD_SetInputCallbacks(OSD_PadStateFn pad, OSD_FrameLimitFn limit);
void OSD_Shutdown(void);

void OSD_ScreenSize(int *width, int *height);

bool OSD_BeginFrame(struct OSDCanvas *canvas);
void OSD_EndFrame(void);
bool OSD_ReadPad(struct OSDPad *pad);
void OSD_FrameLimit(void);

uint32_t OSD_Colour(int r, int g, int b);

const char *OSD_RunLauncher(char *images, size_t images_size,
                            char *boot, size_t boot_size,
                            char *mount_drive, size_t mount_drive_size);

typedef bool (*OSD_ExitPollFn)(void);
void OSD_SetExitPollFn(OSD_ExitPollFn fn);

void OSD_OpenKeyboard(void);
void OSD_CloseOverlays(void);

void OSD_OpenPauseMenu(void);
bool OSD_PauseMenuOpen(void);

typedef void (*OSD_ExitSessionFn)(void);
void OSD_SetExitSessionFn(OSD_ExitSessionFn fn);

typedef void (*OSD_ReturnToLibraryFn)(void);
void OSD_SetReturnToLibraryFn(OSD_ReturnToLibraryFn fn);

enum ModifierId {
	MOD_SHIFT = (1 << 0),
	MOD_CTRL  = (1 << 1),
	MOD_ALT   = (1 << 2)
};

void OSD_ToggleHelp(void);

void OSD_ToggleAbout(void);
void OSD_OpenAbout(void);

void OSD_ShowSessionHint(const char *extra);

void OSD_DrawOverlaysInto(struct OSDCanvas *canvas);

void OSD_UpdateOverlays(const struct OSDPad *pad);

void OSD_SetGlobalShortcuts(bool enabled);

void OSD_SetHardwareModifier(int modifier, bool held);

bool OSD_OverlaysActive(void);

bool OSD_WantsRedraw(void);

enum OSDShader {
	OSD_SHADER_NONE = 0,
	OSD_SHADER_SMOOTH,
	OSD_SHADER_SCANLINES,
	OSD_SHADER_APERTURE,
	OSD_SHADER_CRT,
	OSD_SHADER_COUNT
};

const char *OSD_ShaderName(int shader);
void OSD_ShaderSelect(int shader);
int OSD_ShaderSelected(void);

void OSD_FillRect(struct OSDCanvas *c, int x, int y, int w, int h, uint32_t colour);
void OSD_DrawRect(struct OSDCanvas *c, int x, int y, int w, int h, uint32_t colour);
void OSD_GradientV(struct OSDCanvas *c, int x, int y, int w, int h,
                   uint32_t top, uint32_t bottom);
void OSD_GradientH(struct OSDCanvas *c, int x, int y, int w, int h,
                   uint32_t left, uint32_t right);
void OSD_BlendRect(struct OSDCanvas *c, int x, int y, int w, int h,
                   uint32_t colour, int alpha);
void OSD_Panel(struct OSDCanvas *c, int x, int y, int w, int h,
               uint32_t fill, uint32_t border, int alpha);

void OSD_Text(struct OSDCanvas *c, int x, int y, const char *text,
              uint32_t colour, int scale, bool shadow);
void OSD_TextCentre(struct OSDCanvas *c, int cx, int y, const char *text,
                    uint32_t colour, int scale, bool shadow);
void OSD_TextRight(struct OSDCanvas *c, int rx, int y, const char *text,
                   uint32_t colour, int scale, bool shadow);
int  OSD_TextWidth(const char *text, int scale);

void OSD_DrawSplashLogo(struct OSDCanvas *c, int x, int y, int w, int h);

enum OSDButtonGlyph {
	OSD_GLYPH_A = 0,
	OSD_GLYPH_B,
	OSD_GLYPH_X,
	OSD_GLYPH_Y,
	OSD_GLYPH_L,
	OSD_GLYPH_R,
	OSD_GLYPH_ZL,
	OSD_GLYPH_ZR,
	OSD_GLYPH_PLUS,
	OSD_GLYPH_MINUS,
	OSD_GLYPH_LR,
	OSD_GLYPH_DPAD
};

void OSD_DrawRoundedRect(struct OSDCanvas *c, int x, int y, int w, int h, int r, uint32_t colour);
void OSD_DrawRoundedPill(struct OSDCanvas *c, int x, int y, int w, int h, int r, uint32_t fill, uint32_t border);
int  OSD_DrawButtonGlyph(struct OSDCanvas *c, int x, int y, enum OSDButtonGlyph glyph);
int  OSD_DrawButtonGlyphByName(struct OSDCanvas *c, int x, int y, const char *name);
int  OSD_DrawHintItem(struct OSDCanvas *c, int x, int y, const char *button, const char *label);

struct OSDStatus {
	int battery_percent;
	bool battery_charging;
	bool battery_valid;
	unsigned long long card_free_bytes;
	bool card_valid;
};

typedef bool (*OSD_StatusFn)(struct OSDStatus *status);
void OSD_SetStatusCallbacks(OSD_StatusFn status);

void OSD_DrawStatusReadout(struct OSDCanvas *c, int right, int y);

struct OSD_FTPServer {
	bool available;
	bool running;
	char address[40];
	int port;
	int clients;
};

typedef int (*OSD_FTPStartFn)(void);
typedef void (*OSD_FTPStopFn)(void);
typedef void (*OSD_FTPStatusFn)(struct OSD_FTPServer *out);
typedef bool (*OSD_FTPTakeDirtyFn)(void);
void OSD_SetFTPServer(OSD_FTPStartFn start, OSD_FTPStopFn stop,
                      OSD_FTPStatusFn status, OSD_FTPTakeDirtyFn take_dirty);

#define OSD_COLOUR_BACKDROP_TOP    0x0b1020
#define OSD_COLOUR_BACKDROP_BOTTOM 0x05070f
#define OSD_COLOUR_ACCENT          0xff9b3d
#define OSD_COLOUR_TEXT            0xe8eef8
#define OSD_COLOUR_TEXT_DIM        0x8090ad

#endif
