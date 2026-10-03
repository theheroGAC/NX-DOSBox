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
 *  with the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

/* Machine snapshots ("save states"), 2026.
 *
 * DOSBox-X grew save states by teaching one subsystem at a time to write its
 * own state into a numbered slot, which is also the only shape that fits this
 * tree: the emulator here has no idea how to freeze itself, so the knowledge
 * lives in the module that owns the state. This header is the small framework
 * that makes those modules agree on a file:
 *
 *   header:  "DOSBOXSV"  (8 bytes)
 *            Bit32u version          SAVESTATE_VERSION
 *            Bit32u memsize_kb       main memory, so a state from another
 *                                    configuration is refused instead of
 *                                    landing in a machine it does not fit
 *            Bit32u machine          MachineType
 *            Bit32u vmemsize         video memory
 *   records: char tag[4], Bit32u size, size bytes of payload, repeated
 *   end:     tag "END " with size 0
 *
 * A record carries the state of one subsystem, its tag is fixed by that module
 * and its payload is written by that module with the primitives below. Loading
 * is deliberately forgiving in one direction and strict in the other: a record
 * this build does not know is skipped (a state from a newer build still loads
 * for the parts it shares), while the header has to match exactly, because a
 * snapshot of a 16 MB machine applied to a 4 MB one would scribble over
 * whatever came after the memory block.
 *
 * Every block registers itself next to the state it serialises, so adding a
 * subsystem to the snapshot is a change in one file and nothing else:
 *
 *     static void SaveState_PIC(SaveState &state) { ... }
 *     SAVESTATE_BLOCK(pic, "PIC ", "programmable interrupt controller",
 *                     SaveState_PIC);
 *
 * Nothing here knows about the SD card: the port names the directory once at
 * start up and the slots are the port's business (see SAVESTATE_SetDirectory).
 * A front end that never sets one still gets a working state file in the
 * config directory, which is what the SDL build of this tree would use.
 */

#ifndef DOSBOX_SAVESTATE_H
#define DOSBOX_SAVESTATE_H

#ifndef DOSBOX_DOSBOX_H
#include "dosbox.h"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string>
#include <type_traits>

#define SAVESTATE_VERSION     1
#define SAVESTATE_TAG_LENGTH  4

/* Slots the menu walks through. DOSBox-X offers a hundred; a console menu that
 * is driven with a d-pad is better served by a handful. */
#define SAVESTATE_SLOT_COUNT  10
#define SAVESTATE_SLOT_FIRST  1

class SaveState {
public:
	SaveState(bool saving, FILE *file);
	~SaveState(void);

	bool Saving(void) const { return saving; }
	bool Failed(void) const { return failed; }
	const char *Error(void) const { return error.c_str(); }
	void Fail(const char *format, ...);

	/* ------------------------------------------------------------ stream
	 *
	 * On save the value goes to the file; on load it comes back and replaces
	 * it, which is why every call takes the destination by reference and one
	 * handler can serve both directions (branch on Saving() only where the
	 * two directions really differ, e.g. a derived value that is recomputed
	 * instead of read back). Integers are written little endian so a state
	 * does not depend on the byte order of the host that wrote it. */
	void Bytes(void *data, size_t size);

