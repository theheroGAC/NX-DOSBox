#include "pad_mapping.h"
#include <stdio.h>
#include <string.h>

static const PadActionDef s_actions[ACT_COUNT] = {
{ "Enter + Z (Action/Fire)",    KBD_enter,     KBD_z,         0, -1 },
{ "Z + Ctrl (Gun/Fire)",        KBD_z,         KBD_leftctrl,  0, -1 },
{ "Space + X (Hydra/Space)",    KBD_space,     KBD_x,         1, -1 },
{ "Alt + C (Hellfire/Alt)",     KBD_leftalt,   KBD_c,         1, -1 },
{ "Ctrl (Fire / Jink)",         KBD_leftctrl,  KBD_NONE,      0, -1 },
{ "Alt (Jump / Strafe)",        KBD_leftalt,   KBD_NONE,     -1, -1 },
{ "Space (Open / Jump)",        KBD_space,     KBD_NONE,      0, -1 },
{ "Enter (Confirm / Select)",   KBD_enter,     KBD_NONE,      0, -1 },
{ "Esc (Back / Map)",           KBD_esc,       KBD_NONE,     -1, -1 },
{ "Shift (Run / Walk)",         KBD_leftshift, KBD_NONE,     -1, -1 },
{ "Tab",                        KBD_tab,       KBD_NONE,     -1, -1 },
{ "Backspace",                  KBD_backspace, KBD_NONE,     -1, -1 },
{ "Z (Gun / Weapon 1)",         KBD_z,         KBD_NONE,      0, -1 },
{ "X (Hydra / Weapon 2)",       KBD_x,         KBD_NONE,      1, -1 },
{ "C (Hellfire / Weapon 3)",    KBD_c,         KBD_NONE,     -1, -1 },
{ "Y (Yes)",                    KBD_y,         KBD_NONE,     -1, -1 },
{ "N (No)",                     KBD_n,         KBD_NONE,     -1, -1 },
{ "1 (Weapon 1)",               KBD_1,         KBD_NONE,     -1, -1 },
{ "2 (Weapon 2)",               KBD_2,         KBD_NONE,     -1, -1 },
{ "3 (Weapon 3)",               KBD_3,         KBD_NONE,     -1, -1 },
{ "4 (Weapon 4)",               KBD_4,         KBD_NONE,     -1, -1 },
{ "Mouse Left Click",           KBD_NONE,      KBD_NONE,     -1,  0 },
{ "Mouse Right Click",          KBD_NONE,      KBD_NONE,     -1,  1 },
{ "None (Disabled)",            KBD_NONE,      KBD_NONE,     -1, -1 },
};

static const int s_presets[PAD_PRESET_COUNT][PAD_BTN_COUNT] = {
	{ ACT_ENTER_Z, ACT_ESC, ACT_SPACE_X, ACT_Z_CTRL, ACT_ALT_C, ACT_CTRL, ACT_MOUSE_L, ACT_MOUSE_R },
	{ ACT_SPACE, ACT_ESC, ACT_SHIFT, ACT_CTRL, ACT_ALT, ACT_CTRL, ACT_MOUSE_L, ACT_MOUSE_R },
	{ ACT_ALT, ACT_CTRL, ACT_SPACE, ACT_CTRL, ACT_ESC, ACT_ENTER, ACT_SHIFT, ACT_SPACE },
	{ ACT_ENTER, ACT_ESC, ACT_SPACE, ACT_TAB, ACT_SHIFT, ACT_CTRL, ACT_MOUSE_L, ACT_MOUSE_R },
	{ ACT_ENTER_Z, ACT_ESC, ACT_SPACE_X, ACT_Z_CTRL, ACT_ALT_C, ACT_CTRL, ACT_MOUSE_L, ACT_MOUSE_R },
};

static const char *s_preset_names[PAD_PRESET_COUNT] = {
	"Default",
	"Doom & FPS",
	"Classic Platformer",
	"Adventure / RPG",
	"Custom"
};

