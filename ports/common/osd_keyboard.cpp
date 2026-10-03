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

#include "osd.h"
#include "osd_font.h"
#include "keyboard.h"
#include "platform.h"

#include <stdio.h>
#include <string>

#include "control.h"
#include "cpu.h"
#include "mixer.h"
#include "pad_mapping.h"
#include "savestate.h"
#include "setup.h"

void Platform_InputKey(KBD_KEYS key, bool pressed);

enum KeyAction {
	ACTION_NONE = 0,
	ACTION_CHAR,
	ACTION_KBD,
	ACTION_MODIFIER,
	ACTION_CLOSE,
	ACTION_LAYER
};

struct VirtualKey {
	const char *label;
	unsigned char ch;
	enum KeyAction action;
	KBD_KEYS kbd;
};

#define ROW_LETTERS  "1234567890qwertyuiop"
#define ROW_HOME     "asdfghjkl;'\\"
#define ROW_LOWER    "zxcvbnm,./"
#define MODIFIER_ID(id_) { NULL, (unsigned char)(MOD_##id_), ACTION_MODIFIER, KBD_NONE }

#define KEYBOARD_ALPHA_WIDTH 14
static const struct VirtualKey keyboard_rows[4][KEYBOARD_ALPHA_WIDTH] = {
	{ { "1", '1', ACTION_CHAR, KBD_NONE }, { "2", '2', ACTION_CHAR, KBD_NONE },
	  { "3", '3', ACTION_CHAR, KBD_NONE }, { "4", '4', ACTION_CHAR, KBD_NONE },
	  { "5", '5', ACTION_CHAR, KBD_NONE }, { "6", '6', ACTION_CHAR, KBD_NONE },
	  { "7", '7', ACTION_CHAR, KBD_NONE }, { "8", '8', ACTION_CHAR, KBD_NONE },
	  { "9", '9', ACTION_CHAR, KBD_NONE }, { "0", '0', ACTION_CHAR, KBD_NONE },
	  { "-", '-', ACTION_CHAR, KBD_NONE }, { "=", '=', ACTION_CHAR, KBD_NONE },
	  { "BKSP", 0, ACTION_KBD, KBD_backspace } },
	{ { "q", 'q', ACTION_CHAR, KBD_NONE }, { "w", 'w', ACTION_CHAR, KBD_NONE },
	  { "e", 'e', ACTION_CHAR, KBD_NONE }, { "r", 'r', ACTION_CHAR, KBD_NONE },
	  { "t", 't', ACTION_CHAR, KBD_NONE }, { "y", 'y', ACTION_CHAR, KBD_NONE },
	  { "u", 'u', ACTION_CHAR, KBD_NONE }, { "i", 'i', ACTION_CHAR, KBD_NONE },
	  { "o", 'o', ACTION_CHAR, KBD_NONE }, { "p", 'p', ACTION_CHAR, KBD_NONE },
	  { "[", '[', ACTION_CHAR, KBD_NONE }, { "]", ']', ACTION_CHAR, KBD_NONE },
	  { "ENTER", 0, ACTION_KBD, KBD_enter } },
	{ { "a", 'a', ACTION_CHAR, KBD_NONE }, { "s", 's', ACTION_CHAR, KBD_NONE },
	  { "d", 'd', ACTION_CHAR, KBD_NONE }, { "f", 'f', ACTION_CHAR, KBD_NONE },
	  { "g", 'g', ACTION_CHAR, KBD_NONE }, { "h", 'h', ACTION_CHAR, KBD_NONE },
	  { "j", 'j', ACTION_CHAR, KBD_NONE }, { "k", 'k', ACTION_CHAR, KBD_NONE },
	  { "l", 'l', ACTION_CHAR, KBD_NONE }, { ";", ';', ACTION_CHAR, KBD_NONE },
	  { ":", ':', ACTION_CHAR, KBD_NONE }, { "'", '\'', ACTION_CHAR, KBD_NONE },
	  { "ESC", 0, ACTION_KBD, KBD_esc } },
	{ { "z", 'z', ACTION_CHAR, KBD_NONE }, { "x", 'x', ACTION_CHAR, KBD_NONE },
	  { "c", 'c', ACTION_CHAR, KBD_NONE }, { "v", 'v', ACTION_CHAR, KBD_NONE },
	  { "b", 'b', ACTION_CHAR, KBD_NONE }, { "n", 'n', ACTION_CHAR, KBD_NONE },
	  { "m", 'm', ACTION_CHAR, KBD_NONE }, { ",", ',', ACTION_CHAR, KBD_NONE },
	  { ".", '.', ACTION_CHAR, KBD_NONE }, { "/", '/', ACTION_CHAR, KBD_NONE },
	  { "\\", '\\', ACTION_CHAR, KBD_NONE },
	  { "SPACE", ' ', ACTION_CHAR, KBD_NONE },
	  { "SHIFT", MOD_SHIFT, ACTION_MODIFIER, KBD_NONE },
	  { "FN", 0, ACTION_LAYER, KBD_NONE } },
};
static const int keyboard_row_count[4] = { 13, 13, 13, 14 };

static const struct VirtualKey function_row_0[] = {
	{ "ESC", 0, ACTION_KBD, KBD_esc },
	{ "F1", 0, ACTION_KBD, KBD_f1 },
	{ "F2", 0, ACTION_KBD, KBD_f2 },
	{ "F3", 0, ACTION_KBD, KBD_f3 },
	{ "F4", 0, ACTION_KBD, KBD_f4 },
	{ "F5", 0, ACTION_KBD, KBD_f5 },
	{ "F6", 0, ACTION_KBD, KBD_f6 },
	{ "F7", 0, ACTION_KBD, KBD_f7 },
	{ "F8", 0, ACTION_KBD, KBD_f8 },
	{ "F9", 0, ACTION_KBD, KBD_f9 },
	{ "F10", 0, ACTION_KBD, KBD_f10 },
	{ "F11", 0, ACTION_KBD, KBD_f11 },
	{ "F12", 0, ACTION_KBD, KBD_f12 },
};
static const struct VirtualKey function_row_1[] = {
	{ "\x18", 0, ACTION_KBD, KBD_up },
	{ "\x1b", 0, ACTION_KBD, KBD_esc },
	{ "\x1a", 0, ACTION_KBD, KBD_left },
	{ "\x1d", 0, ACTION_KBD, KBD_down },
	{ "\x1c", 0, ACTION_KBD, KBD_right },
	{ "PGUP", 0, ACTION_KBD, KBD_pageup },
	{ "INS", 0, ACTION_KBD, KBD_insert },
	{ "DEL", 0, ACTION_KBD, KBD_delete },
	{ "HOME", 0, ACTION_KBD, KBD_home },
	{ "END", 0, ACTION_KBD, KBD_end },
	{ "PGDN", 0, ACTION_KBD, KBD_pagedown },
	{ "KP0", 0, ACTION_KBD, KBD_kp0 },
	{ "KP.", 0, ACTION_KBD, KBD_kpperiod },
};
static const struct VirtualKey function_row_2[] = {
	{ "SHIFT", MOD_SHIFT, ACTION_MODIFIER, KBD_NONE },
	{ "CTRL", MOD_CTRL, ACTION_MODIFIER, KBD_NONE },
	{ "ALT", MOD_ALT, ACTION_MODIFIER, KBD_NONE },
	{ "CAPS", 0, ACTION_KBD, KBD_capslock },
	{ "NUM", 0, ACTION_KBD, KBD_numlock },
	{ "SCROLL", 0, ACTION_KBD, KBD_scrolllock },
	{ "PRINT", 0, ACTION_KBD, KBD_printscreen },
	{ "PAUSE", 0, ACTION_KBD, KBD_pause },
	{ "TABC", 0, ACTION_KBD, KBD_tab },
	{ "SPACE", ' ', ACTION_CHAR, KBD_NONE },
	{ "ABC", 0, ACTION_LAYER, KBD_NONE },
	{ "CLOSE", 0, ACTION_CLOSE, KBD_NONE },
};

enum OverlayMode {
	OVERLAY_OFF = 0,
	OVERLAY_KEYBOARD,
	OVERLAY_PAUSE,
	OVERLAY_HELP,
	OVERLAY_STATS,
	OVERLAY_ABOUT,
	OVERLAY_REMAP
};

static float about_offset = 0.0f;
static float about_speed = 1.0f;
static bool about_paused = false;
static Bit32u about_last_tick = 0;
static int about_speed_dir = 0;
static Bit32u about_speed_repeat_at = 0;
static bool about_from_pause = false;

static const float ABOUT_SPEED_BASE = 34.0f;
static const float ABOUT_SPEED_MIN  = -3.0f;
static const float ABOUT_SPEED_MAX  = 6.0f;

static void about_reset(void) {
	about_offset = 0.0f;
	about_speed = 1.0f;
	about_paused = false;
	about_speed_dir = 0;
	about_last_tick = Platform_GetTicks();
}
static bool help_from_pause = false;
static int remap_cursor = 0;
static bool remap_from_pause = false;

