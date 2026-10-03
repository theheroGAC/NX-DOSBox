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

#include "SDL.h"
#include "platform.h"
#include "sdl_video.h"
#include "joystick.h"
#include "mouse.h"
#include "keyboard.h"

#include <stdarg.h>
#include <stdio.h>

/* Implemented by the SDL application frontend. */
extern void GFX_Events(void);
extern void SDL_ShowMessageV(const char *format, va_list msg);

void Platform_ShowMessageV(const char *format, va_list msg) {
	SDL_ShowMessageV(format, msg);
}

/* LOG_MSG/GFX_ShowMsg are the logging funnel of the whole program; the message
 * itself belongs to the SDL front end, so only the forwarding lives here. */
void GFX_ShowMsg(char const *format, ...) {
	va_list msg;
	va_start(msg, format);
	SDL_ShowMessageV(format, msg);
	va_end(msg);
}

static Platform_AudioCallback platform_audio_callback;
static void *platform_audio_userdata;
static bool platform_audio_open;
static PlatformVideoCallback platform_video_callback;

static void Platform_SDLVideoCallback(PlatformVideoCallbackAction action) {
	if (platform_video_callback)
		platform_video_callback(action);
}

static void SDLCALL Platform_SDLAudioCallback(void *, Uint8 *stream, int length) {
	if (platform_audio_callback)
		platform_audio_callback(platform_audio_userdata, stream, length);
}

Bit32u Platform_GetTicks(void) {
	return SDL_GetTicks();
}

void Platform_Delay(Bit32u milliseconds) {
	SDL_Delay(milliseconds);
}

void Platform_PumpEvents(void) {
	GFX_Events();
}

void Platform_InputKey(KBD_KEYS key, bool pressed) {
	KEYBOARD_AddKey(key, pressed);
}

void Platform_InputMouseMove(float xrel, float yrel, float x, float y, bool emulate) {
	Mouse_CursorMoved(xrel, yrel, x, y, emulate);
}

void Platform_InputMouseButton(Bit8u button, bool pressed) {
	if (pressed)
		Mouse_ButtonPressed(button);
	else
		Mouse_ButtonReleased(button);
}

void Platform_InputJoystickButton(Bitu stick, Bitu button, bool pressed) {
	JOYSTICK_Button(stick, button, pressed);
}

void Platform_InputJoystickMoveX(Bitu stick, float position) {
	JOYSTICK_Move_X(stick, position);
}

void Platform_InputJoystickMoveY(Bitu stick, float position) {
	JOYSTICK_Move_Y(stick, position);
}

void PlatformVideo_SetPalette(Bitu start, Bitu count, const PlatformVideoPaletteEntry *entries) {
	GFX_SetPalette(start, count, const_cast<GFX_PalEntry *>(entries));
}

Bitu PlatformVideo_GetBestMode(Bitu flags) {
	return GFX_GetBestMode(flags);
}

Bitu PlatformVideo_GetRGB(Bit8u red, Bit8u green, Bit8u blue) {
	return GFX_GetRGB(red, green, blue);
}

Bitu PlatformVideo_SetSize(Bitu width, Bitu height, Bitu flags,
                           double scalex, double scaley,
                           PlatformVideoCallback callback) {
	platform_video_callback = callback;
	return GFX_SetSize(width, height, flags, scalex, scaley,
	                   callback ? Platform_SDLVideoCallback : NULL);
}

void PlatformVideo_SetShader(const char *src) {
#if C_OPENGL
	GFX_SetShader(src);
#else
	(void)src;
#endif
}

void Platform_UpdateStatus(Bit32s cycles, int frameskip, bool paused) {
	SDL_SetStatusTitle(cycles, frameskip, paused);
}

bool PlatformVideo_StartUpdate(Bit8u *&pixels, Bitu &pitch) {
	return GFX_StartUpdate(pixels, pitch);
}

void PlatformVideo_EndUpdate(const Bit16u *changedLines) {
	GFX_EndUpdate(changedLines);
}

Platform_MutexHandle Platform_MutexCreate(void) {
	return reinterpret_cast<Platform_MutexHandle>(SDL_CreateMutex());
}

void Platform_MutexDestroy(Platform_MutexHandle mutex) {
	SDL_DestroyMutex(reinterpret_cast<SDL_mutex *>(mutex));
}

void Platform_MutexLock(Platform_MutexHandle mutex) {
	SDL_mutexP(reinterpret_cast<SDL_mutex *>(mutex));
}

void Platform_MutexUnlock(Platform_MutexHandle mutex) {
	SDL_mutexV(reinterpret_cast<SDL_mutex *>(mutex));
}

bool Platform_AudioOpen(Bit32u frequency, Bit32u blocksize,
                        Platform_AudioCallback callback, void *userdata,
                        Bit32u *obtained_frequency, Bit32u *obtained_blocksize) {
	SDL_AudioSpec requested;
	SDL_AudioSpec obtained;

	platform_audio_callback = callback;
	platform_audio_userdata = userdata;
	requested.freq = static_cast<int>(frequency);
	requested.format = AUDIO_S16SYS;
	requested.channels = 2;
	requested.callback = Platform_SDLAudioCallback;
	requested.userdata = NULL;
	requested.samples = static_cast<Uint16>(blocksize);

	if (SDL_OpenAudio(&requested, &obtained) < 0) {
		platform_audio_callback = NULL;
		platform_audio_userdata = NULL;
		return false;
	}
	platform_audio_open = true;

	if (obtained_frequency)
		*obtained_frequency = static_cast<Bit32u>(obtained.freq);
	if (obtained_blocksize)
		*obtained_blocksize = static_cast<Bit32u>(obtained.samples);
	return true;
}

void Platform_AudioClose(void) {
	if (platform_audio_open) {
		SDL_CloseAudio();
		platform_audio_open = false;
	}
	platform_audio_callback = NULL;
	platform_audio_userdata = NULL;
}

void Platform_AudioPause(bool paused) {
	SDL_PauseAudio(paused ? 1 : 0);
}

void Platform_AudioLock(void) {
	SDL_LockAudio();
}

void Platform_AudioUnlock(void) {
	SDL_UnlockAudio();
}

const char *Platform_AudioError(void) {
	return SDL_GetError();
}
