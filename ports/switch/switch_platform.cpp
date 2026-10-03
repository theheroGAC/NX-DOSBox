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

#include <switch.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>
#include <math.h>

#include "platform.h"
#include "control.h"
#include "setup.h"
#include "keyboard.h"
#include "mapper.h"
#include "pad_mapping.h"
#include "mouse.h"
#include "joystick.h"
#include "support.h"
#include "regs.h"
#include "timer.h"
#include "render.h"
#include "vga.h"
#include "osd.h"
#include "switch_ftp.h"

extern void ConsoleMapper_KeyChanged(KBD_KEYS key, bool pressed, bool ctrl, bool alt);

static Framebuffer g_framebuffer;
static bool video_ready = false;
static u32 screen_width = 0;
static u32 screen_height = 0;

static Bit8u *surface_pixels = NULL;
static u32 surface_width = 0;
static u32 surface_height = 0;
static u32 surface_pitch = 0;
static bool video_updating = false;
static bool in_video_update = false;
static Bit32u palette32[256];
static PlatformVideoCallback video_callback = NULL;

static Bit32u *screen_mirror = NULL;

bool SwitchPlatform_VideoInit(void) {
	NWindow *win = nwindowGetDefault();
	if (!win) return false;

	u32 w = 0, h = 0;
	if (R_FAILED(nwindowGetDimensions(win, &w, &h)) || w == 0 || h == 0) {
		w = 1280;
		h = 720;
	}
	screen_width = w;
	screen_height = h;

	if (R_FAILED(framebufferCreate(&g_framebuffer, win, screen_width, screen_height,
	                               PIXEL_FORMAT_RGBA_8888, 2))) {
		return false;
	}
	if (R_FAILED(framebufferMakeLinear(&g_framebuffer))) {
		framebufferClose(&g_framebuffer);
		return false;
	}
	video_ready = true;
	memset(palette32, 0, sizeof(palette32));

	free(screen_mirror);
	screen_mirror = (Bit32u *)calloc(screen_width, (size_t)screen_height * 4);
	
	if (screen_mirror) {
		memset(screen_mirror, 0, (size_t)screen_width * screen_height * 4);
	}
	return true;
}

void PlatformVideo_SetPalette(Bitu start, Bitu count, const PlatformVideoPaletteEntry *entries) {
	if (!entries) return;
	for (Bitu i = 0; i < count; i++) {
		if (start + i > 0xFF) break;
		palette32[start + i] = RGBA8_MAXALPHA(entries[i].r, entries[i].g, entries[i].b);
	}
}

Bitu PlatformVideo_GetBestMode(Bitu flags) {
	flags &= ~(PLATFORM_VIDEO_CAN_8 | PLATFORM_VIDEO_CAN_15 | PLATFORM_VIDEO_CAN_16);
	flags |= PLATFORM_VIDEO_CAN_32 | PLATFORM_VIDEO_CAN_RANDOM;
	return flags;
}

Bitu PlatformVideo_GetRGB(Bit8u red, Bit8u green, Bit8u blue) {
	return RGBA8_MAXALPHA(red, green, blue);
}

Bitu PlatformVideo_SetSize(Bitu width, Bitu height, Bitu flags,
                           double scalex, double scaley,
                           PlatformVideoCallback callback) {
	(void)scalex;
	(void)scaley;
	if (video_updating) {
		PlatformVideo_EndUpdate(NULL);
	}
	if (!width || !height) {
		return 0;
	}

	if (width != surface_width || height != surface_height) {
		Bit8u *new_pixels = (Bit8u *)calloc(width, height * 4);
		if (!new_pixels) return 0;
		free(surface_pixels);
		surface_pixels = new_pixels;
		surface_width = width;
		surface_height = height;
		surface_pitch = width * 4;
	}
	video_callback = callback;
	(void)flags;
	return PLATFORM_VIDEO_CAN_32;
}

void PlatformVideo_SetShader(const char *src) {
	(void)src;
}

bool PlatformVideo_StartUpdate(Bit8u *&pixels, Bitu &pitch) {
	if (!video_ready || !surface_pixels || video_updating) return false;
	pixels = surface_pixels;
	pitch = surface_pitch;
	video_updating = true;
	return true;
}

static inline Bit32u Switch_Shade(Bit32u px, u32 r, u32 g, u32 b) {
	if (r == 256 && g == 256 && b == 256) return px;
	const u32 out_r = ((px & 0xffu) * r) >> 8;
	const u32 out_g = (((px >> 8) & 0xffu) * g) >> 8;
	const u32 out_b = (((px >> 16) & 0xffu) * b) >> 8;
	return (px & 0xff000000u) | (out_b << 16) | (out_g << 8) | out_r;
}

static inline u32 Switch_MixChannel(u32 a, u32 b, u32 weight) {
	return (a * (256u - weight) + b * weight) >> 8;
}

static inline Bit32u Switch_Blend4(Bit32u tl, Bit32u tr, Bit32u bl, Bit32u br,
                                   u32 fx, u32 fy) {
	if (fx == 0 && fy == 0) return tl;
	const u32 top_r = Switch_MixChannel(tl & 0xffu, tr & 0xffu, fx);
	const u32 top_g = Switch_MixChannel((tl >> 8) & 0xffu, (tr >> 8) & 0xffu, fx);
	const u32 top_b = Switch_MixChannel((tl >> 16) & 0xffu, (tr >> 16) & 0xffu, fx);
	const u32 bot_r = Switch_MixChannel(bl & 0xffu, br & 0xffu, fx);
	const u32 bot_g = Switch_MixChannel((bl >> 8) & 0xffu, (br >> 8) & 0xffu, fx);
	const u32 bot_b = Switch_MixChannel((bl >> 16) & 0xffu, (br >> 16) & 0xffu, fx);
	const u32 out_r = Switch_MixChannel(top_r, bot_r, fy);
	const u32 out_g = Switch_MixChannel(top_g, bot_g, fy);
	const u32 out_b = Switch_MixChannel(top_b, bot_b, fy);
	return (tl & 0xff000000u) | (out_b << 16) | (out_g << 8) | out_r;
}