static enum OverlayMode overlay_mode = OVERLAY_OFF;
static int  keyboard_cursor_row = 0;
static int  keyboard_cursor_col = 0;
static bool keyboard_function_layer = false;
static int  keyboard_modifiers = 0;
static bool keyboard_held = false;
static KBD_KEYS keyboard_held_kbd = KBD_NONE;
static Bit32u stats_last_toggle = 0;

static int keyboard_rows_count(void) {
	return keyboard_function_layer ? 3 : 4;
}

static const struct VirtualKey *keyboard_row(int row) {
	if (keyboard_function_layer) {
		switch (row) {
		case 0: return function_row_0;
		case 1: return function_row_1;
		case 2: return function_row_2;
		default: return function_row_2;
		}
	}
	return keyboard_rows[row % 4];
}

static int keyboard_row_len(int row) {
	if (keyboard_function_layer) {
		switch (row) {
		case 0: return (int)(sizeof(function_row_0) / sizeof(function_row_0[0]));
		case 1: return (int)(sizeof(function_row_1) / sizeof(function_row_1[0]));
		case 2: return (int)(sizeof(function_row_2) / sizeof(function_row_2[0]));
		default: return 0;
		}
	}
	if (row < 0 || row > 3) return 0;
	return keyboard_row_count[row];
}

static KBD_KEYS keyboard_key_for_char(char ch);
static void keyboard_send_key(KBD_KEYS key);

static void keyboard_send_char(char ch) {
	keyboard_send_key(keyboard_key_for_char(ch));
}

static KBD_KEYS keyboard_key_for_char(char ch) {
	static const struct { char ch; KBD_KEYS key; } table[] = {
		{ 'a', KBD_a }, { 'b', KBD_b }, { 'c', KBD_c }, { 'd', KBD_d },
		{ 'e', KBD_e }, { 'f', KBD_f }, { 'g', KBD_g }, { 'h', KBD_h },
		{ 'i', KBD_i }, { 'j', KBD_j }, { 'k', KBD_k }, { 'l', KBD_l },
		{ 'm', KBD_m }, { 'n', KBD_n }, { 'o', KBD_o }, { 'p', KBD_p },
		{ 'q', KBD_q }, { 'r', KBD_r }, { 's', KBD_s }, { 't', KBD_t },
		{ 'u', KBD_u }, { 'v', KBD_v }, { 'w', KBD_w }, { 'x', KBD_x },
		{ 'y', KBD_y }, { 'z', KBD_z },
		{ '0', KBD_0 }, { '1', KBD_1 }, { '2', KBD_2 }, { '3', KBD_3 },
		{ '4', KBD_4 }, { '5', KBD_5 }, { '6', KBD_6 }, { '7', KBD_7 },
		{ '8', KBD_8 }, { '9', KBD_9 },
		{ ' ', KBD_space }, { '-', KBD_minus }, { '=', KBD_equals },
		{ '[', KBD_leftbracket }, { ']', KBD_rightbracket },
		{ ';', KBD_semicolon }, { ':', KBD_semicolon },
		{ '\'', KBD_quote }, { '"', KBD_quote },
		{ ',', KBD_comma }, { '<', KBD_comma },
		{ '.', KBD_period }, { '>', KBD_period },
		{ '/', KBD_slash }, { '?', KBD_slash },
		{ '\\', KBD_backslash }, { '|', KBD_backslash },
		{ '`', KBD_grave }, { '~', KBD_grave },
		{ '_', KBD_minus }, { '+', KBD_equals },
		{ '{', KBD_leftbracket }, { '}', KBD_rightbracket },
		{ '!', KBD_1 }, { '@', KBD_2 }, { '#', KBD_3 }, { '$', KBD_4 },
		{ '%', KBD_5 }, { '^', KBD_6 }, { '&', KBD_7 }, { '*', KBD_8 },
		{ '(', KBD_9 }, { ')', KBD_0 },
	};
	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (table[i].ch == ch) return table[i].key;
	}
	if (ch >= 'A' && ch <= 'Z') return keyboard_key_for_char((char)(ch - 'A' + 'a'));
	switch (ch) {
	case '\r': case '\n': return KBD_enter;
	case '\t': return KBD_tab;
	case 0x7f: case '\b': return KBD_backspace;
	case 0x1b: return KBD_esc;
	default: return KBD_NONE;
	}
}

static char keyboard_typed[128] = "";
static size_t keyboard_typed_len = 0;

static void keyboard_typed_reset(void) {
	keyboard_typed[0] = '\0';
	keyboard_typed_len = 0;
}

static void keyboard_typed_push(char c) {
	if (!c) return;
	if (c == '\r' || c == '\n') {
		keyboard_typed_reset();
		return;
	}
	if (c == 0x08) {
		if (keyboard_typed_len) {
			keyboard_typed[--keyboard_typed_len] = '\0';
		}
		return;
	}
	if (keyboard_typed_len + 1 >= sizeof(keyboard_typed)) {
		memmove(keyboard_typed, keyboard_typed + 1, sizeof(keyboard_typed) - 2);
		keyboard_typed_len = sizeof(keyboard_typed) - 2;
	}
	keyboard_typed[keyboard_typed_len++] = c;
	keyboard_typed[keyboard_typed_len] = '\0';
}

static void keyboard_send_key(KBD_KEYS key) {
	if (key == KBD_NONE) return;
	if (keyboard_modifiers & MOD_ALT)  Platform_InputKey(KBD_leftalt, true);
	if (keyboard_modifiers & MOD_CTRL) Platform_InputKey(KBD_leftctrl, true);
	if (keyboard_modifiers & MOD_SHIFT) Platform_InputKey(KBD_leftshift, true);
	Platform_InputKey(key, true);
	Platform_InputKey(key, false);
	if (keyboard_modifiers & MOD_SHIFT) Platform_InputKey(KBD_leftshift, false);
	if (keyboard_modifiers & MOD_CTRL) Platform_InputKey(KBD_leftctrl, false);
	if (keyboard_modifiers & MOD_ALT)  Platform_InputKey(KBD_leftalt, false);
}

static char keyboard_shifted_char(char c) {
	if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
	switch (c) {
		case '1': return '!'; case '2': return '"'; case '3': return '#';
		case '4': return '$'; case '5': return '%'; case '6': return '^';
		case '7': return '&'; case '8': return '*'; case '9': return '(';
		case '0': return ')'; case '-': return '_'; case '=': return '+';
		case '[': return '{'; case ']': return '}'; case ';': return ':';
		case '\'': return '"'; case ',': return '<'; case '.': return '>';
		case '/': return '?';  case '\\': return '|'; case '`': return '~';
		default: return c;
	}
}

static void keyboard_activate(const struct VirtualKey *vk) {
	switch (vk->action) {
	case ACTION_CHAR: {
		const char typed = (keyboard_modifiers & MOD_SHIFT)
		                   ? keyboard_shifted_char((char)vk->ch)
		                   : (char)vk->ch;
		keyboard_typed_push(typed);
		keyboard_send_char(typed);
		break;
	}
	case ACTION_KBD:
		if (vk->kbd == KBD_backspace) keyboard_typed_push((char)0x08);
		else if (vk->kbd == KBD_enter) keyboard_typed_push('\r');
		if (keyboard_modifiers & MOD_ALT)  Platform_InputKey(KBD_leftalt, true);
		if (keyboard_modifiers & MOD_CTRL) Platform_InputKey(KBD_leftctrl, true);
		if (keyboard_modifiers & MOD_SHIFT) Platform_InputKey(KBD_leftshift, true);
		Platform_InputKey(vk->kbd, true);
		Platform_InputKey(vk->kbd, false);
		if (keyboard_modifiers & MOD_SHIFT) Platform_InputKey(KBD_leftshift, false);
		if (keyboard_modifiers & MOD_CTRL) Platform_InputKey(KBD_leftctrl, false);
		if (keyboard_modifiers & MOD_ALT)  Platform_InputKey(KBD_leftalt, false);
		break;
	case ACTION_MODIFIER:
		keyboard_modifiers ^= vk->ch;
		break;
	case ACTION_LAYER:
		keyboard_function_layer = !keyboard_function_layer;
		keyboard_cursor_row = 0;
		keyboard_cursor_col = 0;
		break;
	case ACTION_CLOSE:
		OSD_CloseOverlays();
		break;
	default:
		break;
	}
}

static void keyboard_release_held(void) {
	if (keyboard_held && keyboard_held_kbd != KBD_NONE) {
		Platform_InputKey(keyboard_held_kbd, false);
	}
	keyboard_held = false;
	keyboard_held_kbd = KBD_NONE;
}

static void keyboard_shifted_label(const char *base, char *out, size_t out_size) {
	if (!base || strlen(base) != 1) {
		snprintf(out, out_size, "%s", base ? base : "");
		return;
	}
	const char c = base[0];
	char g = c;
	if (c >= 'a' && c <= 'z') {
		g = (char)(c - 'a' + 'A');
	} else {
		switch (c) {
		case '1': g = '!'; break;  case '2': g = '"'; break;
		case '3': g = '#'; break;  case '4': g = '$'; break;
		case '5': g = '%'; break;  case '6': g = '^'; break;
		case '7': g = '&'; break;  case '8': g = '*'; break;
		case '9': g = '('; break;  case '0': g = ')'; break;
		case '-': g = '_'; break;  case '=': g = '+'; break;
		case '[': g = '{'; break;  case ']': g = '}'; break;
		case ';': g = ':'; break;  case '\'': g = '"'; break;
		case ',': g = '<'; break;  case '.': g = '>'; break;
		case '/': g = '?'; break;  case '\\': g = '|'; break;
		case '`': g = '~'; break;
		default: break;
		}
	}
	out[0] = g;
	out[1] = '\0';
}

