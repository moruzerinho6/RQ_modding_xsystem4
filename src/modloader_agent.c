/* Named-pipe input agent. Same protocol as rqmod-ctl (\\.\pipe\rqmod).
 * Uses input.c symbols, not 1.0.0 RVAs.
 */

#ifdef RQ_MODLOADER

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "modloader.h"
#include "modloader_priv.h"

#define VK_NR_KEYCODES    0x92
#define PIPE_NAME         "\\\\.\\pipe\\rqmod"

void mouse_set_pos(int x, int y);
void key_set_state(int code, int down);
void mouse_set_wheel(int dir);

static int pulse_ms = 80;

static char *skip_ws(char *s)
{
	while (*s && isspace((unsigned char)*s))
		s++;
	return s;
}

static char *tok(char **ps)
{
	char *s = skip_ws(*ps), *e;
	if (!*s)
		return s;
	e = s;
	while (*e && !isspace((unsigned char)*e))
		e++;
	if (*e) {
		*e = 0;
		*ps = e + 1;
	} else {
		*ps = e;
	}
	return s;
}

static int key_code(const char *n)
{
	int hex;
	if (!n || !*n)
		return -1;
	if (!_stricmp(n, "enter") || !_stricmp(n, "return"))
		return VK_RETURN;
	if (!_stricmp(n, "esc") || !_stricmp(n, "escape"))
		return VK_ESCAPE;
	if (!_stricmp(n, "space") || !_stricmp(n, "spc"))
		return VK_SPACE;
	if (!_stricmp(n, "tab"))
		return VK_TAB;
	if (!_stricmp(n, "back") || !_stricmp(n, "backspace"))
		return VK_BACK;
	if (!_stricmp(n, "shift"))
		return VK_SHIFT;
	if (!_stricmp(n, "ctrl") || !_stricmp(n, "control"))
		return VK_CONTROL;
	if (!_stricmp(n, "up"))
		return VK_UP;
	if (!_stricmp(n, "down"))
		return VK_DOWN;
	if (!_stricmp(n, "left"))
		return VK_LEFT;
	if (!_stricmp(n, "right"))
		return VK_RIGHT;
	if (!_stricmp(n, "pgup") || !_stricmp(n, "pageup"))
		return VK_PRIOR;
	if (!_stricmp(n, "pgdn") || !_stricmp(n, "pagedown"))
		return VK_NEXT;
	if (!_stricmp(n, "home"))
		return VK_HOME;
	if (!_stricmp(n, "end"))
		return VK_END;
	if (!_stricmp(n, "ins") || !_stricmp(n, "insert"))
		return VK_INSERT;
	if (!_stricmp(n, "del") || !_stricmp(n, "delete"))
		return VK_DELETE;
	if (!_stricmp(n, "z"))
		return 0x5A;
	if (!_stricmp(n, "x"))
		return 0x58;
	if (!_stricmp(n, "c"))
		return 0x43;
	if (!_stricmp(n, "a"))
		return 0x41;
	if (!_stricmp(n, "s"))
		return 0x53;
	if (!_stricmp(n, "leftclick") || !_stricmp(n, "lbutton") || !_stricmp(n, "lmb"))
		return VK_LBUTTON;
	if (!_stricmp(n, "rightclick") || !_stricmp(n, "rbutton") || !_stricmp(n, "rmb"))
		return VK_RBUTTON;
	if (n[0] == '0' && (n[1] == 'x' || n[1] == 'X')) {
		hex = (int)strtol(n, NULL, 16);
		if (hex > 0 && hex < VK_NR_KEYCODES)
			return hex;
	}
	if (n[0] && !n[1]) {
		char c = n[0];
		if (c >= 'a' && c <= 'z')
			return 0x41 + (c - 'a');
		if (c >= 'A' && c <= 'Z')
			return 0x41 + (c - 'A');
		if (c >= '0' && c <= '9')
			return 0x30 + (c - '0');
	}
	return -1;
}

static int mouse_btn(const char *n)
{
	if (!n || !_stricmp(n, "left") || !_stricmp(n, "l") || !_stricmp(n, "lmb"))
		return VK_LBUTTON;
	if (!_stricmp(n, "right") || !_stricmp(n, "r") || !_stricmp(n, "rmb"))
		return VK_RBUTTON;
	if (!_stricmp(n, "middle") || !_stricmp(n, "m") || !_stricmp(n, "mmb"))
		return VK_MBUTTON;
	return -1;
}

static void set_key(int code, int down)
{
	if (code <= 0 || code >= VK_NR_KEYCODES)
		return;
	key_set_state(code, down);
}

static void pulse_key(int code)
{
	set_key(code, 1);
	Sleep((DWORD)pulse_ms);
	set_key(code, 0);
}

