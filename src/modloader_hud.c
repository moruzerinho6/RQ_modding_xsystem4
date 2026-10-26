/* Copyright (C) 2026 Rance Quest modding fork
 *
 * Immediate-mode in-game HUD primitives for Lua mods.
 * Drawn on top of Chipmunk/SACT after scene_render.
 */

#ifdef RQ_MODLOADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/cg.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "input.h"
#include "modloader.h"
#include "modloader_priv.h"
#include "vm.h"
#include "xsystem4.h"

#define HUD_MAX_CMD 160
#define HUD_MAX_CG 48

enum hud_kind {
	HUD_RECT = 1,
	HUD_CG,
	HUD_TEXT
};

struct hud_cmd {
	int kind;
	int x, y, w, h;
	int r, g, b, a;
	int cg;
	float size;
	char *text;
};

struct hud_cg {
	int no;
	Texture t;
};

#define MODAL_CONFIG 1
#define MODAL_RESULT 2
#define MODAL_RESULT_ITEM 4
#define MODAL_ADV 8
#define MODAL_SELECT 16
#define MODAL_N 5

static int g_dungeon;
static int g_shown;
static int g_suspended;
static int g_battle;
static int g_profile;
static int g_modal_n[MODAL_N];
static int g_block_mouse;
static int g_on_frame_ref = LUA_NOREF;
static int g_mx, g_my, g_left, g_right, g_left_edge, g_prev_left;
static int g_ncmd;
static int g_drew;
static struct hud_cmd g_cmd[HUD_MAX_CMD];
static struct hud_cg g_cg[HUD_MAX_CG];
static int g_ncg;

static void emit0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, (uint16_t)op);
}

static void emit1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, (uint16_t)op);
	buffer_write_int32(out, (uint32_t)arg);
}

static int find_fn(struct ain *ain, const char *utf8)
{
	char *sjis = utf2sjis(utf8, 0);
	int fno;
	if (!sjis)
		return -1;
	fno = ain_get_function(ain, sjis);
	free(sjis);
	return fno;
}

static void commit_wrap(struct ain *ain, int fno, struct buffer *out)
{
	ain->functions[fno].address = (uint32_t)(ain->code_size + 6);
	ain->code = xrealloc(ain->code, ain->code_size + out->index);
	memcpy(ain->code + ain->code_size, out->buf, out->index);
	ain->code_size += out->index;
}

static int wrap_method_flag(struct ain *ain, int fno, int sys, int before)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	if (before)
		emit1(&out, CALLSYS, sys);
	/* CALLMETHOD pops nr_args, then the struct page under them. */
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	if (!before)
		emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped method fno=%d (%d args)", fno, nargs);
	return 1;
}

static int wrap_func_flag(struct ain *ain, int fno, int sys, int before)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	if (before)
		emit1(&out, CALLSYS, sys);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	if (!before)
		emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped function fno=%d (%d args)", fno, nargs);
	return 1;
}

static int wrap_func_around(struct ain *ain, int fno, int sys_on, int sys_off)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, CALLSYS, sys_on);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLFUNC, dup);
	emit1(&out, CALLSYS, sys_off);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped function around fno=%d", fno);
	return 1;
}

static int wrap_set_show(struct ain *ain, int fno)
{
	int dup = ain_dup_function(ain, fno);
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, SH_LOCALREF, 0);
	emit1(&out, CALLSYS, VM_XSYS_HUD_SHOW);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("HUD: watching T_MapParts@setShow so Item Previews hide when the map HUD is hidden");
	return 1;
}

static int wrap_method_modal(struct ain *ain, int fno, int mask, int on)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;
	int sys = on ? VM_XSYS_HUD_MODAL_ON : VM_XSYS_HUD_MODAL_OFF;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, PUSH, mask);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	if (g_rq_verbose)
		rq_log("HUD: wrapped create/delete fno=%d mask=%d on=%d", fno, mask, on);
	return 1;
}

/* Blocking @select/@wait: ON for the call, OFF when it returns. Do not wrap
 * create/delete of T_Select / T_MapMessage — those objects stay alive on the
 * map, so a leftover bit kept exploring() false on real map-walk. */