static void keyboard_key_box(struct OSDCanvas *c, int x, int y, int w, int h,
                             const char *label, bool selected, uint32_t colour) {
	if (selected) {
		OSD_GradientV(c, x, y, w, h, OSD_Colour(0x24, 0x40, 0x7a),
		              OSD_Colour(0x1b, 0x2c, 0x55));
		OSD_DrawRect(c, x, y, w, h, OSD_Colour(0xff, 0x9b, 0x3d));
		colour = OSD_Colour(0xff, 0xff, 0xff);
	} else {
		OSD_Panel(c, x, y, w, h, OSD_Colour(0x16, 0x1e, 0x33),
		          OSD_Colour(0x2a, 0x3a, 0x66), 255);
	}
	OSD_TextCentre(c, x + w / 2, y + (h - OSD_FONT_HEIGHT * 2) / 2, label, colour, 2, !selected);
}

static void keyboard_draw(struct OSDCanvas *c) {
	OSD_BlendRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0), 150);

	const int pad = 24;
	const int panel_h = c->height / 2;
	const int panel_y = c->height - panel_h;
	OSD_Panel(c, pad, panel_y, c->width - 2 * pad, panel_h - pad,
	          OSD_Colour(0x0d, 0x13, 0x26), OSD_Colour(0x3d, 0x5a, 0x9e), 245);

	const int header_h = OSD_FONT_HEIGHT * 3;
	const int preview_h = OSD_FONT_HEIGHT * 2;
	const int preview_y = panel_y + 14 + header_h + 6;

	char header[128];
	snprintf(header, sizeof(header), "Keyboard %s%s%s%s",
	         keyboard_function_layer ? "- function keys" : "- letters",
	         (keyboard_modifiers & MOD_SHIFT) ? "  [SHIFT]" : "",
	         (keyboard_modifiers & MOD_CTRL)  ? "  [CTRL]" : "",
	         (keyboard_modifiers & MOD_ALT)   ? "  [ALT]" : "");
	OSD_Text(c, pad + 18, panel_y + 14, header, OSD_Colour(0xff, 0x9b, 0x3d), 3, true);
	OSD_TextRight(c, c->width - pad - 18, panel_y + 14 + OSD_FONT_HEIGHT,
	              "A: TYPE   X / FN: F1-F12 / ABC   B: CLOSE",
	              OSD_Colour(0x80, 0x90, 0xad), 2, false);

	if (keyboard_typed_len) {
		char line[160];
		snprintf(line, sizeof(line), "> %s_", keyboard_typed);
		OSD_Text(c, pad + 18, preview_y, line, OSD_Colour(0xe8, 0xee, 0xf8), 2, false);
	} else {
		OSD_Text(c, pad + 18, preview_y, "> _",
		         OSD_Colour(0x80, 0x90, 0xad), 2, false);
	}

	const int rows = keyboard_rows_count();
	const int grid_y = preview_y + preview_h + 8;
	const int grid_h = (panel_h - pad) - (grid_y - panel_y) - 8;
	const int row_h = grid_h / rows;
	const int row_gap = 8;
	const int key_h = row_h - row_gap;

	const bool shifted = (keyboard_modifiers & MOD_SHIFT) != 0;
	char shown[16][8];

	for (int row = 0; row < rows; row++) {
		const struct VirtualKey *keys = keyboard_row(row);
		const int count = keyboard_row_len(row);
		if (count <= 0) continue;

		const int gap = 4;
		int total = 0;
		for (int i = 0; i < count && i < 16; i++) {
			if (shifted && keys[i].action == ACTION_CHAR)
				keyboard_shifted_label(keys[i].label, shown[i], sizeof(shown[i]));
			else
				snprintf(shown[i], sizeof(shown[i]), "%s", keys[i].label);
			total += OSD_TextWidth(shown[i], 2) + 26;
		}
		const int avail = c->width - 2 * pad - 36 - gap * (count - 1);
		int x = pad + 18;

		for (int i = 0; i < count; i++) {
			const char *label = (i < 16) ? shown[i] : keys[i].label;
			int w = (OSD_TextWidth(label, 2) + 26) * avail / total;
			bool selected = (row == keyboard_cursor_row && i == keyboard_cursor_col);
			uint32_t colour = OSD_Colour(0xe8, 0xee, 0xf8);
			if (keys[i].action == ACTION_MODIFIER &&
			    (keyboard_modifiers & (1 << (keys[i].ch - 1)))) {
				colour = OSD_Colour(0xff, 0x9b, 0x3d);
			} else if (keys[i].action == ACTION_CLOSE ||
			           keys[i].action == ACTION_LAYER) {
				colour = OSD_Colour(0xff, 0x9b, 0x3d);
			}
			keyboard_key_box(c, x, grid_y + row * row_h, w, key_h,
			                 label, selected, colour);
			x += w + gap;
		}
	}
}

static bool help_is_heading(int index) {
	return index == 0 || index == 16 || index == 22;
}

static void help_draw(struct OSDCanvas *c) {
	static const char *const lines[] = {
		"Controller mapping",
		"",
		"  D-pad                    arrow keys / menu navigation",
		"  A                         ENTER / select",
		"  B                         ESC / back",
		"  X                         SPACE (menu: help)",
		"  Y                         'Y' key (menu: quit)",
		"  L / R                     ALT / CTRL",
		"  ZL / ZR                   Left Click / Right Click",
		"  PLUS                      SHIFT",
		"  MINUS                     TAB",
		"  Minus + Plus             in-game menu (pauses the game)",
		"  left stick click (L3)    virtual keyboard",
		"  right stick               mouse cursor",
		"  touch screen             touch mouse click",
		"",
		"Pause menu  (Minus + Plus)",
		"  volume, CPU speed, frame skip, scaler, aspect, shader",
		"  state slot, save state, load state, virtual keyboard",
		"  statistics, exit to the game library",
		"  game saves go to the game folder, states next to it",
		"",
		"Useful shortcuts",
		"  LAYER                     second layer of the virtual keyboard",
		"  SHIFT on the keyboard      capitals and the shifted symbols",
		"  Ctrl+F5 / F6 / F7 / F8    captures and frame skip",
		"  Ctrl+F11 / F12            CPU cycles and speedlock",
		"",
		"Press B or X to close",
	};
	const int count = (int)(sizeof(lines) / sizeof(lines[0]));
	OSD_BlendRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0), 180);

	const int margin = 16;
	const int pad = 24;
	int rows[32], nrows = 0;
	for (int i = 0; i < count; i++) {
		if (!lines[i][0]) continue;
		if (nrows < (int)(sizeof(rows) / sizeof(rows[0]))) rows[nrows++] = i;
	}
	int headings = 0;
	for (int r = 0; r < nrows; r++)
		if (help_is_heading(rows[r])) headings++;

	const int avail = c->height - 2 * margin - 2 * pad;
	const int head_extra = OSD_FONT_HEIGHT;
	int body_scale = 1;
	for (int bs = 2; bs >= 1; bs--) {
		const int base = (avail - headings * head_extra) / nrows;
		if (base >= OSD_FONT_HEIGHT * bs) { body_scale = bs; break; }
	}
	const int head_scale = body_scale + 1;
	int body_h = OSD_FONT_HEIGHT * body_scale, head_h = body_h + head_extra;
	if (avail > 0 && headings < nrows) {
		const int base = (avail - headings * head_extra) / nrows;
		if (base > body_h) {
			body_h = base;
			head_h = body_h + head_extra;
		}
	}

	int total_h = 0;
	for (int r = 0; r < nrows; r++)
		total_h += help_is_heading(rows[r]) ? head_h : body_h;

	const int w = c->width - 160;
	int h = total_h + 2 * pad;
	if (h > c->height - margin) h = c->height - margin;
	const int x = 80;
	int y = (c->height - h) / 2;
	if (y < margin) y = margin;
	OSD_Panel(c, x, y, w, h, OSD_Colour(0x0d, 0x13, 0x26),
	          OSD_Colour(0xff, 0x9b, 0x3d), 250);
	int ty = y + pad;
	for (int r = 0; r < nrows; r++) {
		const int i = rows[r];
		uint32_t colour = OSD_Colour(0xe8, 0xee, 0xf8);
		int scale = body_scale;
		if (help_is_heading(i)) {
			colour = OSD_Colour(0xff, 0x9b, 0x3d);
			scale = head_scale;
		}
		OSD_Text(c, x + 28, ty, lines[i], colour, scale, true);
		ty += help_is_heading(i) ? head_h : body_h;
	}
}

