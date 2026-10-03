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

/* The save state file, the slot bookkeeping and the registry of blocks.
 *
 * See include/savestate.h for the format and for why it looks the way it does.
 * The one rule worth repeating here is the ordering: records are applied in the
 * order they were written, so a block whose state is derived from another one
 * has to be registered after it. Main memory comes first for exactly that
 * reason - the CPU block asks the machine for its descriptor table bases, and
 * those tables live in emulated memory.
 */

#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <string>
#include <vector>

#include "savestate.h"

#include "cross.h"
#include "mem.h"
#include "vga.h"

static const char savestate_magic[8] =
	{ 'D', 'O', 'S', 'B', 'O', 'X', 'S', 'V' };
static const char savestate_end_tag[SAVESTATE_TAG_LENGTH + 1] = "END ";

/* ------------------------------------------------------------------ blocks */

static std::vector<SaveStateBlock> &savestate_blocks(void) {
	/* Function local so the table is built on first use: the blocks register
	 * themselves from static initialisers, and the translation units have no
	 * defined order between them. */
	static std::vector<SaveStateBlock> blocks;
	return blocks;
}

void SAVESTATE_RegisterBlock(const char *tag, const char *name,
                             SaveStateHandler handler) {
	if (!tag || !name || !handler) return;
	SaveStateBlock block;
	block.tag = tag;
	block.name = name;
	block.handler = handler;
	savestate_blocks().push_back(block);
}

static const SaveStateBlock *savestate_find_block(const unsigned char tag[SAVESTATE_TAG_LENGTH]) {
	const std::vector<SaveStateBlock> &blocks = savestate_blocks();
	for (size_t i = 0; i < blocks.size(); i++) {
		if (memcmp(blocks[i].tag, tag, SAVESTATE_TAG_LENGTH) == 0) return &blocks[i];
	}
	return NULL;
}

/* ------------------------------------------------------------------ stream */

SaveState::SaveState(bool saving_, FILE *file_)
	: saving(saving_), file(file_), failed(false), remaining((size_t)-1),
	  record_start(0), record_payload(0) {
}

SaveState::~SaveState(void) {
}

void SaveState::Fail(const char *format, ...) {
	if (failed) return;
	char message[512];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	error = message;
	failed = true;
	LOG_MSG("SAVESTATE: %s", message);
}

void SaveState::Write(const void *data, size_t size) {
	if (failed || !file || !size) return;
	if (fwrite(data, 1, size, file) != size) {
		Fail("could not write to the save state file (%s)", strerror(errno));
	}
}

bool SaveState::Read(void *data, size_t size) {
	if (failed || !file) return false;
	if (!size) return true;
	if (remaining != (size_t)-1 && size > remaining) {
		Fail("the state is shorter than its own record header claims");
		return false;
	}
	if (fread(data, 1, size, file) != size) {
		Fail("the state file ends in the middle of a record");
		return false;
	}
	if (remaining != (size_t)-1) remaining -= size;
	return true;
}

void SaveState::Bytes(void *data, size_t size) {
	if (saving) Write(data, size);
	else Read(data, size);
}

void SaveState::BeginRecord(const char *tag) {
	if (failed || !file) return;
	record_start = ftell(file);
	if (fwrite(tag, 1, SAVESTATE_TAG_LENGTH, file) != SAVESTATE_TAG_LENGTH) {
		Fail("could not write to the save state file (%s)", strerror(errno));
		return;
	}
	/* The size is patched in EndRecord, so a block never has to know how long
	 * its own payload turned out to be. */
	const unsigned char placeholder[4] = { 0, 0, 0, 0 };
	if (fwrite(placeholder, 1, 4, file) != 4) {
		Fail("could not write to the save state file (%s)", strerror(errno));
		return;
	}
	record_payload = ftell(file);
}

void SaveState::EndRecord(void) {
	if (failed || !file) return;
	const long end = ftell(file);
	if (end < record_payload) {
		Fail("the save state file went backwards while it was written");
		return;
	}
	const Bit32u size = (Bit32u)(end - record_payload);
	const unsigned char raw[4] = {
		(unsigned char)(size & 0xff), (unsigned char)((size >> 8) & 0xff),
		(unsigned char)((size >> 16) & 0xff), (unsigned char)((size >> 24) & 0xff)
	};
	if (fseek(file, record_start + SAVESTATE_TAG_LENGTH, SEEK_SET) != 0) {
		Fail("could not patch a record size (%s)", strerror(errno));
		return;
	}
	if (fwrite(raw, 1, 4, file) != 4) {
		Fail("could not write to the save state file (%s)", strerror(errno));
		return;
	}
	if (fseek(file, end, SEEK_SET) != 0) {
		Fail("could not seek in the save state file (%s)", strerror(errno));
	}
}

