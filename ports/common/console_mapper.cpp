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

#include <stddef.h>

#include "dosbox.h"
#include "keyboard.h"
#include "mapper.h"

#define CONSOLE_MAPPER_MAX_HANDLERS 64

struct ConsoleMapperBind {
	MapKeys key;
	Bitu mods;
	MAPPER_Handler *handler;
	bool used;
};

static ConsoleMapperBind console_mapper_binds[CONSOLE_MAPPER_MAX_HANDLERS];

void MAPPER_AddHandler(MAPPER_Handler * handler, MapKeys key, Bitu mods,
                       char const * const eventname, char const * const buttonname) {
	(void)eventname;
	(void)buttonname;
	if (!handler) return;
	for (unsigned i = 0; i < CONSOLE_MAPPER_MAX_HANDLERS; i++) {
		if (console_mapper_binds[i].used) continue;
		console_mapper_binds[i].used    = true;
		console_mapper_binds[i].key     = key;
		console_mapper_binds[i].mods    = mods;
		console_mapper_binds[i].handler = handler;
		return;
	}
}

void MAPPER_Init(void) {
}

void MAPPER_StartUp(Section * sec) {
	(void)sec;
}

void MAPPER_LosingFocus(void) {
}

void MAPPER_Run(bool pressed) {
	(void)pressed;
}

void MAPPER_RunInternal() {
}

void ConsoleMapper_ResetBinds(void) {
	for (unsigned i = 0; i < CONSOLE_MAPPER_MAX_HANDLERS; i++)
		console_mapper_binds[i].used = false;
}

void ConsoleMapper_KeyChanged(KBD_KEYS key, bool pressed, bool ctrl, bool alt) {
	Bitu mods = 0;
	if (ctrl) mods |= MMOD1;
	if (alt) mods |= MMOD2;

	if (!pressed) return;

	MapKeys mapped;
	switch (key) {
	case KBD_f1:  mapped = MK_f1;  break;
	case KBD_f2:  mapped = MK_f2;  break;
	case KBD_f3:  mapped = MK_f3;  break;
	case KBD_f4:  mapped = MK_f4;  break;
	case KBD_f5:  mapped = MK_f5;  break;
	case KBD_f6:  mapped = MK_f6;  break;
	case KBD_f7:  mapped = MK_f7;  break;
	case KBD_f8:  mapped = MK_f8;  break;
	case KBD_f9:  mapped = MK_f9;  break;
	case KBD_f10: mapped = MK_f10; break;
	case KBD_f11: mapped = MK_f11; break;
	case KBD_f12: mapped = MK_f12; break;
	case KBD_enter:       mapped = MK_return;     break;
	case KBD_kpminus:     mapped = MK_kpminus;    break;
	case KBD_scrolllock:  mapped = MK_scrolllock; break;
	case KBD_printscreen: mapped = MK_printscreen;break;
	case KBD_pause:       mapped = MK_pause;      break;
	case KBD_home:        mapped = MK_home;       break;
	default: return;
	}

	for (unsigned i = 0; i < CONSOLE_MAPPER_MAX_HANDLERS; i++) {
		const ConsoleMapperBind *bind = &console_mapper_binds[i];
		if (!bind->used) continue;
		if (bind->key != mapped) continue;
		if (bind->mods != mods) continue;
		bind->handler(true);
		bind->handler(false);
		return;
	}
}