static inline Bit32u Switch_Glow(Bit32u left, Bit32u centre, Bit32u right) {
	const u32 out_r = ((left & 0xffu) + 2u * (centre & 0xffu) + (right & 0xffu)) >> 2;
	const u32 out_g = (((left >> 8) & 0xffu) + 2u * ((centre >> 8) & 0xffu) +
	                   ((right >> 8) & 0xffu)) >> 2;
	const u32 out_b = (((left >> 16) & 0xffu) + 2u * ((centre >> 16) & 0xffu) +
	                   ((right >> 16) & 0xffu)) >> 2;
	return (centre & 0xff000000u) | (out_b << 16) | (out_g << 8) | out_r;
}

static const u32 Switch_ApertureGrid[3][3] = {
	{ 256, 168, 168 },
	{ 168, 256, 168 },
	{ 168, 168, 256 }
};

void PlatformVideo_EndUpdate(const Bit16u *changedLines) {
	(void)changedLines;
	if (!video_updating) return;
	in_video_update = true;

	video_updating = false;

	Platform_PumpEvents();
	in_video_update = false;
	if (!video_ready || !surface_pixels || !screen_mirror) return;

	Bit32u *dst = screen_mirror;
	const u32 stride = screen_width;

	u32 out_w = screen_width;
	u32 out_h = (u32)((u64)surface_height * screen_width / surface_width);
	if (out_h > screen_height || out_h == 0) {
		out_h = screen_height;
		out_w = (u32)((u64)surface_width * screen_height / surface_height);
		if (out_w == 0 || out_w > screen_width) out_w = screen_width;
	}
	const s32 x0 = (s32)((screen_width - out_w) / 2);
	const s32 y0 = (s32)((screen_height - out_h) / 2);

	const u32 step_x = (u32)(((u64)surface_width << 16) / out_w);
	const u32 step_y = (u32)(((u64)surface_height << 16) / out_h);

	const int shader = OSD_ShaderSelected();

	for (u32 y = 0; y < out_h; y++) {
		const u32 sy = (u32)((u64)step_y * y >> 16);
		const Bit32u *src = (const Bit32u *)surface_pixels +
		                    sy * (surface_pitch / 4);
		Bit32u *row = dst + (u32)(y0 + (s32)y) * stride + (u32)x0;
		u32 sx = 0;

		switch (shader) {
		case OSD_SHADER_SMOOTH: {
			const u32 fy = (u32)((((u64)step_y * y) & 0xffffu) >> 8);
			const Bit32u *next = (sy + 1 < surface_height) ? src + surface_pitch / 4 : src;
			for (u32 x = 0; x < out_w; x++) {
				const u32 sxi = sx >> 16;
				const u32 fx = (sx & 0xffffu) >> 8;
				const Bit32u *right = (sxi + 1 < surface_width) ? src + sxi + 1 : src + sxi;
				const Bit32u *right_next = (sxi + 1 < surface_width) ? next + sxi + 1 : next + sxi;
				row[x] = Switch_Blend4(src[sxi], *right, next[sxi], *right_next, fx, fy);
				sx += step_x;
			}
			break;
		}
		case OSD_SHADER_SCANLINES: {
			const u32 dark = (y & 1) ? 140 : 256;
			for (u32 x = 0; x < out_w; x++) {
				row[x] = Switch_Shade(src[sx >> 16], dark, dark, dark);
				sx += step_x;
			}
			break;
		}
		case OSD_SHADER_APERTURE: {
			u32 phase = (u32)(x0 < 0 ? 0 : x0) % 3;
			for (u32 x = 0; x < out_w; x++) {
				const u32 *mask = Switch_ApertureGrid[phase];
				row[x] = Switch_Shade(src[sx >> 16], mask[0], mask[1], mask[2]);
				sx += step_x;
				if (++phase == 3) phase = 0;
			}
			break;
		}
		case OSD_SHADER_CRT: {
			const u32 scan = (y & 1) ? 130 : 256;
			u32 phase = (u32)(x0 < 0 ? 0 : x0) % 3;
			for (u32 x = 0; x < out_w; x++) {
				const u32 sxi = sx >> 16;
				const Bit32u *left = (sxi > 0) ? src + sxi - 1 : src + sxi;
				const Bit32u *right = (sxi + 1 < surface_width) ? src + sxi + 1 : src + sxi;
				Bit32u px = Switch_Glow(*left, src[sxi], *right);
				const u32 *mask = Switch_ApertureGrid[phase];
				row[x] = Switch_Shade(px, scan * mask[0] >> 8, scan * mask[1] >> 8,
				                      scan * mask[2] >> 8);
				sx += step_x;
				if (++phase == 3) phase = 0;
			}
			break;
		}
		default:
			for (u32 x = 0; x < out_w; x++) {
				row[x] = src[sx >> 16];
				sx += step_x;
			}
			break;
		}
	}

	Bit32u black = RGBA8_MAXALPHA(0, 0, 0);
	if (y0 > 0) {
		for (s32 y = 0; y < y0; y++) {
			Bit32u *row = dst + (u32)y * stride;
			for (u32 x = 0; x < screen_width; x++) row[x] = black;
		}
	}
	if (y0 + (s32)out_h < (s32)screen_height) {
		for (s32 y = y0 + (s32)out_h; y < (s32)screen_height; y++) {
			Bit32u *row = dst + (u32)y * stride;
			for (u32 x = 0; x < screen_width; x++) row[x] = black;
		}
	}
	if (x0 > 0) {
		for (s32 y = y0; y < y0 + (s32)out_h; y++) {
			Bit32u *row = dst + (u32)y * stride;
			for (s32 x = 0; x < x0; x++) row[x] = black;
		}
	}		if (x0 + (s32)out_w < (s32)screen_width) {
		for (s32 y = y0; y < y0 + (s32)out_h; y++) {
			Bit32u *row = dst + (u32)y * stride;
			for (s32 x = x0 + (s32)out_w; x < (s32)screen_width; x++) row[x] = black;
		}
	}

	struct OSDCanvas overlay;
	if (OSD_BeginFrame(&overlay)) {
		OSD_DrawOverlaysInto(&overlay);
		OSD_EndFrame();
		return;
	}

	u32 fb_stride = 0;
	Bit32u *fb = (Bit32u *)framebufferBegin(&g_framebuffer, &fb_stride);
	if (!fb) return;
	const u32 fb_pitch = fb_stride / 4;
	for (u32 y = 0; y < screen_height; y++) {
		memcpy(&fb[(size_t)y * fb_pitch], &screen_mirror[(size_t)y * screen_width],
		       (size_t)screen_width * 4);
	}
	framebufferEnd(&g_framebuffer);
}