void SaveState::BeginRecordLoad(Bit32u size) {
	remaining = (size_t)size;
}

bool SaveState::EndRecordLoad(void) {
	if (failed) return false;
	bool ok = true;
	if (remaining != (size_t)-1 && remaining > 0) {
		/* A record this build knows can still be longer than it expects, for
		 * instance when it was written by a build with more fields in that
		 * block; skipping the extra bytes is what makes the file forward
		 * compatible instead of simply unreadable. */
		ok = fseek(file, (long)remaining, SEEK_CUR) == 0;
	}
	remaining = (size_t)-1;
	return ok;
}

/* ---------------------------------------------------------- slot handling */

static std::string savestate_directory;
static std::string savestate_game_name;

void SAVESTATE_SetGameName(const char *name) {
	savestate_game_name = (name && name[0]) ? name : "";
}

const char *SAVESTATE_GameName(void) {
	return savestate_game_name.c_str();
}

void SAVESTATE_SetDirectory(const char *directory) {
	savestate_directory = (directory && directory[0]) ? directory : "";
	if (!savestate_directory.empty()) {
		const char last = savestate_directory[savestate_directory.size() - 1];
		if (last != '/' && last != '\\') savestate_directory += CROSS_FILESPLIT;
	}
}

const char *SAVESTATE_Directory(void) {
	if (savestate_directory.empty()) {
#ifdef DOSBOX_PORT_SWITCH
		/* The Switch front end sets this itself; this is the same default, so a
		 * build that forgets to keeps working. */
		savestate_directory = "sdmc:/dosbox/savestates/";
#else
		std::string dir;
		Cross::GetPlatformConfigDir(dir);
		savestate_directory = dir + "savestates";
		savestate_directory += CROSS_FILESPLIT;
#endif
	}
	return savestate_directory.c_str();
}

static bool savestate_slot_is_valid(int slot) {
	return slot >= SAVESTATE_SLOT_FIRST &&
	       slot < SAVESTATE_SLOT_FIRST + SAVESTATE_SLOT_COUNT;
}

static std::string savestate_slot_path(int slot) {
	char name[32];
	snprintf(name, sizeof(name), "slot%02d.dsv", slot);
	return std::string(SAVESTATE_Directory()) + name;
}

static void savestate_mkdir(const char *path) {
#ifdef WIN32
	mkdir(path);
#else
	mkdir(path, 0777);
#endif
}

bool SAVESTATE_Exists(int slot) {
	if (!savestate_slot_is_valid(slot)) return false;
	struct stat info;
	if (stat(savestate_slot_path(slot).c_str(), &info) != 0) return false;
	return (info.st_mode & S_IFMT) == S_IFREG;
}

bool SAVESTATE_Describe(int slot, char *out, size_t size) {
	if (!out || !size) return false;
	out[0] = '\0';
	if (!SAVESTATE_Exists(slot)) return false;
	const std::string path = savestate_slot_path(slot);
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		snprintf(out, size, "in use");
		return true;
	}
	/* The card's own timestamps are the only clock that is certainly right in
	 * this context: the emulated machine's date is a DOS date, and the host
	 * clock is whatever the console read at boot. */
	char when[48];
	struct tm *broken = NULL;
	const time_t stamp = (time_t)info.st_mtime;
	if (stamp != 0) broken = localtime(&stamp);
	if (broken) {
		snprintf(when, sizeof(when), "%02d/%02d %02d:%02d", broken->tm_mday,
		         broken->tm_mon + 1, broken->tm_hour, broken->tm_min);
	} else {
		snprintf(when, sizeof(when), "saved earlier");
	}
	const unsigned long bytes = (unsigned long)info.st_size;
	snprintf(out, size, "%s, %lu.%lu MB", when, bytes / (1024 * 1024),
	         (bytes / 1024) % 1024 * 10 / 1024);
	return true;
}

/* ---------------------------------------------------------------- writing */

