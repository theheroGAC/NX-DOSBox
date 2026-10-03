#ifndef DOSBOX_PAD_MAPPING_H
#define DOSBOX_PAD_MAPPING_H

#include "dosbox.h"
#include "keyboard.h"

enum PadButtonId {
	PAD_BTN_A = 0,
	PAD_BTN_B,
	PAD_BTN_X,
	PAD_BTN_Y,
	PAD_BTN_L,
	PAD_BTN_R,
	PAD_BTN_ZL,
	PAD_BTN_ZR,
	PAD_BTN_COUNT
};

enum PadPresetId {
	PAD_PRESET_DEFAULT = 0,
	PAD_PRESET_DOOM,
	PAD_PRESET_PLATFORMER,
	PAD_PRESET_ADVENTURE,
	PAD_PRESET_CUSTOM,
	PAD_PRESET_COUNT
};

enum PadActionId {
	ACT_ENTER_Z = 0,
	ACT_Z_CTRL,
	ACT_SPACE_X,
	ACT_ALT_C,
	ACT_CTRL,
	ACT_ALT,
	ACT_SPACE,
	ACT_ENTER,
	ACT_ESC,
	ACT_SHIFT,
	ACT_TAB,
	ACT_BACKSPACE,
	ACT_Z,
	ACT_X,
	ACT_C,
	ACT_Y,
	ACT_N,
	ACT_NUM_1,
	ACT_NUM_2,
	ACT_NUM_3,
	ACT_NUM_4,
	ACT_MOUSE_L,
	ACT_MOUSE_R,
	ACT_NONE,
	ACT_COUNT
};

struct PadActionDef {
	const char *label;
	KBD_KEYS key1;
	KBD_KEYS key2;
	int joy_btn;
	int mouse_btn;
};

void PadMapping_Init(void);
int  PadMapping_GetPreset(void);
void PadMapping_SetPreset(int preset);
const char *PadMapping_GetPresetName(int preset);
int  PadMapping_GetButtonAction(int btn_id);
void PadMapping_SetButtonAction(int btn_id, int action_id);
const char *PadMapping_GetButtonName(int btn_id);
int  PadMapping_GetActionCount(void);
const PadActionDef *PadMapping_GetAction(int action_id);
void PadMapping_Save(void);
void PadMapping_Load(void);
void PadMapping_ResetDefaults(void);

#endif