static void stats_draw(struct OSDCanvas *c) {
	static unsigned frames = 0;
	static Bit32u window_start = 0;
	static unsigned fps = 0;

	const Bit32u now = Platform_GetTicks();
	if (!window_start) window_start = now;
	frames++;
	if (now - window_start >= 1000) {
		fps = frames * 1000u / (now - window_start);
		frames = 0;
		window_start = now;
	}

	const Bit32u elapsed = now - stats_last_toggle;
	char line[128];
	snprintf(line, sizeof(line), "FPS %u   uptime %u:%02u   %dx%d",
	         fps, (unsigned)(elapsed / 60000u), (unsigned)((elapsed / 1000u) % 60u),
	         c->width, c->height);

	const int w = 420, h = 44;
	const int x = c->width - w - 16, y = 16;
	OSD_Panel(c, x, y, w, h, OSD_Colour(0x0d, 0x13, 0x26),
	          OSD_Colour(0xff, 0x9b, 0x3d), 220);
	OSD_Text(c, x + 14, y + 14, line, OSD_Colour(0xff, 0x9b, 0x3d), 2, true);
}

static const struct {
	const char *text;
	int style;
} about_credits[] = {
	{ "NX-DOSBox 1.00 - DOSBox for Nintendo Switch", 1 },
	{ "x86 PC Emulator for Nintendo Switch", 2 },
	{ "Ported to Nintendo Switch by TheheroGAC", 3 },
	{ "", 0 },
	{ "Version 1.00 - Standalone libnx Release", 0 },
	{ "Native libnx port with Game Library launcher, virtual keyboard,", 0 },
	{ "automatic disk swapping, save states, and software shaders.", 0 },
	{ "", 0 },
	{ "=====  Original Authors & The DOSBox Team  =====", 1 },
	{ "DOSBox is copyright 2002-2021 The DOSBox Team (GPLv2).", 0 },
	{ "Peter Veenstra (harekiet)", 0 },
	{ "Tommy Froesslund (Qbix)", 0 },
	{ "Ulrich von Zadow (Moe)", 0 },
	{ "c2woody, Moe, Hal9000 and all upstream contributors.", 0 },
	{ "DOSBox-X fork by Jonathan Campbell (joncampbell123)", 0 },
	{ "github.com/joncampbell123/dosbox-x", 4 },
	{ "Official website: www.dosbox.com", 4 },
	{ "", 0 },
	{ "=====  Switch Port & Libraries  =====", 1 },
	{ "The devkitPro & libnx Team - Nintendo Switch toolchain", 0 },
	{ "Menu font: Fira Sans (Mozilla/Telefonica, SIL OFL 1.1)", 0 },
	{ "TheheroGAC - HD modern GUI & Nintendo Switch enhancements", 3 },
	{ "Nintendo Switch community & all beta testers", 0 },
	{ "", 0 },
	{ "=====  Controller Mapping  =====", 1 },
	{ "D-pad: Arrow keys / Navigation", 0 },
	{ "Left Stick Click (L3): Virtual Keyboard", 0 },
	{ "Right Stick: Mouse cursor movement", 0 },
	{ "ZL / ZR: Left Click / Right Click", 0 },
	{ "Minus + Plus: In-Game Pause Menu", 0 },
	{ "A: ENTER / Select       B: ESC / Back", 0 },
	{ "X: SPACE (or About in library)     Y: 'Y' key", 0 },
	{ "L / R: ALT / CTRL       PLUS: SHIFT", 0 },
	{ "", 0 },
	{ "Thank you for playing!", 3 },
};

static void about_draw(struct OSDCanvas *c) {
	OSD_BlendRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0), 190);

	const int w = c->width - 160;
	const int h = c->height - 80;
	const int x = 80;
	const int y = 40;

	OSD_Panel(c, x, y, w, h, OSD_Colour(0x0d, 0x13, 0x26),
	          OSD_Colour(0xff, 0x9b, 0x3d), 250);

	OSD_Text(c, x + 28, y + 20, "NX-DOSBox 1.00 Switch Edition", OSD_Colour(0xff, 0x9b, 0x3d), 3, true);
	OSD_Text(c, x + 28, y + 54, "About & Credits - by TheheroGAC", OSD_Colour(0x80, 0x90, 0xad), 2, false);
	OSD_FillRect(c, x + 16, y + 80, w - 32, 1, OSD_Colour(0x2a, 0x3a, 0x66));

	const int body_pitch = OSD_FONT_HEIGHT + 7;
	const int title_pitch = OSD_FONT_HEIGHT * 2 + 4;
	const int content_y = y + 90;
	const int content_h = h - 134;
	const int count = (int)(sizeof(about_credits) / sizeof(about_credits[0]));

	int total = 0;
	for (int i = 0; i < count; i++)
		total += (about_credits[i].style == 1) ? title_pitch : body_pitch;
	total += body_pitch * 3;

	const Bit32u now = Platform_GetTicks();
	float dt = (float)(now - about_last_tick) / 1000.0f;
	about_last_tick = now;
	if (!(dt > 0.0f)) dt = 0.0f;
	if (dt > 0.1f) dt = 0.1f;
	if (!about_paused) about_offset += about_speed * ABOUT_SPEED_BASE * dt;
	while (about_offset >= (float)total) about_offset -= (float)total;
	while (about_offset < 0.0f) about_offset += (float)total;

	struct OSDCanvas band = *c;
	band.pixels += (size_t)content_y * (size_t)c->pitch + (size_t)(x + 28);
	band.width = w - 56;
	band.height = content_h;

	const int roll = (int)about_offset;
	int start = 0;
	for (int i = 0; i < count; i++) {
		const int pitch = (about_credits[i].style == 1) ? title_pitch : body_pitch;
		int ly = start - roll;
		start += pitch;
		if (ly + pitch <= 0) ly += total;
		if (ly >= band.height) continue;
		const char *text = about_credits[i].text;
		if (!text[0]) continue;
		uint32_t colour = OSD_Colour(0xe8, 0xee, 0xf8);
		int scale = 1;
		bool shadow = false;
		switch (about_credits[i].style) {
		case 1:
			colour = OSD_Colour(0xff, 0x9b, 0x3d);
			scale = 2;
			shadow = true;
			break;
		case 2:
			colour = OSD_Colour(0x9e, 0xc4, 0xff);
			break;
		case 3:
			colour = OSD_Colour(0xff, 0xc0, 0x60);
			shadow = true;
			break;
		case 4:
			colour = OSD_Colour(0x80, 0x90, 0xad);
			break;
		default:
			break;
		}
		OSD_TextCentre(&band, band.width / 2, ly, text, colour, scale, shadow);
	}

	OSD_FillRect(c, x + 16, y + h - 44, w - 32, 1, OSD_Colour(0x2a, 0x3a, 0x66));
	const int fy = y + h - 36;
	int fx = x + 28;
	fx += OSD_DrawHintItem(c, fx, fy, "DPAD", "SPEED") + 24;
	fx += OSD_DrawHintItem(c, fx, fy, "Y", about_paused ? "RESUME" : "PAUSE") + 24;
	OSD_DrawHintItem(c, fx, fy, "B", about_from_pause ? "BACK" : "CLOSE");

	if (about_paused || about_speed < 0.95f || about_speed > 1.05f) {
		char pace[48];
		if (about_paused)
			snprintf(pace, sizeof(pace), "PAUSED  %d%%", (int)(about_speed * 100.0f));
		else
			snprintf(pace, sizeof(pace), "%d%%", (int)(about_speed * 100.0f));
		OSD_TextRight(c, x + w - 28, fy + 6, pace,
		              about_paused ? OSD_Colour(0xff, 0xc0, 0x60) : OSD_Colour(0x80, 0x90, 0xad),
		              1, false);
	}
}

static Bit32u hint_until = 0;
static char hint_extra[160] = "";

static void hint_draw(struct OSDCanvas *c) {
	static const char *const lines[] = {
		"L3  keyboard   Right Stick  mouse   ZL / ZR  clicks",
		"d-pad  arrows   A  ENTER   B  ESC   X  SPACE   Y  'Y'",
		"Minus + Plus  in-game menu   L / R  ALT / CTRL",
	};
	if (hint_extra[0]) {
		OSD_TextCentre(c, c->width / 2, c->height / 2 - 60, hint_extra,
		               OSD_Colour(0xff, 0x9b, 0x3d), 3, true);
	}
	const int h = (int)(sizeof(lines) / sizeof(lines[0])) * 26 + 44;
	const int y = c->height - h - 16;
	OSD_Panel(c, 40, y, c->width - 80, h, OSD_Colour(0x0d, 0x13, 0x26),
	          OSD_Colour(0x3d, 0x5a, 0x9e), 235);
	for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
		OSD_Text(c, 60, y + 18 + (int)i * 26, lines[i],
		         OSD_Colour(0xe8, 0xee, 0xf8), 2, true);
	}
}

static bool hint_active(void) {
	return hint_until != 0 && (int)(hint_until - Platform_GetTicks()) > 0;
}

enum PauseItem {
	PAUSE_RESUME = 0,
	PAUSE_KEYBOARD,
	PAUSE_CONTROLLER_PRESET,
	PAUSE_REMAP_BUTTONS,
	PAUSE_SWAP_DISK,
	PAUSE_SLOT,
	PAUSE_SAVE,
	PAUSE_LOAD,
	PAUSE_VOLUME,
	PAUSE_CYCLES,
	PAUSE_FRAMESKIP,
	PAUSE_SCALER,
	PAUSE_ASPECT,
	PAUSE_SHADER,
	PAUSE_STATS,
	PAUSE_ABOUT,
	PAUSE_HELP,
	PAUSE_RETURN_LIBRARY,
	PAUSE_QUIT_APP,
	PAUSE_ITEM_COUNT,
	PAUSE_CHANNEL
};