bool SAVESTATE_Save(int slot, std::string *error) {
	if (!savestate_slot_is_valid(slot)) {
		if (error) *error = "no such save slot";
		return false;
	}
	savestate_mkdir(SAVESTATE_Directory());
	const std::string path = savestate_slot_path(slot);
	FILE *file = fopen(path.c_str(), "wb");
	if (!file) {
		if (error) {
			char message[256];
			snprintf(message, sizeof(message), "cannot write %s (%s)",
			         path.c_str(), strerror(errno));
			*error = message;
		}
		return false;
	}

	SaveState state(true, file);
	state.Bytes(const_cast<char *>(savestate_magic), sizeof(savestate_magic));
	Bit32u version = SAVESTATE_VERSION;
	Bit32u memsize_kb = (Bit32u)(MEM_TotalPages() * (MEM_PAGESIZE / 1024));
	Bit32u machine_type = (Bit32u)machine;
	Bit32u video_kb = (Bit32u)(vga.vmemsize / 1024);
	state.Num(version);
	state.Num(memsize_kb);
	state.Num(machine_type);
	state.Num(video_kb);

	const std::vector<SaveStateBlock> &blocks = savestate_blocks();
	for (size_t i = 0; i < blocks.size() && !state.Failed(); i++) {
		state.BeginRecord(blocks[i].tag);
		blocks[i].handler(state);
		state.EndRecord();
	}
	if (!state.Failed()) {
		/* The end record is what tells a reader the state is complete, so a
		 * file that was cut short (the console lost power mid write) is refused
		 * instead of loading half a machine. */
		const unsigned char zero[4] = { 0, 0, 0, 0 };
		if (fwrite(savestate_end_tag, 1, SAVESTATE_TAG_LENGTH, file) != SAVESTATE_TAG_LENGTH ||
		    fwrite(zero, 1, 4, file) != 4) {
			state.Fail("could not write to the save state file (%s)", strerror(errno));
		}
	}

	if (state.Failed()) {
		if (error) *error = state.Error();
		fclose(file);
		remove(path.c_str());
		return false;
	}
	if (fflush(file) != 0) {
		fclose(file);
		remove(path.c_str());
		if (error) *error = "the state could not be written to the card";
		return false;
	}
	fclose(file);
	return true;
}

/* ---------------------------------------------------------------- reading */

static Bit32u savestate_read_le32(const unsigned char raw[4]) {
	return (Bit32u)raw[0] | ((Bit32u)raw[1] << 8) | ((Bit32u)raw[2] << 16) |
	       ((Bit32u)raw[3] << 24);
}

/* Reads the record directory without applying anything: a state that is not a
 * state, or that was cut short, is refused before the first byte of the machine
 * is touched. found tells which registered blocks the file actually carries. */
static bool savestate_check_records(FILE *file, std::string *error,
                                    std::vector<bool> &found,
                                    size_t *records, size_t *unknown) {
	found.assign(savestate_blocks().size(), false);
	*records = 0;
	*unknown = 0;
	for (;;) {
		unsigned char tag[SAVESTATE_TAG_LENGTH];
		unsigned char size_raw[4];
		if (fread(tag, 1, SAVESTATE_TAG_LENGTH, file) != SAVESTATE_TAG_LENGTH ||
		    fread(size_raw, 1, 4, file) != 4) {
			if (error) *error = "the state file is cut short";
			return false;
		}
		const Bit32u size = savestate_read_le32(size_raw);
		if (memcmp(tag, savestate_end_tag, SAVESTATE_TAG_LENGTH) == 0) {
			if (size != 0) {
				if (error) *error = "the state file has a damaged end marker";
				return false;
			}
			break;
		}
		const std::vector<SaveStateBlock> &blocks = savestate_blocks();
		bool known = false;
		for (size_t i = 0; i < blocks.size(); i++) {
			if (memcmp(blocks[i].tag, tag, SAVESTATE_TAG_LENGTH) != 0) continue;
			found[i] = true;
			known = true;
			break;
		}
		if (known) {
			(*records)++;
		} else {
			(*unknown)++;
			char text[SAVESTATE_TAG_LENGTH + 1];
			memcpy(text, tag, SAVESTATE_TAG_LENGTH);
			text[SAVESTATE_TAG_LENGTH] = '\0';
			LOG_MSG("SAVESTATE: skipping the '%s' record, this build has no block for it",
			        text);
		}
		if (fseek(file, (long)size, SEEK_CUR) != 0) {
			if (error) *error = "a record claims to be longer than the file";
			return false;
		}
	}
	/* A block the machine has but the state does not leaves that part of the
	 * machine as it was, which is worth saying out loud: it is what happens when
	 * a state is older than the module that wrote it. */
	const std::vector<SaveStateBlock> &blocks = savestate_blocks();
	for (size_t i = 0; i < blocks.size(); i++) {
		if (found[i]) continue;
		LOG_MSG("SAVESTATE: the state has no '%s' record (%s), that part keeps its current state",
		        blocks[i].tag, blocks[i].name);
	}
	return true;
}

