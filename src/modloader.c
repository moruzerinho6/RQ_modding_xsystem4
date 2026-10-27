/* Copyright (C) 2026 Rance Quest modding fork
 *
 * In-engine Lua host. Called from apply_game_specific_hacks after ain_open.
 * No IAT/RVA inject. Camera-zoom / skip-network-popup stay as Lua in mods\.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "cJSON.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "modloader.h"
#include "modloader_priv.h"
#include "profile.h"
#include "vm.h"
#include "xsystem4.h"

char g_rq_game_dir[RQ_PATH_MAX];
char g_rq_mods_dir[RQ_PATH_MAX];
char g_rq_exe_dir[RQ_PATH_MAX];
FILE *g_rq_log;
int g_rq_verbose;
int g_rq_terminal;
int g_rq_skip_sdl;
static int g_rq_console;

static struct ain *g_ain;
static lua_State *g_L;

lua_State *rq_lua(void)
{
	return g_L;
}

static void rq_vlog(const char *tag, const char *fmt, va_list ap)
{
	char buf[1024];
	const char *t = (tag && *tag) ? tag : "rqmod";
	vsnprintf(buf, sizeof buf, fmt, ap);
	fprintf(stderr, "[%s] %s\n", t, buf);
	fflush(stderr);
	if (g_rq_log) {
		fprintf(g_rq_log, "[%s] %s\n", t, buf);
		fflush(g_rq_log);
	}
}

void rq_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	rq_vlog("rqmod", fmt, ap);
	va_end(ap);
}

void rq_log_tag(const char *tag, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	rq_vlog(tag, fmt, ap);
	va_end(ap);
}

void rq_format_sp_now(char *buf, size_t n)
{
#ifdef _WIN32
	SYSTEMTIME utc, sp;
	TIME_ZONE_INFORMATION tzi;

	if (!buf || !n)
		return;
	memset(&tzi, 0, sizeof tzi);
	/* UTC = local + bias; America/Sao_Paulo is UTC-3 (no DST since 2019). */
	tzi.Bias = 180;
	GetSystemTime(&utc);
	if (!SystemTimeToTzSpecificLocalTime(&tzi, &utc, &sp))
		sp = utc;
	snprintf(buf, n, "%02d/%02d/%04d %02d:%02d",
		 (int)sp.wDay, (int)sp.wMonth, (int)sp.wYear,
		 (int)sp.wHour, (int)sp.wMinute);
#else
	time_t t = time(NULL);
	struct tm tm;
	t -= 3 * 3600;
	gmtime_r(&t, &tm);
	snprintf(buf, n, "%02d/%02d/%04d %02d:%02d",
		 tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
		 tm.tm_hour, tm.tm_min);
#endif
}

void rq_window_title(char *buf, size_t n, const char *game_utf)
{
	const char *game = (game_utf && *game_utf) ? game_utf : "Rance Quest";
	const char *stamp = rq_build_stamp();
	if (!buf || !n)
		return;
	if (stamp && *stamp)
		snprintf(buf, n, "%s - XSystem4 [RQ_modding] - Build %s", game, stamp);
	else
		snprintf(buf, n, "%s - XSystem4 [RQ_modding]", game);
}

static void rq_print_banner(void)
{
	char opened[32];
	char title[256];
	const char *stamp = rq_build_stamp();

	rq_window_title(title, sizeof title, "Rance Quest");
	rq_format_sp_now(opened, sizeof opened);
	fprintf(stderr, "%s\n", title);
	fprintf(stderr, "Opened %s\n", opened);
	fflush(stderr);
	if (g_rq_log) {
		fprintf(g_rq_log, "%s\nOpened %s\n", title, opened);
		fflush(g_rq_log);
	}
	(void)stamp;
}

#ifdef _WIN32
static void rq_open_terminal(void)
{
	FILE *con;
	if (!g_rq_terminal || g_rq_console)
		return;
	if (!GetConsoleWindow()) {
		if (!AllocConsole())
			return;
		SetConsoleTitleA("Rance Quest mod log");
	}
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	con = freopen("CONOUT$", "w", stdout);
	(void)con;
	con = freopen("CONOUT$", "w", stderr);
	(void)con;
	con = freopen("CONIN$", "r", stdin);
	(void)con;
	SetConsoleOutputCP(CP_UTF8);
	g_rq_console = 1;
}
#else
static void rq_open_terminal(void) {}
#endif

static const char *lua_mod_tag(lua_State *L)
{
	lua_Debug ar;
	const char *src, *mods, *slash, *dot;
	size_t n;
	static char tag[64];

	if (!lua_getstack(L, 1, &ar) || !lua_getinfo(L, "S", &ar) || !ar.source)
		return "lua";
	src = ar.source;
	if (src[0] == '@')
		src++;
	mods = strstr(src, "\\mods\\");
	if (!mods)
		mods = strstr(src, "/mods/");
	if (!mods)
		return "lua";
	mods += 6;
	slash = strpbrk(mods, "\\/");
	if (slash) {
		n = (size_t)(slash - mods);
		if (n == 0)
			return "lua";
		if (n >= sizeof tag)
			n = sizeof tag - 1;
		memcpy(tag, mods, n);
		tag[n] = 0;
		return tag;
	}
	dot = strrchr(mods, '.');
	n = dot ? (size_t)(dot - mods) : strlen(mods);
	if (n == 0)
		return "lua";
	if (n >= sizeof tag)
		n = sizeof tag - 1;
	memcpy(tag, mods, n);
	tag[n] = 0;
	return tag;
}

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t rd32(const uint8_t *p)
{
	return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;
	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
}

