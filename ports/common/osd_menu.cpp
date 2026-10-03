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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "osd.h"
#include "osd_font.h"
#include "platform.h"

#define LAUNCHER_ROOT_DEFAULT "sdmc:/"
#define LAUNCHER_LIBRARY_ROOT_A "sdmc:/dosbox"
#define LAUNCHER_LIBRARY_ROOT_B "sdmc:/switch/dosbox"
#define LAUNCHER_MAX_ENTRIES   128
#define LAUNCHER_VISIBLE_ROWS  8
#define LAUNCHER_SCAN_BUDGET   24000
#define LAUNCHER_SCAN_STEP     160
#define LAUNCHER_SCAN_DEPTH    4
#define LAUNCHER_SCAN_QUEUE    128
#define LAUNCHER_MAX_LIBRARY   48
#define LAUNCHER_MAX_ITEMS     96
#define LAUNCHER_MAX_IMAGES    8
#define LAUNCHER_IMAGE_LIST    2048
#define LAUNCHER_PATH_MAX      768

enum EntryKind {
	ENTRY_PARENT,
	ENTRY_START,
	ENTRY_DIRECTORY,
	ENTRY_IMAGE,
	ENTRY_HEADER,
	ENTRY_LIBRARY,
	ENTRY_PROGRAM,
	ENTRY_CLASSIC,
	ENTRY_SETTING
};

enum LauncherMedia {
	MEDIA_NONE = 0,
	MEDIA_FLOPPY,
	MEDIA_ISO,
	MEDIA_HDD
};

struct LauncherEntry {
	char name[256];
	enum EntryKind kind;
	bool playable;
	char starts[256];
	const char *path;
	int setting;
};

static struct LauncherEntry launcher_entries[LAUNCHER_MAX_ENTRIES];
static int launcher_count = 0;
static int launcher_selected = 0;
static int launcher_scroll = 0;
static char launcher_cwd[LAUNCHER_PATH_MAX] = LAUNCHER_ROOT_DEFAULT;
static char launcher_result[LAUNCHER_PATH_MAX];
static char launcher_image_list[LAUNCHER_IMAGE_LIST];
static int launcher_image_count = 0;
static enum LauncherMedia launcher_media_type = MEDIA_NONE;

struct LauncherGame {
	char name[192];
	char path[LAUNCHER_PATH_MAX];
	char starts[96];
};
static struct LauncherGame launcher_library[LAUNCHER_MAX_LIBRARY];
static int launcher_library_count = 0;

enum LauncherTab {
	TAB_GAMES,
	TAB_DISKS,
	TAB_PROGRAMS,
	TAB_FOLDERS,
	TAB_SETTINGS,
	TAB_COUNT
};

static const char *const launcher_tab_names[TAB_COUNT] = {
	"GAMES", "DISKS", "PROGRAMS", "FOLDERS", "SETTINGS"
};

static OSD_FTPStartFn launcher_ftp_start;
static OSD_FTPStopFn launcher_ftp_stop;
static OSD_FTPStatusFn launcher_ftp_status;
static OSD_FTPTakeDirtyFn launcher_ftp_dirty;
static bool launcher_ftp_wanted = false;

void OSD_SetFTPServer(OSD_FTPStartFn start, OSD_FTPStopFn stop,
                      OSD_FTPStatusFn status, OSD_FTPTakeDirtyFn take_dirty) {
	launcher_ftp_start = start;
	launcher_ftp_stop = stop;
	launcher_ftp_status = status;
	launcher_ftp_dirty = take_dirty;
}

static void launcher_ftp_state(struct OSD_FTPServer *server) {
	memset(server, 0, sizeof(*server));
	if (launcher_ftp_status) launcher_ftp_status(server);
}

static void launcher_ftp_toggle(void) {
	extern void SwitchPlatform_Trace(const char *step);
	if (!launcher_ftp_start || !launcher_ftp_stop) return;
	if (launcher_ftp_wanted) {
		launcher_ftp_stop();
		launcher_ftp_wanted = false;
		SwitchPlatform_Trace("OSD: FTP server stopped");
	} else if (launcher_ftp_start() == 0) {
		launcher_ftp_wanted = true;
		SwitchPlatform_Trace("OSD: FTP server started");
	} else {
		SwitchPlatform_Trace("OSD: FTP server did not start");
	}
}

static void launcher_ftp_open(void) {
	if (!launcher_ftp_wanted || !launcher_ftp_start) return;
	if (launcher_ftp_start() != 0) launcher_ftp_wanted = false;
}

static void launcher_ftp_close(void) {
	if (launcher_ftp_stop) launcher_ftp_stop();
}

static OSD_ExitPollFn launcher_exit_poll;

void OSD_SetExitPollFn(OSD_ExitPollFn fn) {
	launcher_exit_poll = fn;
}

struct LauncherItem {
	char name[192];
	char path[LAUNCHER_PATH_MAX];
	char starts[96];
};
static struct LauncherItem launcher_disks[LAUNCHER_MAX_ITEMS];
static int launcher_disk_count = 0;
static struct LauncherItem launcher_programs[LAUNCHER_MAX_ITEMS];
static int launcher_program_count = 0;

static int launcher_tab = TAB_GAMES;
static int launcher_tab_selected[TAB_COUNT] = { 0, 0, 0, 0 };
static int launcher_tab_scroll[TAB_COUNT] = { 0, 0, 0, 0 };

struct LauncherScanItem {
	char path[LAUNCHER_PATH_MAX];
	int depth;
	int root;
	bool files_only;
};
static const char *const launcher_library_roots[] = {
	LAUNCHER_LIBRARY_ROOT_A, LAUNCHER_LIBRARY_ROOT_B
};
#define LAUNCHER_LIBRARY_ROOT_COUNT \
	((int)(sizeof(launcher_library_roots) / sizeof(launcher_library_roots[0])))

static struct LauncherScanItem launcher_scan_queue[LAUNCHER_SCAN_QUEUE];
static int launcher_scan_head = 0;
static int launcher_scan_tail = 0;
static DIR *launcher_scan_dir = NULL;
static char launcher_scan_cwd[LAUNCHER_PATH_MAX] = "";
static int launcher_scan_depth = 0;
static int launcher_scan_root = 0;
static bool launcher_scan_files_only = false;
static int launcher_scan_seen = 0;
static int launcher_scan_folders = 0;
static bool launcher_scan_active = false;
static bool launcher_scan_skipped = false;

struct LauncherFile {
	char name[256];
	bool runnable;
};
#define LAUNCHER_MAX_FILES 32
static struct LauncherFile launcher_files[LAUNCHER_MAX_FILES];
static int launcher_file_count = 0;
static char launcher_boot_label[256] = "";
static char launcher_boot_subfolder[256] = "";
static char launcher_boot_cmds[512] = "";

static void launcher_join(char *out, size_t out_size,
                          const char *dir, const char *name) {
	if (name && *name) {
		size_t len = strlen(dir);
		if (len > 0 && dir[len - 1] == '/')
			snprintf(out, out_size, "%s%s", dir, name);
		else
			snprintf(out, out_size, "%s/%s", dir, name);
	} else if (out != dir) {
		snprintf(out, out_size, "%s", dir);
	}
}

static bool launcher_is_dir(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0) return false;
	return (st.st_mode & S_IFMT) == S_IFDIR;
}

static void launcher_collect_images(const char *first_name);

static const char *const launcher_image_exts[] = {
	".img", ".ima", ".bin", ".iso", ".cue", ".vfd", ".dsk", NULL
};

static enum LauncherMedia launcher_guess_media(const char *path) {
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	size_t n = strlen(base);

	if (n > 4 && (strcasecmp(base + n - 4, ".iso") == 0 ||
	              strcasecmp(base + n - 4, ".cue") == 0))
		return MEDIA_ISO;
	if (n > 4 && (strcasecmp(base + n - 4, ".ima") == 0 ||
	              strcasecmp(base + n - 4, ".vfd") == 0 ||
	              strcasecmp(base + n - 4, ".dsk") == 0))
		return MEDIA_FLOPPY;
	if (n > 4 && strcasecmp(base + n - 4, ".bin") == 0)
		return MEDIA_ISO;
	if (n > 4 && strcasecmp(base + n - 4, ".img") == 0) {
		struct stat st;
		if (stat(path, &st) == 0) {
			if ((unsigned long long)st.st_size <= 3ull * 1024ull * 1024ull)
				return MEDIA_FLOPPY;
			return MEDIA_HDD;
		}
		return MEDIA_FLOPPY;
	}
	return MEDIA_FLOPPY;
}

static bool launcher_is_image(const char *name) {
	for (int i = 0; launcher_image_exts[i]; i++) {
		size_t n = strlen(name), e = strlen(launcher_image_exts[i]);
		if (n > e && strcasecmp(name + n - e, launcher_image_exts[i]) == 0) return true;
	}
	return false;
}

static bool launcher_is_runnable(const char *name) {
	static const char *const exts[] = { ".exe", ".com", ".bat", ".cmd", NULL };
	for (int i = 0; exts[i]; i++) {
		size_t n = strlen(name), e = strlen(exts[i]);
		if (n > e && strcasecmp(name + n - e, exts[i]) == 0) return true;
	}
	return false;
}

static int launcher_kind_rank(enum EntryKind kind) {
	switch (kind) {
	case ENTRY_HEADER:    return -2;
	case ENTRY_LIBRARY:   return -1;
	case ENTRY_PROGRAM:   return 0;
	case ENTRY_PARENT:    return 0;
	case ENTRY_START:     return 1;
	case ENTRY_DIRECTORY: return 3;
	default:              return 4;
	}
}

static int launcher_compare(const void *a, const void *b) {
	const struct LauncherEntry *ea = (const struct LauncherEntry *)a;
	const struct LauncherEntry *eb = (const struct LauncherEntry *)b;
	const int ra = launcher_kind_rank(ea->kind);
	const int rb = launcher_kind_rank(eb->kind);
	if (ra != rb) return ra - rb;
	return strcasecmp(ea->name, eb->name);
}

static bool launcher_is_system_dir(const char *name) {
	static const char *const sys_dirs[] = {
		"atmosphere", "bootloader", "Nintendo", "switch", "config",
		"tools", "themes", "erista", "mariko", "emummc", "warmboot_mariko",
		"sept", "JKSV", "tinfoil", "DBI", "payloads", NULL
	};
	for (int i = 0; sys_dirs[i]; i++) {
		if (strcasecmp(name, sys_dirs[i]) == 0) return true;
	}
	if (strcasecmp(name, "savestates") == 0) return true;
	return false;
}

