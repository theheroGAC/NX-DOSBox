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
#include "switch_ftp.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <switch.h>

#include <stdint.h>

#include "platform.h"

extern void SwitchPlatform_Trace(const char *step);

#define SWITCH_FTP_PORT    5000
#define SWITCH_FTP_CLIENTS 8
#define SWITCH_FTP_LINE    4096
#define SWITCH_FTP_CHUNK   (64 * 1024)
#define SWITCH_FTP_PATH    800
#define SWITCH_FTP_PATH_EXT (SWITCH_FTP_PATH + 16)
#define SWITCH_FTP_SLICE_MS 250
#define SWITCH_FTP_WAIT_MS  15000
#define SWITCH_FTP_SLICES   (SWITCH_FTP_WAIT_MS / SWITCH_FTP_SLICE_MS)

#define SWITCH_FTP_HOME "/sdmc/dosbox"

static volatile int s_running;
static int s_server_fd = -1;
static pthread_t s_accept_thread;
static bool s_sockets_ready;

enum FtpSlotState {
	FTP_SLOT_FREE = 0,
	FTP_SLOT_LIVE,
	FTP_SLOT_JOINABLE
};

struct FtpSlot {
	int state;
	int control_fd;
	int wake_fd;
	pthread_t thread;
};
static struct FtpSlot s_slots[SWITCH_FTP_CLIENTS];
static pthread_mutex_t s_slots_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile int s_dirty;
static char s_address[40] = "";
static Bit32u s_address_read;

static void ftp_refresh_address(void) {
	const Bit32u now = Platform_GetTicks();
	if (s_address_read && (Bit32u)(now - s_address_read) < 5000) return;
	s_address_read = now;
	s_address[0] = '\0';
	if (R_FAILED(nifmInitialize(NifmServiceType_User))) return;
	u32 ip = 0;
	if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0) {
		struct in_addr in;
		in.s_addr = ip;
		const char *text = inet_ntoa(in);
		if (text && strcmp(text, "0.0.0.0") != 0 && strcmp(text, "127.0.0.1") != 0)
			snprintf(s_address, sizeof(s_address), "%s", text);
	}
	nifmExit();
}

struct FtpSessionArg {
	int slot;
	int fd;
};

static void *ftp_client_thread(void *arg);

static int ftp_session_spawn(int control_fd) {
	struct FtpSessionArg *arg = (struct FtpSessionArg *)malloc(sizeof(*arg));
	if (!arg) return -1;
	int slot = -1;
	pthread_mutex_lock(&s_slots_lock);
	for (int i = 0; i < SWITCH_FTP_CLIENTS; i++) {
		if (s_slots[i].state != FTP_SLOT_FREE) continue;
		s_slots[i].control_fd = control_fd;
		s_slots[i].wake_fd = -1;
		arg->slot = i;
		arg->fd = control_fd;
		if (pthread_create(&s_slots[i].thread, NULL, ftp_client_thread, arg) != 0) {
			s_slots[i].control_fd = -1;
			break;
		}
		s_slots[i].state = FTP_SLOT_LIVE;
		slot = i;
		break;
	}
	pthread_mutex_unlock(&s_slots_lock);
	if (slot < 0) free(arg);
	return slot;
}

static void ftp_slot_finish(int slot, int control_fd) {
	if (slot < 0 || slot >= SWITCH_FTP_CLIENTS) return;
	pthread_mutex_lock(&s_slots_lock);
	if (s_slots[slot].control_fd == control_fd) {
		s_slots[slot].state = FTP_SLOT_JOINABLE;
		s_slots[slot].control_fd = -1;
		s_slots[slot].wake_fd = -1;
	}
	pthread_mutex_unlock(&s_slots_lock);
}

static void ftp_slot_watch(int slot, int fd) {
	if (slot < 0 || slot >= SWITCH_FTP_CLIENTS) return;
	pthread_mutex_lock(&s_slots_lock);
	s_slots[slot].wake_fd = fd;
	pthread_mutex_unlock(&s_slots_lock);
}

static int ftp_client_count(void) {
	int count = 0;
	pthread_mutex_lock(&s_slots_lock);
	for (int i = 0; i < SWITCH_FTP_CLIENTS; i++) {
		if (s_slots[i].state == FTP_SLOT_LIVE) count++;
	}
	pthread_mutex_unlock(&s_slots_lock);
	return count;
}

