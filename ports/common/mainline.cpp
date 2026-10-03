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
#include "control.h"
#include "mapper.h"
#include "setup.h"
#include "mainline.h"

#define MAPPERFILE "mapper-" VERSION ".map"

void Config_ApplyConsoleDefaults(void) {
	Section *render = control->GetSection("render");
	if (render) {
		Section_prop *render_prop = static_cast<Section_prop *>(render);
		if (!render_prop->Get_bool("aspect"))
			render_prop->HandleInputline("aspect=true");
	}
}

void Config_Add_Target(void) {
	Section_prop *host_sec = control->AddSection_prop("sdl", &MAPPER_StartUp);

	Prop_bool *Pbool;
	Prop_string *Pstring;
	Prop_multival *Pmulti;
	Prop_int *Pint;

	Pbool = host_sec->Add_bool("fullscreen", Property::Changeable::Always, true);
	Pbool->Set_help("Ignored on a console: the output is always the native display.");

	Pbool = host_sec->Add_bool("fulldouble", Property::Changeable::Always, true);
	Pbool->Set_help("Ignored on a console.");

	Pstring = host_sec->Add_string("fullresolution", Property::Changeable::Always, "desktop");
	Pstring->Set_help("Ignored on a console: the output is always the native display.");

	Pstring = host_sec->Add_string("windowresolution", Property::Changeable::Always, "desktop");
	Pstring->Set_help("Ignored on a console: the output is always the native display.");

	const char *outputs[] = { "surface", 0 };
	Pstring = host_sec->Add_string("output", Property::Changeable::Always, "surface");
	Pstring->Set_help("What video system to use for output.");
	Pstring->Set_values(outputs);

	Pbool = host_sec->Add_bool("autolock", Property::Changeable::Always, false);
	Pbool->Set_help("Mouse will automatically lock, if you click on the screen.");

	Pmulti = host_sec->Add_multi("sensitivity", Property::Changeable::Always, ",");
	Pmulti->Set_help("Mouse sensitivity. The optional second parameter specifies vertical sensitivity (e.g. 100,-50).");
	Pmulti->SetValue("100");
	Pint = Pmulti->GetSection()->Add_int("xsens", Property::Changeable::Always, 100);
	Pint->SetMinMax(-1000, 1000);
	Pint = Pmulti->GetSection()->Add_int("ysens", Property::Changeable::Always, 100);
	Pint->SetMinMax(-1000, 1000);

	Pbool = host_sec->Add_bool("waitonerror", Property::Changeable::Always, false);
	Pbool->Set_help("Wait before exiting if dosbox has an error.");

	Pmulti = host_sec->Add_multi("priority", Property::Changeable::Always, ",");
	Pmulti->SetValue("higher,normal");
	Pmulti->Set_help("Priority levels for dosbox. Ignored on a console.");
	const char *actt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0 };
	Pstring = Pmulti->GetSection()->Add_string("active", Property::Changeable::Always, "higher");
	Pstring->Set_values(actt);
	const char *inactt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0 };
	Pstring = Pmulti->GetSection()->Add_string("inactive", Property::Changeable::Always, "normal");
	Pstring->Set_values(inactt);

	Pstring = host_sec->Add_path("mapperfile", Property::Changeable::Always, MAPPERFILE);
	Pstring->Set_help("File used to load/save the key/event mappings from.");

	Pbool = host_sec->Add_bool("usescancodes", Property::Changeable::Always, false);
	Pbool->Set_help("Avoid usage of symkeys. A console only reports virtual keys.");
}

