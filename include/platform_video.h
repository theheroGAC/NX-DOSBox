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

#ifndef DOSBOX_PLATFORM_VIDEO_H
#define DOSBOX_PLATFORM_VIDEO_H

#include "dosbox.h"

typedef enum {
	PLATFORM_VIDEO_RESET,
	PLATFORM_VIDEO_STOP,
	PLATFORM_VIDEO_REDRAW
} PlatformVideoCallbackAction;

typedef void (*PlatformVideoCallback)(PlatformVideoCallbackAction action);

struct PlatformVideoPaletteEntry {
	Bit8u r;
	Bit8u g;
	Bit8u b;
	Bit8u unused;
};

#define PLATFORM_VIDEO_CAN_8		0x0001
#define PLATFORM_VIDEO_CAN_15		0x0002
#define PLATFORM_VIDEO_CAN_16		0x0004
#define PLATFORM_VIDEO_CAN_32		0x0008
#define PLATFORM_VIDEO_LOVE_8		0x0010
#define PLATFORM_VIDEO_LOVE_15		0x0020
#define PLATFORM_VIDEO_LOVE_16		0x0040
#define PLATFORM_VIDEO_LOVE_32		0x0080
#define PLATFORM_VIDEO_RGB_ONLY		0x0100
#define PLATFORM_VIDEO_SCALING		0x1000
#define PLATFORM_VIDEO_HARDWARE		0x2000
#define PLATFORM_VIDEO_CAN_RANDOM	0x4000

#endif