static void ftp_sessions_stop(void) {
	int woken = 0;
	for (int i = 0; i < SWITCH_FTP_CLIENTS; i++) {
		pthread_mutex_lock(&s_slots_lock);
		if (s_slots[i].state != FTP_SLOT_FREE) {
			if (s_slots[i].control_fd >= 0) shutdown(s_slots[i].control_fd, SHUT_RDWR);
			if (s_slots[i].wake_fd >= 0) shutdown(s_slots[i].wake_fd, SHUT_RDWR);
			s_slots[i].wake_fd = -1;
			woken++;
		}
		pthread_mutex_unlock(&s_slots_lock);
	}
	for (int i = 0; i < SWITCH_FTP_CLIENTS; i++) {
		pthread_mutex_lock(&s_slots_lock);
		const bool present = s_slots[i].state != FTP_SLOT_FREE;
		pthread_mutex_unlock(&s_slots_lock);
		if (!present) continue;
		pthread_join(s_slots[i].thread, NULL);
		pthread_mutex_lock(&s_slots_lock);
		s_slots[i].state = FTP_SLOT_FREE;
		s_slots[i].control_fd = -1;
		s_slots[i].wake_fd = -1;
		pthread_mutex_unlock(&s_slots_lock);
	}
	if (woken) {
		char note[96];
		snprintf(note, sizeof(note), "ftp: %d session(s) woken and joined", woken);
		SwitchPlatform_Trace(note);
	}
}

static void ftp_send_bytes(int fd, const char *data, size_t size) {
	if (fd < 0 || !data) return;
	while (size > 0) {
		const ssize_t sent = send(fd, data, size, 0);
		if (sent <= 0) return;
		data += sent;
		size -= (size_t)sent;
	}
}

static void ftp_send(int fd, const char *text) {
	if (!text) return;
	ftp_send_bytes(fd, text, strlen(text));
}

static bool ftp_send_all(int fd, const char *data, size_t size) {
	int stalls = 0;
	while (size > 0) {
		if (!s_running) return false;
		const ssize_t sent = send(fd, data, size, 0);
		if (sent > 0) {
			data += sent;
			size -= (size_t)sent;
			stalls = 0;
			continue;
		}
		if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
			if (++stalls > SWITCH_FTP_SLICES) return false;
			continue;
		}
		return false;
	}
	return true;
}

static void ftp_normalise(const char *v_cwd, const char *arg, char *out, size_t out_size) {
	char combined[768];
	char normalised[768] = "/";
	const char *p;

	if (!arg || !arg[0])
		snprintf(combined, sizeof(combined), "%s", (v_cwd && v_cwd[0]) ? v_cwd : "/");
	else if (arg[0] == '/')
		snprintf(combined, sizeof(combined), "%s", arg);
	else if (v_cwd && strcmp(v_cwd, "/") != 0)
		snprintf(combined, sizeof(combined), "%s/%s", v_cwd, arg);
	else
		snprintf(combined, sizeof(combined), "/%s", arg);

	p = combined;
	while (*p) {
		const char *segment;
		size_t segment_len, used;
		while (*p == '/') p++;
		if (!*p) break;
		segment = p;
		while (*p && *p != '/') p++;
		segment_len = (size_t)(p - segment);

		if (segment_len == 1 && segment[0] == '.') continue;
		if (segment_len == 2 && segment[0] == '.' && segment[1] == '.') {
			char *last = strrchr(normalised + 1, '/');
			if (last) *last = '\0';
			else normalised[1] = '\0';
			continue;
		}
		used = strlen(normalised);
		if (used > 1) {
			if (used + 1 + segment_len >= sizeof(normalised)) break;
			normalised[used++] = '/';
		} else if (used + segment_len >= sizeof(normalised)) {
			break;
		}
		memcpy(normalised + used, segment, segment_len);
		normalised[used + segment_len] = '\0';
	}
	snprintf(out, out_size, "%s", normalised);
}

static void ftp_resolve(const char *v_cwd, const char *arg, char *out_fs, size_t out_size) {
	char virtual_path[768];
	ftp_normalise(v_cwd, arg, virtual_path, sizeof(virtual_path));
	if (strcmp(virtual_path, "/sdmc") == 0)
		snprintf(out_fs, out_size, "sdmc:/");
	else if (strncmp(virtual_path, "/sdmc/", 6) == 0)
		snprintf(out_fs, out_size, "sdmc:/%s", virtual_path + 6);
	else
		snprintf(out_fs, out_size, "sdmc:/");
}

static void ftp_make_dirs(const char *path) {
	char buffer[SWITCH_FTP_PATH_EXT];
	snprintf(buffer, sizeof(buffer), "%s", path);
	for (char *p = buffer; *p; p++) {
		if (*p == '\\') *p = '/';
	}
	char *start = buffer;
	if (strncmp(buffer, "sdmc:/", 6) == 0) start = buffer + 5;
	for (char *p = start + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(buffer, 0777);
		*p = '/';
	}
	mkdir(buffer, 0777);
}

