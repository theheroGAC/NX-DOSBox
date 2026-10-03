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

#ifndef DOSBOX_SDL_VIDEO_H
#define DOSBOX_SDL_VIDEO_H

#include "platform_video.h"

#define REDUCE_JOYSTICK_POLLING

typedef PlatformVideoPaletteEntry GFX_PalEntry;
typedef PlatformVideoCallback GFX_CallBack_t;
typedef PlatformVideoCallbackAction GFX_CallBackFunctions_t;

#define GFX_CallBackReset PLATFORM_VIDEO_RESET
#define GFX_CallBackStop PLATFORM_VIDEO_STOP
#define GFX_CallBackRedraw PLATFORM_VIDEO_REDRAW

/* SDL frontend compatibility aliases. */
#define GFX_CAN_8		PLATFORM_VIDEO_CAN_8
#define GFX_CAN_15		PLATFORM_VIDEO_CAN_15
#define GFX_CAN_16		PLATFORM_VIDEO_CAN_16
#define GFX_CAN_32		PLATFORM_VIDEO_CAN_32
#define GFX_LOVE_8		PLATFORM_VIDEO_LOVE_8
#define GFX_LOVE_15		PLATFORM_VIDEO_LOVE_15
#define GFX_LOVE_16		PLATFORM_VIDEO_LOVE_16
#define GFX_LOVE_32		PLATFORM_VIDEO_LOVE_32
#define GFX_RGBONLY		PLATFORM_VIDEO_RGB_ONLY
#define GFX_SCALING		PLATFORM_VIDEO_SCALING
#define GFX_HARDWARE		PLATFORM_VIDEO_HARDWARE
#define GFX_CAN_RANDOM	PLATFORM_VIDEO_CAN_RANDOM


void GFX_Events(void);
void SDL_SetStatusTitle(Bit32s cycles, int frameskip, bool paused);
void GFX_SetPalette(Bitu start, Bitu count, GFX_PalEntry *entries);
Bitu GFX_GetBestMode(Bitu flags);
Bitu GFX_GetRGB(Bit8u red, Bit8u green, Bit8u blue);
Bitu GFX_SetSize(Bitu width, Bitu height, Bitu flags, double scalex,
                 double scaley, GFX_CallBack_t callback);
void GFX_SetShader(const char *src);
void GFX_ResetScreen(void);
void GFX_Start(void);
void GFX_Stop(void);
void GFX_SwitchFullScreen(void);
bool GFX_StartUpdate(Bit8u *&pixels, Bitu &pitch);
void GFX_EndUpdate(const Bit16u *changedLines);
void GFX_GetSize(int &width, int &height, bool &fullscreen);
void GFX_LosingFocus(void);
void GFX_CaptureMouse(void);
extern bool mouselocked;

#if defined(WIN32)
bool GFX_SDLUsingWinDIB(void);
#endif

#if defined(REDUCE_JOYSTICK_POLLING)
void MAPPER_UpdateJoysticks(void);
#endif

#endif