static bool launcher_probe_path(const char *dir_path, char *label, size_t label_size,
                                bool descend) {
	if (label && label_size) label[0] = '\0';

	DIR *dir = opendir(dir_path);
	if (!dir) return false;

	char only_run[256] = "";
	int runnable = 0, images = 0;
	int subdirs = 0;
	bool has_autoexec = false, has_start_txt = false, has_start_bat = false;
	bool has_installer = false;
	char installer[256] = "";

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		char path[LAUNCHER_PATH_MAX];
		launcher_join(path, sizeof(path), dir_path, ent->d_name);
		if (launcher_is_dir(path)) {
			if (!launcher_is_system_dir(ent->d_name)) subdirs++;
			continue;
		}

		if (launcher_is_image(ent->d_name)) {
			images++;
			continue;
		}
		if (strcasecmp(ent->d_name, "autoexec.bat") == 0) {
			has_autoexec = true;
		} else if (strcasecmp(ent->d_name, "start.txt") == 0 ||
		           strcasecmp(ent->d_name, "dosbox-start.txt") == 0) {
			has_start_txt = true;
		} else if (strcasecmp(ent->d_name, "start.bat") == 0 ||
		           strcasecmp(ent->d_name, "dosbox-start.bat") == 0) {
			has_start_bat = true;
		} else if (strcasecmp(ent->d_name, "install.exe") == 0 ||
		           strcasecmp(ent->d_name, "setup.exe") == 0 ||
		           strcasecmp(ent->d_name, "install.com") == 0 ||
		           strcasecmp(ent->d_name, "setup.com") == 0 ||
		           strcasecmp(ent->d_name, "install.bat") == 0 ||
		           strcasecmp(ent->d_name, "setup.bat") == 0) {
			if (!has_installer) {
				has_installer = true;
				snprintf(installer, sizeof(installer), "%s", ent->d_name);
			}
		} else if (launcher_is_runnable(ent->d_name)) {
			runnable++;
			if (runnable == 1)
				snprintf(only_run, sizeof(only_run), "%s", ent->d_name);
		}
	}
	closedir(dir);

	if (has_autoexec) {
		if (label) snprintf(label, label_size, "autoexec.bat");
		return true;
	}
	if (has_start_txt) {
		if (label) snprintf(label, label_size, "start.txt");
		return true;
	}
	if (has_start_bat) {
		if (label) snprintf(label, label_size, "start.bat");
		return true;
	}
	if (runnable == 1) {
		if (label) snprintf(label, label_size, "%s", only_run);
		return true;
	}
	if (has_installer) {
		if (label) snprintf(label, label_size, "%s", installer);
		return true;
	}
	if (images > 0) {
		if (label) snprintf(label, label_size, "%d disk image%s",
		                    images, images == 1 ? "" : "s");
		return true;
	}

	if (subdirs > 0 && !descend) {
		if (label) snprintf(label, label_size, "folder");
		return false;
	}

	if (!descend) {
		if (label) snprintf(label, label_size, "browse");
		return false;
	}

	DIR *sdir = opendir(dir_path);
	if (sdir) {
		struct dirent *subent;
		while ((subent = readdir(sdir)) != NULL) {
			if (subent->d_name[0] == '.') continue;
			if (launcher_is_system_dir(subent->d_name)) continue;
			char subpath[LAUNCHER_PATH_MAX];
			launcher_join(subpath, sizeof(subpath), dir_path, subent->d_name);
			if (!launcher_is_dir(subpath)) continue;
			DIR *inner = opendir(subpath);
			if (!inner) continue;
			struct dirent *ient;
			char found_exe[256] = "";
			int inner_run = 0;
			while ((ient = readdir(inner)) != NULL) {
				if (ient->d_name[0] == '.') continue;
				if (strcasecmp(ient->d_name, "autoexec.bat") == 0 ||
				    strcasecmp(ient->d_name, "start.bat") == 0) {
					snprintf(found_exe, sizeof(found_exe), "%s", ient->d_name);
					inner_run = 1;
					break;
				}
				if (launcher_is_runnable(ient->d_name)) {
					inner_run++;
					if (inner_run == 1)
						snprintf(found_exe, sizeof(found_exe), "%s", ient->d_name);
				}
			}
			closedir(inner);
			if (inner_run == 1 && found_exe[0]) {
				if (label) snprintf(label, label_size, "%.28s/%.28s", subent->d_name, found_exe);
				closedir(sdir);
				return true;
			}
		}
		closedir(sdir);
	}

	if (label) snprintf(label, label_size, "browse");
	return false;
}

static void launcher_pick_root(void) {
	snprintf(launcher_cwd, sizeof(launcher_cwd), "%s", LAUNCHER_ROOT_DEFAULT);
}

static void launcher_scan(void) {
	memset(launcher_entries, 0, sizeof(launcher_entries));
	launcher_count = 0;
	launcher_image_count = 0;
	launcher_file_count = 0;

	bool is_root = (strcmp(launcher_cwd, LAUNCHER_ROOT_DEFAULT) == 0);
	if (!is_root) {
		launcher_entries[launcher_count].kind = ENTRY_PARENT;
		launcher_entries[launcher_count].playable = false;
		launcher_entries[launcher_count].starts[0] = '\0';
		snprintf(launcher_entries[launcher_count].name,
		         sizeof(launcher_entries[0].name), ".. (parent folder)");
		launcher_count++;
	}

	DIR *dir = opendir(launcher_cwd);
	if (dir) {
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL && launcher_count < LAUNCHER_MAX_ENTRIES) {
			if (ent->d_name[0] == '.') continue;
			char path[LAUNCHER_PATH_MAX];
			launcher_join(path, sizeof(path), launcher_cwd, ent->d_name);
			enum EntryKind kind;
			bool playable = false;
			char starts[64] = "";
			if (launcher_is_dir(path)) {
				kind = ENTRY_DIRECTORY;
				if (launcher_is_system_dir(ent->d_name)) {
					playable = false;
					snprintf(starts, sizeof(starts), "system");
				} else {
					playable = launcher_probe_path(path, starts, sizeof(starts), true);
				}
			} else if (launcher_is_image(ent->d_name) &&
			           launcher_image_count < LAUNCHER_MAX_IMAGES) {
				kind = ENTRY_IMAGE;
				playable = true;
				snprintf(starts, sizeof(starts), "disk image");
				launcher_image_count++;
			} else {
				if (launcher_file_count < LAUNCHER_MAX_FILES) {
					snprintf(launcher_files[launcher_file_count].name,
					         sizeof(launcher_files[0].name), "%s", ent->d_name);
					launcher_files[launcher_file_count].runnable =
						launcher_is_runnable(ent->d_name);
					launcher_file_count++;
				}
				continue;
			}
			launcher_entries[launcher_count].kind = kind;
			launcher_entries[launcher_count].playable = playable;
			snprintf(launcher_entries[launcher_count].starts,
			         sizeof(launcher_entries[0].starts), "%s", starts);
			snprintf(launcher_entries[launcher_count].name,
			         sizeof(launcher_entries[0].name), "%s", ent->d_name);
			launcher_count++;
		}
		closedir(dir);
	}

	if (!is_root) {
		bool has_run = false;
		bool has_images = launcher_image_count > 0;
		for (int i = 0; i < launcher_file_count; i++) {
			if (launcher_files[i].runnable) { has_run = true; break; }
		}
		int subdirs = 0;
		{
			DIR *sub = opendir(launcher_cwd);
			if (sub) {
				struct dirent *sent;
				while ((sent = readdir(sub)) != NULL) {
					if (sent->d_name[0] == '.') continue;
					if (launcher_is_system_dir(sent->d_name)) continue;
					char spath[LAUNCHER_PATH_MAX];
					launcher_join(spath, sizeof(spath), launcher_cwd, sent->d_name);
					if (launcher_is_dir(spath)) subdirs++;
				}
				closedir(sub);
			}
		}
		const bool container = subdirs > 0;
		if ((has_run || has_images) && !container &&
		    launcher_count < LAUNCHER_MAX_ENTRIES) {
			launcher_entries[launcher_count].kind = ENTRY_START;
			launcher_entries[launcher_count].playable = true;
			snprintf(launcher_entries[launcher_count].starts,
			         sizeof(launcher_entries[0].starts), "%d images", launcher_image_count);
			snprintf(launcher_entries[launcher_count].name,
			         sizeof(launcher_entries[0].name), ">>> PLAY THIS FOLDER");
			launcher_count++;
		}
	}

	qsort(launcher_entries, (size_t)launcher_count,
	      sizeof(launcher_entries[0]), launcher_compare);
}

static const char *launcher_find_file(const char *wanted) {
	for (int i = 0; i < launcher_file_count; i++) {
		if (strcasecmp(launcher_files[i].name, wanted) == 0) return launcher_files[i].name;
	}
	return NULL;
}

static void launcher_basename(char *out, size_t out_size, const char *name) {
	if (!out || !out_size) return;
	const char *dot = name ? strrchr(name, '.') : NULL;
	size_t len = (dot && dot != name) ? (size_t)(dot - name) : (name ? strlen(name) : 0);
	if (len >= out_size) len = out_size - 1;
	if (len > 0) memcpy(out, name, len);
	out[len] = '\0';
}

struct LauncherSetting {
	const char *label;
	const char *section;
	const char *key;
	const char *const *values;
	int count;
	int index;
};

static const char *const setting_machine[] = {
	"hercules", "cga", "tandy", "pcjr", "ega", "vgaonly",
	"svga_s3", "svga_et3000", "svga_et4000", "svga_paradise",
	"vesa_nolfb", "vesa_oldvbe"
};
static const char *const setting_memsize[] = { "4", "8", "16", "32", "63" };
static const char *const setting_cputype[] = {
	"auto", "386", "386_slow", "386_prefetch", "486_slow",
	"pentium_slow", "pentium_mmx_slow"
};
static const char *const setting_core[] = { "auto", "normal", "simple" };
static const char *const setting_sbtype[] = {
	"none", "sb1", "sb2", "sbpro1", "sbpro2", "sb16", "gb"
};
static const char *const setting_oplmode[] = {
	"auto", "none", "cms", "opl2", "dualopl2", "opl3", "opl3gold"
};
static const char *const setting_onoff[] = { "on", "off" };
static const char *const setting_tandy[] = { "auto", "on", "off" };
static const char *const setting_scaler[] = {
	"none", "normal2x", "normal3x",
	"advmame2x", "advmame3x", "advinterp2x", "advinterp3x",
	"hq2x", "hq3x", "2xsai", "super2xsai", "supereagle",
	"tv2x", "tv3x", "rgb2x", "rgb3x", "scan2x", "scan3x"
};
static const char *const setting_ems[] = { "true", "emsboard", "emm386", "false" };

static struct LauncherSetting launcher_settings[] = {
	{ "Machine",           "dosbox",   "machine",   setting_machine, 12,  6 },
	{ "Memory (MB)",       "dosbox",   "memsize",   setting_memsize,  5,  2 },
	{ "CPU type",          "cpu",      "cputype",   setting_cputype,  7,  0 },
	{ "CPU core",          "cpu",      "core",      setting_core,     3,  0 },
	{ "Sound card",        "sblaster", "sbtype",    setting_sbtype,   7,  5 },
	{ "FM synthesis",      "sblaster", "oplmode",   setting_oplmode,  7,  0 },
	{ "PC speaker",        "speaker",  "pcspeaker", setting_onoff,    2,  0 },
	{ "Tandy sound",       "speaker",  "tandy",     setting_tandy,    3,  0 },
	{ "Disney sound",      "speaker",  "disney",    setting_onoff,    2,  0 },
	{ "Gravis Ultrasound", "gus",      "gus",       setting_onoff,    2,  1 },
	{ "Scaler",            "render",   "scaler",    setting_scaler,  18,  1 },
	{ "EMS memory",        "dos",      "ems",       setting_ems,      4,  0 },
	{ "XMS memory",        "dos",      "xms",       setting_onoff,    2,  0 },
	{ "UMB memory",        "dos",      "umb",       setting_onoff,    2,  0 },
};
#define LAUNCHER_SETTING_COUNT ((int)(sizeof(launcher_settings) / sizeof(launcher_settings[0])))

#define LAUNCHER_SETTINGS_FILE "sdmc:/dosbox/settings.conf"

static const char *launcher_setting_value(const struct LauncherSetting *setting) {
	if (setting->index < 0 || setting->index >= setting->count) return "";
	return setting->values[setting->index];
}

