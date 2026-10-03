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

/* Standalone platform service boundary, 2026. */

#ifndef DOSBOX_PLATFORM_H
#define DOSBOX_PLATFORM_H

#include "dosbox.h"
#include "keyboard.h"
#include "platform_video.h"

#include <stdarg.h>

void Platform_UpdateStatus(Bit32s cycles, int frameskip, bool paused);

/* LOG_MSG/GFX_ShowMsg funnel: the emulation core has no way to write to a
 * console of its own, so every target routes the message through the backend. */
void Platform_ShowMessageV(const char *format, va_list msg);

Bit32u Platform_GetTicks(void);
void Platform_PumpEvents(void);
void Platform_InputKey(KBD_KEYS key, bool pressed);
void Platform_InputMouseMove(float xrel, float yrel, float x, float y, bool emulate);
void Platform_InputMouseButton(Bit8u button, bool pressed);
void Platform_InputJoystickButton(Bitu stick, Bitu button, bool pressed);
void Platform_InputJoystickMoveX(Bitu stick, float position);
void Platform_InputJoystickMoveY(Bitu stick, float position);
void Platform_Delay(Bit32u milliseconds);

/* Audio callbacks receive a byte-length buffer of native-endian signed 16-bit
 * stereo samples. Frequency is in Hz and blocksize is in sample frames. */
typedef void (*Platform_AudioCallback)(void *userdata, Bit8u *stream, int length);

bool Platform_AudioOpen(Bit32u frequency, Bit32u blocksize,
                        Platform_AudioCallback callback, void *userdata,
                        Bit32u *obtained_frequency, Bit32u *obtained_blocksize);
void Platform_AudioClose(void);
void Platform_AudioPause(bool paused);
void Platform_AudioLock(void);
void Platform_AudioUnlock(void);
const char *Platform_AudioError(void);

/* Video mode negotiation and frame-buffer access. Returned pixels are an
 * engine-facing, writable surface; ownership/lifetime belongs to the backend. */
void PlatformVideo_SetPalette(Bitu start, Bitu count, const PlatformVideoPaletteEntry *entries);
Bitu PlatformVideo_GetBestMode(Bitu flags);
Bitu PlatformVideo_GetRGB(Bit8u red, Bit8u green, Bit8u blue);
Bitu PlatformVideo_SetSize(Bitu width, Bitu height, Bitu flags,
                           double scalex, double scaley,
                           PlatformVideoCallback callback);
void PlatformVideo_SetShader(const char *src);
bool PlatformVideo_StartUpdate(Bit8u *&pixels, Bitu &pitch);
void PlatformVideo_EndUpdate(const Bit16u *changedLines);

/* Opaque host mutex, used by host-side media decoders that must serialize
 * access between the emulation thread and the audio callback. */
typedef struct Platform_Mutex *Platform_MutexHandle;

Platform_MutexHandle Platform_MutexCreate(void);
void Platform_MutexDestroy(Platform_MutexHandle mutex);
void Platform_MutexLock(Platform_MutexHandle mutex);
void Platform_MutexUnlock(Platform_MutexHandle mutex);

#endif
