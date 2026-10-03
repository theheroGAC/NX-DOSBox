/*
 *  TinySoundFont General MIDI Synth for DOSBox
 *  Allows real orchestral instruments via .sf2 soundfont on Nintendo Switch & standalone
 */

#ifndef DOSBOX_MIDI_TSF_H
#define DOSBOX_MIDI_TSF_H

#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "mixer.h"
#include <stdio.h>

class MidiHandler_tsf : public MidiHandler {
private:
	tsf *synth;
	MixerChannel *channel;
	bool is_open;

	static void MixerCallback(Bitu len);
	static MidiHandler_tsf *instance;

public:
	MidiHandler_tsf() : MidiHandler(), synth(NULL), channel(NULL), is_open(false) {}
	virtual ~MidiHandler_tsf() { Close(); }

	virtual const char *GetName(void) { return "synth"; }

	virtual bool Open(const char *conf) {
		if (is_open) return true;

		const char *paths[] = {
			conf,
			"sdmc:/dosbox/soundfonts/default.sf2",
			"sdmc:/dosbox/soundfont.sf2",
			"sdmc:/dosbox/soundfonts/GeneralUser_GS.sf2",
			"sdmc:/dosbox/soundfonts/gm.sf2",
			"sdmc:/dosbox/default.sf2",
			"soundfonts/default.sf2",
			"soundfont.sf2",
			"default.sf2"
		};

		FILE *f = NULL;
		const char *found_path = NULL;
		for (unsigned int i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
			if (paths[i] && paths[i][0]) {
				f = fopen(paths[i], "rb");
				if (f) {
					fclose(f);
					found_path = paths[i];
					break;
				}
			}
		}

		if (!found_path) {
			LOG_MSG("TSF: No SoundFont found on SD card (sdmc:/dosbox/soundfonts/default.sf2)");
			return false;
		}

		synth = tsf_load_filename(found_path);
		if (!synth) {
			LOG_MSG("TSF: Failed to load SoundFont '%s'", found_path);
			return false;
		}

		/* Initialize stereo output at 48000Hz (native Nintendo Switch rate) */
		tsf_set_output(synth, TSF_STEREO_INTERLEAVED, 48000, 0.0f);

		/* Ensure channel 9 (drums in 0-indexed General MIDI) uses drum kit */
		tsf_channel_set_presetnumber(synth, 9, 0, 1);

		instance = this;
		channel = MIXER_AddChannel(&MidiHandler_tsf::MixerCallback, 48000, "TSF_MIDI");
		if (channel) {
			channel->Enable(true);
			channel->SetVolume(1.0f, 1.0f);
		}

		is_open = true;
		LOG_MSG("TSF: General MIDI SoundFont synth loaded '%s' at 48000Hz", found_path);
		return true;
	}

	virtual void Close(void) {
		if (!is_open) return;
		is_open = false;
		instance = NULL;
		if (channel) {
			MIXER_DelChannel(channel);
			channel = NULL;
		}
		if (synth) {
			tsf_close(synth);
			synth = NULL;
		}
	}

	virtual void PlayMsg(Bit8u *msg) {
		if (!synth || !is_open) return;
		const Bit8u status = msg[0] & 0xF0;
		const int ch = msg[0] & 0x0F;

		switch (status) {
		case 0x80: /* Note Off */
			tsf_channel_note_off(synth, ch, msg[1]);
			break;
		case 0x90: /* Note On */
			if (msg[2] == 0) {
				tsf_channel_note_off(synth, ch, msg[1]);
			} else {
				tsf_channel_note_on(synth, ch, msg[1], (float)msg[2] / 127.0f);
			}
			break;
		case 0xB0: /* Control Change */
			tsf_channel_midi_control(synth, ch, msg[1], msg[2]);
			break;
		case 0xC0: /* Program Change */
			tsf_channel_set_presetnumber(synth, ch, msg[1], (ch == 9) ? 1 : 0);
			break;
		case 0xE0: /* Pitch Bend */ {
			int pitch = ((int)msg[2] << 7) | (int)msg[1];
			tsf_channel_set_pitchwheel(synth, ch, pitch);
			break;
		}
		default:
			break;
		}
	}

	virtual void PlaySysex(Bit8u *sysex, Bitu len) {
		if (!synth || !is_open) return;
		/* General MIDI System Reset: F0 7E 7F 09 01 F7 */
		if (len >= 6 && sysex[0] == 0xF0 && sysex[1] == 0x7E && sysex[3] == 0x09 && sysex[4] == 0x01) {
			tsf_reset(synth);
			tsf_channel_set_presetnumber(synth, 9, 0, 1);
		}
	}
};

MidiHandler_tsf *MidiHandler_tsf::instance = NULL;

void MidiHandler_tsf::MixerCallback(Bitu len) {
	if (!instance || !instance->synth || !instance->channel) return;
	short buf[1024 * 2]; /* 1024 stereo sample frames */
	while (len > 0) {
		Bitu count = len > 1024 ? 1024 : len;
		tsf_render_short(instance->synth, buf, (int)count, 0);
		instance->channel->AddSamples_s16(count, (const Bit16s *)buf);
		len -= count;
	}
}

static MidiHandler_tsf Midi_tsf;

#endif