	template <typename T> void Num(T &value) {
		static_assert(std::is_integral<T>::value, "SaveState::Num needs an integer type");
		static_assert(!std::is_same<T, bool>::value,
		              "use SaveState::Bool for a bool: the width of bool is not fixed");
		typedef typename std::make_unsigned<T>::type Unsigned;
		unsigned char raw[sizeof(T)];
		Unsigned copy = (Unsigned)value;
		if (saving) {
			for (size_t i = 0; i < sizeof(T); i++) raw[i] = (unsigned char)(copy >> (8 * i));
			Bytes(raw, sizeof(T));
		} else {
			Bytes(raw, sizeof(T));
			copy = 0;
			for (size_t i = 0; i < sizeof(T); i++) copy |= (Unsigned)raw[i] << (8 * i);
			value = (T)copy;
		}
	}
	/* A bool is one byte here and one byte in the file, whatever the compiler
	 * decided an in-memory bool looks like. */
	void Bool(bool &value) {
		Bit8u raw = value ? 1 : 0;
		Num(raw);
		value = (raw != 0);
	}
	/* Plain enums are saved as the integer behind them, so a block does not
	 * have to spell out the casts for VGAModes and friends. */
	template <typename T> void Enum(T &value) {
		typedef typename std::underlying_type<T>::type Underlying;
		Underlying raw = (Underlying)value;
		Num(raw);
		value = (T)raw;
	}
	/* A structure whose members are all plain data, written as it sits in
	 * memory; only for records that never leave the machine that wrote them
	 * (that is a deliberate rule: no host pointers, no Bit64u layout
	 * assumptions in the state format). */
	void Pod(void *data, size_t size) { Bytes(data, size); }
	template <typename T> void Pod(T &value) { Bytes(&value, sizeof(T)); }
	void Real32(float &value) { Bytes(&value, sizeof(value)); }
	void Real64(double &value) { Bytes(&value, sizeof(value)); }

	/* ------------------------------------------------------------ record
	 *
	 * Used by the registry loop, not by the blocks themselves. On save the
	 * size is patched afterwards, so a block never has to know its own length;
	 * on load the declared size is what keeps a block from reading past its
	 * own record. */
	void BeginRecord(const char *tag);
	void EndRecord(void);
	void BeginRecordLoad(Bit32u size);
	bool EndRecordLoad(void);
	/* On load: how many payload bytes of the current record are unread. */
	size_t Remaining(void) const { return remaining; }

private:
	bool saving;
	FILE *file;
	bool failed;
	std::string error;
	/* load: bytes left in the record being read */
	size_t remaining;
	long record_start;
	long record_payload;

	void Write(const void *data, size_t size);
	bool Read(void *data, size_t size);
};

typedef void (*SaveStateHandler)(SaveState &state);

struct SaveStateBlock {
	const char *tag;      /* exactly four characters, fixed by the file format */
	const char *name;     /* for the error message when a state is refused */
	SaveStateHandler handler;
};

void SAVESTATE_RegisterBlock(const char *tag, const char *name,
                             SaveStateHandler handler);

/* Static instance that registers a block before main() runs. Kept in an
 * unnamed namespace so two modules can use the same id. */
struct SaveStateRegistration {
	SaveStateRegistration(const char *tag, const char *name, SaveStateHandler handler) {
		SAVESTATE_RegisterBlock(tag, name, handler);
	}
};

#define SAVESTATE_BLOCK(id, tag, name, handler)                                  \
	namespace {                                                                  \
		static_assert(sizeof(tag) == SAVESTATE_TAG_LENGTH + 1,                    \
		              "a save state tag is four characters");                     \
		const SaveStateRegistration savestate_block_##id(tag, name, handler);    \
	}                                                                            \
	static_assert(true, "SAVESTATE_BLOCK needs a trailing semicolon")

/* ------------------------------------------------------------- port facing */

/* Where the slots live. A port that knows its storage (the Switch front end
 * points this at the SD card) calls it once at start up; without a call the
 * states go to the DOSBox config directory. */
void SAVESTATE_SetDirectory(const char *directory);
const char *SAVESTATE_Directory(void);

/* The name of the game these slots belong to, which the menu shows so a player
 * can see whose states the numbered files are. The files themselves are named
 * after their slot, so without this a slot number is all a player gets. A port
 * that never sets one keeps the empty string and the menu just shows the
 * directory. */
void SAVESTATE_SetGameName(const char *name);
const char *SAVESTATE_GameName(void);

/* True when the slot holds a state file. */
bool SAVESTATE_Exists(int slot);

/* One line for the menu: "01/10 14:22, 17 MB". False when the slot is empty. */
bool SAVESTATE_Describe(int slot, char *out, size_t size);

/* Both fail with a reason in *error (when error is not NULL) and leave the
 * machine untouched when they do. SAVESTATE_Load refuses a state whose memory
 * size, machine type or video memory differ from the running machine. */
bool SAVESTATE_Save(int slot, std::string *error);
bool SAVESTATE_Load(int slot, std::string *error);

#endif