static const char *const pause_channels[] = {
	"SB", "FM", "CMS", "GUS", "SPKR", "TANDY", "TANDYDAC", "DISNEY",
	"CDAUDIO", "TSF_MIDI"
};
static const char *const pause_channel_labels[] = {
	"SB volume", "FM volume", "CMS volume", "GUS volume", "PC speaker volume",
	"Tandy volume", "Tandy DAC volume", "Disney volume", "CD audio volume",
	"MIDI volume"
};
#define PAUSE_CHANNEL_COUNT ((int)(sizeof(pause_channels) / sizeof(pause_channels[0])))

struct PauseRow {
	const char *label;
	bool adjustable;
	int item;
	int channel;
};

static const struct PauseRowBase {
	const char *label;
	bool adjustable;
} pause_base_rows[PAUSE_ITEM_COUNT] = {
	{ "Resume",                 false },
	{ "Virtual keyboard",       false },
	{ "Controller profile",     true  },
	{ "Remap buttons...",       false },
	{ "Swap disk",              false },
	{ "State slot",             true  },
	{ "Save state",             false },
	{ "Load state",             false },
	{ "Volume",                 true  },
	{ "CPU speed",              true  },
	{ "Frame skip",             true  },
	{ "Scaler",                 true  },
	{ "Aspect ratio",           true  },
	{ "Shader",                 true  },
	{ "Statistics",             false },
	{ "About & Credits",        false },
	{ "Controls and shortcuts", false },
	{ "Return to game library", false },
	{ "Quit NX-DOSBox",         false },
};

static struct PauseRow pause_rows[PAUSE_ITEM_COUNT + PAUSE_CHANNEL_COUNT];
static int pause_row_count = PAUSE_ITEM_COUNT;

static void pause_layout_build(void) {
	int n = 0;
	for (int i = 0; i < PAUSE_ITEM_COUNT; i++) {
		pause_rows[n].label = pause_base_rows[i].label;
		pause_rows[n].adjustable = pause_base_rows[i].adjustable;
		pause_rows[n].item = i;
		pause_rows[n].channel = -1;
		n++;
		if (i != PAUSE_VOLUME) continue;
		for (int c = 0; c < PAUSE_CHANNEL_COUNT; c++) {
			if (!MIXER_FindChannel(pause_channels[c])) continue;
			pause_rows[n].label = pause_channel_labels[c];
			pause_rows[n].adjustable = true;
			pause_rows[n].item = PAUSE_CHANNEL;
			pause_rows[n].channel = c;
			n++;
		}
	}
	pause_row_count = n;
}

static const Bit32s pause_cycle_values[] = {
	300, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000,
	12000, 20000, 30000, 50000
};
#define PAUSE_CYCLE_COUNT ((int)(sizeof(pause_cycle_values) / sizeof(pause_cycle_values[0])))

static const char *const pause_scalers[] = {
	"none", "normal2x", "normal3x", "tv2x", "tv3x", "scan2x", "scan3x",
	"rgb2x", "rgb3x", "advmame2x", "advmame3x"
};
#define PAUSE_SCALER_COUNT ((int)(sizeof(pause_scalers) / sizeof(pause_scalers[0])))

static int pause_cursor = 0;
static int pause_volume = 100;
static int pause_repeat_dir = 0;
static Bit32u pause_repeat_at = 0;
static OSD_ExitSessionFn pause_exit_fn = NULL;
static OSD_ReturnToLibraryFn return_to_library_fn = NULL;
static bool osd_global_shortcuts = true;
static int pause_slot = SAVESTATE_SLOT_FIRST;
static char pause_status[192] = "";
static Bit32u pause_status_until = 0;

static void pause_set_status(const char *text) {
	snprintf(pause_status, sizeof(pause_status), "%s", text);
	pause_status_until = Platform_GetTicks() + 8000;
}

static bool pause_status_active(void) {
	return pause_status[0] != '\0' &&
	       (Bit32s)(pause_status_until - Platform_GetTicks()) > 0;
}

static void pause_slot_value(char *out, size_t size) {
	const int last = SAVESTATE_SLOT_FIRST + SAVESTATE_SLOT_COUNT - 1;
	char description[96];
	if (SAVESTATE_Describe(pause_slot, description, sizeof(description))) {
		snprintf(out, size, "%d of %d - %s", pause_slot, last, description);
	} else {
		snprintf(out, size, "%d of %d - empty", pause_slot, last);
	}
}

static Section_prop *pause_render_section(void) {
	Section *sec = control ? control->GetSection("render") : NULL;
	return sec ? static_cast<Section_prop *>(sec) : NULL;
}

static void pause_render_setting(const char *name, const char *value) {
	Section *sec = control ? control->GetSection("render") : NULL;
	if (!sec) return;
	char line[64];
	snprintf(line, sizeof(line), "%s=%s", name, value);
	sec->ExecuteDestroy(false);
	sec->HandleInputline(line);
	sec->ExecuteInit(false);
}

static void pause_volume_read(void) {
	int volume = (int)(MIXER_GetMasterVolume() * 100.0f + 0.5f);
	if (volume < 0) volume = 0;
	if (volume > 100) volume = 100;
	pause_volume = (volume / 5) * 5;
}

static MixerChannel *pause_channel_handle(int row) {
	return MIXER_FindChannel(pause_channels[pause_rows[row].channel]);
}

static int pause_channel_percent(int row) {
	MixerChannel *channel = pause_channel_handle(row);
	int percent = channel ? (int)(channel->volmain[0] * 100.0f + 0.5f) : 0;
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	return percent;
}

static void pause_channel_set(int row, int percent) {
	MixerChannel *channel = pause_channel_handle(row);
	if (!channel) return;
	channel->SetVolume((float)percent / 100.0f, (float)percent / 100.0f);
}

static int pause_cycle_index(void) {
	if (CPU_CycleAutoAdjust) return -1;
	int best = 0;
	for (int i = 0; i < PAUSE_CYCLE_COUNT; i++) {
		if (pause_cycle_values[i] == CPU_CycleMax) return i;
		Bit32s diff = pause_cycle_values[i] - CPU_CycleMax;
		Bit32s best_diff = pause_cycle_values[best] - CPU_CycleMax;
		if (diff < 0) diff = -diff;
		if (best_diff < 0) best_diff = -best_diff;
		if (diff < best_diff) best = i;
	}
	return best;
}

static void pause_cycle_apply(int index) {
	if (index < 0) {
		CPU_CycleAutoAdjust = true;
		CPU_CyclePercUsed = 105;
		CPU_CycleLimit = -1;
	} else {
		CPU_CycleAutoAdjust = false;
		CPU_CycleMax = pause_cycle_values[index];
		CPU_CycleLimit = -1;
	}
	CPU_CycleLeft = 0;
	CPU_Cycles = 0;
	Platform_UpdateStatus(CPU_CycleAutoAdjust ? CPU_CyclePercUsed : CPU_CycleMax, -1, false);
}

static int pause_scaler_index(void) {
	Section_prop *sec = pause_render_section();
	if (!sec) return 0;
	Prop_multival *scaler = sec->Get_multival("scaler");
	if (!scaler) return 0;
	std::string type = scaler->GetSection()->Get_string("type");
	for (int i = 0; i < PAUSE_SCALER_COUNT; i++) {
		if (type == pause_scalers[i]) return i;
	}
	return 0;
}

static void pause_value(int row, char *out, size_t size) {
	const int item = pause_rows[row].item;
	out[0] = '\0';
	switch (item) {
	case PAUSE_CONTROLLER_PRESET:
		snprintf(out, size, "%s", PadMapping_GetPresetName(PadMapping_GetPreset()));
		break;
	case PAUSE_REMAP_BUTTONS:
		snprintf(out, size, "press A to edit");
		break;
	case PAUSE_VOLUME:
		snprintf(out, size, "%d%%", pause_volume);
		break;
	case PAUSE_CYCLES: {
		const int index = pause_cycle_index();
		if (index < 0) snprintf(out, size, "auto");
		else snprintf(out, size, "%d", (int)pause_cycle_values[index]);
		break;
	}
	case PAUSE_FRAMESKIP: {
		Section_prop *sec = pause_render_section();
		const int skip = sec ? sec->Get_int("frameskip") : 0;
		if (skip == 0) snprintf(out, size, "off");
		else snprintf(out, size, "draw 1 in %d", skip + 1);
		break;
	}
	case PAUSE_SCALER:
		snprintf(out, size, "%s", pause_scalers[pause_scaler_index()]);
		break;
	case PAUSE_ASPECT: {
		Section_prop *sec = pause_render_section();
		const bool on = sec ? sec->Get_bool("aspect") : false;
		snprintf(out, size, "%s", on ? "4:3" : "square pixels");
		break;
	}
	case PAUSE_SHADER:
		snprintf(out, size, "%s", OSD_ShaderName(OSD_ShaderSelected()));
		break;
	case PAUSE_SLOT:
		pause_slot_value(out, size);
		break;
	case PAUSE_SAVE:
		snprintf(out, size, "slot %d", pause_slot);
		break;
	case PAUSE_LOAD: {
		char description[96];
		if (SAVESTATE_Describe(pause_slot, description, sizeof(description)))
			snprintf(out, size, "slot %d", pause_slot);
		else
			snprintf(out, size, "slot %d is empty", pause_slot);
		break;
	}
	case PAUSE_CHANNEL:
		snprintf(out, size, "%d%%", pause_channel_percent(row));
		break;
	default:
		break;
	}
}