#define SWITCH_AUDIO_SAMPLE_RATE   48000
#define SWITCH_AUDIO_SAMPLES_FRAME (SWITCH_AUDIO_SAMPLE_RATE / 200)
#define SWITCH_AUDIO_FRAME_SIZE    (SWITCH_AUDIO_SAMPLES_FRAME * 2 * sizeof(s16))
#define SWITCH_AUDIO_NUM_BUFFERS   4
#define SWITCH_AUDIO_MEMPOOL_SIZE  0x2000
static_assert(SWITCH_AUDIO_NUM_BUFFERS <= 4, "one audren voice owns four wavebufs");

static AudioDriver g_audrv;
static AudioRendererConfig g_audren_config;
static AudioDriverWaveBuf g_wavebufs[SWITCH_AUDIO_NUM_BUFFERS];
static Bit8u *g_audio_mempool = NULL;
static int g_audio_mempool_id = -1;

static bool audio_open = false;
static bool audio_paused = false;
static bool audio_initialized = false;
static Platform_AudioCallback audio_callback = NULL;
static void *audio_userdata = NULL;
static char audio_error[128] = "";

static RMutex g_audio_mutex;
static bool g_audio_mutex_ready = false;
static Thread g_audio_thread;
static volatile bool g_audio_thread_run = false;

static void audio_report(Result rc, const char *what) {
	if (R_FAILED(rc)) {
		snprintf(audio_error, sizeof(audio_error), "%s failed: 0x%08x", what, rc);
	}
}

static void audio_log(const char *text) {
	extern void SwitchPlatform_Trace(const char *step);
	SwitchPlatform_Trace(text);
}

static void audio_log_rc(const char *what, Result rc) {
	char msg[96];
	snprintf(msg, sizeof(msg), "audio: %s rc=0x%08x", what, (unsigned)rc);
	audio_log(msg);
}

static bool audio_update_logged = false;

static s16 audio_hold[2] = { 0, 0 };

static void SwitchPlatform_AudioHold(s16 *dst) {
	const u32 value = (u32)(Bit16u)audio_hold[0] | ((u32)(Bit16u)audio_hold[1] << 16);
	u32 *words = (u32 *)dst;
	for (u32 i = 0; i < SWITCH_AUDIO_FRAME_SIZE / 4; i++) words[i] = value;
}

static void SwitchPlatform_AudioRemember(const s16 *src) {
	audio_hold[0] = src[SWITCH_AUDIO_FRAME_SIZE / 2 - 2];
	audio_hold[1] = src[SWITCH_AUDIO_FRAME_SIZE / 2 - 1];
}

static void SwitchPlatform_AudioFrame(void) {
	if (!audio_open || !audio_initialized) return;

	audrenWaitFrame();

	for (int i = 0; i < SWITCH_AUDIO_NUM_BUFFERS; i++) {
		if (g_wavebufs[i].state == AudioDriverWaveBufState_Free ||
		    g_wavebufs[i].state == AudioDriverWaveBufState_Done) {
			s16 *dst = g_wavebufs[i].data_pcm16;
			if (audio_paused || !audio_callback) {
				memset(dst, 0, SWITCH_AUDIO_FRAME_SIZE);
				audio_hold[0] = audio_hold[1] = 0;
			} else {
				SwitchPlatform_AudioHold(dst);
				Platform_AudioLock();
				audio_callback(audio_userdata, (Bit8u *)dst, (int)SWITCH_AUDIO_FRAME_SIZE);
				Platform_AudioUnlock();
				SwitchPlatform_AudioRemember(dst);
			}
			armDCacheFlush(dst, SWITCH_AUDIO_FRAME_SIZE);
			audrvVoiceAddWaveBuf(&g_audrv, 0, &g_wavebufs[i]);
		}
	}

	Result rc = audrvUpdate(&g_audrv);
	if (R_FAILED(rc) && !audio_update_logged) {
		audio_update_logged = true;
		audio_log_rc("audrvUpdate", rc);
	}
}

static void SwitchPlatform_AudioThreadEntry(void *arg) {
	(void)arg;
	while (g_audio_thread_run) {
		SwitchPlatform_AudioFrame();
	}
}

static bool SwitchPlatform_StartAudioThread(void) {
	g_audio_thread_run = true;
	Result rc = threadCreate(&g_audio_thread, SwitchPlatform_AudioThreadEntry, NULL,
	                         NULL, 0x8000, 0x20, -2);
	if (R_FAILED(rc)) {
		rc = threadCreate(&g_audio_thread, SwitchPlatform_AudioThreadEntry, NULL,
		                  NULL, 0x8000, 0x2C, -2);
	}
	if (R_FAILED(rc)) {
		audio_log_rc("threadCreate", rc);
		g_audio_thread_run = false;
		return false;
	}
	if (R_FAILED(rc = threadStart(&g_audio_thread))) {
		audio_log_rc("threadStart", rc);
		g_audio_thread_run = false;
		threadClose(&g_audio_thread);
		return false;
	}
	return true;
}

static void SwitchPlatform_StopAudioThread(void) {
	if (!g_audio_thread_run) return;
	g_audio_thread_run = false;
	threadWaitForExit(&g_audio_thread);
	threadClose(&g_audio_thread);
}