static int wrap_modal_around(struct ain *ain, int fno, int mask, int method)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	emit1(&out, PUSH, mask);
	emit1(&out, CALLSYS, VM_XSYS_HUD_MODAL_ON);
	if (method)
		emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	if (method)
		emit1(&out, CALLMETHOD, dup);
	else
		emit1(&out, CALLFUNC, dup);
	emit1(&out, PUSH, mask);
	emit1(&out, CALLSYS, VM_XSYS_HUD_MODAL_OFF);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	return 1;
}

static void wrap_blocking(struct ain *ain, const char *name, int mask, int important)
{
	int fno = find_fn(ain, name);
	int method;
	if (fno < 0) {
		if (important || g_rq_verbose)
			rq_log("HUD: %s is not in this AIN, so it was not wrapped", name);
		return;
	}
	method = strchr(name, '@') != NULL;
	wrap_modal_around(ain, fno, mask, method);
	rq_log("HUD: wrapped %s — Item Previews hide while that screen runs", name);
}

static void wrap_lifetime(struct ain *ain, const char *cls, int mask)
{
	char buf[160];
	int c, d;

	snprintf(buf, sizeof buf, "%s@create", cls);
	c = find_fn(ain, buf);
	if (c >= 0)
		wrap_method_modal(ain, c, mask, 1);
	snprintf(buf, sizeof buf, "%s@delete", cls);
	d = find_fn(ain, buf);
	if (d >= 0)
		wrap_method_modal(ain, d, mask, 0);
	if (c >= 0 || d >= 0)
		rq_log("HUD: watching %s create/delete so Item Previews hide on the result screen", cls);
	else if (g_rq_verbose)
		rq_log("HUD: %s is not in this AIN (result overlay skipped)", cls);
}

static void hud_make_ts(struct text_style *ts, float size, int r, int g, int b)
{
	memset(ts, 0, sizeof(*ts));
	ts->face = FONT_GOTHIC;
	ts->size = size > 1.0f ? size : 16.0f;
	ts->weight = FW_NORMAL;
	ts->edge_left = 1.0f;
	ts->edge_up = 1.0f;
	ts->edge_right = 1.0f;
	ts->edge_down = 1.0f;
	ts->color.r = (uint8_t)r;
	ts->color.g = (uint8_t)g;
	ts->color.b = (uint8_t)b;
	ts->color.a = 255;
	ts->edge_color.a = 255;
	ts->scale_x = 1.0f;
	ts->space_scale_x = 1.0f;
}

static int hud_measure_sjis(const char *sjis, float size)
{
	struct text_style ts;
	if (!sjis || !*sjis)
		return 0;
	hud_make_ts(&ts, size, 255, 255, 255);
	return (int)ceilf(gfx_size_text(&ts, sjis));
}

static void clear_cmds(void)
{
	int i;
	for (i = 0; i < g_ncmd; i++)
		free(g_cmd[i].text);
	g_ncmd = 0;
}

static struct hud_cmd *push_cmd(int kind)
{
	struct hud_cmd *c;
	if (g_ncmd >= HUD_MAX_CMD)
		return NULL;
	c = &g_cmd[g_ncmd++];
	memset(c, 0, sizeof(*c));
	c->kind = kind;
	g_drew = 1;
	return c;
}

static Texture *cg_tex(int no)
{
	int i;
	struct cg *cg;
	if (no <= 0)
		return NULL;
	for (i = 0; i < g_ncg; i++) {
		if (g_cg[i].no == no)
			return &g_cg[i].t;
	}
	if (g_ncg >= HUD_MAX_CG)
		return g_ncg ? &g_cg[0].t : NULL;
	cg = asset_cg_load(no);
	if (!cg)
		return NULL;
	g_cg[g_ncg].no = no;
	gfx_init_texture_with_cg(&g_cg[g_ncg].t, cg);
	cg_free(cg);
	return &g_cg[g_ncg++].t;
}

static int hit(int x, int y, int w, int h)
{
	return g_mx >= x && g_my >= y && g_mx < x + w && g_my < y + h;
}