static void pause_adjust(int row, int delta) {
	const int item = pause_rows[row].item;
	switch (item) {
	case PAUSE_CONTROLLER_PRESET: {
		int p = PadMapping_GetPreset() + delta;
		if (p < 0) p = PAD_PRESET_COUNT - 1;
		if (p >= PAD_PRESET_COUNT) p = 0;
		PadMapping_SetPreset(p);
		break;
	}
	case PAUSE_VOLUME:
		pause_volume += delta * 5;
		if (pause_volume < 0) pause_volume = 0;
		if (pause_volume > 100) pause_volume = 100;
		MIXER_SetMasterVolume((float)pause_volume / 100.0f);
		break;
	case PAUSE_CYCLES: {
		int index = pause_cycle_index() + delta;
		if (index < -1) index = -1;
		if (index >= PAUSE_CYCLE_COUNT) index = PAUSE_CYCLE_COUNT - 1;
		pause_cycle_apply(index);
		break;
	}
	case PAUSE_FRAMESKIP: {
		Section_prop *sec = pause_render_section();
		int skip = (sec ? sec->Get_int("frameskip") : 0) + delta;
		if (skip < 0) skip = 0;
		if (skip > 10) skip = 10;
		char value[16];
		snprintf(value, sizeof(value), "%d", skip);
		pause_render_setting("frameskip", value);
		break;
	}
	case PAUSE_SCALER: {
		int index = pause_scaler_index() + delta;
		if (index < 0) index = PAUSE_SCALER_COUNT - 1;
		if (index >= PAUSE_SCALER_COUNT) index = 0;
		pause_render_setting("scaler", pause_scalers[index]);
		break;
	}
	case PAUSE_ASPECT: {
		Section_prop *sec = pause_render_section();
		const bool on = sec ? sec->Get_bool("aspect") : false;
		pause_render_setting("aspect", on ? "false" : "true");
		break;
	}
	case PAUSE_SHADER: {
		int shader = OSD_ShaderSelected() + delta;
		if (shader < 0) shader = OSD_SHADER_COUNT - 1;
		if (shader >= OSD_SHADER_COUNT) shader = 0;
		OSD_ShaderSelect(shader);
		break;
	}
	case PAUSE_SLOT: {
		pause_slot += delta;
		if (pause_slot < SAVESTATE_SLOT_FIRST)
			pause_slot = SAVESTATE_SLOT_FIRST + SAVESTATE_SLOT_COUNT - 1;
		if (pause_slot >= SAVESTATE_SLOT_FIRST + SAVESTATE_SLOT_COUNT)
			pause_slot = SAVESTATE_SLOT_FIRST;
		pause_status[0] = '\0';
		break;
	}
	case PAUSE_CHANNEL: {
		int percent = pause_channel_percent(row) + delta * 5;
		if (percent < 0) percent = 0;
		if (percent > 100) percent = 100;
		pause_channel_set(row, percent);
		break;
	}
	default:
		break;
	}
}

static void pause_activate(int row) {
	const int item = pause_rows[row].item;
	switch (item) {
	case PAUSE_RESUME:
		OSD_CloseOverlays();
		break;
	case PAUSE_KEYBOARD:
		OSD_OpenKeyboard();
		break;
	case PAUSE_REMAP_BUTTONS:
		remap_from_pause = true;
		remap_cursor = 0;
		overlay_mode = OVERLAY_REMAP;
		break;
	case PAUSE_STATS:
		stats_last_toggle = Platform_GetTicks();
		overlay_mode = OVERLAY_STATS;
		break;
	case PAUSE_ABOUT:
		about_from_pause = true;
		about_reset();
		overlay_mode = OVERLAY_ABOUT;
		break;
	case PAUSE_HELP:
		help_from_pause = true;
		overlay_mode = OVERLAY_HELP;
		break;
	case PAUSE_SAVE: {
		std::string error;
		char text[192];
		if (SAVESTATE_Save(pause_slot, &error)) {
			snprintf(text, sizeof(text), "State saved to slot %d.", pause_slot);
		} else {
			snprintf(text, sizeof(text), "Slot %d: %s", pause_slot, error.c_str());
		}
		pause_set_status(text);
		break;
	}
	case PAUSE_LOAD: {
		std::string error;
		char text[192];
		if (SAVESTATE_Load(pause_slot, &error)) {
			char reminder[32];
			snprintf(reminder, sizeof(reminder), "SLOT %d LOADED", pause_slot);
			OSD_CloseOverlays();
			OSD_ShowSessionHint(reminder);
		} else {
			snprintf(text, sizeof(text), "Slot %d: %s", pause_slot, error.c_str());
			pause_set_status(text);
		}
		break;
	}
	case PAUSE_SWAP_DISK: {
		extern void swapInNextDisk(bool pressed);
		swapInNextDisk(true);
		pause_set_status("Disk swapped.");
		OSD_ShowSessionHint("DISK SWAPPED");
		break;
	}
	case PAUSE_RETURN_LIBRARY:
		OSD_CloseOverlays();
		if (return_to_library_fn) return_to_library_fn();
		else if (pause_exit_fn) pause_exit_fn();
		break;
	case PAUSE_QUIT_APP:
		OSD_CloseOverlays();
		if (pause_exit_fn) pause_exit_fn();
		break;
	default:
		if (pause_rows[row].adjustable) pause_adjust(row, 1);
		break;
	}
}

static void remap_draw(struct OSDCanvas *c) {
	OSD_BlendRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0), 200);

	const int w = 840;
	const int h = 530;
	const int x = (c->width - w) / 2;
	const int y = (c->height - h) / 2;
	OSD_Panel(c, x, y, w, h, OSD_Colour(0x0d, 0x13, 0x26), OSD_Colour(0xff, 0x9b, 0x3d), 248);

	OSD_Text(c, x + 32, y + 16, "BUTTON REMAPPING", OSD_Colour(0xff, 0x9b, 0x3d), 3, false);
	OSD_TextRight(c, x + w - 32, y + 22, "Left / Right changes mapping   ·   X reset",
	              OSD_Colour(0x80, 0x90, 0xad), 2, false);

	OSD_FillRect(c, x + 20, y + 54, w - 40, 1, OSD_Colour(0x2a, 0x3a, 0x66));

	const int row_h = 42;
	const int list_y = y + 64;

	{
		const int ry = list_y;
		const bool selected = (remap_cursor == 0);
		if (selected) {
			OSD_GradientV(c, x + 16, ry, w - 32, row_h - 4,
			              OSD_Colour(0x24, 0x40, 0x7a), OSD_Colour(0x1b, 0x2c, 0x55));
			OSD_DrawRect(c, x + 16, ry, w - 32, row_h - 4, OSD_Colour(0xff, 0x9b, 0x3d));
		}
		OSD_Text(c, x + 36, ry + 8, "Controller Profile",
		         selected ? OSD_Colour(0xff, 0xff, 0xff) : OSD_Colour(0xff, 0xc0, 0x60), 2, false);
		char val[96];
		if (selected)
			snprintf(val, sizeof(val), "< %s >", PadMapping_GetPresetName(PadMapping_GetPreset()));
		else
			snprintf(val, sizeof(val), "%s", PadMapping_GetPresetName(PadMapping_GetPreset()));
		OSD_TextRight(c, x + w - 36, ry + 8, val,
		              selected ? OSD_Colour(0xff, 0x9b, 0x3d) : OSD_Colour(0x9e, 0xc4, 0xff), 2, false);
	}

	for (int i = 0; i < PAD_BTN_COUNT; i++) {
		const int row_idx = i + 1;
		const int ry = list_y + row_idx * row_h;
		const bool selected = (remap_cursor == row_idx);
		if (selected) {
			OSD_GradientV(c, x + 16, ry, w - 32, row_h - 4,
			              OSD_Colour(0x24, 0x40, 0x7a), OSD_Colour(0x1b, 0x2c, 0x55));
			OSD_DrawRect(c, x + 16, ry, w - 32, row_h - 4, OSD_Colour(0xff, 0x9b, 0x3d));
		}

		const char *btn_name = PadMapping_GetButtonName(i);
		OSD_Text(c, x + 36, ry + 8, btn_name,
		         selected ? OSD_Colour(0xff, 0xff, 0xff) : OSD_Colour(0xe8, 0xee, 0xf8), 2, false);

		int act_id = PadMapping_GetButtonAction(i);
		const PadActionDef *act = PadMapping_GetAction(act_id);
		char val[96];
		if (selected)
			snprintf(val, sizeof(val), "< %s >", act->label);
		else
			snprintf(val, sizeof(val), "%s", act->label);
		OSD_TextRight(c, x + w - 36, ry + 8, val,
		              selected ? OSD_Colour(0xff, 0x9b, 0x3d) : OSD_Colour(0x80, 0x90, 0xad), 2, false);
	}

	const int fy = y + h - 42;
	OSD_FillRect(c, x + 20, fy - 8, w - 40, 1, OSD_Colour(0x2a, 0x3a, 0x66));
	int fx = x + 36;
	fx += OSD_DrawHintItem(c, fx, fy, "DPAD", "SELECT & CHANGE") + 24;
	fx += OSD_DrawHintItem(c, fx, fy, "X", "DEFAULT") + 24;
	OSD_DrawHintItem(c, fx, fy, "B", "SAVE & CLOSE");
}