static void launcher_settings_load(void) {
	FILE *f = fopen(LAUNCHER_SETTINGS_FILE, "r");
	if (!f) return;
	char line[256];
	char section[64] = "";
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = '\0';
				snprintf(section, sizeof(section), "%s", p + 1);
			}
			continue;
		}
		char *eq = strchr(p, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = p;
		char *value = eq + 1;
		size_t klen = strlen(key);
		while (klen && (key[klen - 1] == ' ' || key[klen - 1] == '\t')) key[--klen] = '\0';
		while (*value == ' ' || *value == '\t') value++;
		size_t vlen = strlen(value);
		while (vlen && (value[vlen - 1] == '\n' || value[vlen - 1] == '\r' ||
		                value[vlen - 1] == ' ' || value[vlen - 1] == '\t'))
			value[--vlen] = '\0';
		for (int i = 0; i < LAUNCHER_SETTING_COUNT; i++) {
			struct LauncherSetting *setting = &launcher_settings[i];
			if (strcasecmp(setting->section, section) != 0 ||
			    strcasecmp(setting->key, key) != 0)
				continue;
			for (int v = 0; v < setting->count; v++) {
				if (strcasecmp(setting->values[v], value) == 0) {
					setting->index = v;
					break;
				}
			}
		}
	}
	fclose(f);
}

static void launcher_settings_save(void) {
	FILE *f = fopen(LAUNCHER_SETTINGS_FILE, "w");
	if (!f) {
		extern void SwitchPlatform_Trace(const char *step);
		SwitchPlatform_Trace("OSD: settings.conf not writable, change not saved");
		return;
	}
	fputs("# Written by the SETTINGS tab of the launcher. Safe to delete: without\n"
	      "# this file the emulator starts from its own defaults, and a hand edited\n"
	      "# dosbox.conf is parsed before this one and kept.\n", f);
	const char *section = NULL;
	for (int i = 0; i < LAUNCHER_SETTING_COUNT; i++) {
		struct LauncherSetting *setting = &launcher_settings[i];
		if (!section || strcmp(section, setting->section) != 0) {
			section = setting->section;
			fprintf(f, "\n[%s]\n", section);
		}
		fprintf(f, "%s=%s\n", setting->key, launcher_setting_value(setting));
	}
	fclose(f);
}

static void launcher_tab_reset(void) {
	memset(launcher_entries, 0, sizeof(launcher_entries));
	launcher_count = 0;
	launcher_image_count = 0;
	launcher_file_count = 0;
	launcher_boot_label[0] = '\0';
	launcher_boot_subfolder[0] = '\0';
	launcher_boot_cmds[0] = '\0';
}

static void launcher_tab_header(const char *title, int found) {
	if (launcher_count >= LAUNCHER_MAX_ENTRIES) return;
	struct LauncherEntry *entry = &launcher_entries[launcher_count++];
	memset(entry, 0, sizeof(*entry));
	entry->kind = ENTRY_HEADER;
	snprintf(entry->name, sizeof(entry->name), "%s   %d FOUND", title, found);
}

static void launcher_build_games(void) {
	launcher_tab_reset();
	launcher_tab_header("GAMES", launcher_library_count);
	if (launcher_count < LAUNCHER_MAX_ENTRIES) {
		struct LauncherEntry *classic = &launcher_entries[launcher_count++];
		memset(classic, 0, sizeof(*classic));
		classic->kind = ENTRY_CLASSIC;
		classic->playable = true;
		snprintf(classic->name, sizeof(classic->name), "Classic DOSBox - SD card on C:");
	}
	for (int i = 0; i < launcher_library_count && launcher_count < LAUNCHER_MAX_ENTRIES; i++) {
		struct LauncherEntry *entry = &launcher_entries[launcher_count++];
		memset(entry, 0, sizeof(*entry));
		entry->kind = ENTRY_LIBRARY;
		entry->playable = true;
		entry->path = launcher_library[i].path;
		snprintf(entry->name, sizeof(entry->name), "%s", launcher_library[i].name);
		snprintf(entry->starts, sizeof(entry->starts), "%s", launcher_library[i].starts);
	}
}

static void launcher_build_disks(void) {
	launcher_tab_reset();
	launcher_tab_header("DISK IMAGES", launcher_disk_count);
	for (int i = 0; i < launcher_disk_count && launcher_count < LAUNCHER_MAX_ENTRIES; i++) {
		struct LauncherEntry *entry = &launcher_entries[launcher_count++];
		memset(entry, 0, sizeof(*entry));
		entry->kind = ENTRY_IMAGE;
		entry->playable = true;
		entry->path = launcher_disks[i].path;
		snprintf(entry->name, sizeof(entry->name), "%s", launcher_disks[i].name);
		snprintf(entry->starts, sizeof(entry->starts), "disk image");
	}
}

static void launcher_build_programs(void) {
	launcher_tab_reset();
	launcher_tab_header("PROGRAMS", launcher_program_count);
	for (int i = 0; i < launcher_program_count && launcher_count < LAUNCHER_MAX_ENTRIES; i++) {
		struct LauncherEntry *entry = &launcher_entries[launcher_count++];
		memset(entry, 0, sizeof(*entry));
		entry->kind = ENTRY_PROGRAM;
		entry->playable = true;
		entry->path = launcher_programs[i].path;
		snprintf(entry->name, sizeof(entry->name), "%s", launcher_programs[i].name);
		snprintf(entry->starts, sizeof(entry->starts), "%s", launcher_programs[i].path);
	}
}

static void launcher_build_settings(void) {
	launcher_tab_reset();
	launcher_tab_header("EMULATOR SETTINGS", LAUNCHER_SETTING_COUNT);
	for (int i = 0; i < LAUNCHER_SETTING_COUNT && launcher_count < LAUNCHER_MAX_ENTRIES; i++) {
		struct LauncherEntry *entry = &launcher_entries[launcher_count++];
		memset(entry, 0, sizeof(*entry));
		entry->kind = ENTRY_SETTING;
		entry->playable = true;
		entry->setting = i;
		snprintf(entry->name, sizeof(entry->name), "%s", launcher_settings[i].label);
		snprintf(entry->starts, sizeof(entry->starts), "%s",
		         launcher_setting_value(&launcher_settings[i]));
	}
}

static void launcher_build_tab(int tab) {
	switch (tab) {
	case TAB_GAMES:    launcher_build_games(); break;
	case TAB_DISKS:    launcher_build_disks(); break;
	case TAB_PROGRAMS: launcher_build_programs(); break;
	case TAB_SETTINGS: launcher_build_settings(); break;
	default:           break;
	}
}

struct FatEntry {
	char name[13];
	bool directory;
};

static bool fat_read_root(const char *path, struct FatEntry *out, int max) {
	FILE *f = fopen(path, "rb");
	if (!f) return false;

	unsigned char bpb[512];
	unsigned reserved = 0, fat_count = 0, root_entries = 0, fat_sectors = 0;
	unsigned long root_start = 0;
	int found = 0;
	unsigned char dir[32];

	if (fread(bpb, 1, sizeof(bpb), f) == sizeof(bpb) &&
	    bpb[510] == 0x55 && bpb[511] == 0xAA &&
	    (bpb[11] | (bpb[12] << 8)) == 512 && bpb[13]) {
		reserved     = bpb[14] | (bpb[15] << 8);
		fat_count    = bpb[16];
		root_entries = bpb[17] | (bpb[18] << 8);
		fat_sectors  = bpb[22] | (bpb[23] << 8);
		if (fat_count && root_entries)
			root_start = (unsigned long)(reserved + fat_count * fat_sectors) * 512u;
	}

	if (root_start && fseek(f, (long)root_start, SEEK_SET) == 0) {
		for (unsigned i = 0; i < root_entries; i++) {
			if (fread(dir, 1, sizeof(dir), f) != sizeof(dir)) break;
			if (dir[0] == 0x00) break;
			if (dir[0] == 0xE5) continue;
			const unsigned attr = dir[11];
			if (attr & 0x08) continue;
			if ((attr & 0x0F) == 0x0F) continue;
			if (found >= max) break;

			struct FatEntry *e = &out[found++];
			int len = 0;
			for (int c = 0; c < 8 && dir[c] != ' '; c++)
				e->name[len++] = (char)dir[c];
			if (dir[8] != ' ') {
				e->name[len++] = '.';
				for (int c = 8; c < 11 && dir[c] != ' '; c++)
					e->name[len++] = (char)dir[c];
			}
			e->name[len] = '\0';
			e->directory = (attr & 0x10) != 0;
		}
	}

	fclose(f);
	return found > 0;
}

static const char *fat_find_name(const struct FatEntry *entries, int count,
                                 const char *const *wanted) {
	for (int w = 0; wanted[w]; w++) {
		for (int i = 0; i < count; i++) {
			if (strcasecmp(entries[i].name, wanted[w]) == 0) return entries[i].name;
		}
	}
	return NULL;
}

static bool launcher_boot_from_disk(char *out, size_t out_size, char *label, size_t label_size) {
	struct FatEntry entries[64];
	char path[LAUNCHER_PATH_MAX];

	const struct LauncherEntry *first = NULL;
	for (int i = 0; i < launcher_count; i++) {
		if (launcher_entries[i].kind != ENTRY_IMAGE) continue;
		first = &launcher_entries[i];
		break;
	}
	if (!first) return false;
	launcher_join(path, sizeof(path), launcher_cwd, first->name);

	if (!fat_read_root(path, entries, (int)(sizeof(entries) / sizeof(entries[0])))) {
		snprintf(label, label_size, "disk 1 unreadable");
		return false;
	}

	static const char *const installers[] = {
		"install.exe", "setup.exe", "install.com", "setup.com",
		"install.bat", "setup.bat", NULL
	};
	const char *found = fat_find_name(entries,
	                                 (int)(sizeof(entries) / sizeof(entries[0])),
	                                 installers);
	char base[64];
	if (found) {
		launcher_basename(base, sizeof(base), found);
		snprintf(out, out_size, "a:\n%.32s c:", base);
		snprintf(label, label_size, "%.32s on disk 1", found);
		return true;
	}

	const struct FatEntry *only = NULL;
	int runnable = 0, total = 0;
	for (int i = 0; i < (int)(sizeof(entries) / sizeof(entries[0])); i++) {
		if (entries[i].directory || !entries[i].name[0]) continue;
		total++;
		if (!launcher_is_runnable(entries[i].name)) continue;
		only = &entries[i];
		runnable++;
	}
	if (runnable == 1 && total <= 4 && only) {
		launcher_basename(base, sizeof(base), only->name);
		snprintf(out, out_size, "a:\n%.32s c:", base);
		snprintf(label, label_size, "%.32s on disk 1", only->name);
		return true;
	}

	snprintf(label, label_size, "%d files on disk 1", total);
	return false;
}

static bool launcher_images_are_floppy(void) {
	if (launcher_media_type != MEDIA_NONE) {
		return launcher_media_type == MEDIA_FLOPPY;
	}
	int floppy = 0, other = 0;
	for (int i = 0; i < launcher_count; i++) {
		if (launcher_entries[i].kind != ENTRY_IMAGE) continue;
		char path[LAUNCHER_PATH_MAX];
		launcher_join(path, sizeof(path), launcher_cwd, launcher_entries[i].name);
		if (launcher_guess_media(path) == MEDIA_FLOPPY) floppy++;
		else other++;
	}
	if (floppy == 0) return false;
	return floppy >= other;
}