static int modal_index(int mask)
{
	switch (mask) {
	case MODAL_CONFIG: return 0;
	case MODAL_RESULT: return 1;
	case MODAL_RESULT_ITEM: return 2;
	case MODAL_ADV: return 3;
	case MODAL_SELECT: return 4;
	default: return -1;
	}
}

static int modal_any(void)
{
	int i;
	for (i = 0; i < MODAL_N; i++) {
		if (g_modal_n[i] > 0)
			return 1;
	}
	return 0;
}

static void modal_clear(void)
{
	memset(g_modal_n, 0, sizeof g_modal_n);
}

static const char *modal_reason(void)
{
	if (g_modal_n[0] > 0)
		return "Game Config is open";
	if (g_modal_n[1] > 0)
		return "the dungeon result screen is open";
	if (g_modal_n[2] > 0)
		return "the appraisal / identify screen is open";
	if (g_modal_n[3] > 0)
		return "a dungeon dialog / ADV is running";
	if (g_modal_n[4] > 0)
		return "a choice dialog is open";
	return "an overlay is open";
}

int rq_dungeon_active(void)
{
	return g_dungeon;
}

int rq_dungeon_exploring(void)
{
	return g_dungeon && g_shown && !g_suspended && !g_battle && !g_profile && !modal_any();
}

static void log_exploring_change(void)
{
	static int prev = -1;
	static int prev_d, prev_sh, prev_su, prev_b, prev_p, prev_m[MODAL_N];
	int now, i, changed;

	if (!g_rq_verbose && !g_rq_terminal)
		return;
	now = rq_dungeon_exploring() ? 1 : 0;
	changed = now != prev || prev_d != g_dungeon || prev_sh != g_shown
		|| prev_su != g_suspended || prev_b != g_battle || prev_p != g_profile;
	for (i = 0; i < MODAL_N; i++) {
		if (prev_m[i] != g_modal_n[i])
			changed = 1;
	}
	if (!changed)
		return;
	prev = now;
	prev_d = g_dungeon;
	prev_sh = g_shown;
	prev_su = g_suspended;
	prev_b = g_battle;
	prev_p = g_profile;
	memcpy(prev_m, g_modal_n, sizeof prev_m);
	if (now) {
		rq_log("Item Previews: map-walk (button can show)");
		return;
	}
	if (!g_dungeon)
		rq_log("Item Previews hidden: not in a dungeon");
	else if (!g_shown)
		rq_log("Item Previews hidden: the map HUD is hidden");
	else if (g_suspended)
		rq_log("Item Previews hidden: the map is suspended (join dialog / event)");
	else if (g_battle)
		rq_log("Item Previews hidden: battle");
	else if (g_profile)
		rq_log("Item Previews hidden: character profile");
	else if (modal_any())
		rq_log("Item Previews hidden: %s", modal_reason());
	else
		rq_log("Item Previews hidden: exploring is false");
}

int rq_hud_block_mouse(void)
{
	return g_block_mouse;
}

int rq_hud_syscall(int code)
{
	int i;
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_HUD_ON:
		g_dungeon = 1;
		g_shown = 1;
		g_suspended = 0;
		g_battle = 0;
		g_profile = 0;
		modal_clear();
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_OFF:
		g_dungeon = 0;
		g_shown = 0;
		g_suspended = 0;
		g_battle = 0;
		g_profile = 0;
		modal_clear();
		g_block_mouse = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_SUSPEND:
		g_suspended = 1;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_RESUME:
		g_suspended = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_BATTLE_ON:
		g_battle = 1;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_BATTLE_OFF:
		g_battle = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_SHOW:
		g_shown = stack_pop().i ? 1 : 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_PROFILE_ON:
		g_profile = 1;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_PROFILE_OFF:
		g_profile = 0;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_MODAL_ON:
		i = modal_index(stack_pop().i);
		if (i >= 0)
			g_modal_n[i]++;
		log_exploring_change();
		return 1;
	case VM_XSYS_HUD_MODAL_OFF:
		i = modal_index(stack_pop().i);
		if (i >= 0 && g_modal_n[i] > 0)
			g_modal_n[i]--;
		log_exploring_change();
		return 1;
	default:
		return 0;
	}
}