bool Platform_AudioOpen(Bit32u frequency, Bit32u blocksize,
                        Platform_AudioCallback callback, void *userdata,
                        Bit32u *obtained_frequency, Bit32u *obtained_blocksize) {
	(void)frequency;
	(void)blocksize;
	audio_error[0] = '\0';
	audio_update_logged = false;

	if (!g_audio_mutex_ready) {
		rmutexInit(&g_audio_mutex);
		g_audio_mutex_ready = true;
	}

	memset(&g_audren_config, 0, sizeof(g_audren_config));
	g_audren_config.output_rate     = AudioRendererOutputRate_48kHz;
	g_audren_config.num_voices      = 4;
	g_audren_config.num_effects     = 0;
	g_audren_config.num_sinks       = 1;
	g_audren_config.num_mix_objs    = 1;
	g_audren_config.num_mix_buffers = 2;

	Result rc = audrenInitialize(&g_audren_config);
	if (R_FAILED(rc)) {
		audio_report(rc, "audrenInitialize");
		audio_log_rc("audrenInitialize", rc);
		return false;
	}
	audio_initialized = true;

	rc = audrvCreate(&g_audrv, &g_audren_config, 2);
	if (R_FAILED(rc)) {
		audio_report(rc, "audrvCreate");
		audio_log_rc("audrvCreate", rc);
		Platform_AudioClose();
		return false;
	}

	g_audio_mempool = (Bit8u *)memalign(AUDREN_MEMPOOL_ALIGNMENT, SWITCH_AUDIO_MEMPOOL_SIZE);
	if (!g_audio_mempool) {
		snprintf(audio_error, sizeof(audio_error), "mempool alloc failed");
		audio_log("audio: mempool allocation failed");
		Platform_AudioClose();
		return false;
	}
	memset(g_audio_mempool, 0, SWITCH_AUDIO_MEMPOOL_SIZE);

	g_audio_mempool_id = audrvMemPoolAdd(&g_audrv, g_audio_mempool, SWITCH_AUDIO_MEMPOOL_SIZE);
	if (g_audio_mempool_id < 0) {
		snprintf(audio_error, sizeof(audio_error), "audrvMemPoolAdd failed");
		audio_log("audio: audrvMemPoolAdd failed");
		Platform_AudioClose();
		return false;
	}
	audrvMemPoolAttach(&g_audrv, g_audio_mempool_id);

	static const u8 sink_channels[] = { 0, 1 };
	if (audrvDeviceSinkAdd(&g_audrv, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels) < 0) {
		audio_log("audio: audrvDeviceSinkAdd failed");
	}

	if (!audrvVoiceInit(&g_audrv, 0, 2, PcmFormat_Int16, SWITCH_AUDIO_SAMPLE_RATE)) {
		snprintf(audio_error, sizeof(audio_error), "audrvVoiceInit failed");
		audio_log("audio: audrvVoiceInit failed");
		Platform_AudioClose();
		return false;
	}
	audrvVoiceSetDestinationMix(&g_audrv, 0, AUDREN_FINAL_MIX_ID);
	audrvVoiceSetMixFactor(&g_audrv, 0, 1.0f, 0, 0);
	audrvVoiceSetMixFactor(&g_audrv, 0, 1.0f, 1, 1);
	audrvVoiceSetVolume(&g_audrv, 0, 1.0f);
	audrvVoiceSetPitch(&g_audrv, 0, 1.0f);

	audio_callback = callback;
	audio_userdata = userdata;

	for (int i = 0; i < SWITCH_AUDIO_NUM_BUFFERS; i++) {
		memset(&g_wavebufs[i], 0, sizeof(g_wavebufs[i]));
		g_wavebufs[i].data_pcm16 = (s16 *)(g_audio_mempool + (size_t)i * SWITCH_AUDIO_FRAME_SIZE);
		g_wavebufs[i].size = SWITCH_AUDIO_FRAME_SIZE;
		g_wavebufs[i].start_sample_offset = 0;
		g_wavebufs[i].end_sample_offset = SWITCH_AUDIO_SAMPLES_FRAME;
		if (audio_callback) {
			audio_callback(audio_userdata, (Bit8u *)g_wavebufs[i].data_pcm16, (int)SWITCH_AUDIO_FRAME_SIZE);
		} else {
			memset(g_wavebufs[i].data_pcm16, 0, SWITCH_AUDIO_FRAME_SIZE);
		}
		armDCacheFlush(g_wavebufs[i].data_pcm16, SWITCH_AUDIO_FRAME_SIZE);
		audrvVoiceAddWaveBuf(&g_audrv, 0, &g_wavebufs[i]);
	}

	audrvVoiceStart(&g_audrv, 0);

	rc = audrvUpdate(&g_audrv);
	if (R_FAILED(rc)) {
		audio_report(rc, "audrvUpdate initial");
		audio_log_rc("audrvUpdate initial", rc);
	}

	rc = audrenStartAudioRenderer();
	if (R_FAILED(rc)) {
		audio_report(rc, "audrenStartAudioRenderer");
		audio_log_rc("audrenStartAudioRenderer", rc);
		Platform_AudioClose();
		return false;
	}

	audio_open = true;
	audio_paused = false;

	if (!SwitchPlatform_StartAudioThread()) {
		Platform_AudioClose();
		return false;
	}

	if (obtained_frequency) *obtained_frequency = SWITCH_AUDIO_SAMPLE_RATE;
	if (obtained_blocksize) *obtained_blocksize = SWITCH_AUDIO_SAMPLES_FRAME;
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "audio: open %u Hz, %u frames x %d wavebufs",
		         (unsigned)SWITCH_AUDIO_SAMPLE_RATE, (unsigned)SWITCH_AUDIO_SAMPLES_FRAME,
		         SWITCH_AUDIO_NUM_BUFFERS);
		audio_log(msg);
	}
	return true;
}

void Platform_AudioClose(void) {
	if (audio_open) {
		SwitchPlatform_StopAudioThread();
		audrvVoiceStop(&g_audrv, 0);
		audrvUpdate(&g_audrv);
		audrenStopAudioRenderer();
		audio_open = false;
	}
	audio_callback = NULL;
	audio_userdata = NULL;

	if (audio_initialized) {
		audrvClose(&g_audrv);
		audrenExit();
		audio_initialized = false;
	}
	if (g_audio_mempool) {
		free(g_audio_mempool);
		g_audio_mempool = NULL;
	}
	g_audio_mempool_id = -1;
}

void Platform_AudioPause(bool paused) {
	audio_paused = paused;
}

void Platform_AudioLock(void) {
	if (g_audio_mutex_ready) rmutexLock(&g_audio_mutex);
}

void Platform_AudioUnlock(void) {
	if (g_audio_mutex_ready) rmutexUnlock(&g_audio_mutex);
}

const char *Platform_AudioError(void) {
	return audio_error;
}