static void launcher_find_boot(char *out, size_t out_size, bool have_images) {
	out[0] = '\0';
	launcher_boot_subfolder[0] = '\0';

	if (launcher_find_file("autoexec.bat")) {
		snprintf(launcher_boot_label, sizeof(launcher_boot_label), "autoexec.bat");
		return;
	}

	const char *text = launcher_find_file("start.txt");
	if (!text) text = launcher_find_file("dosbox-start.txt");
	if (text) {
		char path[LAUNCHER_PATH_MAX];
		launcher_join(path, sizeof(path), launcher_cwd, text);
		FILE *f = fopen(path, "r");
		if (f) {
			char line[128];
			size_t used = 0;
			int taken = 0;
			while (taken < 4 && fgets(line, sizeof(line), f)) {
				char *nl = strpbrk(line, "\r\n");
				if (nl) *nl = '\0';
				if (!line[0]) continue;
				int n = snprintf(out + used, out_size - used, "%s%s",
				                 taken ? "\n" : "", line);
				if (n < 0 || (size_t)n >= out_size - used) break;
				used += (size_t)n;
				taken++;
			}
			fclose(f);
			if (taken) {
				snprintf(launcher_boot_label, sizeof(launcher_boot_label), "%.250s", text);
				return;
			}
		}
		out[0] = '\0';
	}

	const char *bat = launcher_find_file("start.bat");
	if (!bat) bat = launcher_find_file("dosbox-start.bat");
	if (bat) {
		snprintf(out, out_size, "%.250s", bat);
		snprintf(launcher_boot_label, sizeof(launcher_boot_label), "%.250s", bat);
		return;
	}

	static const char *const installers[] = {
		"install.exe", "setup.exe", "install.com", "setup.com",
		"install.bat", "setup.bat", NULL
	};
	for (int i = 0; installers[i]; i++) {
		const char *found = launcher_find_file(installers[i]);
		if (!found) continue;
		if (have_images) {
			char base[256];
			launcher_basename(base, sizeof(base), found);
			if (launcher_images_are_floppy())
				snprintf(out, out_size, "a:\n%.240s c:", base);
			else
				snprintf(out, out_size, "d:\n%.240s c:", base);
		} else {
			snprintf(out, out_size, "%.240s c:", found);
		}
		snprintf(launcher_boot_label, sizeof(launcher_boot_label), "%.250s", found);
		return;
	}

	const struct LauncherFile *only = NULL;
	int runnable = 0;
	for (int i = 0; i < launcher_file_count; i++) {
		if (!launcher_files[i].runnable) continue;
		only = &launcher_files[i];
		runnable++;
	}
	if (runnable == 1 && only) {
		if (have_images) {
			char base[256];
			launcher_basename(base, sizeof(base), only->name);
			if (launcher_images_are_floppy())
				snprintf(out, out_size, "a:\n%.250s c:", base);
			else
				snprintf(out, out_size, "d:\n%.250s c:", base);
		} else {
			snprintf(out, out_size, "%.250s", only->name);
		}
		snprintf(launcher_boot_label, sizeof(launcher_boot_label), "%.250s", only->name);
		return;
	}

	DIR *sub_dir = opendir(launcher_cwd);
	if (sub_dir) {
		struct dirent *subent;
		while ((subent = readdir(sub_dir)) != NULL) {
			if (subent->d_name[0] == '.') continue;
			if (launcher_is_system_dir(subent->d_name)) continue;
			char subpath[LAUNCHER_PATH_MAX];
			launcher_join(subpath, sizeof(subpath), launcher_cwd, subent->d_name);
			if (!launcher_is_dir(subpath)) continue;
			DIR *inner = opendir(subpath);
			if (!inner) continue;
			struct dirent *ient;
			char found_exe[256] = "";
			int inner_run = 0;
			while ((ient = readdir(inner)) != NULL) {
				if (ient->d_name[0] == '.') continue;
				if (strcasecmp(ient->d_name, "autoexec.bat") == 0 ||
				    strcasecmp(ient->d_name, "start.bat") == 0) {
					snprintf(found_exe, sizeof(found_exe), "%s", ient->d_name);
					inner_run = 1;
					break;
				}
				if (launcher_is_runnable(ient->d_name)) {
					inner_run++;
					if (inner_run == 1)
						snprintf(found_exe, sizeof(found_exe), "%s", ient->d_name);
				}
			}
			closedir(inner);
			if (inner_run == 1 && found_exe[0]) {
				snprintf(launcher_boot_subfolder, sizeof(launcher_boot_subfolder), "%.250s", subent->d_name);
				snprintf(out, out_size, "%.250s", found_exe);
				snprintf(launcher_boot_label, sizeof(launcher_boot_label), "%.64s/%.64s", subent->d_name, found_exe);
				closedir(sub_dir);
				return;
			}
		}
		closedir(sub_dir);
	}

	if (have_images && launcher_images_are_floppy() &&
	    launcher_boot_from_disk(out, out_size, launcher_boot_label, sizeof(launcher_boot_label)))
		return;

	snprintf(launcher_boot_label, sizeof(launcher_boot_label), "nothing found");
}

static void launcher_refresh_boot(void) {
	launcher_find_boot(launcher_boot_cmds, sizeof(launcher_boot_cmds),
	                   launcher_image_count > 0);
	const bool playable = launcher_boot_label[0] &&
	                      strcmp(launcher_boot_label, "nothing found") != 0;
	for (int i = 0; i < launcher_count; i++) {
		if (launcher_entries[i].kind != ENTRY_START) continue;
		launcher_entries[i].playable = playable;
		snprintf(launcher_entries[i].starts, sizeof(launcher_entries[0].starts),
		         "%s", launcher_boot_label);
		break;
	}
}

static void launcher_commit_run(const char *command) {
	launcher_image_list[0] = '\0';
	launcher_image_count = 0;
	launcher_media_type = MEDIA_NONE;
	launcher_boot_subfolder[0] = '\0';
	snprintf(launcher_boot_cmds, sizeof(launcher_boot_cmds), "%s", command);
	snprintf(launcher_boot_label, sizeof(launcher_boot_label), "%s", command);
	launcher_join(launcher_result, sizeof(launcher_result), launcher_cwd, NULL);
}

static void launcher_collect_images(const char *first_name) {
	launcher_image_list[0] = '\0';
	launcher_media_type = MEDIA_NONE;
	size_t used = 0;
	int taken = 0;
	int floppy = 0, iso = 0, hdd = 0;
	for (int i = 0; i < launcher_count; i++) {
		const struct LauncherEntry *entry = &launcher_entries[i];
		if (entry->kind != ENTRY_IMAGE) continue;
		if (first_name && strcasecmp(entry->name, first_name) != 0) continue;
		char path[LAUNCHER_PATH_MAX];
		launcher_join(path, sizeof(path), launcher_cwd, entry->name);
		enum LauncherMedia media = launcher_guess_media(path);
		if (media == MEDIA_FLOPPY) floppy++;
		else if (media == MEDIA_ISO) iso++;
		else if (media == MEDIA_HDD) hdd++;
		const int n = snprintf(launcher_image_list + used,
		                       sizeof(launcher_image_list) - used, "%s%s",
		                       taken ? "\n" : "", path);
		if (n < 0 || (size_t)n >= sizeof(launcher_image_list) - used) break;
		used += (size_t)n;
		taken++;
	}
	if (iso >= floppy && iso >= hdd && iso > 0) launcher_media_type = MEDIA_ISO;
	else if (floppy >= hdd && floppy > 0) launcher_media_type = MEDIA_FLOPPY;
	else if (hdd > 0) launcher_media_type = MEDIA_HDD;
	else if (taken > 0) launcher_media_type = MEDIA_FLOPPY;
}

static bool launcher_scan_queue_empty(void) {
	return launcher_scan_head == launcher_scan_tail;
}

static void launcher_scan_push(const char *path, int depth, int root, bool files_only) {
	const int next = (launcher_scan_tail + 1) % LAUNCHER_SCAN_QUEUE;
	if (next == launcher_scan_head) return;
	snprintf(launcher_scan_queue[launcher_scan_tail].path,
	         sizeof(launcher_scan_queue[0].path), "%s", path);
	launcher_scan_queue[launcher_scan_tail].depth = depth;
	launcher_scan_queue[launcher_scan_tail].root = root;
	launcher_scan_queue[launcher_scan_tail].files_only = files_only;
	launcher_scan_tail = next;
}

static bool launcher_scan_pop(char *path, size_t path_size, int *depth, int *root,
                              bool *files_only) {
	if (launcher_scan_queue_empty()) return false;
	const struct LauncherScanItem *item = &launcher_scan_queue[launcher_scan_head];
	snprintf(path, path_size, "%s", item->path);
	*depth = item->depth;
	*root = item->root;
	*files_only = item->files_only;
	launcher_scan_head = (launcher_scan_head + 1) % LAUNCHER_SCAN_QUEUE;
	return true;
}

static void launcher_library_add(int root, const char *path, const char *starts) {
	if (launcher_library_count >= LAUNCHER_MAX_LIBRARY) return;
	struct LauncherGame *game = &launcher_library[launcher_library_count];
	const char *label = path;
	const size_t root_len = strlen(launcher_library_roots[root]);
	if (strncmp(path, launcher_library_roots[root], root_len) == 0) {
		label = path + root_len;
		while (*label == '/') label++;
	}
	if (!*label) label = path;
	snprintf(game->name, sizeof(game->name), "%.191s", label);
	snprintf(game->path, sizeof(game->path), "%s", path);
	snprintf(game->starts, sizeof(game->starts), "%s",
	         starts && starts[0] ? starts : "-");
	launcher_library_count++;
}

static int launcher_library_compare(const void *a, const void *b) {
	const struct LauncherGame *ga = (const struct LauncherGame *)a;
	const struct LauncherGame *gb = (const struct LauncherGame *)b;
	return strcasecmp(ga->name, gb->name);
}

static void launcher_item_add(struct LauncherItem *list, int *count,
                              const char *name, const char *folder,
                              const char *starts) {
	if (*count >= LAUNCHER_MAX_ITEMS) return;
	struct LauncherItem *item = &list[(*count)++];
	snprintf(item->name, sizeof(item->name), "%s", name);
	snprintf(item->path, sizeof(item->path), "%s", folder);
	snprintf(item->starts, sizeof(item->starts), "%s", starts ? starts : "-");
}

static int launcher_item_compare(const void *a, const void *b) {
	const struct LauncherItem *ia = (const struct LauncherItem *)a;
	const struct LauncherItem *ib = (const struct LauncherItem *)b;
	const int folders = strcasecmp(ia->path, ib->path);
	if (folders != 0) return folders;
	return strcasecmp(ia->name, ib->name);
}