int rq_hud_install(struct ain *ain)
{
	int f_create, f_delete, f_show, f_susp, f_res, f_battle, f_st_c, f_st_d;
	if (!ain)
		return 0;
	f_create = find_fn(ain, "T_MapParts@create");
	f_delete = find_fn(ain, "T_MapParts@delete");
	if (f_create < 0 || f_delete < 0) {
		rq_log("HUD: T_MapParts create/delete missing — exploring cannot turn on");
		return 0;
	}
	wrap_method_flag(ain, f_create, VM_XSYS_HUD_ON, 0);
	wrap_method_flag(ain, f_delete, VM_XSYS_HUD_OFF, 0);
	rq_log("HUD: dungeon map create/delete turns exploring on and off");
	f_show = find_fn(ain, "T_MapParts@setShow");
	if (f_show >= 0)
		wrap_set_show(ain, f_show);
	f_susp = find_fn(ain, "マップ_サスペンド");
	f_res = find_fn(ain, "マップ_レジューム");
	if (f_susp >= 0) {
		wrap_func_flag(ain, f_susp, VM_XSYS_HUD_SUSPEND, 0);
		rq_log("HUD: map suspend (join dialog / events) hides Item Previews");
	}
	if (f_res >= 0)
		wrap_func_flag(ain, f_res, VM_XSYS_HUD_RESUME, 0);
	f_battle = find_fn(ain, "Ｒ８戦闘");
	if (f_battle >= 0) {
		wrap_func_around(ain, f_battle, VM_XSYS_HUD_BATTLE_ON,
				 VM_XSYS_HUD_BATTLE_OFF);
		rq_log("HUD: battle hides Item Previews until it returns");
	}
	f_st_c = find_fn(ain, "T_Status@create");
	f_st_d = find_fn(ain, "T_Status@delete");
	if (f_st_c >= 0)
		wrap_method_flag(ain, f_st_c, VM_XSYS_HUD_PROFILE_ON, 0);
	if (f_st_d >= 0)
		wrap_method_flag(ain, f_st_d, VM_XSYS_HUD_PROFILE_OFF, 0);
	if (f_st_c >= 0)
		rq_log("HUD: character profile (T_Status) hides Item Previews");
	/* Blocking loops only. create/delete of T_Select / T_MapMessage / T_SystemMenu
	 * stays off — those objects live on the map and never deleted, which stuck
	 * exploring() false on real map-walk. */
	wrap_blocking(ain, "T_SystemMenu@select", MODAL_CONFIG, 1);
	wrap_blocking(ain, "T_Result@select", MODAL_RESULT, 0);
	wrap_blocking(ain, "T_ResultItem@select", MODAL_RESULT_ITEM, 0);
	wrap_blocking(ain, "リザルト鑑定", MODAL_RESULT_ITEM, 0);
	wrap_lifetime(ain, "T_Result", MODAL_RESULT);
	wrap_lifetime(ain, "T_ResultItem", MODAL_RESULT_ITEM);
	wrap_blocking(ain, "T_MapMessage@select", MODAL_ADV, 0);
	wrap_blocking(ain, "T_MapMessage@wait", MODAL_ADV, 0);
	wrap_blocking(ain, "T_ADV@select", MODAL_ADV, 0);
	wrap_blocking(ain, "T_ADV@wait", MODAL_ADV, 0);
	wrap_blocking(ain, "T_Select@select", MODAL_SELECT, 0);
	return 1;
}

int rq_lua_on_frame(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TFUNCTION);
	if (g_on_frame_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, g_on_frame_ref);
	lua_pushvalue(L, 1);
	g_on_frame_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int rq_lua_dungeon_active(lua_State *L)
{
	lua_pushboolean(L, g_dungeon);
	return 1;
}

int rq_lua_dungeon_exploring(lua_State *L)
{
	lua_pushboolean(L, rq_dungeon_exploring());
	return 1;
}

int rq_lua_input_mouse(lua_State *L)
{
	lua_pushinteger(L, g_mx);
	lua_pushinteger(L, g_my);
	lua_pushboolean(L, g_left);
	lua_pushboolean(L, g_right);
	return 4;
}

int rq_lua_input_key(lua_State *L)
{
	int code = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, key_is_down_raw(code));
	return 1;
}