static void wrf(uint8_t *p, float f)
{
	union { float f; uint32_t u; } c;
	c.f = f;
	wr32(p, (int32_t)c.u);
}

static float rdf(const uint8_t *p)
{
	union { float f; uint32_t u; } c;
	c.u = (uint32_t)rd32(p);
	return c.f;
}

static int op_nargs(uint16_t op)
{
	switch (op) {
	case 0x00: case 0x2c: case 0x2d: case 0x2e: case 0x30:
	case 0x40: case 0x41: case 0x5c: case 0x5d: case 0x5e:
	case 0x61: case 0x63: case 0x67: case 0x7e: case 0x82:
	case 0x8a: case 0x8b:
		return 1;
	case 0x5a: case 0x81: case 0x8c: case 0xc7:
		return 2;
	case 0xb8: case 0xb9:
		return 3;
	default:
		return 0;
	}
}

static int instr_len(uint16_t op)
{
	return 2 + op_nargs(op) * 4;
}

static uint32_t func_end_addr(struct ain *ain, int fno)
{
	uint32_t start = ain->functions[fno].address;
	uint32_t end = (uint32_t)ain->code_size;
	for (int i = 0; i < ain->nr_functions; i++) {
		uint32_t a = ain->functions[i].address;
		if (a > start && a < end)
			end = a;
	}
	return end;
}

static int find_func_utf8(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
}

static int grow_code(struct ain *ain, size_t extra)
{
	ain->code = xrealloc(ain->code, ain->code_size + extra);
	return ain->code != NULL;
}

static uint32_t relocate_func(struct ain *ain, uint32_t start, uint32_t end,
			      uint32_t insert_at, const uint8_t *extra, size_t extra_len)
{
	size_t old_len = end - start;
	size_t new_len = old_len + extra_len;
	uint32_t new_start;
	uint8_t *dst;
	uint32_t ip;

	if (!grow_code(ain, new_len + 16))
		return 0;
	new_start = (uint32_t)ain->code_size;
	dst = ain->code + new_start;

	for (ip = start; ip < end; ) {
		uint16_t op;
		int narg, i, len;
		if (insert_at && ip == insert_at && extra_len) {
			memcpy(dst, extra, extra_len);
			dst += extra_len;
		}
		if (ip + 2 > end)
			break;
		op = rd16(ain->code + ip);
		narg = op_nargs(op);
		len = 2 + narg * 4;
		if (ip + (uint32_t)len > end) {
			memcpy(dst, ain->code + ip, end - ip);
			dst += end - ip;
			break;
		}
		wr16(dst, op);
		for (i = 0; i < narg; i++) {
			int32_t a = rd32(ain->code + ip + 2 + i * 4);
			int is_addr = 0;
			if ((op == 0x2c || op == 0x2d || op == 0x2e) && i == 0)
				is_addr = 1;
			if (op == 0xc7 && i == 1)
				is_addr = 1;
			if (is_addr && (uint32_t)a >= start && (uint32_t)a < end) {
				uint32_t na = (uint32_t)a;
				if (insert_at && na >= insert_at)
					na += (uint32_t)extra_len;
				na = na - start + new_start;
				a = (int32_t)na;
			}
			wr32(dst + 2 + i * 4, a);
		}
		dst += len;
		ip += (uint32_t)len;
	}
	ain->code_size = (size_t)(dst - ain->code);
	return new_start;
}

static int patch_floats(struct ain *ain, int fno, float from, float to, int nth)
{
	uint32_t start = ain->functions[fno].address;
	uint32_t end = func_end_addr(ain, fno);
	uint32_t ip;
	int hit = 0, n = 0;
	for (ip = start; ip + 6 <= end; ) {
		uint16_t op = rd16(ain->code + ip);
		int len = instr_len(op);
		if (op == 0x40) {
			float v = rdf(ain->code + ip + 2);
			if (v == from || (from != 0.f && fabsf(v - from) < 0.0001f)) {
				if (nth < 0 || n == nth) {
					wrf(ain->code + ip + 2, to);
					hit++;
				}
				n++;
			}
		}
		if (len < 2)
			len = 2;
		ip += (uint32_t)len;
	}
	return hit;
}

static uint32_t find_op_after_float(struct ain *ain, int fno, float needle, uint16_t want_op, int nth_float)
{
	uint32_t start = ain->functions[fno].address;
	uint32_t end = func_end_addr(ain, fno);
	uint32_t ip;
	int n = 0;
	int seen = 0;
	for (ip = start; ip + 2 <= end; ) {
		uint16_t op = rd16(ain->code + ip);
		int len = instr_len(op);
		if (seen && op == want_op)
			return ip;
		if (op == 0x40 && ip + 6 <= end) {
			float v = rdf(ain->code + ip + 2);
			if (fabsf(v - needle) < 0.0001f) {
				if (nth_float < 0 || n == nth_float)
					seen = 1;
				n++;
			}
		}
		if (len < 2)
			len = 2;
		ip += (uint32_t)len;
	}
	return 0;
}