static int ftp_accept_data(int slot, int *pasv_fd) {
	if (!pasv_fd || *pasv_fd < 0) return -1;
	bool arrived = false;
	for (int waited = 0; waited < SWITCH_FTP_SLICES && s_running; waited++) {
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(*pasv_fd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = SWITCH_FTP_SLICE_MS * 1000;
		const int selected = select(*pasv_fd + 1, &readfds, NULL, NULL, &tv);
		if (selected != 0) {
			arrived = selected > 0;
			break;
		}
	}
	ftp_slot_watch(slot, -1);
	if (!arrived) {
		close(*pasv_fd);
		*pasv_fd = -1;
		return -1;
	}
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	const int data_fd = accept(*pasv_fd, (struct sockaddr *)&addr, &addr_len);
	close(*pasv_fd);
	*pasv_fd = -1;
	if (data_fd >= 0) {
		struct timeval slice;
		slice.tv_sec = 0;
		slice.tv_usec = SWITCH_FTP_SLICE_MS * 1000;
		setsockopt(data_fd, SOL_SOCKET, SO_RCVTIMEO, &slice, sizeof(slice));
		setsockopt(data_fd, SOL_SOCKET, SO_SNDTIMEO, &slice, sizeof(slice));
		ftp_slot_watch(slot, data_fd);
	}
	return data_fd;
}

static int ftp_data_open(int slot, int control_fd, int *pasv_fd) {
	const int data_fd = ftp_accept_data(slot, pasv_fd);
	if (data_fd < 0) {
		ftp_send(control_fd, "425 Can't open data connection.\r\n");
		return -1;
	}
	return data_fd;
}

static void ftp_data_close(int slot, int data_fd) {
	ftp_slot_watch(slot, -1);
	close(data_fd);
}

static void ftp_list_send(int data_fd, const char *fs_path) {
	char dir_path[SWITCH_FTP_PATH_EXT];
	snprintf(dir_path, sizeof(dir_path), "%s", fs_path);
	DIR *dir = opendir(dir_path);
	if (!dir) {
		snprintf(dir_path, sizeof(dir_path), "%s/", fs_path);
		dir = opendir(dir_path);
	}
	if (!dir) return;

	const size_t base = strlen(dir_path);
	const bool has_slash = base > 0 && dir_path[base - 1] == '/';
	static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

		char full[SWITCH_FTP_PATH_EXT];
		snprintf(full, sizeof(full), has_slash ? "%s%s" : "%s/%s", dir_path, entry->d_name);
		struct stat st;
		memset(&st, 0, sizeof(st));
		stat(full, &st);
		struct tm *gm = gmtime(&st.st_mtime);
		const int mon = gm ? gm->tm_mon : 0;
		const int mday = gm ? gm->tm_mday : 1;
		const int year = gm ? gm->tm_year + 1900 : 2026;

		char line[768];
		snprintf(line, sizeof(line),
		         "%crwxr-xr-x 1 root root %llu %s %02d %04d %s\r\n",
		         S_ISDIR(st.st_mode) ? 'd' : '-', (unsigned long long)st.st_size,
		         (mon >= 0 && mon <= 11) ? months[mon] : months[0], mday, year,
		         entry->d_name);
		ftp_send(data_fd, line);
	}
	closedir(dir);
}

static void ftp_mlsd_send(int data_fd, const char *fs_path) {
	char dir_path[SWITCH_FTP_PATH_EXT];
	snprintf(dir_path, sizeof(dir_path), "%s", fs_path);
	DIR *dir = opendir(dir_path);
	if (!dir) {
		snprintf(dir_path, sizeof(dir_path), "%s/", fs_path);
		dir = opendir(dir_path);
	}
	if (!dir) return;

	const size_t base = strlen(dir_path);
	const bool has_slash = base > 0 && dir_path[base - 1] == '/';
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

		char full[SWITCH_FTP_PATH_EXT];
		snprintf(full, sizeof(full), has_slash ? "%s%s" : "%s/%s", dir_path, entry->d_name);
		struct stat st;
		memset(&st, 0, sizeof(st));
		stat(full, &st);
		struct tm *gm = gmtime(&st.st_mtime);
		char stamp[32];
		if (gm) {
			snprintf(stamp, sizeof(stamp), "%04d%02d%02d%02d%02d%02d",
			         gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
			         gm->tm_hour, gm->tm_min, gm->tm_sec);
		} else {
			snprintf(stamp, sizeof(stamp), "20260101000000");
		}
		char line[768];
		if (S_ISDIR(st.st_mode))
			snprintf(line, sizeof(line), "type=dir;modify=%s; %s\r\n", stamp, entry->d_name);
		else
			snprintf(line, sizeof(line), "type=file;size=%llu;modify=%s; %s\r\n",
			         (unsigned long long)st.st_size, stamp, entry->d_name);
		ftp_send(data_fd, line);
	}
	closedir(dir);
}