static int rq_agent_do(const char *line_in, char *err, int errsz)
{
	char buf[512], *p, *cmd, *a, *b, *c;
	int code, x, y, btn;

	if (err && errsz)
		err[0] = 0;
	if (!line_in)
		return 0;
	strncpy(buf, line_in, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	p = buf;
	cmd = tok(&p);
	if (!*cmd || cmd[0] == '#')
		return 1;

	if (!_stricmp(cmd, "ping") || !_stricmp(cmd, "ok"))
		return 1;

	if (!_stricmp(cmd, "key") || !_stricmp(cmd, "press")) {
		a = tok(&p);
		code = key_code(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown key '%s'", a);
			return 0;
		}
		pulse_key(code);
		return 1;
	}
	if (!_stricmp(cmd, "hold") || !_stricmp(cmd, "down")) {
		a = tok(&p);
		btn = mouse_btn(a);
		if (btn > 0) {
			set_key(btn, 1);
			return 1;
		}
		code = key_code(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown key '%s'", a);
			return 0;
		}
		set_key(code, 1);
		return 1;
	}
	if (!_stricmp(cmd, "release") || !_stricmp(cmd, "up")) {
		a = tok(&p);
		btn = mouse_btn(a);
		if (btn > 0) {
			set_key(btn, 0);
			return 1;
		}
		code = key_code(a);
		if (code < 0) {
			if (err)
				snprintf(err, errsz, "unknown key '%s'", a);
			return 0;
		}
		set_key(code, 0);
		return 1;
	}
	if (!_stricmp(cmd, "move")) {
		a = tok(&p);
		b = tok(&p);
		if (!*a || !*b) {
			if (err)
				snprintf(err, errsz, "usage: move X Y");
			return 0;
		}
		x = atoi(a);
		y = atoi(b);
		mouse_set_pos(x, y);
		return 1;
	}
	if (!_stricmp(cmd, "click")) {
		a = tok(&p);
		b = tok(&p);
		c = tok(&p);
		btn = VK_LBUTTON;
		if (mouse_btn(a) > 0 && *b && *c) {
			btn = mouse_btn(a);
			x = atoi(b);
			y = atoi(c);
		} else if (*a && *b) {
			x = atoi(a);
			y = atoi(b);
		} else {
			if (err)
				snprintf(err, errsz, "usage: click [left|right] X Y");
			return 0;
		}
		mouse_set_pos(x, y);
		Sleep(16);
		set_key(btn, 1);
		Sleep((DWORD)pulse_ms);
		set_key(btn, 0);
		return 1;
	}
	if (!_stricmp(cmd, "wheel")) {
		a = tok(&p);
		mouse_set_wheel(a && *a ? atoi(a) : 1);
		return 1;
	}
	if (!_stricmp(cmd, "type")) {
		a = skip_ws(p);
		for (; *a; a++) {
			unsigned char ch = (unsigned char)*a;
			if (ch == ' ')
				code = VK_SPACE;
			else if (ch >= 'a' && ch <= 'z')
				code = 0x41 + (ch - 'a');
			else if (ch >= 'A' && ch <= 'Z')
				code = 0x41 + (ch - 'A');
			else if (ch >= '0' && ch <= '9')
				code = 0x30 + (ch - '0');
			else
				continue;
			pulse_key(code);
			Sleep(20);
		}
		return 1;
	}
	if (!_stricmp(cmd, "pulse")) {
		a = tok(&p);
		if (*a)
			pulse_ms = atoi(a);
		if (pulse_ms < 16)
			pulse_ms = 16;
		if (pulse_ms > 1000)
			pulse_ms = 1000;
		return 1;
	}
	if (err)
		snprintf(err, errsz, "unknown cmd '%s' (key|click|move|hold|release|type|wheel|ping)", cmd);
	return 0;
}

static DWORD WINAPI pipe_thread(LPVOID unused)
{
	(void)unused;
	for (;;) {
		HANDLE h;
		char in[512], out[256], err[128];
		DWORD n, w;
		h = CreateNamedPipeA(PIPE_NAME, PIPE_ACCESS_DUPLEX,
				     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
				     PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			Sleep(500);
			continue;
		}
		if (!ConnectNamedPipe(h, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
			CloseHandle(h);
			continue;
		}
		n = 0;
		memset(in, 0, sizeof in);
		if (ReadFile(h, in, sizeof in - 1, &n, NULL) && n > 0) {
			char *nl;
			in[n] = 0;
			nl = in;
			while (*nl) {
				char *end = nl;
				while (*end && *end != '\n' && *end != '\r')
					end++;
				if (*end)
					*end++ = 0;
				while (*end == '\n' || *end == '\r')
					end++;
				if (rq_agent_do(nl, err, (int)sizeof err))
					snprintf(out, sizeof out, "ok\n");
				else
					snprintf(out, sizeof out, "err %s\n", err[0] ? err : "fail");
				WriteFile(h, out, (DWORD)strlen(out), &w, NULL);
				nl = end;
			}
		}
		FlushFileBuffers(h);
		DisconnectNamedPipe(h);
		CloseHandle(h);
	}
	return 0;
}

void rq_mods_agent_start(void)
{
	HANDLE th = CreateThread(NULL, 0, pipe_thread, NULL, 0, NULL);
	if (!th) {
		rq_log("agent: pipe thread failed");
		return;
	}
	CloseHandle(th);
	rq_log("agent: pipe %s  —  rqmod-ctl key enter | rqmod-ctl click X Y", PIPE_NAME);
}

#endif /* RQ_MODLOADER */