static bool launcher_scan_step(void) {
	if (!launcher_scan_active) return false;
	if (launcher_scan_seen >= LAUNCHER_SCAN_BUDGET) {
		launcher_scan_active = false;
		return false;
	}

	if (!launcher_scan_dir) {
		char path[LAUNCHER_PATH_MAX];
		int depth = 0, root = 0;
		bool files_only = false;
		if (!launcher_scan_pop(path, sizeof(path), &depth, &root, &files_only)) {
			launcher_scan_active = false;
			return false;
		}
		launcher_scan_dir = opendir(path);
		if (!launcher_scan_dir) return true;
		snprintf(launcher_scan_cwd, sizeof(launcher_scan_cwd), "%s", path);
		launcher_scan_depth = depth;
		launcher_scan_root = root;
		launcher_scan_files_only = files_only;
		launcher_scan_folders++;
		return true;
	}

	struct dirent *ent = readdir(launcher_scan_dir);
	if (!ent) {
		closedir(launcher_scan_dir);
		launcher_scan_dir = NULL;
		return true;
	}
	launcher_scan_seen++;
	if (ent->d_name[0] == '.') return true;

	char path[LAUNCHER_PATH_MAX];
	launcher_join(path, sizeof(path), launcher_scan_cwd, ent->d_name);
	if (!launcher_is_dir(path)) {
		if (launcher_is_image(ent->d_name))
			launcher_item_add(launcher_disks, &launcher_disk_count,
			                  ent->d_name, launcher_scan_cwd, NULL);
		if (launcher_scan_files_only && launcher_is_runnable(ent->d_name))
			launcher_item_add(launcher_programs, &launcher_program_count,
			                  ent->d_name, launcher_scan_cwd, NULL);
		return true;
	}
	if (launcher_scan_files_only) return true;
	if (launcher_is_system_dir(ent->d_name)) return true;

	char starts[96] = "";
	if (launcher_probe_path(path, starts, sizeof(starts), false)) {
		launcher_library_add(launcher_scan_root, path, starts);
		launcher_scan_push(path, launcher_scan_depth, launcher_scan_root, true);
	} else if (launcher_scan_depth + 1 < LAUNCHER_SCAN_DEPTH) {
		launcher_scan_push(path, launcher_scan_depth + 1, launcher_scan_root, false);
	}
	return true;
}

static void launcher_scan_draw(struct OSDCanvas *canvas, bool done);

static bool launcher_scan_run(void) {
	launcher_library_count = 0;
	launcher_disk_count = 0;
	launcher_program_count = 0;
	if (launcher_scan_dir) {
		closedir(launcher_scan_dir);
		launcher_scan_dir = NULL;
	}
	launcher_scan_head = launcher_scan_tail = 0;
	launcher_scan_seen = 0;
	launcher_scan_folders = 0;
	launcher_scan_skipped = false;
	launcher_scan_cwd[0] = '\0';
	for (int i = 0; i < LAUNCHER_LIBRARY_ROOT_COUNT; i++) {
		if (launcher_is_dir(launcher_library_roots[i]))
			launcher_scan_push(launcher_library_roots[i], 0, i, false);
	}
	launcher_scan_active = !launcher_scan_queue_empty();

	bool quit = false;
	while (launcher_scan_active && !quit) {
		for (int i = 0; i < LAUNCHER_SCAN_STEP && launcher_scan_active; i++)
			launcher_scan_step();

		struct OSDPad pad = {};
		if (OSD_ReadPad(&pad)) {
			if (pad.b) {
				launcher_scan_skipped = true;
				launcher_scan_active = false;
			}
			if (pad.y) quit = true;
		}

		struct OSDCanvas c;
		if (OSD_BeginFrame(&c)) {
			launcher_scan_draw(&c, false);
			OSD_EndFrame();
		}
		OSD_FrameLimit();
	}
	if (launcher_scan_dir) {
		closedir(launcher_scan_dir);
		launcher_scan_dir = NULL;
	}
	if (quit) return false;

	qsort(launcher_library, (size_t)launcher_library_count,
	      sizeof(launcher_library[0]), launcher_library_compare);
	qsort(launcher_disks, (size_t)launcher_disk_count,
	      sizeof(launcher_disks[0]), launcher_item_compare);
	qsort(launcher_programs, (size_t)launcher_program_count,
	      sizeof(launcher_programs[0]), launcher_item_compare);
	const Bit32u hold = Platform_GetTicks();
	while ((int)(Platform_GetTicks() - hold) < 450) {
		struct OSDCanvas c;
		if (OSD_BeginFrame(&c)) {
			launcher_scan_draw(&c, true);
			OSD_EndFrame();
		}
		OSD_FrameLimit();
	}

	if (launcher_scan_skipped) {
		struct OSDPad pad = {};
		for (int i = 0; i < 600 && OSD_ReadPad(&pad) && pad.b; i++)
			OSD_FrameLimit();
	}

	{
		extern void SwitchPlatform_Trace(const char *step);
		char msg[160];
		snprintf(msg, sizeof(msg),
		         "OSD: scan found %d games, %d disks, %d programs (%d folders, %d entries%s)",
		         launcher_library_count, launcher_disk_count, launcher_program_count,
		         launcher_scan_folders, launcher_scan_seen,
		         launcher_scan_skipped ? ", skipped" : "");
		SwitchPlatform_Trace(msg);
	}
	return true;
}

static void launcher_draw_icon_play(struct OSDCanvas *c, int x, int y, uint32_t colour) {
	for (int row = 0; row < 18; row++) {
		int inset = (row < 9) ? (9 - row) : (row - 8);
		int w = 14 - inset;
		if (w > 0) OSD_FillRect(c, x + inset / 2, y + row, w, 1, colour);
	}
}

static void launcher_draw_icon_folder(struct OSDCanvas *c, int x, int y, uint32_t colour) {
	OSD_FillRect(c, x, y + 4, 22, 14, colour);
	OSD_FillRect(c, x, y, 10, 5, colour);
}

static void launcher_draw_icon_disk(struct OSDCanvas *c, int x, int y, uint32_t colour) {
	OSD_FillRect(c, x + 2, y, 18, 18, colour);
	OSD_FillRect(c, x + 8, y + 6, 6, 6, OSD_Colour(0x0d, 0x13, 0x26));
}

static void launcher_fit_text(char *out, size_t out_size, const char *text,
                              int scale, int max_w) {
	if (!out || !out_size) return;
	if (!text) { out[0] = '\0'; return; }
	snprintf(out, out_size, "%s", text);
	const size_t full = strlen(out);
	if (max_w <= 0) return;
	while (strlen(out) > 4 && OSD_TextWidth(out, scale) > max_w) {
		out[strlen(out) - 1] = '\0';
	}
	if (strlen(out) < full && strlen(out) > 3) {
		out[strlen(out) - 3] = '\0';
		strcat(out, "...");
	}
}

static void launcher_draw_background(struct OSDCanvas *c) {
	OSD_GradientV(c, 0, 0, c->width, c->height,
	              OSD_Colour(0x08, 0x0c, 0x18), OSD_Colour(0x02, 0x04, 0x0a));

	OSD_BlendRect(c, 0, 0, c->width / 2, c->height / 2,
	              OSD_Colour(0xff, 0x9b, 0x3d), 18);
	for (int y = 0; y < c->height; y += 3)
		OSD_FillRect(c, 0, y, c->width, 1, OSD_Colour(0x00, 0x00, 0x00));

	for (int i = 0; i < 48; i++) {
		int x = c->width - 220 + i * 4;
		OSD_BlendRect(c, x, 0, 2, c->height, OSD_Colour(0xff, 0x9b, 0x3d), 12);
	}
}

static void launcher_draw_title(struct OSDCanvas *c, const char *subtitle) {
	const int w = c->width;
	OSD_GradientH(c, 0, 0, w, 88,
	              OSD_Colour(0x14, 0x1c, 0x34), OSD_Colour(0x08, 0x0c, 0x18));
	OSD_FillRect(c, 0, 86, w, 3, OSD_Colour(0xff, 0x9b, 0x3d));
	OSD_BlendRect(c, 0, 89, w, 8, OSD_Colour(0xff, 0x9b, 0x3d), 40);

	OSD_Text(c, 32, 16, "NX-DOSBox 1.00", OSD_Colour(0xff, 0x9b, 0x3d), 3, false);
	const int title_w = OSD_TextWidth("NX-DOSBox 1.00", 3);
	OSD_Text(c, 32 + title_w + 18, 22,
	         "GAME LIBRARY", OSD_Colour(0xe8, 0xee, 0xf8), 2, false);
	OSD_Text(c, 32, 58, subtitle ? subtitle : "DOSBox SVN for Nintendo Switch by TheheroGAC",
	         OSD_Colour(0x80, 0x90, 0xad), 1, false);

	OSD_DrawStatusReadout(c, w - 32, 22);

	struct OSD_FTPServer server;
	launcher_ftp_state(&server);
	if (server.available && server.running) {
		char line[160];
		if (server.address[0])
			snprintf(line, sizeof(line), "FTP ftp://%s:%d", server.address, server.port);
		else
			snprintf(line, sizeof(line), "FTP waiting for a network");
		if (server.clients > 0) {
			char more[36];
			snprintf(more, sizeof(more), "  (%d connected)", server.clients);
			strncat(line, more, sizeof(line) - strlen(line) - 1);
		}
		OSD_TextRight(c, w - 32, 54, line, OSD_Colour(0x7d, 0xd8, 0x9a), 2, false);
	}
}

static void launcher_scan_draw(struct OSDCanvas *c, bool done) {
	launcher_draw_background(c);
	launcher_draw_title(c, done ? "Library ready" : "Looking for games on the card...");

	OSD_Panel(c, 24, 104, c->width - 48, 40,
	          OSD_Colour(0x0d, 0x13, 0x26), OSD_Colour(0x2a, 0x3a, 0x66), 230);
	OSD_Text(c, 36, 112, "SEARCHING", OSD_Colour(0xff, 0x9b, 0x3d), 1, false);
	char roots[160];
	snprintf(roots, sizeof(roots), "%s and %s",
	         launcher_library_roots[0], launcher_library_roots[1]);
	OSD_Text(c, 36, 124, roots, OSD_Colour(0x80, 0x90, 0xad), 1, false);

	const int bar_x = 80;
	const int bar_w = c->width - 160;
	const int bar_h = 30;
	const int bar_y = c->height / 2 - bar_h / 2;

	int percent = done ? 100 : (launcher_scan_seen * 100) / LAUNCHER_SCAN_BUDGET;
	if (percent > 100) percent = 100;

	if (!done)
		OSD_TextCentre(c, c->width / 2, bar_y - 54, "Scanning in progress...",
		               OSD_Colour(0xff, 0x9b, 0x3d), 2, false);

	OSD_Panel(c, bar_x - 10, bar_y - 10, bar_w + 20, bar_h + 20,
	          OSD_Colour(0x0a, 0x10, 0x20), OSD_Colour(0x2a, 0x3a, 0x66), 240);
	OSD_FillRect(c, bar_x, bar_y, bar_w, bar_h, OSD_Colour(0x05, 0x08, 0x12));

	const int fill = (bar_w - 6) * percent / 100;
	if (fill > 0)
		OSD_GradientH(c, bar_x + 3, bar_y + 3, fill, bar_h - 6,
		              OSD_Colour(0xff, 0xc8, 0x7a), OSD_Colour(0xd8, 0x6a, 0x12));

	if (!done) {
		const int sweep_w = 60;
		const int sweep = (int)((Platform_GetTicks() / 6) % (bar_w + sweep_w)) - sweep_w;
		int sx = bar_x + sweep;
		int sw = sweep_w;
		if (sx < bar_x) { sw -= bar_x - sx; sx = bar_x; }
		if (sx + sw > bar_x + bar_w) sw = bar_x + bar_w - sx;
		if (sw > 0)
			OSD_BlendRect(c, sx, bar_y + 3, sw, bar_h - 6, OSD_Colour(0xff, 0xff, 0xff), 45);
	}
	OSD_DrawRect(c, bar_x, bar_y, bar_w, bar_h, OSD_Colour(0x3d, 0x5a, 0x9e));

	char line[64];
	snprintf(line, sizeof(line), "%d%%", percent);
	OSD_TextCentre(c, c->width / 2, bar_y + (bar_h - 16) / 2, line,
	               OSD_Colour(0xe8, 0xee, 0xf8), 2, false);

	char status[192];
	snprintf(status, sizeof(status), "%d game%s found  ·  %d folders",
	         launcher_library_count, launcher_library_count == 1 ? "" : "s",
	         launcher_scan_folders);
	OSD_TextCentre(c, c->width / 2, bar_y + bar_h + 26, status,
	               done ? OSD_Colour(0xff, 0x9b, 0x3d) : OSD_Colour(0x80, 0x90, 0xad), 2, false);

	if (!done) {
		char current[LAUNCHER_PATH_MAX];
		snprintf(current, sizeof(current), "%s", launcher_scan_cwd);
		while (OSD_TextWidth(current, 2) > c->width - 200 && current[0]) {
			char *slash = strchr(current + 1, '/');
			if (!slash) break;
			memmove(current, slash, strlen(slash) + 1);
		}
		OSD_TextCentre(c, c->width / 2, bar_y + bar_h + 56, current,
		               OSD_Colour(0xe8, 0xee, 0xf8), 1, false);
		OSD_TextCentre(c, c->width / 2, bar_y + bar_h + 82,
		               "B  skip scan        Y  quit", OSD_Colour(0x80, 0x90, 0xad), 2, false);
	}
}

