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

#define HUD_MAX_CMD 96
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

static int g_dungeon;
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

static int wrap_method_flag(struct ain *ain, int fno, int sys)
{
	int dup = ain_dup_function(ain, fno);
	int nargs = ain->functions[fno].nr_args;
	int i;
	struct buffer out;

	buffer_init(&out, NULL, 0);
	emit1(&out, FUNC, fno);
	/* CALLMETHOD pops nr_args, then the struct page under them.
	 * Vanilla: PUSH this, PUSH arg0..argN, CALLMETHOD (see T_Sprite@setCg).
	 * Args then PUSHSTRUCTPAGE made create's this = nZ and blew up in
	 * stack_pop_var (heap/page like 121/14) while loading a dungeon. */
	emit0(&out, PUSHSTRUCTPAGE);
	for (i = 0; i < nargs; i++)
		emit1(&out, SH_LOCALREF, i);
	emit1(&out, CALLMETHOD, dup);
	emit1(&out, CALLSYS, sys);
	emit0(&out, RETURN);
	emit1(&out, ENDFUNC, fno);
	commit_wrap(ain, fno, &out);
	rq_log("hud: wrap method fno=%d nargs=%d sys=0x%x", fno, nargs, sys);
	return 1;
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

int rq_dungeon_active(void)
{
	return g_dungeon;
}

int rq_hud_block_mouse(void)
{
	return g_block_mouse;
}

int rq_hud_syscall(int code)
{
	switch ((enum vm_extra_syscall)code) {
	case VM_XSYS_HUD_ON:
		g_dungeon = 1;
		return 1;
	case VM_XSYS_HUD_OFF:
		g_dungeon = 0;
		g_block_mouse = 0;
		return 1;
	default:
		return 0;
	}
}

int rq_hud_install(struct ain *ain)
{
	int f_create, f_delete;
	if (!ain)
		return 0;
	f_create = find_fn(ain, "T_MapParts@create");
	f_delete = find_fn(ain, "T_MapParts@delete");
	if (f_create < 0 || f_delete < 0) {
		rq_log("hud: T_MapParts create/delete missing (%d/%d)", f_create, f_delete);
		return 0;
	}
	wrap_method_flag(ain, f_create, VM_XSYS_HUD_ON);
	wrap_method_flag(ain, f_delete, VM_XSYS_HUD_OFF);
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
		rq_log("hud on_frame: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	return g_drew;
}

void rq_hud_draw(void)
{
	int i;
	struct text_style ts = {
		.face = FONT_GOTHIC,
		.size = 16.0f,
		.bold_width = 0.0f,
		.weight = FW_NORMAL,
		.edge_left = 1.0f,
		.edge_up = 1.0f,
		.edge_right = 1.0f,
		.edge_down = 1.0f,
		.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
		.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = 0.0f,
		.font_size = NULL
	};

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
			ts.size = c->size > 1.0f ? c->size : 16.0f;
			ts.color.r = (uint8_t)c->r;
			ts.color.g = (uint8_t)c->g;
			ts.color.b = (uint8_t)c->b;
			ts.color.a = 255;
			sjis = utf2sjis(c->text, 0);
			if (!sjis)
				continue;
			tw = (int)(strlen(c->text) * ts.size) + 24;
			if (tw < 32)
				tw = 32;
			th = (int)ts.size + 16;
			gfx_init_texture_rgba(&t, tw, th, (SDL_Color){ 0, 0, 0, 0 });
			gfx_render_text(&t, 0.0f, 0, sjis, &ts, true);
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