int rq_lua_hud_screen(lua_State *L)
{
	lua_pushinteger(L, config.view_width);
	lua_pushinteger(L, config.view_height);
	return 2;
}

int rq_lua_hud_text_width(lua_State *L)
{
	const char *utf = luaL_checkstring(L, 1);
	float size = 16.0f;
	char *sjis;
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "size");
		if (lua_isnumber(L, -1))
			size = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);
	} else if (lua_isnumber(L, 2)) {
		size = (float)lua_tonumber(L, 2);
	}
	sjis = utf2sjis(utf ? utf : "", 0);
	if (!sjis) {
		lua_pushinteger(L, 0);
		return 1;
	}
	lua_pushinteger(L, hud_measure_sjis(sjis, size));
	free(sjis);
	return 1;
}

static void hud_push_utf_line(lua_State *L, int n, const char *sjis)
{
	char *utf = sjis2utf(sjis ? sjis : "", 0);
	lua_pushinteger(L, n);
	lua_pushstring(L, utf ? utf : "");
	lua_settable(L, -3);
	free(utf);
}

int rq_lua_hud_wrap(lua_State *L)
{
	const char *utf = luaL_checkstring(L, 1);
	int max_w = (int)luaL_checkinteger(L, 2);
	float size = (float)luaL_optnumber(L, 3, 14.0f);
	struct text_style ts;
	char *sjis, *p, *line_start, *last_brk;
	int n = 0;

	lua_newtable(L);
	if (max_w < 8)
		max_w = 8;
	hud_make_ts(&ts, size, 255, 255, 255);
	sjis = utf2sjis(utf ? utf : "", 0);
	if (!sjis || !*sjis) {
		free(sjis);
		return 1;
	}
	p = sjis;
	line_start = p;
	last_brk = NULL;
	while (*p) {
		char *next;
		char saved;
		int w;

		if (*p == '\n' || *p == '\r') {
			saved = *p;
			*p = 0;
			hud_push_utf_line(L, ++n, line_start);
			*p = saved;
			if (*p == '\r' && p[1] == '\n')
				p++;
			p++;
			line_start = p;
			last_brk = NULL;
			continue;
		}
		next = sjis_skip_char(p);
		saved = *next;
		*next = 0;
		w = (int)ceilf(gfx_size_text(&ts, line_start));
		*next = saved;
		if (w > max_w && p != line_start) {
			char *cut = last_brk ? last_brk : p;
			char c2 = *cut;
			*cut = 0;
			hud_push_utf_line(L, ++n, line_start);
			*cut = c2;
			line_start = (c2 == ' ') ? cut + 1 : cut;
			last_brk = NULL;
			p = line_start;
			continue;
		}
		if (*p == ' ')
			last_brk = p;
		p = next;
	}
	if (line_start && *line_start)
		hud_push_utf_line(L, ++n, line_start);
	free(sjis);
	return 1;
}

int rq_lua_hud_rect(lua_State *L)
{
	struct hud_cmd *c = push_cmd(HUD_RECT);
	if (!c)
		return 0;
	c->x = (int)luaL_checkinteger(L, 1);
	c->y = (int)luaL_checkinteger(L, 2);
	c->w = (int)luaL_checkinteger(L, 3);
	c->h = (int)luaL_checkinteger(L, 4);
	c->r = (int)luaL_optinteger(L, 5, 20);
	c->g = (int)luaL_optinteger(L, 6, 20);
	c->b = (int)luaL_optinteger(L, 7, 30);
	c->a = (int)luaL_optinteger(L, 8, 220);
	return 0;
}

int rq_lua_hud_text(lua_State *L)
{
	struct hud_cmd *c = push_cmd(HUD_TEXT);
	const char *s;
	if (!c)
		return 0;
	c->x = (int)luaL_checkinteger(L, 1);
	c->y = (int)luaL_checkinteger(L, 2);
	s = luaL_checkstring(L, 3);
	c->text = strdup(s ? s : "");
	c->size = 16.0f;
	c->r = 255;
	c->g = 255;
	c->b = 255;
	c->a = 255;
	if (lua_istable(L, 4)) {
		lua_getfield(L, 4, "size");
		if (lua_isnumber(L, -1))
			c->size = (float)lua_tonumber(L, -1);
		lua_getfield(L, 4, "r");
		if (lua_isinteger(L, -1))
			c->r = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "g");
		if (lua_isinteger(L, -1))
			c->g = (int)lua_tointeger(L, -1);
		lua_getfield(L, 4, "b");
		if (lua_isinteger(L, -1))
			c->b = (int)lua_tointeger(L, -1);
		lua_settop(L, 4);
	}
	return 0;
}