#define PAUSE_VISIBLE_ROWS 11
static int pause_scroll = 0;

static void pause_scroll_into_view(void) {
	if (pause_cursor < pause_scroll) pause_scroll = pause_cursor;
	if (pause_cursor >= pause_scroll + PAUSE_VISIBLE_ROWS)
		pause_scroll = pause_cursor - PAUSE_VISIBLE_ROWS + 1;
	if (pause_scroll > pause_row_count - PAUSE_VISIBLE_ROWS)
		pause_scroll = pause_row_count - PAUSE_VISIBLE_ROWS;
	if (pause_scroll < 0) pause_scroll = 0;
}

static void pause_draw(struct OSDCanvas *c) {
	OSD_BlendRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0), 176);

	const int row_h = OSD_FONT_HEIGHT * 2 + 16;
	const int header_h = 50;
	const int footer_h = 58;
	const int w = c->width - 120;
	const int h = header_h + PAUSE_VISIBLE_ROWS * row_h + footer_h;
	const int x = (c->width - w) / 2;
	const int y = (c->height - h) / 2;
	pause_scroll_into_view();
	OSD_Panel(c, x, y, w, h, OSD_Colour(0x0d, 0x13, 0x26),
	          OSD_Colour(0xff, 0x9b, 0x3d), 248);

	OSD_Text(c, x + 28, y + 14, "PAUSED", OSD_Colour(0xff, 0x9b, 0x3d), 3, false);
	OSD_TextRight(c, x + w - 28, y + 18, "left/right changes a value",
	              OSD_Colour(0x80, 0x90, 0xad), 2, false);

	if (return_to_library_fn) {
		const char *label = "ZL  GAME LIBRARY";
		const int lw = OSD_TextWidth(label, 1) + 18;
		const int lx = x + w - OSD_TextWidth("left/right changes a value", 2) - 44 - lw;
		OSD_Panel(c, lx, y + 12, lw, 24, OSD_Colour(0x3a, 0x1c, 0x22),
		          OSD_Colour(0xff, 0x8a, 0x8a), 255);
		OSD_Text(c, lx + 9, y + 18, label, OSD_Colour(0xff, 0x8a, 0x8a), 1, false);
	}

	char value[192];
	char shown[200];
	for (int row = 0; row < PAUSE_VISIBLE_ROWS; row++) {
		const int i = pause_scroll + row;
		if (i >= pause_row_count) break;
		const int ry = y + header_h + row * row_h;
		const bool selected = (i == pause_cursor);
		if (selected) {
			OSD_GradientV(c, x + 12, ry, w - 24, row_h - 4,
			              OSD_Colour(0x24, 0x40, 0x7a), OSD_Colour(0x1b, 0x2c, 0x55));
			OSD_DrawRect(c, x + 12, ry, w - 24, row_h - 4, OSD_Colour(0xff, 0x9b, 0x3d));
		}
		const int item = pause_rows[i].item;
		const uint32_t label_colour = (item == PAUSE_RETURN_LIBRARY || item == PAUSE_QUIT_APP)
			? OSD_Colour(0xff, 0x8a, 0x8a)
			: (item == PAUSE_SWAP_DISK || item == PAUSE_CHANNEL) ? OSD_Colour(0x9e, 0xc4, 0xff)
			: (item == PAUSE_CONTROLLER_PRESET || item == PAUSE_REMAP_BUTTONS) ? OSD_Colour(0xff, 0xc8, 0x7a)
			: OSD_Colour(0xe8, 0xee, 0xf8);
		OSD_Text(c, x + 40, ry + 4, pause_rows[i].label, label_colour, 2, false);

		pause_value(i, value, sizeof(value));
		if (!value[0]) continue;
		if (selected && pause_rows[i].adjustable)
			snprintf(shown, sizeof(shown), "< %s >", value);
		else
			snprintf(shown, sizeof(shown), "%s", value);
		const uint32_t value_colour = pause_rows[i].adjustable
			? OSD_Colour(0xff, 0x9b, 0x3d) : OSD_Colour(0x80, 0x90, 0xad);
		OSD_TextRight(c, x + w - 40, ry + 4, shown, value_colour, 2, false);
	}

	{
		const int track_y = y + header_h + 2;
		const int track_h = PAUSE_VISIBLE_ROWS * row_h - 4;
		int thumb_h = track_h * PAUSE_VISIBLE_ROWS / pause_row_count;
		if (thumb_h < 20) thumb_h = 20;
		const int thumb_y = track_y + (track_h - thumb_h) * pause_scroll /
		                    (pause_row_count - PAUSE_VISIBLE_ROWS);
		OSD_BlendRect(c, x + w - 10, track_y, 4, track_h, OSD_Colour(0x2a, 0x3a, 0x66), 170);
		OSD_FillRect(c, x + w - 10, thumb_y, 4, thumb_h, OSD_Colour(0x3d, 0x5a, 0x9e));
	}

	const int fy = y + header_h + PAUSE_VISIBLE_ROWS * row_h + 8;
	OSD_Text(c, x + 40, fy, "A select    B or X resume    L3 virtual keyboard",
	         OSD_Colour(0xe8, 0xee, 0xf8), 2, false);
	if (pause_status_active()) {
		OSD_Text(c, x + 40, fy + 22, pause_status, OSD_Colour(0xff, 0x9b, 0x3d), 2, false);
	} else {
		char line[192];
		const char *game = SAVESTATE_GameName();
		if (game && game[0])
			snprintf(line, sizeof(line), "%s: %s", game, SAVESTATE_Directory());
		else
			snprintf(line, sizeof(line), "States: %s", SAVESTATE_Directory());
		OSD_Text(c, x + 40, fy + 22, line, OSD_Colour(0x80, 0x90, 0xad), 2, false);
	}
}

void OSD_OpenKeyboard(void) {
	overlay_mode = OVERLAY_KEYBOARD;
	keyboard_cursor_row = 0;
	keyboard_cursor_col = 0;
	keyboard_typed_reset();
}

void OSD_OpenPauseMenu(void) {
	keyboard_release_held();
	pause_cursor = 0;
	pause_repeat_dir = 0;
	pause_status[0] = '\0';
	pause_layout_build();
	pause_volume_read();
	overlay_mode = OVERLAY_PAUSE;
}

bool OSD_PauseMenuOpen(void) {
	return overlay_mode == OVERLAY_PAUSE ||
	       overlay_mode == OVERLAY_REMAP ||
	       (about_from_pause && overlay_mode == OVERLAY_ABOUT) ||
	       (help_from_pause && overlay_mode == OVERLAY_HELP);
}

void OSD_SetReturnToLibraryFn(OSD_ReturnToLibraryFn fn) {
	return_to_library_fn = fn;
}

void OSD_SetGlobalShortcuts(bool enabled) {
	osd_global_shortcuts = enabled;
}

void OSD_SetHardwareModifier(int modifier, bool held) {
	if (held) keyboard_modifiers |=  modifier;
	else       keyboard_modifiers &= ~modifier;
}

void OSD_SetExitSessionFn(OSD_ExitSessionFn fn) {
	pause_exit_fn = fn;
}

void OSD_ShowSessionHint(const char *extra) {
	snprintf(hint_extra, sizeof(hint_extra), "%s", extra ? extra : "");
	hint_until = Platform_GetTicks() + 3500;
}

void OSD_ToggleHelp(void) {
	if (overlay_mode == OVERLAY_HELP) OSD_CloseOverlays();
	else {
		help_from_pause = false;
		overlay_mode = OVERLAY_HELP;
	}
}

void OSD_ToggleAbout(void) {
	if (overlay_mode == OVERLAY_ABOUT) OSD_CloseOverlays();
	else {
		about_from_pause = false;
		about_reset();
		overlay_mode = OVERLAY_ABOUT;
	}
}

void OSD_OpenAbout(void) {
	about_from_pause = false;
	about_reset();
	overlay_mode = OVERLAY_ABOUT;
}

void OSD_CloseOverlays(void) {
	keyboard_release_held();
	keyboard_modifiers = 0;
	pause_repeat_dir = 0;
	about_from_pause = false;
	help_from_pause = false;
	remap_from_pause = false;
	overlay_mode = OVERLAY_OFF;
	
	KEYBOARD_ClrBuffer();
	extern void MAPPER_LosingFocus(void);
	MAPPER_LosingFocus();
}

bool OSD_OverlaysActive(void) {
	return overlay_mode != OVERLAY_OFF;
}

bool OSD_WantsRedraw(void) {
	return overlay_mode != OVERLAY_OFF || hint_active();
}

