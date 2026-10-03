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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "dosbox.h"
#include "mouse.h"
#include "platform.h"

bool autofire = false;

void GFX_ShowMsg(char const *format, ...) {
	va_list msg;
	va_start(msg, format);
	Platform_ShowMessageV(format, msg);
	va_end(msg);
}

void Mouse_AutoLock(bool enable) {
	(void)enable;
}

void restart_program(std::vector<std::string> &parameters) {
	std::string joined;
	for (size_t i = 0; i < parameters.size(); i++) {
		if (i) joined += " ";
		joined += parameters[i];
	}
	LOG_MSG("RESTART is not available on a console, exiting. Arguments were: %s",
	         joined.c_str());
	E_Exit("Restart requested");
}