Bit32u Platform_GetTicks(void) {
	static u64 freq = 0;
	if (!freq) {
		freq = armGetSystemTickFreq();
		if (!freq) freq = 19200000;
	}
	return (Bit32u)(armGetSystemTick() * 1000 / freq);
}

void Platform_Delay(Bit32u milliseconds) {
	svcSleepThread((s64)milliseconds * 1000000LL);
}

static PadState g_pad;
static bool pad_ready = false;
static bool g_prev_ctrl = false;
static bool g_prev_alt = false;

void SwitchPlatform_InputInit(void) {
	extern void SwitchPlatform_Trace(const char *step);

	Result rc = hidInitialize();
	if (R_FAILED(rc)) {
		char msg[64];
		snprintf(msg, sizeof(msg), "input: hidInitialize failed 0x%08x", rc);
		SwitchPlatform_Trace(msg);
	}
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&g_pad);
	hidInitializeTouchScreen();
	pad_ready = true;

	{
		char msg[128];
		snprintf(msg, sizeof(msg),
		         "input: ready (hidInitialize 0x%08x, id_mask 0x%02x)",
		         rc, (unsigned)g_pad.id_mask);
		SwitchPlatform_Trace(msg);
	}
}

void Platform_InputKey(KBD_KEYS key, bool pressed) {
	extern void SwitchPlatform_Trace(const char *step);

	static unsigned traced = 0;
	if (traced < 64) {
		char msg[64];
		snprintf(msg, sizeof(msg), "key: %d %s", (int)key,
		         pressed ? "down" : "up");
		SwitchPlatform_Trace(msg);
		traced++;
	}

	KEYBOARD_AddKey(key, pressed);
	ConsoleMapper_KeyChanged(key, pressed, g_prev_ctrl, g_prev_alt);
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

void Platform_UpdateStatus(Bit32s cycles, int frameskip, bool paused) {
	(void)cycles;
	(void)frameskip;
	(void)paused;
}

void Platform_ShowMessageV(const char *format, va_list msg) {
	char buf[512];
	vsnprintf(buf, sizeof(buf), format, msg);
	buf[sizeof(buf) - 1] = '\0';

	svcOutputDebugString(buf, strlen(buf) + 1);
	fputs(buf, stderr);
}

static int trace_log = -1;

void SwitchPlatform_Trace(const char *step) {
	if (trace_log < 0)
		trace_log = open("sdmc:/dosbox/startup.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (trace_log < 0) return;
	char line[256];
	const int len = snprintf(line, sizeof(line), "[dosbox] %s\n", step);
	if (len > 0) write(trace_log, line, (size_t)len);
}

void SwitchPlatform_TraceClose(void) {
	if (trace_log >= 0) close(trace_log);
	trace_log = -1;
}

void SwitchPlatform_ShowFatal(const char *title, const char *detail) {
	ErrorApplicationConfig c;
	if (R_FAILED(errorApplicationCreate(&c, title, detail))) {
		svcOutputDebugString(title, strlen(title) + 1);
		svcOutputDebugString(detail, strlen(detail) + 1);
		return;
	}
	errorApplicationShow(&c);
}

static void trace_pad_state(void) {
	static u64 last_buttons = ~(u64)0;
	static unsigned logged = 0;
	const u64 buttons = padGetButtons(&g_pad);
	if (buttons == last_buttons || logged >= 32) return;
	last_buttons = buttons;
	char msg[160];
	snprintf(msg, sizeof(msg),
	         "pad: buttons 0x%016llx active_mask 0x%02x attrs 0x%08x stick %d,%d",
	         (unsigned long long)buttons, (unsigned)g_pad.active_id_mask,
	         (unsigned)g_pad.attributes, g_pad.sticks[0].x, g_pad.sticks[0].y);
	SwitchPlatform_Trace(msg);
	logged++;
}

static void refresh_pad(void) {
	if (!pad_ready) return;
	padUpdate(&g_pad);
	trace_pad_state();
}

static bool prev_nav_state[6];
static bool prev_remap_state[8];
static bool prev_mouse_l = false;
static bool prev_mouse_r = false;
static bool touch_down = false;

void SwitchPlatform_ResetInput(void) {
	memset(prev_nav_state, 0, sizeof(prev_nav_state));
	memset(prev_remap_state, 0, sizeof(prev_remap_state));
	prev_mouse_l = false;
	prev_mouse_r = false;
	touch_down = false;
	Platform_InputMouseButton(0, false);
	Platform_InputMouseButton(1, false);
	Platform_InputJoystickButton(0, 0, false);
	Platform_InputJoystickButton(0, 1, false);
	Platform_InputJoystickMoveX(0, 0.0f);
	Platform_InputJoystickMoveY(0, 0.0f);
	
	KEYBOARD_ClrBuffer();
	MAPPER_LosingFocus();
}

#define SWITCH_STICK_MOUSE_PERIOD_MS 8
#define SWITCH_STICK_MOUSE_SPEED     700.0f

static void stick_mouse_scale(float *scale_x, float *scale_y) {
	static bool cached = false;
	static float sx = 1.0f, sy = 1.0f;
	if (!cached) {
		cached = true;
		Section *sec = control->GetSection("sdl");
		if (sec) {
			Prop_multival *sens = static_cast<Section_prop *>(sec)->Get_multival("sensitivity");
			if (sens) {
				sx = (float)sens->GetSection()->Get_int("xsens") / 100.0f;
				sy = (float)sens->GetSection()->Get_int("ysens") / 100.0f;
			}
		}
	}
	*scale_x = sx;
	*scale_y = sy;
}

#define SWITCH_STICK_REST_BAND 12000.0f
#define SWITCH_STICK_REST_TAU  500.0f

static void stick_centre(int stick, float *out_x, float *out_y) {
	static float centre_x[2] = { 0.0f, 0.0f };
	static float centre_y[2] = { 0.0f, 0.0f };
	static Bit32u last[2] = { 0, 0 };
	const int s = (stick == 1) ? 1 : 0;
	const float rx = (float)g_pad.sticks[s].x;
	const float ry = (float)g_pad.sticks[s].y;

	const Bit32u now = Platform_GetTicks();
	Bit32u elapsed = last[s] ? (now - last[s]) : 0;
	last[s] = now;
	if (elapsed > 200) elapsed = 200;
	const float rate = (float)elapsed / SWITCH_STICK_REST_TAU;
	const float dx = rx - centre_x[s];
	const float dy = ry - centre_y[s];
	if (dx > -SWITCH_STICK_REST_BAND && dx < SWITCH_STICK_REST_BAND)
		centre_x[s] += dx * rate;
	if (dy > -SWITCH_STICK_REST_BAND && dy < SWITCH_STICK_REST_BAND)
		centre_y[s] += dy * rate;

	*out_x = rx - centre_x[s];
	*out_y = ry - centre_y[s];
}

void SwitchPlatform_UpdateModifiers(void);

static void pump_npad(void) {
	refresh_pad();
	if (!pad_ready) return;

	const u64 cur = padGetButtons(&g_pad);

	SwitchPlatform_UpdateModifiers();

	struct { u64 mask; KBD_KEYS key; } const nav_map[] = {
		{ HidNpadButton_Up,    KBD_up    },
		{ HidNpadButton_Down,  KBD_down  },
		{ HidNpadButton_Left,  KBD_left  },
		{ HidNpadButton_Right, KBD_right },
		{ HidNpadButton_Minus, KBD_tab   },
		{ HidNpadButton_Plus,  KBD_leftshift },
	};
	static_assert(sizeof(prev_nav_state) / sizeof(prev_nav_state[0]) ==
	              sizeof(nav_map) / sizeof(nav_map[0]), "prev_nav_state must cover nav_map");

	struct { u64 mask; int id; } const remap_buttons[] = {
		{ HidNpadButton_A,  PAD_BTN_A  },
		{ HidNpadButton_B,  PAD_BTN_B  },
		{ HidNpadButton_X,  PAD_BTN_X  },
		{ HidNpadButton_Y,  PAD_BTN_Y  },
		{ HidNpadButton_L,  PAD_BTN_L  },
		{ HidNpadButton_R,  PAD_BTN_R  },
		{ HidNpadButton_ZL, PAD_BTN_ZL },
		{ HidNpadButton_ZR, PAD_BTN_ZR },
	};
	static_assert(sizeof(prev_remap_state) / sizeof(prev_remap_state[0]) ==
	              sizeof(remap_buttons) / sizeof(remap_buttons[0]), "prev_remap_state must cover remap_buttons");

	static bool joystick_inited = false;
	if (!joystick_inited) {
		JOYSTICK_Enable(0, true);
		JOYSTICK_Enable(1, true);
		PadMapping_Init();
		joystick_inited = true;
	}

	const bool overlay = OSD_OverlaysActive();

	for (unsigned i = 0; i < sizeof(nav_map) / sizeof(nav_map[0]); i++) {
		const bool is_shift = (nav_map[i].mask == HidNpadButton_Plus);
		const bool allowed = !overlay || is_shift;
		const bool held = allowed && (cur & nav_map[i].mask) != 0;
		if (held == prev_nav_state[i]) continue;
		prev_nav_state[i] = held;
		if (!overlay) {
			Platform_InputKey(nav_map[i].key, held);
		}
		if (is_shift) {
			OSD_SetHardwareModifier(MOD_SHIFT, held);
		}
	}

	bool mouse_l = false;
	bool mouse_r = false;
	bool joy0 = false;
	bool joy1 = false;

	for (unsigned i = 0; i < sizeof(remap_buttons) / sizeof(remap_buttons[0]); i++) {
		const int btn_id = remap_buttons[i].id;
		const int act_id = PadMapping_GetButtonAction(btn_id);
		const PadActionDef *act = PadMapping_GetAction(act_id);
		const u64 mask = remap_buttons[i].mask;
		const bool is_hw_mod = (mask == HidNpadButton_L || mask == HidNpadButton_R);
		const bool allowed = !overlay || is_hw_mod;
		const bool held = allowed && (cur & mask) != 0;

		if (held != prev_remap_state[i]) {
			prev_remap_state[i] = held;
			if (!overlay) {
				if (act->key1 != KBD_NONE) Platform_InputKey(act->key1, held);
				if (act->key2 != KBD_NONE) Platform_InputKey(act->key2, held);
			}
			if (mask == HidNpadButton_L) OSD_SetHardwareModifier(MOD_ALT, held);
			if (mask == HidNpadButton_R) OSD_SetHardwareModifier(MOD_CTRL, held);
		}

		if (!overlay && (cur & mask) != 0) {
			if (act->joy_btn == 0) joy0 = true;
			if (act->joy_btn == 1) joy1 = true;
			if (act->mouse_btn == 0) mouse_l = true;
			if (act->mouse_btn == 1) mouse_r = true;
		}
	}

	if (!overlay && (cur & HidNpadButton_StickR) != 0) {
		mouse_l = true;
	}

	if (!overlay) {
		float lx = 0.0f, ly = 0.0f;
		stick_centre(0, &lx, &ly);
		const float JOY_DEADZONE = 5000.0f;
		float jx = 0.0f, jy = 0.0f;
		const float lmag = sqrtf(lx * lx + ly * ly);
		if (lmag > JOY_DEADZONE) {
			jx = lx / 32767.0f;
			jy = -ly / 32767.0f;
			if (jx > 1.0f) jx = 1.0f; else if (jx < -1.0f) jx = -1.0f;
			if (jy > 1.0f) jy = 1.0f; else if (jy < -1.0f) jy = -1.0f;
		} else {
			if (cur & HidNpadButton_Left)  jx = -1.0f;
			if (cur & HidNpadButton_Right) jx =  1.0f;
			if (cur & HidNpadButton_Up)    jy = -1.0f;
			if (cur & HidNpadButton_Down)  jy =  1.0f;
		}
		Platform_InputJoystickMoveX(0, jx);
		Platform_InputJoystickMoveY(0, jy);
		Platform_InputJoystickButton(0, 0, joy0);
		Platform_InputJoystickButton(0, 1, joy1);
	} else {
		Platform_InputJoystickMoveX(0, 0.0f);
		Platform_InputJoystickMoveY(0, 0.0f);
		Platform_InputJoystickButton(0, 0, false);
		Platform_InputJoystickButton(0, 1, false);
	}

	if (mouse_l != prev_mouse_l) {
		prev_mouse_l = mouse_l;
		Platform_InputMouseButton(0, mouse_l);
	}
	if (mouse_r != prev_mouse_r) {
		prev_mouse_r = mouse_r;
		Platform_InputMouseButton(1, mouse_r);
	}

	static Bit32u stick_mouse_last = 0;
	static float stick_mouse_frac_x = 0.0f;
	static float stick_mouse_frac_y = 0.0f;
	if (overlay) {
		stick_mouse_frac_x = 0.0f;
		stick_mouse_frac_y = 0.0f;
	} else {
		const Bit32u now = Platform_GetTicks();
		if (stick_mouse_last == 0) stick_mouse_last = now;
		Bit32u elapsed = now - stick_mouse_last;
		if (elapsed >= SWITCH_STICK_MOUSE_PERIOD_MS) {
			stick_mouse_last = now;
			if (elapsed > 100) elapsed = 100;
			float rx = 0.0f, ry = 0.0f;
			stick_centre(1, &rx, &ry);
			const float DEADZONE = 6000.0f;
			const float mag = sqrtf(rx * rx + ry * ry);
			float dx = 0.0f;
			float dy = 0.0f;
			float scale_x = 1.0f, scale_y = 1.0f;
			stick_mouse_scale(&scale_x, &scale_y);
			if (mag > DEADZONE) {
				float norm = (mag - DEADZONE) / (32767.0f - DEADZONE);
				if (norm > 1.0f) norm = 1.0f;
				const float speed = (norm * norm * 0.5f + norm * 0.5f) * SWITCH_STICK_MOUSE_SPEED;
				dx = (rx / mag) * speed * (float)elapsed / 1000.0f;
				dy = -(ry / mag) * speed * (float)elapsed / 1000.0f;
			}
			stick_mouse_frac_x += dx * scale_x;
			stick_mouse_frac_y += dy * scale_y;
			const float step_x = truncf(stick_mouse_frac_x);
			const float step_y = truncf(stick_mouse_frac_y);
			if (step_x != 0.0f || step_y != 0.0f) {
				stick_mouse_frac_x -= step_x;
				stick_mouse_frac_y -= step_y;
				Platform_InputMouseMove(step_x, step_y, 0.0f, 0.0f, true);
			}
		}
	}
}

static void pump_touch(void) {
	HidTouchScreenState touch;
	hidGetTouchScreenStates(&touch, 1);
	const bool down = touch.count > 0;
	if (down) {
		float x = (float)touch.touches[0].x / (float)screen_width;
		float y = (float)touch.touches[0].y / (float)screen_height;
		if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
		if (y < 0.0f) y = 0.0f; else if (y > 1.0f) y = 1.0f;
		Platform_InputMouseMove(0.0f, 0.0f, x, y, false);
		if (!touch_down) {
			Platform_InputMouseButton(0, true);
			touch_down = true;
		}
	} else {
		if (touch_down) {
			Platform_InputMouseButton(0, false);
			touch_down = false;
		}
	}
}

static void trace_heartbeat(void);

static void SwitchPlatform_PauseLoop(void) {
	struct PauseGuard {
		PauseGuard(void) { Platform_AudioPause(true); }
		~PauseGuard(void) { Platform_AudioPause(false); }
	} guard;

	while (appletMainLoop()) {
		trace_heartbeat();
		struct OSDPad pad = {};
		if (OSD_ReadPad(&pad)) {
			OSD_UpdateOverlays(&pad);
			if (!OSD_PauseMenuOpen()) break;
		}
		struct OSDCanvas canvas;
		if (OSD_BeginFrame(&canvas)) {
			OSD_DrawOverlaysInto(&canvas);
			OSD_EndFrame();
		}
		OSD_FrameLimit();
	}
}

static void trace_heartbeat(void) {
	static Bit32u next = 0;
	static unsigned beats = 0;
	static unsigned pumps = 0;
	pumps++;
	const Bit32u now = Platform_GetTicks();
	if (next == 0) next = now + 5000;
	if (now < next) return;
	next = now + 5000;
	char msg[160];
	snprintf(msg, sizeof(msg), "alive %u: pumps=%u overlay=%d cs:ip=%04x:%04x",
	         ++beats, pumps, OSD_OverlaysActive() ? 1 : 0,
	         (unsigned)SegValue(cs), (unsigned)reg_ip);
	pumps = 0;
	SwitchPlatform_Trace(msg);
}

void Platform_PumpEvents(void) {
	trace_heartbeat();

	RENDER_SetForceUpdate(OSD_WantsRedraw());

	pump_npad();
	pump_touch();
	if (!pad_ready) return;

	struct OSDPad pad = {};
	if (!OSD_ReadPad(&pad)) return;

	if (in_video_update) {
		if (!OSD_PauseMenuOpen()) OSD_UpdateOverlays(&pad);
		return;
	}

	OSD_UpdateOverlays(&pad);
	if (OSD_PauseMenuOpen()) SwitchPlatform_PauseLoop();
}

static bool SwitchOSD_BeginFrame(struct OSDCanvas *canvas) {
	if (!screen_mirror) return false;
	canvas->pixels = screen_mirror;
	canvas->width = (int)screen_width;
	canvas->height = (int)screen_height;
	canvas->pitch = (int)screen_width;
	return true;
}

static void SwitchOSD_EndFrame(void) {
	if (!screen_mirror) return;
	u32 stride = 0;
	uint32_t *dst = (uint32_t *)framebufferBegin(&g_framebuffer, &stride);
	if (!dst) return;
	const u32 dst_pitch = stride / 4;
	for (u32 y = 0; y < screen_height; y++) {
		memcpy(&dst[(size_t)y * dst_pitch], &screen_mirror[(size_t)y * screen_width],
		       (size_t)screen_width * 4);
	}
	framebufferEnd(&g_framebuffer);
}

static bool SwitchOSD_PadState(struct OSDPad *pad) {
	if (!pad || !pad_ready) return false;
	refresh_pad();
	const u64 cur = padGetButtons(&g_pad);

	pad->up    = (cur & HidNpadButton_Up) != 0;
	pad->down  = (cur & HidNpadButton_Down) != 0;
	pad->left  = (cur & HidNpadButton_Left) != 0;
	pad->right = (cur & HidNpadButton_Right) != 0;
	pad->a      = (cur & HidNpadButton_A) != 0;
	pad->b      = (cur & HidNpadButton_B) != 0;
	pad->x      = (cur & HidNpadButton_X) != 0;
	pad->y      = (cur & HidNpadButton_Y) != 0;
	pad->l      = (cur & HidNpadButton_L) != 0;
	pad->r      = (cur & HidNpadButton_R) != 0;
	pad->zl     = (cur & HidNpadButton_ZL) != 0;
	pad->zr     = (cur & HidNpadButton_ZR) != 0;
	pad->plus   = (cur & HidNpadButton_Plus) != 0;
	pad->minus  = (cur & HidNpadButton_Minus) != 0;
	pad->stick_l = (cur & HidNpadButton_StickL) != 0;
	pad->stick_r = (cur & HidNpadButton_StickR) != 0;
	float lx = 0.0f, ly = 0.0f;
	stick_centre(0, &lx, &ly);
	pad->lx = lx / 15000.0f;
	pad->ly = ly / 15000.0f;
	return true;
}

static void SwitchOSD_FrameLimit(void) {
	svcSleepThread(16 * 1000000LL);
}

static void SwitchPlatform_ReturnToLibrary(void) {
	SwitchPlatform_Trace("exit: the menu asked for the library");
	throw (int)0;
}

static void SwitchPlatform_QuitApp(void) {
	SwitchPlatform_Trace("exit: the menu asked to quit DOSBox");
	throw (int)1;
}

static bool SwitchOSD_Status(struct OSDStatus *status) {
	bool any = false;

	u32 percent = 0;
	if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent))) {
		status->battery_percent = (int)percent;
		status->battery_valid = true;
		any = true;
	}
	PsmChargerType charger = PsmChargerType_Unconnected;
	if (R_SUCCEEDED(psmGetChargerType(&charger)))
		status->battery_charging = (charger != PsmChargerType_Unconnected);

	FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
	if (sd) {
		s64 free_bytes = 0;
		if (R_SUCCEEDED(fsFsGetFreeSpace(sd, "/", &free_bytes)) && free_bytes > 0) {
			status->card_free_bytes = (unsigned long long)free_bytes;
			status->card_valid = true;
			any = true;
		}
	}
	return any;
}