static void launcher_draw_breadcrumb(struct OSDCanvas *c) {
	const struct LauncherEntry *selected =
		(launcher_selected >= 0 && launcher_selected < launcher_count)
			? &launcher_entries[launcher_selected] : NULL;
	const bool own_folder = selected && selected->path;
	char run[256] = "";
	const char *starts;
	if (selected && selected->kind == ENTRY_PROGRAM) {
		snprintf(run, sizeof(run), "%s", selected->name);
		starts = run;
	} else if (own_folder) {
		starts = selected->starts[0] ? selected->starts : "-";
	} else {
		starts = launcher_boot_label[0] ? launcher_boot_label : "-";
	}
	const bool starts_ok = own_folder
		? (starts[0] && strcmp(starts, "-") != 0)
		: (launcher_boot_label[0] && strcmp(launcher_boot_label, "nothing found") != 0);
	const char *caption = own_folder
		? (selected->kind == ENTRY_LIBRARY ? "GAME FOLDER" : "ON THE CARD")
		: "CARD PATH";

	char line[LAUNCHER_PATH_MAX + 16];
	snprintf(line, sizeof(line), "%s", own_folder ? selected->path : launcher_cwd);
	OSD_Panel(c, 24, 144, c->width - 48, 40,
	          OSD_Colour(0x0d, 0x13, 0x26), OSD_Colour(0xff, 0x9b, 0x3d), 230);
	OSD_Text(c, 36, 152, caption, OSD_Colour(0xff, 0x9b, 0x3d), 1, false);

	char start[sizeof(launcher_boot_label) + 16];
	snprintf(start, sizeof(start), "starts: %s", starts);

	const int panel_l = 36;
	const int panel_r = c->width - 36;
	const int gap = 24;
	const int label_room = (c->width - 72) / 2;
	char label[sizeof(start)];
	launcher_fit_text(label, sizeof(label), start, 2, label_room);

	const int start_w = OSD_TextWidth(label, 2);
	int max_line_w = panel_r - start_w - gap - panel_l;
	if (max_line_w < 80) max_line_w = 80;

	if (OSD_TextWidth(line, 2) > max_line_w) {
		while (line[0] && OSD_TextWidth(line, 2) > max_line_w) {
			char *slash = strchr(line + 1, '/');
			if (!slash) break;
			memmove(line, slash, strlen(slash) + 1);
		}
	}
	{
		char fitted[LAUNCHER_PATH_MAX + 64];
		launcher_fit_text(fitted, sizeof(fitted), line, 2, max_line_w);
		OSD_Text(c, panel_l, 164, fitted, OSD_Colour(0xe8, 0xee, 0xf8), 2, false);
	}

	OSD_TextRight(c, panel_r, 164, label,
	              starts_ok ? OSD_Colour(0xff, 0x9b, 0x3d)
	                        : OSD_Colour(0x80, 0x90, 0xad),
	              2, false);
}

static void launcher_draw_tabs(struct OSDCanvas *c) {
	const int y = 98;
	const int h = 40;
	const int tab_w = c->width / TAB_COUNT;
	for (int i = 0; i < TAB_COUNT; i++) {
		const int x = i * tab_w;
		const bool active = (i == launcher_tab);
		int count = -1;
		if (i == TAB_GAMES) count = launcher_library_count;
		else if (i == TAB_DISKS) count = launcher_disk_count;
		else if (i == TAB_PROGRAMS) count = launcher_program_count;

		char label[40];
		if (count >= 0)
			snprintf(label, sizeof(label), "%s %d", launcher_tab_names[i], count);
		else
			snprintf(label, sizeof(label), "%s", launcher_tab_names[i]);

		if (active) {
			OSD_Panel(c, x + 4, y, tab_w - 8, h,
			          OSD_Colour(0x24, 0x40, 0x7a), OSD_Colour(0x3d, 0x5a, 0x9e), 255);
			OSD_FillRect(c, x + 16, y + h - 5, tab_w - 32, 3,
			             OSD_Colour(0xff, 0x9b, 0x3d));
		} else {
			OSD_BlendRect(c, x + 4, y, tab_w - 8, h, OSD_Colour(0x14, 0x1c, 0x34), 150);
		}
		OSD_TextCentre(c, x + tab_w / 2, y + (h - 20) / 2, label,
		               active ? OSD_Colour(0xff, 0xf0, 0xd8)
		                       : OSD_Colour(0x80, 0x90, 0xad),
		               2, false);
	}
}

static void launcher_draw_list(struct OSDCanvas *c) {
	const int row_h = 56;
	const int list_x = 24;
	const int list_y = 192;
	const int list_w = c->width - 48;
	const int list_h = LAUNCHER_VISIBLE_ROWS * row_h + 16;
	const Bit32u ticks = Platform_GetTicks();
	const int pulse = (int)((ticks / 40) % 40);
	const int glow = pulse < 20 ? pulse : 40 - pulse;

	OSD_Panel(c, list_x, list_y, list_w, list_h,
	          OSD_Colour(0x0a, 0x10, 0x20), OSD_Colour(0x2a, 0x3a, 0x66), 240);

	if (launcher_count == 0) {
		const char *hint = "Nothing here. Put games in sdmc:/dosbox or sdmc:/switch/dosbox";
		if (launcher_tab == TAB_GAMES)
			hint = "No game folders found. Put them in sdmc:/dosbox, or browse with FOLDERS";
		else if (launcher_tab == TAB_DISKS)
			hint = "No disk images (.img .ima .iso .cue .vfd .dsk) found on the card";
		else if (launcher_tab == TAB_PROGRAMS)
			hint = "No programs found inside the game folders the scan recognised";
		OSD_Text(c, list_x + 24, list_y + 24, hint,
		         OSD_Colour(0x80, 0x90, 0xad), 2, false);
		return;
	}

	for (int row = 0; row < LAUNCHER_VISIBLE_ROWS; row++) {
		int index = launcher_scroll + row;
		if (index >= launcher_count) break;

		const struct LauncherEntry *entry = &launcher_entries[index];
		int y = list_y + 8 + row * row_h;
		bool selected = (index == launcher_selected);

		if (entry->kind == ENTRY_HEADER) {
			OSD_FillRect(c, list_x + 4, y + row_h / 2 + 6, list_w - 8, 2,
			             OSD_Colour(0x2a, 0x3a, 0x66));
			OSD_Text(c, list_x + 18, y + row_h / 2 - 12, entry->name,
			         OSD_Colour(0xff, 0x9b, 0x3d), 2, true);
			continue;
		}

		if (selected) {
			OSD_GradientH(c, list_x + 4, y, list_w - 8, row_h - 6,
			              OSD_Colour(0x2a, 0x48, 0x82), OSD_Colour(0x18, 0x28, 0x4c));
			OSD_FillRect(c, list_x + 4, y, 6, row_h - 6,
			             OSD_Colour(0xff, 0x9b, 0x3d));
			OSD_BlendRect(c, list_x + 10, y, list_w - 14, row_h - 6,
			              OSD_Colour(0xff, 0x9b, 0x3d), 20 + glow);
		} else if (row % 2 == 0) {
			OSD_BlendRect(c, list_x + 4, y, list_w - 8, row_h - 6,
			              OSD_Colour(0x14, 0x1c, 0x34), 90);
		}

		uint32_t colour = OSD_Colour(0xe8, 0xee, 0xf8);
		uint32_t icon = OSD_Colour(0x80, 0x90, 0xad);
		int scale = 2;
		int text_x = list_x + 52;

		if (entry->kind == ENTRY_START) {
			colour = OSD_Colour(0xff, 0x9b, 0x3d);
			icon = colour;
			scale = 3;
			launcher_draw_icon_play(c, list_x + 20, y + 14, icon);
		} else if (entry->kind == ENTRY_PROGRAM) {
			colour = OSD_Colour(0xff, 0x9b, 0x3d);
			icon = colour;
			launcher_draw_icon_play(c, list_x + 20, y + 14, icon);
		} else if (entry->kind == ENTRY_LIBRARY) {
			colour = OSD_Colour(0xff, 0xf0, 0xd8);
			icon = OSD_Colour(0xff, 0x9b, 0x3d);
			launcher_draw_icon_play(c, list_x + 20, y + 14, icon);
		} else if (entry->kind == ENTRY_CLASSIC) {
			colour = OSD_Colour(0x7d, 0xd8, 0x9a);
			icon = colour;
			launcher_draw_icon_disk(c, list_x + 18, y + 12, icon);
		} else if (entry->kind == ENTRY_PARENT) {
			colour = OSD_Colour(0x80, 0x90, 0xad);
			OSD_Text(c, list_x + 18, y + 16, "<<", colour, 2, false);
		} else if (entry->kind == ENTRY_IMAGE) {
			colour = OSD_Colour(0x9e, 0xc4, 0xff);
			icon = colour;
			launcher_draw_icon_disk(c, list_x + 18, y + 12, icon);
		} else if (entry->kind == ENTRY_DIRECTORY) {
			icon = entry->playable ? OSD_Colour(0xff, 0x9b, 0x3d)
			                       : OSD_Colour(0x3d, 0x5a, 0x9e);
			launcher_draw_icon_folder(c, list_x + 18, y + 12, icon);
			if (entry->playable) colour = OSD_Colour(0xff, 0xf0, 0xd8);
		}

		const bool has_detail = entry->kind == ENTRY_LIBRARY ||
		                        entry->kind == ENTRY_CLASSIC ||
		                        entry->kind == ENTRY_PROGRAM ||
		                        (entry->kind == ENTRY_IMAGE && entry->path);
		int text_y = y + (row_h - OSD_FONT_HEIGHT * scale) / 2 - 2;
		if (has_detail) text_y = y + 6;
		char setting_tag_buf[96];
		const char *setting_tag = NULL;
		if (entry->kind == ENTRY_SETTING) {
			snprintf(setting_tag_buf, sizeof(setting_tag_buf), "< %s >",
			         launcher_setting_value(&launcher_settings[entry->setting]));
			setting_tag = setting_tag_buf;
		}
		const int tag_w = setting_tag ? OSD_TextWidth(setting_tag, 2) + 16 : 88;
		const int name_room = list_x + list_w - tag_w - 18 - text_x;
		char name_buf[256];
		launcher_fit_text(name_buf, sizeof(name_buf), entry->name, scale, name_room);
		OSD_Text(c, text_x, text_y, name_buf, colour, scale, false);
		if (has_detail) {
			char detail[192];
			if (entry->kind == ENTRY_LIBRARY)
				snprintf(detail, sizeof(detail), "starts %s", entry->starts);
			else if (entry->kind == ENTRY_CLASSIC)
				snprintf(detail, sizeof(detail),
				         "DOS prompt, nothing started - the card is C: (type with L3)");
			else
				snprintf(detail, sizeof(detail), "in %.180s", entry->path);
			char detail_buf[256];
			launcher_fit_text(detail_buf, sizeof(detail_buf), detail, 1,
			                  list_x + list_w - tag_w - 18 - text_x);
			OSD_Text(c, text_x, text_y + OSD_FONT_HEIGHT * scale, detail_buf,
			         OSD_Colour(0x80, 0x90, 0xad), 1, false);
		}

		const char *tag = NULL;
		uint32_t tag_colour = OSD_Colour(0x3d, 0x5a, 0x9e);
		if (entry->kind == ENTRY_DIRECTORY) {
			tag = entry->playable ? "PLAY" : "DIR";
			tag_colour = entry->playable ? OSD_Colour(0xff, 0x9b, 0x3d)
			                             : OSD_Colour(0x3d, 0x5a, 0x9e);
		} else if (entry->kind == ENTRY_IMAGE) {
			tag = "IMG";
			tag_colour = OSD_Colour(0x9e, 0xc4, 0xff);
		} else if (entry->kind == ENTRY_START ||
		           entry->kind == ENTRY_PROGRAM) {
			tag = "GO";
			tag_colour = OSD_Colour(0xff, 0x9b, 0x3d);
		} else if (entry->kind == ENTRY_CLASSIC) {
			tag = "DOS";
			tag_colour = OSD_Colour(0x7d, 0xd8, 0x9a);
		} else if (entry->kind == ENTRY_SETTING) {
			tag = setting_tag;
			tag_colour = selected ? OSD_Colour(0xff, 0x9b, 0x3d)
			                      : OSD_Colour(0x9e, 0xc4, 0xff);
		} else if (entry->kind == ENTRY_LIBRARY) {
			tag = "GAME";
			tag_colour = OSD_Colour(0xff, 0x9b, 0x3d);
		}
		if (tag) {
			int tw = OSD_TextWidth(tag, 2) + 16;
			OSD_Panel(c, list_x + list_w - tw - 18, y + 12, tw, 24,
			          OSD_Colour(0x0d, 0x13, 0x26), tag_colour, 255);
			OSD_Text(c, list_x + list_w - tw - 10, y + 18, tag, tag_colour, 2, false);
		}

		if (selected && entry->starts[0] && entry->kind == ENTRY_DIRECTORY) {
			char detail[96];
			snprintf(detail, sizeof(detail), "-> %s", entry->starts);
			OSD_TextRight(c, list_x + list_w - 100, y + 18, detail,
			              OSD_Colour(0x80, 0x90, 0xad), 1, false);
		}
	}
}