int rq_lua_hud_cg(lua_State *L)
{
	struct hud_cmd *c = push_cmd(HUD_CG);
	Texture *t;
	if (!c)
		return 0;
	c->x = (int)luaL_checkinteger(L, 1);
	c->y = (int)luaL_checkinteger(L, 2);
	c->cg = (int)luaL_checkinteger(L, 3);
	t = cg_tex(c->cg);
	if (t) {
		lua_pushinteger(L, t->w);
		lua_pushinteger(L, t->h);
		return 2;
	}
	lua_pushinteger(L, 0);
	lua_pushinteger(L, 0);
	return 2;
}

int rq_lua_hud_hit(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	lua_pushboolean(L, hit(x, y, w, h));
	return 1;
}

int rq_lua_hud_clicked(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	int w = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_checkinteger(L, 4);
	lua_pushboolean(L, g_left_edge && hit(x, y, w, h));
	return 1;
}

int rq_lua_hud_block(lua_State *L)
{
	(void)L;
	g_block_mouse = 1;
	return 0;
}

int rq_hud_tick(void)
{
	lua_State *L = rq_lua();
	g_block_mouse = 0;
	clear_cmds();
	g_drew = 0;
	mouse_get_pos(&g_mx, &g_my);
	g_left = key_is_down_raw(VK_LBUTTON);
	g_right = key_is_down_raw(VK_RBUTTON);
	g_left_edge = g_left && !g_prev_left;
	g_prev_left = g_left;
	if (!L || g_on_frame_ref == LUA_NOREF)
		return 0;
	lua_rawgeti(L, LUA_REGISTRYINDEX, g_on_frame_ref);
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		rq_log("HUD on_frame error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	log_exploring_change();
	return g_drew;
}

void rq_hud_draw(void)
{
	int i;
	struct text_style ts;

	for (i = 0; i < g_ncmd; i++) {
		struct hud_cmd *c = &g_cmd[i];
		if (c->kind == HUD_RECT && c->w > 0 && c->h > 0) {
			Texture t;
			Rectangle r;
			SDL_Color col = {
				(uint8_t)c->r, (uint8_t)c->g, (uint8_t)c->b, (uint8_t)c->a
			};
			gfx_init_texture_rgba(&t, c->w, c->h, col);
			r = RECT(c->x, c->y, c->w, c->h);
			gfx_render_texture(&t, &r);
			gfx_delete_texture(&t);
		} else if (c->kind == HUD_CG) {
			Texture *src = cg_tex(c->cg);
			Rectangle r;
			if (!src)
				continue;
			r = RECT(c->x, c->y, src->w, src->h);
			gfx_render_texture(src, &r);
		} else if (c->kind == HUD_TEXT && c->text) {
			Texture t;
			Rectangle r;
			char *sjis;
			int tw, th;
			hud_make_ts(&ts, c->size, c->r, c->g, c->b);
			sjis = utf2sjis(c->text, 0);
			if (!sjis)
				continue;
			tw = hud_measure_sjis(sjis, ts.size) + 8;
			if (tw < 32)
				tw = 32;
			th = (int)ts.size + 20;
			/* COPY, not blended: blended glyphs keep dest alpha 0 so
			 * labels were invisible (blank quads / empty Close). */
			gfx_init_texture_rgba(&t, tw, th, (SDL_Color){ 0, 0, 0, 0 });
			gfx_render_text(&t, 0.0f, 0, sjis, &ts, false);
			free(sjis);
			r = RECT(c->x, c->y, t.w, t.h);
			gfx_render_texture(&t, &r);
			gfx_delete_texture(&t);
		}
	}
}

void rq_hud_bind_lua(lua_State *L)
{
	(void)L;
}

#endif /* RQ_MODLOADER */