static uint32_t find_jump_after_op(struct ain *ain, int fno, uint16_t after_op)
{
	uint32_t start = ain->functions[fno].address;
	uint32_t end = func_end_addr(ain, fno);
	uint32_t ip;
	int seen = 0;
	for (ip = start; ip + 6 <= end; ) {
		uint16_t op = rd16(ain->code + ip);
		int len = instr_len(op);
		if (seen && op == 0x2c)
			return ip;
		if (op == after_op)
			seen = 1;
		if (len < 2)
			len = 2;
		ip += (uint32_t)len;
	}
	return 0;
}

static uint32_t last_localref_before_last_callfunc(struct ain *ain, int fno)
{
	uint32_t start = ain->functions[fno].address;
	uint32_t end = func_end_addr(ain, fno);
	uint32_t ip, last_call = 0, last_ref = 0;
	for (ip = start; ip + 6 <= end; ) {
		uint16_t op = rd16(ain->code + ip);
		int len = instr_len(op);
		if (op == 0x5e)
			last_ref = ip;
		if (op == 0x30)
			last_call = ip;
		if (len < 2)
			len = 2;
		ip += (uint32_t)len;
	}
	if (last_ref && last_call && last_ref < last_call)
		return last_ref;
	return 0;
}

static int dungeon_camera(struct ain *ain, float lock, float dmin, float dmax, float fog_far, int steps)
{
	int clamp_fno, scroll_fno, update_fno, fog_fno, move_fno;
	float step;
	int n;
	uint32_t insert_at, new_addr, jump_ip, dest;
	uint8_t extra[16];
	uint8_t *p;

	if (steps < 1)
		steps = 25;
	step = (dmax - dmin) / (float)steps;

	clamp_fno = find_func_utf8(ain, "３Ｄマップ_カメラ距離設定");
	scroll_fno = find_func_utf8(ain, "T_MapParts@scrollCameraLength");
	update_fno = find_func_utf8(ain, "T_MapParts@updateCameraLength");
	fog_fno = find_func_utf8(ain, "CMapMode@SetFogParam");
	move_fno = find_func_utf8(ain, "キャラ_移動距離設定");

	if (clamp_fno < 0 || scroll_fno < 0 || update_fno < 0 || fog_fno < 0 || move_fno < 0) {
		rq_log("dungeon_camera: missing function (clamp=%d scroll=%d update=%d fog=%d move=%d)",
		       clamp_fno, scroll_fno, update_fno, fog_fno, move_fno);
		return 0;
	}

	n = patch_floats(ain, clamp_fno, 35.0f, dmin, -1);
	n += patch_floats(ain, clamp_fno, 60.0f, dmax, -1);
	rq_log("clamp fno=%d: patched %d float immediates -> [%.2f, %.2f] (lock %.2f still in range)",
	       clamp_fno, n, dmin, dmax, lock);

	n = patch_floats(ain, fog_fno, 80.0f, fog_far, -1);
	rq_log("CMapMode@SetFogParam: patched %d x 80 -> fog_far %.2f (battle/viewer fog untouched)", n, fog_far);

	insert_at = find_op_after_float(ain, update_fno, 35.0f, 0x33, 0);
	n = patch_floats(ain, update_fno, 35.0f, dmin, 1);
	if (insert_at) {
		p = extra;
		wr16(p, 0x40); p += 2;
		wrf(p, step); p += 4;
		wr16(p, 0x39); p += 2;
		new_addr = relocate_func(ain, ain->functions[update_fno].address,
					 func_end_addr(ain, update_fno), insert_at, extra, (size_t)(p - extra));
		if (new_addr) {
			ain->functions[update_fno].address = new_addr;
			rq_log("updateCameraLength: slider maps (d-%.2f)/%.2f, relocated to 0x%x",
			       dmin, step, new_addr);
		}
	} else {
		rq_log("updateCameraLength: could not find slider FTOI (float patches=%d)", n);
	}

	n = patch_floats(ain, scroll_fno, 35.0f, dmin, -1);
	insert_at = find_op_after_float(ain, scroll_fno, dmin, 0x36, 0);
	if (!insert_at)
		insert_at = find_op_after_float(ain, scroll_fno, 35.0f, 0x36, 0);
	if (insert_at) {
		p = extra;
		wr16(p, 0x40); p += 2;
		wrf(p, step); p += 4;
		wr16(p, 0x38); p += 2;
		new_addr = relocate_func(ain, ain->functions[scroll_fno].address,
					 func_end_addr(ain, scroll_fno), insert_at, extra, (size_t)(p - extra));
		if (new_addr) {
			ain->functions[scroll_fno].address = new_addr;
			rq_log("scrollCameraLength: formula dmin+n*%.2f, relocated to 0x%x", step, new_addr);
		}
	} else {
		rq_log("scrollCameraLength: could not find F_ADD after 35.0 (float patches=%d)", n);
	}

	jump_ip = find_jump_after_op(ain, move_fno, 0x3c);
	dest = last_localref_before_last_callfunc(ain, move_fno);
	if (jump_ip && dest) {
		wr32(ain->code + jump_ip + 2, (int32_t)dest);
		rq_log("move-distance: else-if fix JUMP 0x%x -> 0x%x (below %.1f stays stride 1.8)",
		       jump_ip, dest, lock);
	} else {
		rq_log("move-distance: jump fix skipped (jump=%u dest=%u)", jump_ip, dest);
	}

	(void)lock;
	return 1;
}