static int launcher_choice_count(void) {
	int choices = 0;
	for (int i = 0; i < launcher_count; i++) {
		if (launcher_entries[i].kind != ENTRY_HEADER) choices++;
	}
	return choices;
}

static int launcher_choice_rank(void) {
	int rank = 0;
	for (int i = 0; i <= launcher_selected && i < launcher_count; i++) {
		if (launcher_entries[i].kind != ENTRY_HEADER) rank++;
	}
	return rank;
}

static void launcher_draw_footer(struct OSDCanvas *c) {
	const int y = c->height - 44;
	OSD_FillRect(c, 0, y - 12, c->width, 1, OSD_Colour(0x1c, 0x25, 0x40));
	OSD_BlendRect(c, 0, y - 11, c->width, 70, OSD_Colour(0x05, 0x08, 0x12), 180);

	static const char *const tab_buttons[] = { "A", "+", "B", "X", "L/R", "Y", "-" };
	static const char *const tab_what[] = { "PLAY", "OPEN", "BACK", "ABOUT", "TAB", "QUIT", "FTP" };
	static const char *const set_buttons[] = { "D-PAD", "A", "L/R", "B", "X", "Y", "-" };
	static const char *const set_what[] = { "LEFT/RIGHT CHANGES", "NEXT VALUE", "TAB", "BACK", "ABOUT", "QUIT", "FTP" };
	const bool settings_tab = (launcher_tab == TAB_SETTINGS);
	const char *const *buttons = settings_tab ? set_buttons : tab_buttons;
	const char *const *what = settings_tab ? set_what : tab_what;
	const int hints = (int)(sizeof(tab_buttons) / sizeof(tab_buttons[0]));
	int x = 28;
	for (int i = 0; i < hints; i++) {
		int item_w = OSD_DrawHintItem(c, x, y, buttons[i], what[i]);
		x += item_w + 24;
	}

	const int choices = launcher_choice_count();
	if (choices > 0) {
		char pos[64];
		snprintf(pos, sizeof(pos), "%d / %d", launcher_choice_rank(), choices);
		OSD_TextRight(c, c->width - 28, y + (26 - OSD_FONT_HEIGHT) / 2, pos,
		              OSD_Colour(0x80, 0x90, 0xad), 1, false);
	}

}

static void launcher_draw(struct OSDCanvas *c) {
	launcher_draw_background(c);
	launcher_draw_title(c, NULL);
	launcher_draw_tabs(c);
	launcher_draw_breadcrumb(c);
	launcher_draw_list(c);
	launcher_draw_footer(c);
}

static void splash_draw(struct OSDCanvas *c, int fade) {
	OSD_FillRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0));
	if (fade <= 0) return;

	int h = c->height;
	int w = (int)((uint64_t)h * 640 / 400);
	if (w > c->width) { w = c->width; h = (int)((uint64_t)w * 400 / 640); }

	OSD_DrawSplashLogo(c, (c->width - w) / 2, (c->height - h) / 2, w, h);
	if (fade < 255) {
		OSD_BlendRect(c, 0, 0, c->width, c->height, OSD_Colour(0, 0, 0), 255 - fade);
	}
}

static bool splash_already_shown = false;

static void splash_run(void) {
	if (splash_already_shown) return;
	splash_already_shown = true;
	extern void SwitchPlatform_Trace(const char *step);
	SwitchPlatform_Trace("OSD: splash start");
	const int hold_ms = 900;
	const int fade_ms = 450;
	Bit32u start = Platform_GetTicks();
	for (;;) {
		Bit32u now = Platform_GetTicks();
		Bit32u elapsed = now - start;
		int alpha;
		if (elapsed < (Bit32u)fade_ms) {
			alpha = (int)(elapsed * 255 / fade_ms);
		} else if (elapsed < (Bit32u)(fade_ms + hold_ms)) {
			alpha = 255;
		} else if (elapsed < (Bit32u)(2 * fade_ms + hold_ms)) {
			alpha = 255 - (int)((elapsed - (Bit32u)(fade_ms + hold_ms)) * 255 / fade_ms);
		} else {
			break;
		}

		struct OSDCanvas c;
		if (OSD_BeginFrame(&c)) {
			splash_draw(&c, alpha);
			OSD_EndFrame();
		}
		struct OSDPad pad = {};
		if (OSD_ReadPad(&pad) && (pad.a || pad.b)) break;
		OSD_FrameLimit();
	}
}

static void launcher_scroll_into_view(void) {
	if (launcher_selected < launcher_scroll) launcher_scroll = launcher_selected;
	if (launcher_selected >= launcher_scroll + LAUNCHER_VISIBLE_ROWS)
		launcher_scroll = launcher_selected - LAUNCHER_VISIBLE_ROWS + 1;
	if (launcher_scroll < 0) launcher_scroll = 0;
}

static bool edge_down(bool held, bool *previous) {
	bool down = held && !*previous;
	*previous = held;
	return down;
}

static bool launcher_commit(bool with_images, const char *single_image) {
	launcher_media_type = MEDIA_NONE;
	if (with_images) {
		launcher_collect_images(single_image);
		if (!launcher_image_list[0] && single_image) return false;
	} else {
		launcher_image_list[0] = '\0';
		launcher_image_count = 0;
	}
	launcher_find_boot(launcher_boot_cmds, sizeof(launcher_boot_cmds),
	                   with_images && launcher_image_list[0]);
	if (launcher_boot_subfolder[0]) {
		char candidate[LAUNCHER_PATH_MAX];
		launcher_join(candidate, sizeof(candidate), launcher_cwd,
		              launcher_boot_subfolder);
		if (launcher_is_dir(candidate))
			snprintf(launcher_result, sizeof(launcher_result), "%s", candidate);
		else
			launcher_boot_subfolder[0] = '\0';
	}
	if (!launcher_boot_subfolder[0])
		launcher_join(launcher_result, sizeof(launcher_result), launcher_cwd, NULL);
	return true;
}

static void launcher_enter_path(const char *path) {
	if (!path || !path[0]) return;
	snprintf(launcher_cwd, sizeof(launcher_cwd), "%s", path);
	launcher_scan();
	launcher_refresh_boot();
	launcher_selected = 1 < launcher_count ? 1 : 0;
	launcher_scroll = 0;
	launcher_scroll_into_view();
}

static void launcher_enter(const char *name) {
	if (name)
		launcher_join(launcher_cwd, sizeof(launcher_cwd), launcher_cwd, name);
	launcher_scan();
	launcher_refresh_boot();
	launcher_selected = 1 < launcher_count ? 1 : 0;
	launcher_scroll = 0;
	launcher_scroll_into_view();
}

static void launcher_go_parent(void) {
	char *slash = strrchr(launcher_cwd, '/');
	if (slash && slash != launcher_cwd) *slash = '\0';
	else snprintf(launcher_cwd, sizeof(launcher_cwd), "%s", LAUNCHER_ROOT_DEFAULT);
	launcher_scan();
	launcher_refresh_boot();
	launcher_selected = 0;
	launcher_scroll_into_view();
}

static int launcher_step(int from, int delta) {
	if (launcher_count <= 0) return 0;
	int index = from;
	for (int guard = 0; guard <= launcher_count; guard++) {
		index += delta;
		if (index < 0) index = launcher_count - 1;
		if (index >= launcher_count) index = 0;
		if (index < 0 || index >= launcher_count) return 0;
		if (launcher_entries[index].kind != ENTRY_HEADER) return index;
	}
	return from;
}

static int launcher_first_choice(void) {
	for (int i = 0; i < launcher_count; i++) {
		if (launcher_entries[i].kind == ENTRY_HEADER ||
		    launcher_entries[i].kind == ENTRY_CLASSIC)
			continue;
		return i;
	}
	for (int i = 0; i < launcher_count; i++) {
		if (launcher_entries[i].kind != ENTRY_HEADER) return i;
	}
	return 0;
}

static void launcher_select_tab(int tab) {
	if (tab < 0 || tab >= TAB_COUNT || tab == launcher_tab) return;
	launcher_tab_selected[launcher_tab] = launcher_selected;
	launcher_tab_scroll[launcher_tab] = launcher_scroll;
	launcher_tab = tab;

	if (tab == TAB_FOLDERS) {
		launcher_scan();
		launcher_refresh_boot();
	} else {
		launcher_build_tab(tab);
	}

	launcher_selected = launcher_tab_selected[tab];
	launcher_scroll = launcher_tab_scroll[tab];
	if (launcher_count > 0) {
		if (launcher_selected < 0 || launcher_selected >= launcher_count)
			launcher_selected = 1 < launcher_count ? 1 : 0;
		if (launcher_entries[launcher_selected].kind == ENTRY_HEADER)
			launcher_selected = launcher_step(launcher_selected, 1);
	} else {
		launcher_selected = 0;
	}
	launcher_scroll_into_view();
}