bool SAVESTATE_Load(int slot, std::string *error) {
	if (!savestate_slot_is_valid(slot)) {
		if (error) *error = "no such save slot";
		return false;
	}
	const std::string path = savestate_slot_path(slot);
	FILE *file = fopen(path.c_str(), "rb");
	if (!file) {
		if (error) {
			char message[128];
			snprintf(message, sizeof(message), "slot %d is empty", slot);
			*error = message;
		}
		return false;
	}

	unsigned char magic[sizeof(savestate_magic)];
	Bit32u version = 0, memsize_kb = 0, machine_type = 0, video_kb = 0;
	{
		SaveState header(false, file);
		header.Bytes(magic, sizeof(magic));
		header.Num(version);
		header.Num(memsize_kb);
		header.Num(machine_type);
		header.Num(video_kb);
		if (header.Failed()) {
			if (error) *error = header.Error();
			fclose(file);
			return false;
		}
	}
	if (memcmp(magic, savestate_magic, sizeof(magic)) != 0) {
		if (error) *error = "that file is not a DOSBox save state";
		fclose(file);
		return false;
	}
	if (version != SAVESTATE_VERSION) {
		if (error) {
			char message[160];
			snprintf(message, sizeof(message),
			         "the state was written by save state version %u, this build reads %u",
			         (unsigned)version, (unsigned)SAVESTATE_VERSION);
			*error = message;
		}
		fclose(file);
		return false;
	}
	/* DOSBox-X refuses a state whose memory size does not match, for the same
	 * reason: the memory record is a flat copy, so a 16 MB state applied to a
	 * 4 MB machine would write straight past the end of the allocation. */
	const Bit32u running_kb = (Bit32u)(MEM_TotalPages() * (MEM_PAGESIZE / 1024));
	if (memsize_kb != running_kb) {
		if (error) {
			char message[192];
			snprintf(message, sizeof(message),
			         "memory size mismatch: the state holds %u MB, this machine has %u MB",
			         (unsigned)(memsize_kb / 1024), (unsigned)(running_kb / 1024));
			*error = message;
		}
		fclose(file);
		return false;
	}
	if ((Bit32u)machine != machine_type) {
		if (error) {
			char message[192];
			snprintf(message, sizeof(message),
			         "machine type mismatch: the state was taken on a machine type %u, this one is %u",
			         (unsigned)machine_type, (unsigned)machine);
			*error = message;
		}
		fclose(file);
		return false;
	}
	if ((Bit32u)(vga.vmemsize / 1024) != video_kb) {
		if (error) {
			char message[192];
			snprintf(message, sizeof(message),
			         "video memory mismatch: the state holds %u KB, this machine has %u KB",
			         (unsigned)video_kb, (unsigned)(vga.vmemsize / 1024));
			*error = message;
		}
		fclose(file);
		return false;
	}

	std::vector<bool> found;
	size_t records = 0, unknown = 0;
	if (!savestate_check_records(file, error, found, &records, &unknown)) {
		fclose(file);
		return false;
	}
	if (records == 0) {
		if (error) *error = "the state carries no record this build can apply";
		fclose(file);
		return false;
	}
	if (fseek(file, (long)sizeof(savestate_magic) + 4 * 4, SEEK_SET) != 0) {
		if (error) *error = "the state file could not be read back";
		fclose(file);
		return false;
	}

	/* Second pass: the records are applied in the order they were written. */
	SaveState state(false, file);
	for (;;) {
		unsigned char tag[SAVESTATE_TAG_LENGTH];
		unsigned char size_raw[4];
		if (fread(tag, 1, SAVESTATE_TAG_LENGTH, file) != SAVESTATE_TAG_LENGTH ||
		    fread(size_raw, 1, 4, file) != 4) {
			state.Fail("the state file is cut short");
			break;
		}
		const Bit32u size = savestate_read_le32(size_raw);
		if (memcmp(tag, savestate_end_tag, SAVESTATE_TAG_LENGTH) == 0) break;
		const SaveStateBlock *block = savestate_find_block(tag);
		if (!block) {
			if (fseek(file, (long)size, SEEK_CUR) != 0) {
				state.Fail("a record claims to be longer than the file");
				break;
			}
			continue;
		}
		state.BeginRecordLoad(size);
		block->handler(state);
		if (state.Failed()) break;
		if (!state.EndRecordLoad()) {
			state.Fail("the '%s' record (%s) could not be skipped to its end",
			           block->tag, block->name);
			break;
		}
	}

	const bool ok = !state.Failed();
	if (!ok) {
		if (error) *error = state.Error();
	} else {
		LOG_MSG("SAVESTATE: slot %d loaded, %u records applied, %u ignored",
		        slot, (unsigned)records, (unsigned)unknown);
	}
	fclose(file);
	return ok;
}