static bool SwitchPlatform_ExitRequested(void) {
	return !appletMainLoop();
}

void SwitchPlatform_OSDInit(void) {
	OSD_Init(SwitchOSD_BeginFrame, SwitchOSD_EndFrame, OSD_PIXFMT_RGBA);
	OSD_SetInputCallbacks(SwitchOSD_PadState, SwitchOSD_FrameLimit);
	if (R_SUCCEEDED(psmInitialize())) {
		OSD_SetStatusCallbacks(SwitchOSD_Status);
		SwitchPlatform_Trace("osd: battery and card readout on");
	} else {
		SwitchPlatform_Trace("osd: no power service, readout off");
	}
	OSD_SetExitSessionFn(SwitchPlatform_QuitApp);
	OSD_SetReturnToLibraryFn(SwitchPlatform_ReturnToLibrary);
	OSD_SetFTPServer(SwitchPlatform_FTPStart, SwitchPlatform_FTPStop,
	                 SwitchPlatform_FTPStatus, SwitchPlatform_FTPTakeLibraryDirty);
	OSD_SetExitPollFn(SwitchPlatform_ExitRequested);
}

void SwitchPlatform_ResetSession(void) {
	in_video_update = false;
	video_updating = false;
	video_callback = NULL;
	if (screen_mirror) {
		memset(screen_mirror, 0, (size_t)screen_width * screen_height * 4);
	}
	RENDER_ResetSession();
	VGA_ResetDrawState();
	TIMER_ClearTickHandlers();
	
	SwitchPlatform_FTPStop();

	KEYBOARD_ClrBuffer();
	MAPPER_LosingFocus();
	
	if (video_ready) {
		framebufferBegin(&g_framebuffer, NULL);
		framebufferEnd(&g_framebuffer);
	}
}