static const char *s_button_names[PAD_BTN_COUNT] = {
	"Button A",
	"Button B",
	"Button X",
	"Button Y",
	"Button L",
	"Button R",
	"Button ZL",
	"Button ZR"
};

static int s_current_preset = PAD_PRESET_DEFAULT;
static int s_active_mapping[PAD_BTN_COUNT];
static bool s_inited = false;

static const char *CONFIG_PATH = "sdmc:/dosbox/controls.cfg";

void PadMapping_Init(void) {
	if (s_inited) return;
	s_inited = true;
	for (int i = 0; i < PAD_BTN_COUNT; i++) {
		s_active_mapping[i] = s_presets[PAD_PRESET_DEFAULT][i];
	}
	PadMapping_Load();
}

int PadMapping_GetPreset(void) {
	if (!s_inited) PadMapping_Init();
	return s_current_preset;
}

void PadMapping_SetPreset(int preset) {
	if (!s_inited) PadMapping_Init();
	if (preset < 0) preset = PAD_PRESET_COUNT - 1;
	if (preset >= PAD_PRESET_COUNT) preset = 0;
	s_current_preset = preset;
	if (preset != PAD_PRESET_CUSTOM) {
		for (int i = 0; i < PAD_BTN_COUNT; i++) {
			s_active_mapping[i] = s_presets[preset][i];
		}
	}
	PadMapping_Save();
}

const char *PadMapping_GetPresetName(int preset) {
	if (preset < 0 || preset >= PAD_PRESET_COUNT) return "Unknown";
	return s_preset_names[preset];
}

int PadMapping_GetButtonAction(int btn_id) {
	if (!s_inited) PadMapping_Init();
	if (btn_id < 0 || btn_id >= PAD_BTN_COUNT) return ACT_NONE;
	return s_active_mapping[btn_id];
}

void PadMapping_SetButtonAction(int btn_id, int action_id) {
	if (!s_inited) PadMapping_Init();
	if (btn_id < 0 || btn_id >= PAD_BTN_COUNT) return;
	if (action_id < 0) action_id = ACT_COUNT - 1;
	if (action_id >= ACT_COUNT) action_id = 0;
	s_active_mapping[btn_id] = action_id;
	s_current_preset = PAD_PRESET_CUSTOM;
	PadMapping_Save();
}

const char *PadMapping_GetButtonName(int btn_id) {
	if (btn_id < 0 || btn_id >= PAD_BTN_COUNT) return "";
	return s_button_names[btn_id];
}

int PadMapping_GetActionCount(void) {
	return ACT_COUNT;
}

const PadActionDef *PadMapping_GetAction(int action_id) {
	if (action_id < 0 || action_id >= ACT_COUNT) return &s_actions[ACT_NONE];
	return &s_actions[action_id];
}

void PadMapping_Save(void) {
	FILE *f = fopen(CONFIG_PATH, "w");
	if (!f) return;
	fprintf(f, "# DOSBox Nintendo Switch Controller Mapping\n");
	fprintf(f, "preset=%d\n", s_current_preset);
	for (int i = 0; i < PAD_BTN_COUNT; i++) {
		fprintf(f, "btn%d=%d\n", i, s_active_mapping[i]);
	}
	fclose(f);
}

void PadMapping_Load(void) {
	FILE *f = fopen(CONFIG_PATH, "r");
	if (!f) return;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
		int val = 0;
		if (sscanf(line, "preset=%d", &val) == 1) {
			if (val >= 0 && val < PAD_PRESET_COUNT) s_current_preset = val;
		}
		for (int i = 0; i < PAD_BTN_COUNT; i++) {
			char key[32];
			snprintf(key, sizeof(key), "btn%d=%%d", i);
			if (sscanf(line, key, &val) == 1) {
				if (val >= 0 && val < ACT_COUNT) s_active_mapping[i] = val;
			}
		}
	}
	fclose(f);
}

void PadMapping_ResetDefaults(void) {
	PadMapping_SetPreset(PAD_PRESET_DEFAULT);
}