static void launcher_move_selection(int delta) {
	launcher_selected = launcher_step(launcher_selected, delta);
	launcher_scroll_into_view();
}

static void launcher_setting_step(int delta) {
	if (launcher_selected < 0 || launcher_selected >= launcher_count) return;
	struct LauncherEntry *entry = &launcher_entries[launcher_selected];
	if (entry->kind != ENTRY_SETTING) return;
	struct LauncherSetting *setting = &launcher_settings[entry->setting];
	if (setting->count <= 0) return;
	int index = setting->index + delta;
	if (index < 0) index = setting->count - 1;
	if (index >= setting->count) index = 0;
	if (index == setting->index) return;
	setting->index = index;
	snprintf(entry->starts, sizeof(entry->starts), "%s", launcher_setting_value(setting));
	launcher_settings_save();
}
static bool launcher_run(void) {
	static bool launcher_scanned_once = false;
	bool drawn_once = false;
	struct OSDPad init_pad = {};
	OSD_ReadPad(&init_pad);
	bool prev_up = init_pad.up, prev_down = init_pad.down, prev_left = init_pad.left, prev_right = init_pad.right;
	bool prev_a = init_pad.a, prev_b = init_pad.b, prev_x = init_pad.x, prev_y = init_pad.y;
	bool prev_l = init_pad.l, prev_r = init_pad.r, prev_plus = init_pad.plus, prev_minus = init_pad.minus;

	if (!launcher_scanned_once) {
		if (!launcher_scan_run()) return false;
		launcher_scanned_once = true;
	}

	launcher_pick_root();
	launcher_settings_load();
	launcher_tab = TAB_GAMES;
	launcher_build_tab(TAB_GAMES);
	{
		extern void SwitchPlatform_Trace(const char *step);
		char msg[LAUNCHER_PATH_MAX + 64];
		snprintf(msg, sizeof(msg), "OSD: library tab with %d rows in %s",
		         launcher_count, launcher_cwd);
		SwitchPlatform_Trace(msg);
	}
	launcher_selected = launcher_first_choice();
	launcher_scroll = 0;
	launcher_scroll_into_view();

	for (;;) {
		struct OSDPad pad = {};
		OSD_ReadPad(&pad);

		if (launcher_exit_poll && launcher_exit_poll()) return false;

		const float dead = 0.45f;
		bool up = pad.up || pad.ly < -dead;
		bool down = pad.down || pad.ly > dead;
		bool left = pad.left || pad.lx < -dead;
		bool right = pad.right || pad.lx > dead;

		bool a = edge_down(pad.a, &prev_a);
		bool b = edge_down(pad.b, &prev_b);
		bool x = edge_down(pad.x, &prev_x);
		bool y = edge_down(pad.y, &prev_y);
		bool l = edge_down(pad.l, &prev_l);
		bool r = edge_down(pad.r, &prev_r);
		bool plus = edge_down(pad.plus, &prev_plus);
		bool minus = edge_down(pad.minus, &prev_minus);
		bool up_edge = edge_down(up, &prev_up);
		bool down_edge = edge_down(down, &prev_down);
		bool left_edge = edge_down(left, &prev_left);
		bool right_edge = edge_down(right, &prev_right);

		const bool was_busy = OSD_OverlaysActive();
		OSD_UpdateOverlays(&pad);
		if (!was_busy && x) OSD_ToggleAbout();

		const bool ui_consumed = was_busy && !OSD_OverlaysActive();
		const bool ui_busy = OSD_OverlaysActive() || ui_consumed;

		if (!ui_busy && minus) launcher_ftp_toggle();

		if (launcher_ftp_dirty && launcher_ftp_dirty()) {
			if (launcher_tab == TAB_GAMES && !launcher_scan_run()) return false;
			launcher_build_tab(launcher_tab);
			if (launcher_selected >= launcher_count)
				launcher_selected = launcher_count > 0 ? launcher_count - 1 : 0;
			if (launcher_count > 0 && launcher_entries[launcher_selected].kind == ENTRY_HEADER)
				launcher_selected = launcher_step(launcher_selected, 1);
			launcher_scroll_into_view();
		}

		if (!ui_busy && l) launcher_select_tab(launcher_tab - 1 < 0 ? TAB_COUNT - 1 : launcher_tab - 1);
		if (!ui_busy && r) launcher_select_tab((launcher_tab + 1) % TAB_COUNT);

		if (!ui_busy && up_edge) launcher_move_selection(-1);
		if (!ui_busy && down_edge) launcher_move_selection(1);
		const bool settings_tab = (launcher_tab == TAB_SETTINGS);
		if (!ui_busy && left_edge) {
			if (settings_tab) {
				launcher_setting_step(-1);
			} else {
				launcher_selected -= LAUNCHER_VISIBLE_ROWS;
				if (launcher_selected < 0) launcher_selected = 0;
				launcher_selected = launcher_step(launcher_selected, 1);
				launcher_scroll_into_view();
			}
		}
		if (!ui_busy && right_edge) {
			if (settings_tab) {
				launcher_setting_step(1);
			} else {
				launcher_selected += LAUNCHER_VISIBLE_ROWS;
				if (launcher_selected >= launcher_count) launcher_selected = launcher_count - 1;
				launcher_selected = launcher_step(launcher_selected, -1);
				launcher_scroll_into_view();
			}
		}

		if (!ui_busy && y) return false;

		if (!ui_busy && b) {
			if (launcher_tab != TAB_FOLDERS) {
				launcher_select_tab(TAB_FOLDERS);
			} else if (strcmp(launcher_cwd, LAUNCHER_ROOT_DEFAULT) != 0) {
				launcher_go_parent();
			} else {
				return false;
			}
		}

		if (!ui_busy && plus) {
			const struct LauncherEntry *entry = &launcher_entries[launcher_selected];
			if (entry->kind == ENTRY_DIRECTORY) {
				launcher_enter(entry->name);
			} else if (entry->kind == ENTRY_PARENT) {
				launcher_go_parent();
			} else if (entry->path) {
				char folder[LAUNCHER_PATH_MAX];
				snprintf(folder, sizeof(folder), "%s", entry->path);
				if (launcher_tab != TAB_FOLDERS) {
					launcher_tab = TAB_FOLDERS;
					launcher_pick_root();
				}
				launcher_enter_path(folder);
			}
		}

		if (!ui_busy && a) {
			const struct LauncherEntry *entry = &launcher_entries[launcher_selected];
			if (entry->kind == ENTRY_SETTING) {
				launcher_setting_step(1);
			} else if (entry->kind == ENTRY_CLASSIC) {
				snprintf(launcher_result, sizeof(launcher_result), "%s", LAUNCHER_ROOT_DEFAULT);
				launcher_image_list[0] = '\0';
				launcher_image_count = 0;
				launcher_media_type = MEDIA_NONE;
				launcher_boot_cmds[0] = '\0';
				return true;
			}
			if (entry->kind == ENTRY_START) {
				if (!entry->playable) continue;
				if (launcher_commit(launcher_image_count > 0, NULL))
					return true;
			} else if (entry->kind == ENTRY_IMAGE || entry->kind == ENTRY_PROGRAM) {
				const bool is_program = entry->kind == ENTRY_PROGRAM;
				char folder[LAUNCHER_PATH_MAX] = "";
				char name[256];
				snprintf(name, sizeof(name), "%s", entry->name);
				if (entry->path) snprintf(folder, sizeof(folder), "%s", entry->path);
				if (folder[0]) launcher_enter_path(folder);
				if (is_program) {
					launcher_commit_run(name);
					return true;
				}
				const char *only_image = folder[0] ? NULL : name;
				if (launcher_commit(true, only_image)) return true;
				if (folder[0]) {
					launcher_tab = TAB_FOLDERS;
					launcher_selected = 0;
					launcher_scroll = 0;
					launcher_scroll_into_view();
				}
			} else if (entry->kind == ENTRY_PARENT) {
				launcher_go_parent();
			} else if (entry->kind == ENTRY_LIBRARY) {
				snprintf(launcher_cwd, sizeof(launcher_cwd), "%s", entry->path);
				launcher_scan();
				launcher_refresh_boot();
				if (launcher_commit(launcher_image_count > 0, NULL))
					return true;
				launcher_go_parent();
			} else if (entry->kind == ENTRY_DIRECTORY) {
				if (entry->playable) {
					char folder[LAUNCHER_PATH_MAX];
					launcher_join(folder, sizeof(folder), launcher_cwd, entry->name);
					snprintf(launcher_cwd, sizeof(launcher_cwd), "%s", folder);
					launcher_scan();
					launcher_refresh_boot();
					if (launcher_commit(launcher_image_count > 0, NULL))
						return true;
					launcher_go_parent();
				} else {
					launcher_enter(entry->name);
				}
			}
		}

		struct OSDCanvas c;
		if (OSD_BeginFrame(&c)) {
			if (!drawn_once) {
				extern void SwitchPlatform_Trace(const char *step);
				char msg[64];
				snprintf(msg, sizeof(msg), "OSD: menu canvas %dx%d pitch %d",
				         c.width, c.height, c.pitch);
				SwitchPlatform_Trace(msg);
				SwitchPlatform_Trace("OSD: menu drawn");
				drawn_once = true;
			}
			launcher_draw(&c);
			OSD_DrawOverlaysInto(&c);
			OSD_EndFrame();
		}
		OSD_FrameLimit();
	}
}

static bool launcher_loop(void) {
	OSD_SetGlobalShortcuts(false);
	launcher_ftp_open();
	const bool result = launcher_run();
	launcher_ftp_close();
	OSD_SetGlobalShortcuts(true);
	return result;
}

const char *OSD_RunLauncher(char *images, size_t images_size,
                            char *boot, size_t boot_size,
                            char *mount_drive, size_t mount_drive_size) {
	extern void SwitchPlatform_Trace(const char *step);

	int screen_w = 0, screen_h = 0;
	OSD_ScreenSize(&screen_w, &screen_h);
	SwitchPlatform_Trace("OSD_RunLauncher: enter");

	struct OSDCanvas probe;
	if (OSD_BeginFrame(&probe)) {
		SwitchPlatform_Trace("OSD: canvas ok");
		OSD_GradientV(&probe, 0, 0, probe.width, probe.height,
		              OSD_Colour(0x20, 0x30, 0x60), OSD_Colour(0x08, 0x0c, 0x18));
		OSD_EndFrame();
		SwitchPlatform_Trace("OSD: test frame drawn");
	} else {
		SwitchPlatform_Trace("OSD: NO CANVAS");
	}

	splash_run();
	SwitchPlatform_Trace("OSD: splash done");
	launcher_result[0] = '\0';
	launcher_image_list[0] = '\0';
	launcher_boot_cmds[0] = '\0';
	launcher_image_count = 0;
	launcher_media_type = MEDIA_NONE;
	if (!launcher_loop()) return NULL;
	SwitchPlatform_Trace("OSD: launcher done");
	if (images && images_size) {
		snprintf(images, images_size, "%s", launcher_image_list);
	}
	if (boot && boot_size) {
		snprintf(boot, boot_size, "%s", launcher_boot_cmds);
	}
	if (mount_drive && mount_drive_size >= 2) {
		const bool floppy = launcher_image_list[0] &&
		                    launcher_media_type == MEDIA_FLOPPY;
		snprintf(mount_drive, mount_drive_size, "%s", floppy ? "a" : "d");
	}
	return launcher_result[0] ? launcher_result : NULL;
}