static void ftp_list_root_entry(int data_fd, bool machine_readable) {
	if (machine_readable)
		ftp_send(data_fd, "type=dir;modify=20260101000000; sdmc\r\n");
	else
		ftp_send(data_fd, "drwxr-xr-x 1 root root 0 Jan 01 2026 sdmc\r\n");
}

static void ftp_session(int slot, int fd) {
	char line[SWITCH_FTP_LINE];
	char pending[SWITCH_FTP_LINE * 2];
	size_t pending_len = 0;
	int pasv_fd = -1;
	char v_cwd[SWITCH_FTP_PATH];
	char rename_from[SWITCH_FTP_PATH];
	off_t restart_at = 0;
	bool changed = false;

	snprintf(v_cwd, sizeof(v_cwd), "%s", SWITCH_FTP_HOME);
	rename_from[0] = '\0';

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = SWITCH_FTP_SLICE_MS * 1000;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	ftp_send(fd, "220 NX-DOSBox FTP Server Ready\r\n");

	while (s_running) {
		size_t line_len = 0;
		bool have_line = false;
		bool dropped = false;

		while (!have_line && s_running) {
			char *nl = (char *)memchr(pending, '\n', pending_len);
			if (nl) {
				line_len = (size_t)(nl - pending) + 1;
				have_line = true;
				break;
			}
			if (pending_len >= sizeof(pending) - 1) {
				ftp_send(fd, "500 Command line too long.\r\n");
				pending_len = 0;
				continue;
			}
			const ssize_t got = recv(fd, pending + pending_len,
			                         sizeof(pending) - pending_len - 1, 0);
			if (got == 0) {
				dropped = true;
				break;
			}
			if (got < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
					continue;
				dropped = true;
				break;
			}
			pending_len += (size_t)got;
			pending[pending_len] = '\0';
		}
		if (!have_line) {
			if (dropped || !s_running) break;
			continue;
		}
		if (line_len >= sizeof(line)) {
			ftp_send(fd, "500 Command line too long.\r\n");
			memmove(pending, pending + line_len, pending_len - line_len);
			pending_len -= line_len;
			continue;
		}
		memcpy(line, pending, line_len);
		line[line_len] = '\0';
		memmove(pending, pending + line_len, pending_len - line_len);
		pending_len -= line_len;

		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		char *command = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
		if (*p) {
			*p++ = '\0';
			while (*p == ' ' || *p == '\t') p++;
		}
		char *arg = p;
		char *end = arg + strlen(arg);
		while (end > arg && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' '))
			*(--end) = '\0';
		if (!arg[0]) arg = NULL;
		if (arg && arg[0] == '"') {
			arg++;
			char *closing = strrchr(arg, '"');
			if (closing) *closing = '\0';
			if (!arg[0]) arg = NULL;
		}
		if (!command[0]) continue;

		if (!strcasecmp(command, "USER") || !strcasecmp(command, "PASS")) {
			ftp_send(fd, "230 User logged in.\r\n");
		} else if (!strcasecmp(command, "SYST")) {
			ftp_send(fd, "215 UNIX Type: L8\r\n");
		} else if (!strcasecmp(command, "FEAT")) {
			ftp_send(fd, "211-Features:\r\n PASV\r\n EPSV\r\n UTF8\r\n SIZE\r\n"
			             " MDTM\r\n MLST type*;size*;modify*;\r\n REST STREAM\r\n211 End\r\n");
		} else if (!strcasecmp(command, "OPTS")) {
			ftp_send(fd, "200 OPTS command successful.\r\n");
		} else if (!strcasecmp(command, "NOOP")) {
			ftp_send(fd, "200 OK.\r\n");
		} else if (!strcasecmp(command, "TYPE")) {
			ftp_send(fd, "200 Type set to I.\r\n");
		} else if (!strcasecmp(command, "MODE")) {
			ftp_send(fd, "200 Mode set to S.\r\n");
		} else if (!strcasecmp(command, "STRU")) {
			ftp_send(fd, "200 Structure set to F.\r\n");
		} else if (!strcasecmp(command, "ALLO")) {
			ftp_send(fd, "200 OK.\r\n");
		} else if (!strcasecmp(command, "ABOR")) {
			ftp_send(fd, "226 ABOR command successful.\r\n");
		} else if (!strcasecmp(command, "STAT")) {
			ftp_send(fd, "211 NX-DOSBox FTP Server status OK.\r\n");
		} else if (!strcasecmp(command, "SITE")) {
			ftp_send(fd, "200 Command OK.\r\n");
		} else if (!strcasecmp(command, "PWD") || !strcasecmp(command, "XPWD")) {
			char reply[SWITCH_FTP_PATH + 64];
			snprintf(reply, sizeof(reply), "257 \"%s\" is current directory\r\n", v_cwd);
			ftp_send(fd, reply);
		} else if (!strcasecmp(command, "REST")) {
			restart_at = arg ? (off_t)strtoll(arg, NULL, 10) : 0;
			if (restart_at < 0) restart_at = 0;
			char reply[128];
			snprintf(reply, sizeof(reply), "350 Restart position accepted (%lld).\r\n",
			         (long long)restart_at);
			ftp_send(fd, reply);
		} else if (!strcasecmp(command, "PASV") || !strcasecmp(command, "EPSV")) {
			const bool extended = !strcasecmp(command, "EPSV");
			if (pasv_fd >= 0) {
				ftp_slot_watch(slot, -1);
				close(pasv_fd);
			}
			pasv_fd = socket(AF_INET, SOCK_STREAM, 0);
			if (pasv_fd < 0) {
				ftp_send(fd, "425 Can't open data connection.\r\n");
				continue;
			}
			int on = 1;
			setsockopt(pasv_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
			struct sockaddr_in addr;
			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = INADDR_ANY;
			addr.sin_port = 0;
			if (bind(pasv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
			    listen(pasv_fd, 1) < 0) {
				close(pasv_fd);
				pasv_fd = -1;
				ftp_send(fd, "425 Can't open data connection.\r\n");
				continue;
			}
			ftp_slot_watch(slot, pasv_fd);
			socklen_t addr_len = sizeof(addr);
			getsockname(pasv_fd, (struct sockaddr *)&addr, &addr_len);
			const int port = ntohs(addr.sin_port);
			char reply[160];
			if (extended) {
				snprintf(reply, sizeof(reply),
				         "229 Entering Extended Passive Mode (|||%d|)\r\n", port);
			} else {
				struct sockaddr_in local;
				socklen_t local_len = sizeof(local);
				unsigned int a = 127, b = 0, c = 0, d = 1;
				if (getsockname(fd, (struct sockaddr *)&local, &local_len) == 0 &&
				    local.sin_addr.s_addr != 0) {
					const unsigned char *bytes = (const unsigned char *)&local.sin_addr.s_addr;
					a = bytes[0]; b = bytes[1]; c = bytes[2]; d = bytes[3];
				}
				snprintf(reply, sizeof(reply),
				         "227 Entering Passive Mode (%u,%u,%u,%u,%d,%d)\r\n",
				         a, b, c, d, port >> 8, port & 0xff);
			}
			ftp_send(fd, reply);
		} else if (!strcasecmp(command, "LIST") || !strcasecmp(command, "NLST") ||
		           !strcasecmp(command, "MLSD")) {
			const bool simple = !strcasecmp(command, "NLST");
			const bool machine = !strcasecmp(command, "MLSD");
			const char *target = (arg && arg[0] != '-') ? arg : "";
			char virtual_path[SWITCH_FTP_PATH];
			char fs_path[SWITCH_FTP_PATH];
			ftp_normalise(v_cwd, target, virtual_path, sizeof(virtual_path));
			ftp_resolve(v_cwd, target, fs_path, sizeof(fs_path));
			ftp_send(fd, "150 Opening data connection for directory list.\r\n");
			const int data_fd = ftp_data_open(slot, fd, &pasv_fd);
			if (data_fd >= 0) {
				if (!strcmp(virtual_path, "/")) {
					if (simple) ftp_send(data_fd, "sdmc\r\n");
					else ftp_list_root_entry(data_fd, machine);
				} else if (simple) {
					char dir_path[SWITCH_FTP_PATH_EXT];
					snprintf(dir_path, sizeof(dir_path), "%s", fs_path);
					DIR *dir = opendir(dir_path);
					if (!dir) {
						snprintf(dir_path, sizeof(dir_path), "%s/", fs_path);
						dir = opendir(dir_path);
					}
					if (dir) {
						struct dirent *entry;
						while ((entry = readdir(dir)) != NULL) {
							if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
								continue;
							char item[600];
							snprintf(item, sizeof(item), "%s\r\n", entry->d_name);
							ftp_send(data_fd, item);
						}
						closedir(dir);
					}
				} else if (machine) {
					ftp_mlsd_send(data_fd, fs_path);
				} else {
					ftp_list_send(data_fd, fs_path);
				}
				ftp_data_close(slot, data_fd);
				ftp_send(fd, simple ? "226 Transfer complete.\r\n"
				                    : "226 Directory send OK.\r\n");
			}
		} else if (!strcasecmp(command, "MLST")) {
			char virtual_path[SWITCH_FTP_PATH];
			char fs_path[SWITCH_FTP_PATH];
			ftp_normalise(v_cwd, arg, virtual_path, sizeof(virtual_path));
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			struct stat st;
			memset(&st, 0, sizeof(st));
			const bool root_entry = !strcmp(virtual_path, "/") || !strcmp(virtual_path, "/sdmc");
			if (!root_entry && stat(fs_path, &st) != 0) {
				ftp_send(fd, "550 File or directory not found.\r\n");
			} else {
				struct tm *gm = gmtime(&st.st_mtime);
				char stamp[32];
				if (gm && !root_entry)
					snprintf(stamp, sizeof(stamp), "%04d%02d%02d%02d%02d%02d",
					         gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
					         gm->tm_hour, gm->tm_min, gm->tm_sec);
				else
					snprintf(stamp, sizeof(stamp), "20260101000000");
				char reply[SWITCH_FTP_PATH * 2 + 128];
				if (root_entry || S_ISDIR(st.st_mode))
					snprintf(reply, sizeof(reply),
					         "250-Listing %s\r\n type=dir;modify=%s; %s\r\n250 End\r\n",
					         virtual_path, stamp, virtual_path);
				else
					snprintf(reply, sizeof(reply),
					         "250-Listing %s\r\n type=file;size=%llu;modify=%s; %s\r\n250 End\r\n",
					         virtual_path, (unsigned long long)st.st_size, stamp, virtual_path);
				ftp_send(fd, reply);
			}
		} else if (!strcasecmp(command, "CWD") || !strcasecmp(command, "XCWD") ||
		           !strcasecmp(command, "CDUP") || !strcasecmp(command, "XCUP")) {
			const bool up = !strcasecmp(command, "CDUP") || !strcasecmp(command, "XCUP");
			char wanted[SWITCH_FTP_PATH];
			ftp_normalise(v_cwd, up ? ".." : arg, wanted, sizeof(wanted));
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(wanted, "", fs_path, sizeof(fs_path));
			DIR *dir = opendir(fs_path);
			struct stat st;
			const bool is_dir = dir != NULL ||
				(stat(fs_path, &st) == 0 && S_ISDIR(st.st_mode));
			if (dir) closedir(dir);
			if (is_dir) {
				snprintf(v_cwd, sizeof(v_cwd), "%s", wanted);
				ftp_send(fd, "250 Directory successfully changed.\r\n");
			} else {
				ftp_send(fd, "550 Directory not found.\r\n");
			}
		} else if (!strcasecmp(command, "MDTM")) {
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			struct stat st;
			if (stat(fs_path, &st) == 0) {
				struct tm *gm = gmtime(&st.st_mtime);
				char reply[128];
				if (gm) {
					snprintf(reply, sizeof(reply), "213 %04d%02d%02d%02d%02d%02d\r\n",
					         gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday,
					         gm->tm_hour, gm->tm_min, gm->tm_sec);
				} else {
					snprintf(reply, sizeof(reply), "213 20260101000000\r\n");
				}
				ftp_send(fd, reply);
			} else {
				ftp_send(fd, "550 File not found.\r\n");
			}
		} else if (!strcasecmp(command, "SIZE")) {
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			struct stat st;
			if (stat(fs_path, &st) == 0 && S_ISREG(st.st_mode)) {
				char reply[128];
				snprintf(reply, sizeof(reply), "213 %llu\r\n", (unsigned long long)st.st_size);
				ftp_send(fd, reply);
			} else {
				ftp_send(fd, "550 Could not get file size.\r\n");
			}
		} else if (!strcasecmp(command, "RETR")) {
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			FILE *file = fopen(fs_path, "rb");
			if (!file) {
				restart_at = 0;
				ftp_send(fd, "550 File not found.\r\n");
				continue;
			}
			if (restart_at > 0) {
				fseek(file, (long)restart_at, SEEK_SET);
				restart_at = 0;
			}
			ftp_send(fd, "150 Opening BINARY mode data connection.\r\n");
			const int data_fd = ftp_data_open(slot, fd, &pasv_fd);
			if (data_fd >= 0) {
				char *buffer = (char *)malloc(SWITCH_FTP_CHUNK);
				if (buffer) {
					size_t got;
					while (s_running && (got = fread(buffer, 1, SWITCH_FTP_CHUNK, file)) > 0) {
						if (!ftp_send_all(data_fd, buffer, got)) break;
					}
					free(buffer);
				}
				ftp_data_close(slot, data_fd);
				ftp_send(fd, "226 Transfer complete.\r\n");
			}
			fclose(file);
		} else if (!strcasecmp(command, "STOR") || !strcasecmp(command, "APPE")) {
			const bool append = !strcasecmp(command, "APPE");
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			char *slash = strrchr(fs_path, '/');
			if (slash) {
				*slash = '\0';
				ftp_make_dirs(fs_path);
				*slash = '/';
			}
			FILE *file = NULL;
			if (!append && restart_at > 0) {
				file = fopen(fs_path, "r+b");
				if (file) fseek(file, (long)restart_at, SEEK_SET);
			}
			if (!file) file = fopen(fs_path, append ? "ab" : "wb");
			if (!file && !append) {
				remove(fs_path);
				file = fopen(fs_path, "wb");
			}
			restart_at = 0;
			if (!file) {
				ftp_send(fd, "550 Failed to open file for writing.\r\n");
				continue;
			}
			ftp_send(fd, "150 Ok to send data.\r\n");
			const int data_fd = ftp_data_open(slot, fd, &pasv_fd);
			if (data_fd >= 0) {
				char *buffer = (char *)malloc(SWITCH_FTP_CHUNK);
				if (buffer) {
					int stalls = 0;
					while (s_running) {
						const ssize_t got = recv(data_fd, buffer, SWITCH_FTP_CHUNK, 0);
						if (got > 0) {
							fwrite(buffer, 1, (size_t)got, file);
							stalls = 0;
							continue;
						}
						if (got == 0) break;
						if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
							if (++stalls > SWITCH_FTP_SLICES) break;
							continue;
						}
						break;
					}
					free(buffer);
				}
				ftp_data_close(slot, data_fd);
				changed = true;
				char note[256];
				snprintf(note, sizeof(note), "ftp: %s received", arg ? arg : "(file)");
				SwitchPlatform_Trace(note);
				ftp_send(fd, "226 Transfer complete.\r\n");
			}
			fclose(file);
		} else if (!strcasecmp(command, "RNFR")) {
			ftp_resolve(v_cwd, arg, rename_from, sizeof(rename_from));
			struct stat st;
			if (stat(rename_from, &st) == 0) {
				ftp_send(fd, "350 File exists, ready for destination name.\r\n");
			} else {
				rename_from[0] = '\0';
				ftp_send(fd, "550 File not found.\r\n");
			}
		} else if (!strcasecmp(command, "RNTO")) {
			if (!rename_from[0]) {
				ftp_send(fd, "503 Bad sequence of commands.\r\n");
			} else {
				char to_path[SWITCH_FTP_PATH];
				ftp_resolve(v_cwd, arg, to_path, sizeof(to_path));
				if (rename(rename_from, to_path) == 0) {
					changed = true;
					ftp_send(fd, "250 Rename successful.\r\n");
				} else {
					ftp_send(fd, "550 Rename failed.\r\n");
				}
				rename_from[0] = '\0';
			}
		} else if (!strcasecmp(command, "DELE")) {
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			if (remove(fs_path) == 0) {
				changed = true;
				ftp_send(fd, "250 File deleted.\r\n");
			} else {
				ftp_send(fd, "550 Failed to delete file.\r\n");
			}
		} else if (!strcasecmp(command, "MKD") || !strcasecmp(command, "XMKD")) {
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			if (mkdir(fs_path, 0777) == 0) {
				changed = true;
				char reply[SWITCH_FTP_PATH + 64];
				snprintf(reply, sizeof(reply), "257 \"%s\" created.\r\n",
				         arg ? arg : "directory");
				ftp_send(fd, reply);
			} else {
				ftp_send(fd, "550 Failed to create directory.\r\n");
			}
		} else if (!strcasecmp(command, "RMD") || !strcasecmp(command, "XRMD")) {
			char fs_path[SWITCH_FTP_PATH];
			ftp_resolve(v_cwd, arg, fs_path, sizeof(fs_path));
			if (rmdir(fs_path) == 0) {
				changed = true;
				ftp_send(fd, "250 Directory removed.\r\n");
			} else {
				ftp_send(fd, "550 Failed to remove directory.\r\n");
			}
		} else if (!strcasecmp(command, "QUIT")) {
			ftp_send(fd, "221 Goodbye.\r\n");
			break;
		} else {
			ftp_send(fd, "502 Command not implemented.\r\n");
		}
	}

	if (pasv_fd >= 0) {
		ftp_slot_watch(slot, -1);
		close(pasv_fd);
	}
	if (changed) {
		s_dirty = 1;
		fsdevCommitDevice("sdmc");
	}
}