static int str_has_needle(struct string *s, const char *sjis_needle)
{
	if (!s || !sjis_needle || !*sjis_needle || s->size <= 0)
		return 0;
	if (s->size > 4000)
		return 0;
	return strstr(s->text, sjis_needle) != NULL;
}

static int skip_msgbox_sites(struct ain *ain, const char *utf8_needle)
{
	char *sjis;
	int i, nstr, str_hits = 0, hits = 0;
	uint8_t *used;

	if (!ain || !utf8_needle || !*utf8_needle)
		return 0;
	sjis = utf2sjis(utf8_needle, 0);
	if (!sjis || !*sjis) {
		free(sjis);
		return 0;
	}

	nstr = ain->nr_strings;
	if (nstr < 0)
		nstr = 0;
	used = xcalloc((size_t)nstr + 1, 1);
	if (used && ain->strings) {
		for (i = 0; i < nstr; i++) {
			if (str_has_needle(ain->strings[i], sjis)) {
				used[i] = 1;
				str_hits++;
			}
		}
	}
	rq_log("skip_msgbox '%s': nr_strings=%d matches=%d", utf8_needle, nstr, str_hits);

	for (i = 0; i < ain->nr_functions; i++) {
		uint32_t start = ain->functions[i].address;
		uint32_t end = func_end_addr(ain, i);
		uint32_t ip;
		for (ip = start; ip + 6 <= end; ) {
			uint16_t op = rd16(ain->code + ip);
			int len = instr_len(op);
			if (op == 0x41 && used) {
				int32_t idx = rd32(ain->code + ip + 2);
				if (idx >= 0 && idx < nstr && used[idx]) {
					uint32_t look, found = 0;
					for (look = ip + 6; look + 6 <= end && look <= ip + 80; ) {
						uint16_t op2 = rd16(ain->code + look);
						int len2 = instr_len(op2);
						if (op2 == 0x63) {
							int32_t sys = rd32(ain->code + look + 2);
							if (sys == 0x07 || sys == 0x12) {
								uint32_t after = look + 6;
								if (sys == 0x12) {
									wr16(ain->code + look, 0x00);
									wr32(ain->code + look + 2, 1);
								} else {
									wr16(ain->code + look, 0x2c);
									wr32(ain->code + look + 2, (int32_t)after);
								}
								hits++;
								found = after;
								break;
							}
						}
						if (len2 < 2)
							len2 = 2;
						look += (uint32_t)len2;
					}
					if (found) {
						ip = found;
						continue;
					}
				}
			}
			if (len < 2)
				len = 2;
			ip += (uint32_t)len;
		}
	}
	free(used);
	free(sjis);
	return hits;
}

static int log_matching_funcs(struct ain *ain, const char *utf8_needle)
{
	char *sjis;
	int i, n = 0;
	if (!ain || !utf8_needle || !*utf8_needle)
		return 0;
	sjis = utf2sjis(utf8_needle, 0);
	if (!sjis)
		return 0;
	for (i = 0; i < ain->nr_functions; i++) {
		const char *name = ain->functions[i].name;
		if (!name || !strstr(name, sjis))
			continue;
		rq_log("find '%s' fno=%d", utf8_needle, i);
		n++;
		if (n >= 40)
			break;
	}
	free(sjis);
	return n;
}

static int l_log(lua_State *L)
{
	int n = lua_gettop(L);
	luaL_Buffer b;
	int i;
	luaL_buffinit(L, &b);
	for (i = 1; i <= n; i++) {
		if (i > 1)
			luaL_addchar(&b, '\t');
		luaL_tolstring(L, i, NULL);
		luaL_addvalue(&b);
	}
	luaL_pushresult(&b);
	rq_log_tag(lua_mod_tag(L), "%s", lua_tostring(L, -1));
	return 0;
}

static int l_dungeon_camera(lua_State *L)
{
	float lock = 35.0f, dmin = 17.5f, dmax = 120.0f, fog = 160.0f;
	int steps = 25;
	if (!lua_istable(L, 1))
		return luaL_error(L, "dungeon_camera expects a table");
	lua_getfield(L, 1, "lock");
	if (lua_isnumber(L, -1)) lock = (float)lua_tonumber(L, -1);
	lua_getfield(L, 1, "min");
	if (lua_isnumber(L, -1)) dmin = (float)lua_tonumber(L, -1);
	lua_getfield(L, 1, "max");
	if (lua_isnumber(L, -1)) dmax = (float)lua_tonumber(L, -1);
	lua_getfield(L, 1, "fog_far");
	if (lua_isnumber(L, -1)) fog = (float)lua_tonumber(L, -1);
	lua_getfield(L, 1, "slider_steps");
	if (lua_isinteger(L, -1)) steps = (int)lua_tointeger(L, -1);
	lua_settop(L, 1);
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	lua_pushboolean(L, dungeon_camera(g_ain, lock, dmin, dmax, fog, steps));
	return 1;
}