void OSD_UpdateOverlays(const struct OSDPad *pad) {
	if (!pad) return;

	static bool prev_y = false, prev_r = false, prev_a = false;
	static bool prev_up = false, prev_down = false, prev_left = false, prev_right = false;
	static bool prev_stick = false;
	static bool prev_b = false, prev_x = false;

	bool y_down = pad->y && !prev_y;
	bool a_down = pad->a && !prev_a;
	const bool up_level = pad->up || pad->ly < -0.5f;
	const bool down_level = pad->down || pad->ly > 0.5f;
	bool up_down = up_level && !prev_up;
	bool down_down = down_level && !prev_down;
	bool left_down = pad->left && !prev_left;
	bool right_down = pad->right && !prev_right;
	bool stick_down = pad->stick_l && !prev_stick;
	bool r_down = pad->r && !prev_r;
	bool b_down = pad->b && !prev_b;
	bool x_down = pad->x && !prev_x;
	prev_y = pad->y; prev_a = pad->a; prev_r = pad->r;
	prev_b = pad->b; prev_x = pad->x;
	prev_up = up_level; prev_down = down_level;
	prev_left = pad->left; prev_right = pad->right; prev_stick = pad->stick_l;

	if (osd_global_shortcuts) {
		if (stick_down) {
			overlay_mode = (overlay_mode == OVERLAY_KEYBOARD) ? OVERLAY_OFF : OVERLAY_KEYBOARD;
			if (overlay_mode == OVERLAY_OFF) keyboard_release_held();
		}
		if (r_down && overlay_mode == OVERLAY_OFF) {
			stats_last_toggle = Platform_GetTicks();
			overlay_mode = OVERLAY_STATS;
		}

		static bool prev_combo = false;
		const bool combo = pad->minus && pad->plus;
		if (combo && !prev_combo) {
			if (overlay_mode == OVERLAY_PAUSE || overlay_mode == OVERLAY_REMAP) OSD_CloseOverlays();
			else OSD_OpenPauseMenu();
		}
		prev_combo = combo;
	}

	if (pad->zl && overlay_mode == OVERLAY_PAUSE && return_to_library_fn) {
		OSD_CloseOverlays();
		return_to_library_fn();
	}

	if (overlay_mode == OVERLAY_KEYBOARD) {
		const int rows = keyboard_rows_count();
		int col_max = keyboard_row_len(keyboard_cursor_row) - 1;

		if (up_down && keyboard_cursor_row > 0) {
			keyboard_release_held();
			keyboard_cursor_row--;
			col_max = keyboard_row_len(keyboard_cursor_row) - 1;
		}
		if (down_down && keyboard_cursor_row < rows - 1) {
			keyboard_release_held();
			keyboard_cursor_row++;
			col_max = keyboard_row_len(keyboard_cursor_row) - 1;
		}
		if (left_down && keyboard_cursor_col > 0) {
			keyboard_release_held();
			keyboard_cursor_col--;
		}
		if (right_down && keyboard_cursor_col < col_max) {
			keyboard_release_held();
			keyboard_cursor_col++;
		}
		if (keyboard_cursor_col > col_max) keyboard_cursor_col = col_max;

		if (a_down) {
			const struct VirtualKey *keys = keyboard_row(keyboard_cursor_row);
			if (keys && keyboard_cursor_col < keyboard_row_len(keyboard_cursor_row)) {
				keyboard_activate(&keys[keyboard_cursor_col]);
			}
		}
		if (x_down) {
			keyboard_release_held();
			keyboard_function_layer = !keyboard_function_layer;
			keyboard_cursor_row = 0;
			keyboard_cursor_col = 0;
		}
		if (b_down) OSD_CloseOverlays();
	} else if (overlay_mode == OVERLAY_PAUSE) {
		if (up_down) {
			pause_cursor = (pause_cursor + pause_row_count - 1) % pause_row_count;
			pause_repeat_dir = 0;
		}
		if (down_down) {
			pause_cursor = (pause_cursor + 1) % pause_row_count;
			pause_repeat_dir = 0;
		}

		if (left_down || right_down) {
			const int dir = left_down ? -1 : 1;
			if (pause_rows[pause_cursor].adjustable) {
				pause_adjust(pause_cursor, dir);
				pause_repeat_dir = dir;
				pause_repeat_at = Platform_GetTicks() + 400;
			} else {
				pause_repeat_dir = 0;
			}
		} else if (pause_repeat_dir != 0) {
			const bool held = (pause_repeat_dir < 0) ? pad->left : pad->right;
			if (!held || !pause_rows[pause_cursor].adjustable) {
				pause_repeat_dir = 0;
			} else if ((Bit32s)(Platform_GetTicks() - pause_repeat_at) >= 0) {
				pause_adjust(pause_cursor, pause_repeat_dir);
				pause_repeat_at = Platform_GetTicks() + 90;
			}
		}

		if (pad->zl && return_to_library_fn) {
			OSD_CloseOverlays();
			return_to_library_fn();
			return;
		}
		if (a_down) pause_activate(pause_cursor);
		if (b_down || x_down) OSD_CloseOverlays();
	} else if (overlay_mode == OVERLAY_ABOUT) {
		if (b_down || x_down || a_down) {
			if (about_from_pause) {
				overlay_mode = OVERLAY_PAUSE;
				about_from_pause = false;
			} else {
				OSD_CloseOverlays();
			}
		}
		if (up_down) {
			about_speed -= 0.5f;
			about_speed_dir = -1;
			about_speed_repeat_at = Platform_GetTicks() + 400;
		}
		if (down_down) {
			about_speed += 0.5f;
			about_speed_dir = 1;
			about_speed_repeat_at = Platform_GetTicks() + 400;
		}
		if (!up_level && !down_level) {
			about_speed_dir = 0;
			if (about_speed > 1.0f) {
				about_speed -= 0.25f;
				if (about_speed < 1.0f) about_speed = 1.0f;
			} else if (about_speed < 1.0f) {
				about_speed += 0.25f;
				if (about_speed > 1.0f) about_speed = 1.0f;
			}
		} else if (about_speed_dir != 0 &&
		           (Bit32s)(Platform_GetTicks() - about_speed_repeat_at) >= 0) {
			about_speed += about_speed_dir * 0.5f;
			about_speed_repeat_at = Platform_GetTicks() + 120;
		}
		if (about_speed < ABOUT_SPEED_MIN) about_speed = ABOUT_SPEED_MIN;
		if (about_speed > ABOUT_SPEED_MAX) about_speed = ABOUT_SPEED_MAX;
		if (y_down) about_paused = !about_paused;
	} else if (overlay_mode == OVERLAY_HELP) {
		if (b_down || x_down) {
			if (help_from_pause) {
				overlay_mode = OVERLAY_PAUSE;
				help_from_pause = false;
			} else {
				OSD_CloseOverlays();
			}
		}
	} else if (overlay_mode == OVERLAY_STATS) {
		if (b_down || x_down) OSD_CloseOverlays();
	} else if (overlay_mode == OVERLAY_REMAP) {
		const int total_rows = PAD_BTN_COUNT + 1;
		if (up_down) {
			remap_cursor = (remap_cursor + total_rows - 1) % total_rows;
		}
		if (down_down) {
			remap_cursor = (remap_cursor + 1) % total_rows;
		}
		if (left_down || right_down) {
			const int dir = left_down ? -1 : 1;
			if (remap_cursor == 0) {
				int preset = (PadMapping_GetPreset() + PAD_PRESET_COUNT + dir) % PAD_PRESET_COUNT;
				PadMapping_SetPreset(preset);
			} else {
				int btn = remap_cursor - 1;
				int act = (PadMapping_GetButtonAction(btn) + ACT_COUNT + dir) % ACT_COUNT;
				PadMapping_SetButtonAction(btn, act);
			}
		}
		if (a_down) {
			if (remap_cursor == 0) {
				int preset = (PadMapping_GetPreset() + 1) % PAD_PRESET_COUNT;
				PadMapping_SetPreset(preset);
			} else {
				int btn = remap_cursor - 1;
				int act = (PadMapping_GetButtonAction(btn) + 1) % ACT_COUNT;
				PadMapping_SetButtonAction(btn, act);
			}
		}
		if (x_down) {
			PadMapping_ResetDefaults();
		}
		if (b_down) {
			PadMapping_Save();
			if (remap_from_pause) {
				overlay_mode = OVERLAY_PAUSE;
				remap_from_pause = false;
			} else {
				OSD_CloseOverlays();
			}
		}
	}
}

void OSD_DrawOverlaysInto(struct OSDCanvas *canvas) {
	if (!canvas || !canvas->pixels) return;

	switch (overlay_mode) {
	case OVERLAY_KEYBOARD: keyboard_draw(canvas); break;
	case OVERLAY_PAUSE:    pause_draw(canvas);    break;
	case OVERLAY_HELP:     help_draw(canvas);     break;
	case OVERLAY_ABOUT:    about_draw(canvas);    break;
	case OVERLAY_STATS:    stats_draw(canvas);    break;
	case OVERLAY_REMAP:    remap_draw(canvas);    break;
	default:
		if (overlay_mode == OVERLAY_OFF && hint_active()) hint_draw(canvas);
		break;
	}
}