static void *ftp_client_thread(void *arg) {
	const struct FtpSessionArg session = *(const struct FtpSessionArg *)arg;
	free(arg);
	ftp_session(session.slot, session.fd);
	ftp_slot_finish(session.slot, session.fd);
	shutdown(session.fd, SHUT_RDWR);
	close(session.fd);
	return NULL;
}

static void *ftp_accept_thread(void *unused) {
	(void)unused;
	while (s_running) {
		if (s_server_fd < 0) break;
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(s_server_fd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100 * 1000;
		const int ready = select(s_server_fd + 1, &readfds, NULL, NULL, &tv);
		if (ready <= 0 || !s_running) continue;

		struct sockaddr_in addr;
		socklen_t addr_len = sizeof(addr);
		const int fd = accept(s_server_fd, (struct sockaddr *)&addr, &addr_len);
		if (fd < 0) continue;
		if (!s_running) {
			close(fd);
			break;
		}

		if (ftp_session_spawn(fd) < 0) {
			ftp_send(fd, "421 Too many connections.\r\n");
			close(fd);
		}
	}
	return NULL;
}

static void ftp_socket_release(void) {
	if (!s_sockets_ready) return;
	fsdevCommitDevice("sdmc");
	socketExit();
	s_sockets_ready = false;
	SwitchPlatform_Trace("ftp: socket service closed");
}

int SwitchPlatform_FTPStart(void) {
	if (s_running) return 0;

	ftp_sessions_stop();
	for (int i = 0; i < SWITCH_FTP_CLIENTS; i++) {
		s_slots[i].state = FTP_SLOT_FREE;
		s_slots[i].control_fd = -1;
		s_slots[i].wake_fd = -1;
	}
	s_dirty = 0;

	if (!s_sockets_ready) {
		if (R_FAILED(socketInitializeDefault())) {
			SwitchPlatform_Trace("ftp: no socket service");
			return -1;
		}
		s_sockets_ready = true;
	}
	ftp_refresh_address();

	s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s_server_fd < 0) {
		SwitchPlatform_Trace("ftp: listen socket failed");
		ftp_socket_release();
		return -1;
	}
	int on = 1;
	setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(SWITCH_FTP_PORT);
	if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(s_server_fd, 4) < 0) {
		close(s_server_fd);
		s_server_fd = -1;
		SwitchPlatform_Trace("ftp: could not bind port 5000");
		ftp_socket_release();
		return -1;
	}
	s_running = 1;
	if (pthread_create(&s_accept_thread, NULL, ftp_accept_thread, NULL) != 0) {
		s_running = 0;
		close(s_server_fd);
		s_server_fd = -1;
		SwitchPlatform_Trace("ftp: no thread for the server");
		ftp_socket_release();
		return -1;
	}
	{
		char note[128];
		snprintf(note, sizeof(note), "ftp: listening on %s:%d, root sdmc:/",
		         s_address[0] ? s_address : "(no network)", SWITCH_FTP_PORT);
		SwitchPlatform_Trace(note);
	}
	return 0;
}

void SwitchPlatform_FTPStop(void) {
	if (!s_running && s_server_fd < 0 && !s_sockets_ready) return;
	SwitchPlatform_Trace("ftp: stopping");
	s_running = 0;
	if (s_server_fd >= 0) {
		pthread_join(s_accept_thread, NULL);
		close(s_server_fd);
		s_server_fd = -1;
	}
	ftp_sessions_stop();
	ftp_socket_release();
	SwitchPlatform_Trace("ftp: stopped, every session joined");
	s_dirty = 0;
}

void SwitchPlatform_FTPShutdown(void) {
	SwitchPlatform_FTPStop();
}

void SwitchPlatform_FTPStatus(struct OSD_FTPServer *out) {
	if (!out) return;
	memset(out, 0, sizeof(*out));
	out->available = true;
	out->port = SWITCH_FTP_PORT;
	out->running = s_running != 0;
	out->clients = ftp_client_count();
	if (s_running) ftp_refresh_address();
	snprintf(out->address, sizeof(out->address), "%s", s_address);
}

bool SwitchPlatform_FTPTakeLibraryDirty(void) {
	if (!s_dirty) return false;
	s_dirty = 0;
	return true;
}