static int l_skip_sdl_msgbox(lua_State *L)
{
	(void)L;
	g_rq_skip_sdl = 1;
	rq_log("skip-network-popup: SDL msgbox filter on (getserverversion/TADA/HTTP)");
	return 0;
}

static int l_skip_msgbox(lua_State *L)
{
	const char *needle = luaL_checkstring(L, 1);
	int n;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	n = skip_msgbox_sites(g_ain, needle);
	rq_log("skip_msgbox '%s': %d CALLSYS site(s)", needle, n);
	lua_pushinteger(L, n);
	return 1;
}

static int l_find_funcs(lua_State *L)
{
	const char *needle = luaL_checkstring(L, 1);
	int n;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	n = log_matching_funcs(g_ain, needle);
	lua_pushinteger(L, n);
	return 1;
}

static int l_loot_install(lua_State *L)
{
	int ok;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	ok = rq_loot_install(g_ain);
	if (!ok)
		return luaL_error(L, "loot.install failed (see mods/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

static int l_chest_install(lua_State *L)
{
	int ok;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	ok = rq_chest_install(g_ain);
	if (!ok)
		return luaL_error(L, "chest.install failed (see mods/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

static int l_orb_install(lua_State *L)
{
	int ok;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	ok = rq_orb_install(g_ain);
	if (!ok)
		return luaL_error(L, "orb.install failed (see mods/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

static int l_skill_install(lua_State *L)
{
	int ok;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	ok = rq_skill_install(g_ain);
	if (!ok)
		return luaL_error(L, "skill.install failed (see mods/rqmod.log)");
	lua_pushboolean(L, 1);
	return 1;
}

static void set_cfn(lua_State *L, const char *name, lua_CFunction fn)
{
	lua_pushcfunction(L, fn);
	lua_setfield(L, -2, name);
}

static int l_patch_float(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	float from = (float)luaL_checknumber(L, 2);
	float to = (float)luaL_checknumber(L, 3);
	int nth = (int)luaL_optinteger(L, 4, -1);
	int fno, n;
	if (!g_ain)
		return luaL_error(L, "AIN not loaded");
	fno = find_func_utf8(g_ain, name);
	if (fno < 0)
		return luaL_error(L, "function not found: %s", name);
	n = patch_floats(g_ain, fno, from, to, nth);
	lua_pushinteger(L, n);
	return 1;
}

static void open_rq(lua_State *L)
{
	lua_newtable(L);
	set_cfn(L, "log", l_log);
	set_cfn(L, "on_frame", rq_lua_on_frame);
	lua_pushstring(L, g_rq_game_dir);
	lua_setfield(L, -2, "game_dir");
	lua_pushstring(L, g_rq_mods_dir);
	lua_setfield(L, -2, "mods_dir");

	lua_newtable(L);
	set_cfn(L, "patch_float", l_patch_float);
	set_cfn(L, "skip_msgbox", l_skip_msgbox);
	set_cfn(L, "find", l_find_funcs);
	lua_setfield(L, -2, "ain");

	lua_newtable(L);
	set_cfn(L, "camera", l_dungeon_camera);
	set_cfn(L, "active", rq_lua_dungeon_active);
	set_cfn(L, "exploring", rq_lua_dungeon_exploring);
	lua_setfield(L, -2, "dungeon");

	lua_newtable(L);
	set_cfn(L, "mouse", rq_lua_input_mouse);
	set_cfn(L, "key", rq_lua_input_key);
	lua_setfield(L, -2, "input");

	lua_newtable(L);
	set_cfn(L, "rect", rq_lua_hud_rect);
	set_cfn(L, "text", rq_lua_hud_text);
	set_cfn(L, "cg", rq_lua_hud_cg);
	set_cfn(L, "hit", rq_lua_hud_hit);
	set_cfn(L, "clicked", rq_lua_hud_clicked);
	set_cfn(L, "block_game_input", rq_lua_hud_block);
	set_cfn(L, "screen", rq_lua_hud_screen);
	set_cfn(L, "text_width", rq_lua_hud_text_width);
	set_cfn(L, "wrap", rq_lua_hud_wrap);
	lua_setfield(L, -2, "hud");

	lua_newtable(L);
	set_cfn(L, "install", l_loot_install);
	set_cfn(L, "list", rq_lua_loot_list);
	lua_setfield(L, -2, "loot");

	lua_newtable(L);
	set_cfn(L, "install", l_chest_install);
	set_cfn(L, "on_attempt", rq_lua_chest_on_attempt);
	set_cfn(L, "on_result", rq_lua_chest_on_result);
	set_cfn(L, "get", rq_lua_chest_get);
	set_cfn(L, "set", rq_lua_chest_set);
	lua_setfield(L, -2, "chest");

	lua_newtable(L);
	set_cfn(L, "install", l_orb_install);
	lua_setfield(L, -2, "orb");

	lua_newtable(L);
	set_cfn(L, "install", l_skill_install);
	set_cfn(L, "on_describe", rq_lua_skill_on_describe);
	set_cfn(L, "get", rq_lua_skill_get);
	lua_setfield(L, -2, "skill");

	/* Aliases so camera-zoom / skip-network-popup stay unchanged. */
	set_cfn(L, "dungeon_camera", l_dungeon_camera);
	set_cfn(L, "patch_float", l_patch_float);
	set_cfn(L, "skip_msgbox", l_skip_msgbox);
	set_cfn(L, "skip_sdl_msgbox", l_skip_sdl_msgbox);
	set_cfn(L, "find", l_find_funcs);

	lua_setglobal(L, "rq");
}

static int load_one_lua(lua_State *L, const char *path)
{
	int err = luaL_loadfile(L, path);
	if (err != LUA_OK) {
		rq_log("load error %s: %s", path, lua_tostring(L, -1));
		lua_pop(L, 1);
		return 0;
	}
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		rq_log("run error %s: %s", path, lua_tostring(L, -1));
		lua_pop(L, 1);
		return 0;
	}
	rq_log("Loaded mod file %s", path);
	return 1;
}

#ifdef _WIN32
static int load_mods(lua_State *L)
{
	WIN32_FIND_DATAA fd;
	HANDLE h;
	char pattern[RQ_PATH_MAX], path[RQ_PATH_MAX];
	int n = 0;

	snprintf(pattern, sizeof pattern, "%s\\*.lua", g_rq_mods_dir);
	h = FindFirstFileA(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			snprintf(path, sizeof path, "%s\\%s", g_rq_mods_dir, fd.cFileName);
			n += load_one_lua(L, path);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}

	snprintf(pattern, sizeof pattern, "%s\\*", g_rq_mods_dir);
	h = FindFirstFileA(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;
			if (fd.cFileName[0] == '.')
				continue;
			snprintf(path, sizeof path, "%s\\%s\\init.lua", g_rq_mods_dir, fd.cFileName);
			if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
				n += load_one_lua(L, path);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	return n;
}

static int dir_exists(const char *p)
{
	DWORD a = GetFileAttributesA(p);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int file_exists(const char *p)
{
	DWORD a = GetFileAttributesA(p);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void resolve_dirs(void)
{
	char exe[RQ_PATH_MAX];
	char parent[RQ_PATH_MAX];
	char *slash;
	char ainpath[RQ_PATH_MAX], inipath[RQ_PATH_MAX], mods1[RQ_PATH_MAX], mods2[RQ_PATH_MAX];

	GetModuleFileNameA(NULL, exe, RQ_PATH_MAX);
	slash = strrchr(exe, '\\');
	if (slash)
		*slash = 0;
	snprintf(g_rq_exe_dir, sizeof g_rq_exe_dir, "%s", exe);

	snprintf(parent, sizeof parent, "%s", exe);
	slash = strrchr(parent, '\\');
	if (slash)
		*slash = 0;
	else
		snprintf(parent, sizeof parent, "%s", exe);

	snprintf(mods1, sizeof mods1, "%s\\mods", exe);
	snprintf(mods2, sizeof mods2, "%s\\mods", parent);
	snprintf(ainpath, sizeof ainpath, "%s\\RanceQuest.ain", parent);
	snprintf(inipath, sizeof inipath, "%s\\AliceStart.ini", parent);

	if (file_exists(ainpath) || file_exists(inipath) || dir_exists(mods2)) {
		snprintf(g_rq_game_dir, sizeof g_rq_game_dir, "%s", parent);
		snprintf(g_rq_mods_dir, sizeof g_rq_mods_dir, "%s", mods2);
	} else {
		snprintf(g_rq_game_dir, sizeof g_rq_game_dir, "%s", exe);
		snprintf(g_rq_mods_dir, sizeof g_rq_mods_dir, "%s", mods1);
	}
}
#else
static int load_mods(lua_State *L) { (void)L; return 0; }
static int dir_exists(const char *p) { (void)p; return 0; }
static void resolve_dirs(void)
{
	g_rq_exe_dir[0] = '.';
	g_rq_exe_dir[1] = 0;
	snprintf(g_rq_game_dir, sizeof g_rq_game_dir, ".");
	snprintf(g_rq_mods_dir, sizeof g_rq_mods_dir, "./mods");
}
#endif

static int json_bool_obj(cJSON *o, const char *key, int deflt)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
	if (!v)
		return deflt;
	if (cJSON_IsBool(v))
		return cJSON_IsTrue(v);
	if (cJSON_IsNumber(v))
		return v->valuedouble != 0;
	return deflt;
}

static const char DEFAULT_JSON[] =
	"{\n"
	"  \"terminal\": true,\n"
	"  \"verbose\": false,\n"
	"  \"profile\": {\n"
	"    \"csv\": false,\n"
	"    \"leftover_ain\": false,\n"
	"    \"hll\": false,\n"
	"    \"text_cache\": false,\n"
	"    \"update_split\": false,\n"
	"    \"vm_ops\": false,\n"
	"    \"ain_fn\": false\n"
	"  }\n"
	"}\n";

static void apply_profile_json(cJSON *root)
{
	struct rq_prof_runtime rt = {0};
	cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "profile");
	if (p && (cJSON_IsBool(p) || cJSON_IsNumber(p))) {
		int on = cJSON_IsTrue(p) || (cJSON_IsNumber(p) && p->valuedouble != 0);
		rt.csv = rt.leftover_ain = rt.hll = rt.text_cache = rt.update_split = rt.vm_ops = rt.ain_fn = on;
	} else if (p && cJSON_IsObject(p)) {
		rt.csv = json_bool_obj(p, "csv", 0);
		rt.leftover_ain = json_bool_obj(p, "leftover_ain", 0);
		rt.hll = json_bool_obj(p, "hll", 0);
		rt.text_cache = json_bool_obj(p, "text_cache", 0);
		rt.update_split = json_bool_obj(p, "update_split", 0);
		rt.vm_ops = json_bool_obj(p, "vm_ops", 0);
		rt.ain_fn = json_bool_obj(p, "ain_fn", 0);
	}
	rq_prof_set_runtime(&rt);
}

static void load_config(void)
{
	char path[RQ_PATH_MAX];
	FILE *fp;
	long n;
	char *buf;
	cJSON *root;

	g_rq_verbose = 0;
	g_rq_terminal = 1;
	snprintf(path, sizeof path, "%s\\config.json", g_rq_exe_dir);
	fp = fopen(path, "rb");
	if (!fp) {
		fp = fopen(path, "wb");
		if (fp) {
			fwrite(DEFAULT_JSON, 1, sizeof DEFAULT_JSON - 1, fp);
			fclose(fp);
		}
		{
			struct rq_prof_runtime z = {0};
			rq_prof_set_runtime(&z);
		}
		return;
	}
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (n < 0 || n > 1 << 20) {
		fclose(fp);
		return;
	}
	buf = xmalloc((size_t)n + 1);
	n = (long)fread(buf, 1, (size_t)n, fp);
	fclose(fp);
	buf[n] = 0;
	root = cJSON_Parse(buf);
	free(buf);
	if (!root)
		return;
	g_rq_verbose = json_bool_obj(root, "verbose", 0);
	g_rq_terminal = json_bool_obj(root, "terminal", 1);
	apply_profile_json(root);
	cJSON_Delete(root);
}

void rq_mods_early_init(void)
{
	static int once;
	char logpath[RQ_PATH_MAX];
	if (once)
		return;
	once = 1;
	resolve_dirs();
	load_config();
	rq_open_terminal();
	if (!g_rq_log && dir_exists(g_rq_mods_dir)) {
		snprintf(logpath, sizeof logpath, "%s\\rqmod.log", g_rq_mods_dir);
		g_rq_log = fopen(logpath, "w");
	}
	if (g_rq_terminal)
		rq_print_banner();
	rq_log("Config: terminal %s, verbose %s, profiling off unless a profile flag is on",
	       g_rq_terminal ? "on (this console)" : "off",
	       g_rq_verbose ? "on" : "off");
}

bool rq_mods_swallow_msgbox(const char *title, const char *msg)
{
	const char *p;
	if (!g_rq_skip_sdl)
		return false;
	p = msg && msg[0] ? msg : title;
	if (!p)
		return false;
	if (strstr(p, "getserverversion")
	    || strstr(p, "TADA")
	    || strstr(p, "HTTPDownloader")
	    || strstr(p, "Unimplemented HLL")
	    || strstr(p, "データ送信")) {
		rq_log("skip-network-popup: swallowed SDL msgbox: %.160s", p);
		return true;
	}
	return false;
}

static int utf_has(const char *s, const char *n)
{
	return s && n && *n && strstr(s, n) != NULL;
}

static const char *classify(const char *u)
{
	if (!u || !*u)
		return NULL;
	if (utf_has(u, "セーブ") || utf_has(u, "Save") || utf_has(u, "SAVE")
	    || utf_has(u, "保存") || utf_has(u, "ロード")
	    || utf_has(u, "LoadData") || utf_has(u, "SaveData"))
		return "save";
	if (utf_has(u, "ダンジョン") || utf_has(u, "Dungeon")
	    || utf_has(u, "突入") || utf_has(u, "脱出") || utf_has(u, "逃走")
	    || utf_has(u, "Escape") || utf_has(u, "パーティ") || utf_has(u, "Party")
	    || utf_has(u, "加入") || utf_has(u, "マップ_サスペンド")
	    || utf_has(u, "マップ_レジューム") || utf_has(u, "３Ｄマップ_初期")
	    || utf_has(u, "３Ｄマップ_終了"))
		return "dungeon";
	if (utf_has(u, "クエスト")
	    && (utf_has(u, "開始") || utf_has(u, "Start") || utf_has(u, "Enter")
		|| utf_has(u, "Load") || utf_has(u, "クリア")))
		return "dungeon";
	if (utf_has(u, "戦闘") || utf_has(u, "Battle") || utf_has(u, "攻撃")
	    || utf_has(u, "ダメージ") || utf_has(u, "Damage") || utf_has(u, "Attack")
	    || utf_has(u, "スキル") || utf_has(u, "Skill"))
		return "combat";
	if (utf_has(u, "購入") || utf_has(u, "買う") || utf_has(u, "Buy")
	    || utf_has(u, "Shop") || utf_has(u, "ショップ"))
		return "shop";
	if (utf_has(u, "会話") || utf_has(u, "Dialog") || utf_has(u, "Talk")
	    || utf_has(u, "メッセージ開"))
		return "dialog";
	if (utf_has(u, "解像度") || utf_has(u, "Resolution") || utf_has(u, "Config")
	    || utf_has(u, "コンフィグ") || utf_has(u, "音量") || utf_has(u, "Volume")
	    || utf_has(u, "オート") || utf_has(u, "Checkbox") || utf_has(u, "チェック"))
		return "settings";
	return NULL;
}

void rq_mods_on_vm_call(int fno, int struct_page)
{
	static unsigned last_tick;
	static int last_fno = -1;
	static unsigned spam;
	struct ain_function *fn;
	char *utf = NULL;
	const char *cat;
	char line[768];
	int used, nargs, i;
	unsigned now;

	if (!g_rq_verbose || !g_ain)
		return;
	if (fno < 0 || fno >= g_ain->nr_functions)
		return;
	fn = &g_ain->functions[fno];
	if (!fn->name)
		return;
	utf = sjis2utf(fn->name, 0);
	cat = classify(utf);
	if (!cat) {
		free(utf);
		return;
	}
#ifdef _WIN32
	now = GetTickCount();
#else
	now = 0;
#endif
	if (fno == last_fno && now - last_tick < 40) {
		spam++;
		free(utf);
		return;
	}
	if (now == last_tick && spam > 30) {
		free(utf);
		return;
	}
	if (now != last_tick)
		spam = 0;
	last_tick = now;
	last_fno = fno;

	nargs = fn->nr_args;
	if (nargs < 0)
		nargs = 0;
	if (nargs > 8)
		nargs = 8;
	if (stack_ptr < nargs)
		nargs = stack_ptr;

	used = snprintf(line, sizeof line, "verbose %s %s", cat, utf ? utf : "?");
	for (i = 0; i < nargs && used > 0 && used < (int)sizeof line - 48; i++) {
		int tcode = AIN_INT;
		const char *vname = "arg";
		char nbuf[72];
		union vm_value val = stack[stack_ptr - nargs + i];
		if (fn->vars && i < fn->nr_vars) {
			char *un;
			tcode = fn->vars[i].type.data;
			if (fn->vars[i].name) {
				un = sjis2utf(fn->vars[i].name, 0);
				snprintf(nbuf, sizeof nbuf, "%s", un && *un ? un : "arg");
				free(un);
				vname = nbuf;
			}
		} else {
			snprintf(nbuf, sizeof nbuf, "arg%d", i);
			vname = nbuf;
		}
		if (tcode == AIN_FLOAT) {
			used += snprintf(line + used, sizeof line - used, " %s=%g", vname, val.f);
		} else if (tcode == AIN_STRING && val.i > 0) {
			/* heap string slot — skip detailed decode if not a pointer page */
			used += snprintf(line + used, sizeof line - used, " %s=%d", vname, val.i);
		} else {
			used += snprintf(line + used, sizeof line - used, " %s=%d", vname, val.i);
		}
	}
	if (struct_page >= 0 && used < (int)sizeof line - 24)
		snprintf(line + used, sizeof line - used, " this=%d", struct_page);
	rq_log("%s", line);
	free(utf);
}

void rq_mods_init(struct ain *ain)
{
	char logpath[RQ_PATH_MAX];
	int n;

	rq_mods_early_init();
	g_ain = ain;

	if (!g_rq_log && dir_exists(g_rq_mods_dir)) {
		snprintf(logpath, sizeof logpath, "%s\\rqmod.log", g_rq_mods_dir);
		g_rq_log = fopen(logpath, "w");
	} else if (!dir_exists(g_rq_mods_dir)) {
		rq_log("No mods folder at %s — running vanilla AIN", g_rq_mods_dir);
	}

	rq_log("Game folder: %s", g_rq_game_dir);
	rq_log("Mods folder: %s", g_rq_mods_dir);
	rq_log("AIN loaded (%d functions, version %d)",
	       ain ? ain->nr_functions : 0, ain ? ain->version : 0);
	if (g_rq_verbose)
		rq_log("Verbose event log is on (settings / save / dungeon / shop / combat)");
	rq_log("Profile: csv=%d leftover=%d hll=%d text_cache=%d update_split=%d vm_ops=%d ain_fn=%d",
	       g_rq_prof.csv, g_rq_prof.leftover_ain, g_rq_prof.hll, g_rq_prof.text_cache,
	       g_rq_prof.update_split, g_rq_prof.vm_ops, g_rq_prof.ain_fn);

	rq_mods_agent_start();

	g_L = luaL_newstate();
	if (!g_L) {
		rq_log("luaL_newstate failed");
		return;
	}
	luaL_openlibs(g_L);
	open_rq(g_L);
	n = load_mods(g_L);
	rq_log("Loaded %d Lua mod(s). Ready.", n);
}

#endif /* RQ_MODLOADER */