void SwitchPlatform_UpdateModifiers(void) {
	if (!pad_ready) return;
	g_prev_ctrl = (padGetButtons(&g_pad) & HidNpadButton_R) != 0;
	g_prev_alt   = (padGetButtons(&g_pad) & HidNpadButton_L) != 0;
}

struct Platform_Mutex {
	RMutex m;
};

Platform_MutexHandle Platform_MutexCreate(void) {
	Platform_Mutex *handle = (Platform_Mutex *)malloc(sizeof(Platform_Mutex));
	if (!handle) return NULL;
	rmutexInit(&handle->m);
	return reinterpret_cast<Platform_MutexHandle>(handle);
}

void Platform_MutexDestroy(Platform_MutexHandle mutex) {
	free(mutex);
}

void Platform_MutexLock(Platform_MutexHandle mutex) {
	if (mutex) rmutexLock(&((Platform_Mutex *)mutex)->m);
}

void Platform_MutexUnlock(Platform_MutexHandle mutex) {
	if (mutex) rmutexUnlock(&((Platform_Mutex *)mutex)->m);
}

void SwitchPlatform_VideoShutdown(void) {
	if (video_ready) {
		framebufferClose(&g_framebuffer);
		video_ready = false;
	}
	video_updating = false;
	in_video_update = false;
	video_callback = NULL;
	free(surface_pixels);
	surface_pixels = NULL;
	surface_width = surface_height = surface_pitch = 0;
	free(screen_mirror);
	screen_mirror = NULL;
}

void SwitchPlatform_RecreateFramebuffer(void) {
	if (video_updating) {
		PlatformVideo_EndUpdate(NULL);
	}
	PlatformVideoCallback callback = video_callback;
	SwitchPlatform_VideoShutdown();
	SwitchPlatform_VideoInit();
	video_callback = callback;
	if (video_callback) video_callback(PLATFORM_VIDEO_REDRAW);
}

void SwitchPlatform_AudioShutdown(void) {
	Platform_AudioClose();
}

u32 SwitchPlatform_ScreenWidth(void) {
	return screen_width;
}

u32 SwitchPlatform_ScreenHeight(void) {
	return screen_height;
}
